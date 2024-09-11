# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

import os
import sys
import signal
import gi

gi.require_version("Gst", "1.0")
gi.require_version("GLib", "2.0")
from gi.repository import Gst, GLib


def construct_pipeline(pipe):
    # Create all elements
    rtspsrc = Gst.ElementFactory.make("rtspsrc")
    Gst.util_set_object_arg(rtspsrc, "location", "rtsp://127.0.0.1:8900/live")

    rtph264depay = Gst.ElementFactory.make("rtph264depay")

    capsfilter_rtph264depay = Gst.ElementFactory.make("capsfilter")
    Gst.util_set_object_arg(
        capsfilter_rtph264depay,
        "caps",
        "video/x-h264,colorimetry=bt709",
    )

    h264parse_decoder = Gst.ElementFactory.make("h264parse")

    v4l2h264dec = Gst.ElementFactory.make("v4l2h264dec")
    Gst.util_set_object_arg(v4l2h264dec, "capture-io-mode", "5")
    Gst.util_set_object_arg(v4l2h264dec, "output-io-mode", "5")

    # queue after decoder
    queue_0 = Gst.ElementFactory.make("queue")

    tee = Gst.ElementFactory.make("tee", "split")

    metamux = Gst.ElementFactory.make("qtimetamux", "metamux")

    mlvconverter = Gst.ElementFactory.make("qtimlvconverter", "preproc")

    # queue after preprocess
    queue_1 = Gst.ElementFactory.make("queue")

    mltflite = Gst.ElementFactory.make("qtimltflite", "inference")
    Gst.util_set_object_arg(mltflite, "delegate", "external")
    Gst.util_set_object_arg(
        mltflite, "external-delegate-path", "libQnnTFLiteDelegate.so"
    )
    Gst.util_set_object_arg(
        mltflite,
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        mltflite, "model", "/opt/data/YoloV8N_Detection_Quantized.tflite"
    )

    # queue after inference
    queue_2 = Gst.ElementFactory.make("queue")

    mlvdetection = Gst.ElementFactory.make("qtimlvdetection", "postprocess")
    Gst.util_set_object_arg(mlvdetection, "threshold", "75.0")
    Gst.util_set_object_arg(mlvdetection, "results", "4")
    Gst.util_set_object_arg(mlvdetection, "module", "yolov8")
    Gst.util_set_object_arg(
        mlvdetection,
        "constants",
        "YoloV8,q-offsets=<-107.0,-128.0,0.0>,q-scales=<3.093529462814331,0.00390625,1.0>;",
    )
    Gst.util_set_object_arg(mlvdetection, "labels", "/opt/data/yolov8n.labels")

    capsfilter_postprocess = Gst.ElementFactory.make(
        "capsfilter", "postproc_caps"
    )
    Gst.util_set_object_arg(capsfilter_postprocess, "caps", "text/x-raw")

    # queue between postprocess and metamux
    queue_3 = Gst.ElementFactory.make("queue")

    # queue after metamux
    queue_4 = Gst.ElementFactory.make("queue")

    overlay = Gst.ElementFactory.make("qtioverlay")
    Gst.util_set_object_arg(overlay, "engine", "gles")

    display = Gst.ElementFactory.make("waylandsink")
    Gst.util_set_object_arg(display, "sync", "false")
    Gst.util_set_object_arg(display, "fullscreen", "true")

    if (
        not rtspsrc
        or not rtph264depay
        or not capsfilter_rtph264depay
        or not h264parse_decoder
        or not v4l2h264dec
        or not queue_0
        or not tee
        or not metamux
        or not mlvconverter
        or not queue_1
        or not mltflite
        or not queue_2
        or not mlvdetection
        or not capsfilter_postprocess
        or not queue_3
        or not queue_4
        or not overlay
        or not display
    ):
        print("Failed to create all elements!")
        sys.exit(1)

    # Add all elements
    pipe.add(rtspsrc)
    pipe.add(rtph264depay)
    pipe.add(capsfilter_rtph264depay)
    pipe.add(h264parse_decoder)
    pipe.add(v4l2h264dec)
    pipe.add(queue_0)
    pipe.add(tee)
    pipe.add(metamux)
    pipe.add(mlvconverter)
    pipe.add(queue_1)
    pipe.add(mltflite)
    pipe.add(queue_2)
    pipe.add(mlvdetection)
    pipe.add(capsfilter_postprocess)
    pipe.add(queue_3)
    pipe.add(queue_4)
    pipe.add(overlay)
    pipe.add(display)

    # Link most of the elements
    linked = True

    # rtspsrc will link with rtph264depay
    linked = linked and rtph264depay.link(capsfilter_rtph264depay)
    linked = linked and capsfilter_rtph264depay.link(h264parse_decoder)
    linked = linked and h264parse_decoder.link(v4l2h264dec)
    linked = linked and v4l2h264dec.link(queue_0)
    linked = linked and queue_0.link(tee)

    linked = linked and tee.link(metamux)
    linked = linked and tee.link(mlvconverter)

    linked = linked and mlvconverter.link(queue_1)
    linked = linked and queue_1.link(mltflite)
    linked = linked and mltflite.link(queue_2)
    linked = linked and queue_2.link(mlvdetection)
    linked = linked and mlvdetection.link(capsfilter_postprocess)
    linked = linked and capsfilter_postprocess.link(queue_3)
    linked = linked and queue_3.link(metamux)

    linked = linked and metamux.link(queue_4)
    linked = linked and queue_4.link(overlay)
    linked = linked and overlay.link(display)

    if not linked:
        print("Failed to link all elements!")
        sys.exit(1)

    # Link rtsp stream to rtph264depay
    def on_pad_added(elem, pad):
        if "rtp" in pad.get_name():
            rtph264depay_sink_pad = rtph264depay.get_static_pad("sink")
            if pad.link(rtph264depay_sink_pad) != Gst.PadLinkReturn.OK:
                print("Failed to link rtsp stream!")
                sys.exit(1)

    rtspsrc.connect("pad-added", on_pad_added)

    return pipe


