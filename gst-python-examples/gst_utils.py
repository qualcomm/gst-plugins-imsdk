################################################################################
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

# This script provides utility functions for running GStreamer pipelines in Python,
# handling video buffers, converting NV12 frames to JPEG, and saving tensor data.

import os
import signal
from typing import Optional, Callable
import cv2
import numpy as np
import time
from collections import deque

# Import GStreamer and GLib via PyGObject
import gi
gi.require_version('Gst', '1.0')
gi.require_version("GLib", "2.0")
gi.require_version('GstVideo', '1.0')
from gi.repository import Gst, GLib, GstVideo

eos_received = False

# Initialize GStreamer
Gst.init(None)

def is_linux():
    """
    Check if the current operating system is Linux.
    This is used to determine whether to set Wayland environment variables.
    """
    try:
        with open("/etc/os-release") as f:
            for line in f:
                if "Linux" in line:
                    return True
    except FileNotFoundError:
        return False
    return False

def handle_interrupt_signal(pipeline, mloop):
    """Handle Ctrl+C."""

    _, state, _ = pipeline.get_state(Gst.CLOCK_TIME_NONE)
    if state != Gst.State.PLAYING:
        mloop.quit()
        return GLib.SOURCE_CONTINUE

    event = Gst.Event.new_eos()
    if pipeline.send_event(event):
        print("EoS sent to the pipeline")
    else:
        print("Failed to send EoS event to the pipeline!")
        mloop.quit()
    return GLib.SOURCE_CONTINUE

def handle_bus_message(bus, message, mloop):
    """Handle messages posted on pipeline bus."""
    global eos_received

    if message.type == Gst.MessageType.ERROR:
        error, debug_info = message.parse_error()
        print("ERROR:", message.src.get_name(), " ", error.message)
        if debug_info:
            print("debugging info:", debug_info)
        mloop.quit()
    elif message.type == Gst.MessageType.EOS:
        print("EoS received")
        eos_received = True
        mloop.quit()
    return True

def gst_run_pipeline(pipeline: str, on_frame: Optional[Callable] = None):
    """
    Launch and run a GStreamer pipeline.
    If running on Linux, set Wayland environment variables.
    If a on_frame is provided, it will be connected to all appsink 'new-sample' signals.
    """

    # Parse the pipeline string into a Gst.Pipeline object
    pipeline = Gst.parse_launch(pipeline)

    # Appsink handler, call the callback with the buffer
    def on_new_sample(sink):
        name = sink.get_name()
        sample = sink.emit("pull-sample")
        if sample:
            print(f"New sample received from {name}")
            buffer = sample.get_buffer()
            on_frame(name, buffer)
        return Gst.FlowReturn.OK

    ts = {}
    probe_target_elements = {
        "qtimlvconverter",
        "qtimlaconverter",
        "qtimltflite",
        "qtimlqnn",
        "qtimlsnpe",
        "qtimlpostprocess"
    }

    def attach_probe(elem):
        if not hasattr(attach_probe, "mark_count"):
            attach_probe.mark_count = 0

        name = elem.get_name()
        src_pad = elem.get_static_pad("src")
        sink_pad = elem.get_static_pad("sink")

        def make_cb(direction):
            def cb(pad, info, _user):
                if info.type & Gst.PadProbeType.BUFFER:
                    buffer = info.get_buffer()
                    if buffer:
                        pts = int(buffer.pts)
                        if pts not in ts:
                            ts[pts] = {}
                        ts[pts][f"{name}@{direction}"] = time.perf_counter()
                        if len(ts[pts]) == attach_probe.mark_count:
                            process_marks(ts[pts])
                            ts.pop(pts)
                return Gst.PadProbeReturn.OK
            return cb

        src_pad.add_probe(Gst.PadProbeType.BUFFER, make_cb("finish"), None)
        sink_pad.add_probe(Gst.PadProbeType.BUFFER, make_cb("start"), None)
        attach_probe.mark_count += 2

    for element in pipeline.iterate_elements():
        if on_frame and isinstance(element, Gst.Element) and element.get_factory().get_name() == "appsink":
            element.connect("new-sample", on_new_sample)
            print(f"Connected on_frame to appsink: {element.get_name()}")
        if isinstance(element, Gst.Element) and element.get_factory().get_name() in probe_target_elements:
            attach_probe(element)

    # Run the main loop to keep the pipeline alive
    loop = GLib.MainLoop()

    # Handle Ctrl+C
    interrupt_watch_id = GLib.unix_signal_add(
        GLib.PRIORITY_HIGH, signal.SIGINT, handle_interrupt_signal, pipeline, loop
    )

    # Wait until error or EOS
    bus = pipeline.get_bus()
    bus.add_signal_watch()
    bus.connect("message", handle_bus_message, loop)

    # Set pipeline to PLAYING state
    pipeline.set_state(Gst.State.PLAYING)

    loop.run()

    GLib.source_remove(interrupt_watch_id)
    bus.remove_signal_watch()

    # On exit, set pipeline to NULL state to clean up
    pipeline.set_state(Gst.State.NULL)

