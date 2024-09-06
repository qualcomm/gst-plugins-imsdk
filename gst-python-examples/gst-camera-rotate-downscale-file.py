# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

import os
import sys
import signal
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst


def construct_pipeline(pipe):
    # Create all elements
    qmmfsrc = Gst.ElementFactory.make("qtiqmmfsrc")
    Gst.util_set_object_arg(qmmfsrc, "camera", "0")

    capsfilter_0 = Gst.ElementFactory.make("capsfilter", "camoutcaps")
    Gst.util_set_object_arg(
        capsfilter_0,
        "caps",
        "video/x-raw(memory:GBM),format=NV12,\
        width=1920,height=1080,framerate=30/1",
    )

    queue_0 = Gst.ElementFactory.make("queue")

    # Rotate 90 degrees clockwise
    vtransform = Gst.ElementFactory.make("qtivtransform")
    Gst.util_set_object_arg(vtransform, "rotate", "90CW")

    # Downscale to 640x480
    capsfilter_1 = Gst.ElementFactory.make("capsfilter", "transformcaps")
    Gst.util_set_object_arg(
        capsfilter_1,
        "caps",
        "video/x-raw(memory:GBM),width=480,height=640,colorimetry=bt709",
    )

    queue_1 = Gst.ElementFactory.make("queue")

    v4l2h264enc = Gst.ElementFactory.make("v4l2h264enc")
    Gst.util_set_object_arg(v4l2h264enc, "capture-io-mode", "5")
    Gst.util_set_object_arg(v4l2h264enc, "output-io-mode", "5")

    queue_2 = Gst.ElementFactory.make("queue")

    h264parse = Gst.ElementFactory.make("h264parse")
    Gst.util_set_object_arg(h264parse, "config-interval", "1")

    mp4mux = Gst.ElementFactory.make("mp4mux")

    filesink = Gst.ElementFactory.make("filesink")
    Gst.util_set_object_arg(filesink, "location", "/opt/data/test.mp4")

    if (
        not qmmfsrc
        or not capsfilter_0
        or not queue_0
        or not vtransform
        or not capsfilter_1
        or not queue_1
        or not v4l2h264enc
        or not queue_2
        or not h264parse
        or not mp4mux
        or not filesink
    ):
        print("Failed to create all elements!")
        sys.exit(1)

    # Add all elements
    pipe.add(qmmfsrc)
    pipe.add(capsfilter_0)
    pipe.add(queue_0)
    pipe.add(vtransform)
    pipe.add(capsfilter_1)
    pipe.add(queue_1)
    pipe.add(v4l2h264enc)
    pipe.add(queue_2)
    pipe.add(h264parse)
    pipe.add(mp4mux)
    pipe.add(filesink)

    # Link all elements
    linked = True
    linked = linked and qmmfsrc.link(capsfilter_0)
    linked = linked and capsfilter_0.link(queue_0)
    linked = linked and queue_0.link(vtransform)
    linked = linked and vtransform.link(capsfilter_1)
    linked = linked and capsfilter_1.link(queue_1)
    linked = linked and queue_1.link(v4l2h264enc)
    linked = linked and v4l2h264enc.link(queue_2)
    linked = linked and queue_2.link(h264parse)
    linked = linked and h264parse.link(mp4mux)
    linked = linked and mp4mux.link(filesink)

    if not linked:
        print("Failed to link all elements!")
        sys.exit(1)

    return pipe


def main():
    os.environ["XDG_RUNTIME_DIR"] = "/dev/socket/weston"
    os.environ["WAYLAND_DISPLAY"] = "wayland-1"

    Gst.init(None)

    pipe = Gst.Pipeline.new()
    pipe = construct_pipeline(pipe)

    def handle_interrupt_signal(signal, frame):
        event = Gst.Event.new_eos()
        success = pipe.send_event(event)
        if success == False:
            print("Failed to send EoS event to the pipeline!")

    signal.signal(signal.SIGINT, handle_interrupt_signal)

    pipe.set_state(Gst.State.PLAYING)

    bus = pipe.get_bus()
    while True:
        message = bus.timed_pop_filtered(
            100 * Gst.MSECOND, Gst.MessageType.EOS | Gst.MessageType.ERROR
        )

        if not message:
            continue

        message_type = message.type
        if message_type == Gst.MessageType.ERROR:
            error, debug_info = message.parse_error()
            print("ERROR:", message.src.get_name(), " ", error.message)
            if debug_info:
                print("debugging info:", debug_info)
            terminate = True
        elif message_type == Gst.MessageType.EOS:
            print("EoS received")
            terminate = True
        else:
            print("ERROR: Unexpected message received")
            break

        if terminate:
            break

    pipe.set_state(Gst.State.NULL)


if __name__ == "__main__":
    sys.exit(main())