def quit_mainloop(loop):
    if loop.is_running():
        print("Quitting mainloop!")
        loop.quit()
    else:
        print("Loop is not running!")


def bus_call(bus, message, loop):
    message_type = message.type
    if message_type == Gst.MessageType.EOS:
        print("EoS received")
        quit_mainloop(loop)
    elif message_type == Gst.MessageType.ERROR:
        error, debug_info = message.parse_error()
        print("ERROR:", message.src.get_name(), " ", error.message)
        if debug_info:
            print("debugging info:", debug_info)
        quit_mainloop(loop)
    return True


def handle_interrupt_signal(pipe, loop):
    _, state, _ = pipe.get_state(Gst.CLOCK_TIME_NONE)
    if state == Gst.State.PLAYING:
        print("Sending EoS!")
        event = Gst.Event.new_eos()
        success = pipe.send_event(event)
        if success == False:
            print("Failed to send EoS event to the pipeline!")
    else:
        print("Pipeline is not playing, terminating!")
        quit_mainloop(loop)
    return GLib.SOURCE_CONTINUE


def main():
    os.environ["XDG_RUNTIME_DIR"] = "/dev/socket/weston"
    os.environ["WAYLAND_DISPLAY"] = "wayland-1"

    Gst.init(None)
    pipe = Gst.Pipeline.new()
    construct_pipeline(pipe)
    loop = GLib.MainLoop()

    bus = pipe.get_bus()
    bus.add_signal_watch()
    bus.connect("message", bus_call, loop)
    bus = None

    interrupt_watch_id = GLib.unix_signal_add(
        GLib.PRIORITY_HIGH, signal.SIGINT, handle_interrupt_signal, pipe, loop
    )

    pipe.set_state(Gst.State.PLAYING)
    loop.run()

    GLib.source_remove(interrupt_watch_id)
    pipe.set_state(Gst.State.NULL)
    loop = None
    pipe = None
    Gst.deinit()


if __name__ == "__main__":
    sys.exit(main())