def gst_get_sink(type: str):
    """
    Return a GStreamer sink element string based on the output type.
    Supported types: 'display', 'video', 'appsink'.
    """
    if type == "display":
        # Display the video using Wayland
        return "waylandsink sync=true fullscreen=true"
    elif type == "video":
        # Encode and save the video
        return (
            "v4l2h264enc capture-io-mode=4 output-io-mode=4 ! queue ! "
            "h264parse ! mp4mux ! filesink location=output.mp4"
        )
    elif type.startswith("appsink"):
        # Send video frames to an appsink for processing in Python
        return f"queue ! appsink name={type} sync=false emit-signals=true"
    else:
        # Raise an error for unsupported sink types
        raise ValueError(f"Unknown sink type: {type}")

def nv12_buffer_to_jpeg(buffer: Gst.Buffer, out_path: str, quality: int = 90):
    """
    Converts a NV12 buffer to a JPEG file.
    Handles non-standard stride using GstVideoMeta.
    """
    # Get video metadata (width, height, stride, offsets)
    vmeta = GstVideo.buffer_get_video_meta(buffer)
    if vmeta is None:
        raise RuntimeError("Missing GstVideoMeta - cannot determine width/height/stride.")

    width, height = vmeta.width, vmeta.height
    ok, map_info = buffer.map(Gst.MapFlags.READ)
    if not ok:
        raise RuntimeError("Failed to map buffer.")

    try:
        data = map_info.data  # raw bytes
        stride_y, off_y = vmeta.stride[0], vmeta.offset[0]
        stride_uv, off_uv = vmeta.stride[1], vmeta.offset[1]

        # Extract Y plane
        y_plane = (np.frombuffer(data, dtype=np.uint8, count=stride_y * height, offset=off_y)
                     .reshape(height, stride_y)[:, :width])

        # Extract interleaved UV plane
        uv_plane = (np.frombuffer(data, dtype=np.uint8, count=stride_uv * (height // 2), offset=off_uv)
                      .reshape(height // 2, stride_uv)[:, :width])

        # Stack Y and UV to form NV12
        nv12 = np.vstack([y_plane, uv_plane])

        # Convert NV12 to BGR
        bgr = cv2.cvtColor(nv12, cv2.COLOR_YUV2BGR_NV12)

        # Encode to JPEG
        ok, enc = cv2.imencode(".jpg", bgr, [int(cv2.IMWRITE_JPEG_QUALITY), int(quality)])
        if not ok:
            raise RuntimeError("JPEG encoding failed.")
        with open(out_path, "wb") as f:
            f.write(enc.tobytes())
    finally:
        buffer.unmap(map_info)

def buffer_to_file(buffer: Gst.Buffer, out_path: str):
    """
    Saves a buffer to a file.
    """
    ok, map_info = buffer.map(Gst.MapFlags.READ)
    if not ok:
        raise RuntimeError("Failed to map buffer.")

    try:
        with open(out_path, "wb") as f:
            f.write(map_info.data)
    finally:
        buffer.unmap(map_info)

def process_marks(ts):
    """
    Process time measurement marks and print preformance information
    """
    elements = set()
    for key in ts.keys():
        elements.add(key.split('@')[0])

    if not hasattr(process_marks, "times"):
        process_marks.times = {k: deque(maxlen = 30) for k in elements}
        process_marks.last_print = time.perf_counter()

    for e in elements:
        process_marks.times[e].append(ts[e + "@finish"] - ts[e + "@start"])

    if time.perf_counter() - process_marks.last_print > 1:
        res = "Timings - "
        for k, v in process_marks.times.items():
            res += f'{k}: {sum(v) / len(v) * 1000:.3f}ms | '
        print(res[:-3])
        process_marks.last_print = time.perf_counter()
