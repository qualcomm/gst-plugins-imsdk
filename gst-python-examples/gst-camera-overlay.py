#!/usr/bin/env python3

################################################################################
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

import os
import re
import sys
import threading

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstVideo", "1.0")

from gi.repository import Gst, GstVideo, GObject, GLib

Gst.init(None)

DEFAULT_TEXT = "Type new text below and press Enter..."
DEFAULT_POSITION = (50, 50)
DEFAULT_COLOR = 0xFFFFFFFF

ROI_TYPE_NAME = "OverlayText"
ROI_SIZE = (1, 1)

CAMX_SOCKET = "/run/cam_server/le_cam_socket"

_state_lock = threading.Lock()

_current = {
    "text": DEFAULT_TEXT,
    "position": DEFAULT_POSITION,
    "color": DEFAULT_COLOR,
}

# Recognized optional tokens:
#   --pos=x,y
#   --color=0xRRGGBBAA
#
# Example:
#   Hello --pos=100,200 --color=0xFF00FFFF
TOKEN_RE = re.compile(r"--(pos|color)=(\S+)")

def is_camx_present():
    """Return True when the CamX camera server socket is present."""
    return os.access(CAMX_SOCKET, os.F_OK)

def get_camera_source_str(camera_id):
    """Return the appropriate camera source for the detected backend."""
    if is_camx_present():
        print("CAMX detected - using qtiqmmfsrc")
        return f"qtiqmmfsrc camera={camera_id} name=camsrc"

    print("CAMX not detected - falling back to libcamerasrc")
    return "libcamerasrc name=camsrc ! qtivtransform"

def get_pipeline_str(camera_id=0):
    """Build the camera overlay pipeline."""
    src_cam = get_camera_source_str(camera_id)

    return (
        f"{src_cam} ! "
        "queue ! "
        "identity name=text_injector ! "
        "qtivoverlay name=my_overlay ! "
        "waylandsink fullscreen=true sync=false"
    )

def make_color_value(color):
    value = GObject.Value()
    value.init(GObject.TYPE_UINT)
    value.set_uint(color)
    return value

def inject_text_probe(pad, info):
    """
    Attach a fresh ROI and ObjectDetection meta to every video buffer.

    The current text is stored as the ROI type. Spaces are replaced with
    periods to match the metadata naming convention.
    """
    buf = info.get_buffer()

    if buf is None:
        return Gst.PadProbeReturn.OK

    buf.make_writable()

    with _state_lock:
        text = _current["text"]
        x, y = _current["position"]
        color = _current["color"]

    w, h = ROI_SIZE

    # Spaces are not valid in the ROI type name.
    roi_type = text.replace(" ", ".") or ROI_TYPE_NAME

    roi = GstVideo.buffer_add_video_region_of_interest_meta(
        buf,
        roi_type,
        x,
        y,
        w,
        h,
    )

    if roi is not None:
        param = Gst.Structure.new_empty("ObjectDetection")
        param.set_value("color", make_color_value(color))
        roi.add_param(param)

    # Make sure the modified buffer is passed downstream.
    info.set_buffer(buf)

    return Gst.PadProbeReturn.OK

def parse_input_line(line):
    """
    Parse a typed line.

    Plain text updates only the overlay text. Optional position and color
    values update the corresponding settings for subsequent frames.
    """
    position = None
    color = None

    def consume(match):
        nonlocal position, color

        key = match.group(1)
        value = match.group(2)

        if key == "pos":
            try:
                x_str, y_str = value.split(",", 1)
                position = (int(x_str), int(y_str))
            except ValueError:
                print("Invalid position. Use --pos=x,y")
                position = None

        elif key == "color":
            try:
                color = int(value, 16)
            except ValueError:
                print("Invalid color. Use --color=0xRRGGBBAA")
                color = None

        return ""

    text = TOKEN_RE.sub(consume, line).strip()

    return text, position, color

def keyboard_input_loop(loop):
    print("")
    print("--- Keyboard Input Active ---")
    print("Type text and press Enter to update the video overlay.")
    print("Optional position: --pos=x,y")
    print("Optional color:    --color=0xRRGGBBAA")
    print("")
    print("Example:")
    print("Hello there --pos=100,200 --color=0xFF00FFFF")
    print("")
    print("Press Ctrl+C to exit.")
    print("")

    try:
        while True:
            line = input("Enter new overlay text: ")

            if not line:
                continue

            text, position, color = parse_input_line(line)

            if not text:
                continue

            with _state_lock:
                _current["text"] = text

                if position is not None:
                    _current["position"] = position

                if color is not None:
                    _current["color"] = color

    except (KeyboardInterrupt, EOFError):
        loop.quit()


def main():
    camera_id = 0

    pipeline_str = get_pipeline_str(camera_id)

    print("")
    print("Pipeline:")
    print(pipeline_str)
    print("")

    try:
        pipeline = Gst.parse_launch(pipeline_str)
    except GLib.Error as error:
        sys.exit(f"Failed to create pipeline: {error}")

    injector = pipeline.get_by_name("text_injector")

    if injector is None:
        pipeline.set_state(Gst.State.NULL)
        sys.exit("Could not find identity element: text_injector")

    injector_src_pad = injector.get_static_pad("src")

    if injector_src_pad is None:
        pipeline.set_state(Gst.State.NULL)
        sys.exit("Could not get text_injector src pad")

    injector_src_pad.add_probe(
        Gst.PadProbeType.BUFFER,
        inject_text_probe,
    )

    state_result = pipeline.set_state(Gst.State.PLAYING)

    if state_result == Gst.StateChangeReturn.FAILURE:
        pipeline.set_state(Gst.State.NULL)
        sys.exit("Failed to start pipeline")

    loop = GLib.MainLoop()

    keyboard_thread = threading.Thread(
        target=keyboard_input_loop,
        args=(loop,),
        daemon=True,
    )
    keyboard_thread.start()

    bus = pipeline.get_bus()
    bus.add_signal_watch()

    def on_bus_message(bus, message):
        message_type = message.type

        if message_type == Gst.MessageType.ERROR:
            error, debug = message.parse_error()
            print(f"GStreamer error: {error}")

            if debug:
                print(f"Debug information: {debug}")

            loop.quit()

        elif message_type == Gst.MessageType.EOS:
            print("Received end-of-stream")
            loop.quit()

    bus.connect("message", on_bus_message)

    try:
        loop.run()
    except KeyboardInterrupt:
        print("")
        print("Stopping pipeline...")
    finally:
        pipeline.set_state(Gst.State.NULL)


if __name__ == "__main__":
    main()
