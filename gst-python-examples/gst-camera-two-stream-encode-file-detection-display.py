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
    qmmfsrc = Gst.ElementFactory.make("qtiqmmfsrc")
    Gst.util_set_object_arg(qmmfsrc, "camera", "0")

    # Camera stream 0
    capsfilter_preview = Gst.ElementFactory.make("capsfilter", "preview_caps")
    Gst.util_set_object_arg(
        capsfilter_preview,
        "caps",
        "video/x-raw(memory:GBM),format=NV12,compression=ubwc,\
        width=1920,height=1080,framerate=30/1,colorimetry=bt709",
    )

    # queue after camera source for preview
    queue_0 = Gst.ElementFactory.make("queue")

    v4l2h264enc = Gst.ElementFactory.make("v4l2h264enc")
    Gst.util_set_object_arg(v4l2h264enc, "capture-io-mode", "5")
    Gst.util_set_object_arg(v4l2h264enc, "output-io-mode", "5")

    # queue after encoder
    queue_1 = Gst.ElementFactory.make("queue")

    h264parse = Gst.ElementFactory.make("h264parse")
    Gst.util_set_object_arg(h264parse, "config-interval", "1")

    mp4mux = Gst.ElementFactory.make("mp4mux")

    # Save Camera stream 0 in file
    filesink = Gst.ElementFactory.make("filesink")
    Gst.util_set_object_arg(filesink, "location", "/opt/data/test.mp4")

    # Camera stream 1
    capsfilter_video = Gst.ElementFactory.make("capsfilter", "video_caps")
    Gst.util_set_object_arg(
        capsfilter_video,
        "caps",
        "video/x-raw\(memory:GBM\),format=NV12,\
        width=640,height=360,framerate=30/1",
    )

    # queue after camera source for video
    queue_2 = Gst.ElementFactory.make("queue")

    tee = Gst.ElementFactory.make("tee")

    # queue between tee and metamux
    queue_3 = Gst.ElementFactory.make("queue")

    metamux = Gst.ElementFactory.make("qtimetamux")

    mlvconverter = Gst.ElementFactory.make("qtimlvconverter")

    # queue after preprocess
    queue_4 = Gst.ElementFactory.make("queue")

    mltflite = Gst.ElementFactory.make("qtimltflite")
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
    queue_5 = Gst.ElementFactory.make("queue")

    mlvdetection = Gst.ElementFactory.make("qtimlvdetection")
    Gst.util_set_object_arg(mlvdetection, "threshold", "75.0")
    Gst.util_set_object_arg(mlvdetection, "results", "4")
    Gst.util_set_object_arg(mlvdetection, "module", "yolov8")
    Gst.util_set_object_arg(
        mlvdetection,
        "constants",
        "YoloV8,q-offsets=<-107.0,-128.0,0.0>,\
        q-scales=<3.093529462814331,0.00390625,1.0>;",
    )
    Gst.util_set_object_arg(mlvdetection, "labels", "/opt/data/yolov8n.labels")

    capsfilter_postprocess = Gst.ElementFactory.make(
        "capsfilter", "postproc_caps"
    )
    Gst.util_set_object_arg(capsfilter_postprocess, "caps", "text/x-raw")

    # queue between postprocess and metamux
    queue_6 = Gst.ElementFactory.make("queue")

    # queue after metamux
    queue_7 = Gst.ElementFactory.make("queue")

    overlay = Gst.ElementFactory.make("qtioverlay")
    Gst.util_set_object_arg(overlay, "engine", "gles")

    # queue after overlay
    queue_8 = Gst.ElementFactory.make("queue")

    display = Gst.ElementFactory.make("waylandsink")
    Gst.util_set_object_arg(display, "sync", "false")
    Gst.util_set_object_arg(display, "fullscreen", "true")

    if (
        not qmmfsrc
        or not capsfilter_preview
        or not queue_0
        or not v4l2h264enc
        or not queue_1
        or not h264parse
        or not mp4mux
        or not filesink
        or not capsfilter_video
        or not queue_2
        or not tee
        or not queue_3
        or not metamux
        or not mlvconverter
        or not queue_4
        or not mltflite
        or not queue_5
        or not mlvdetection
        or not capsfilter_postprocess
        or not queue_6
        or not queue_7
        or not overlay
        or not queue_8
        or not display
    ):
        print("Failed to create all elements!")
        sys.exit(1)

    # Add all elements
    pipe.add(qmmfsrc)
    pipe.add(capsfilter_preview)
    pipe.add(queue_0)
    pipe.add(v4l2h264enc)
    pipe.add(queue_1)
    pipe.add(h264parse)
    pipe.add(mp4mux)
    pipe.add(filesink)
    pipe.add(capsfilter_video)
    pipe.add(queue_2)
    pipe.add(tee)
    pipe.add(queue_3)
    pipe.add(metamux)
    pipe.add(mlvconverter)
    pipe.add(queue_4)
    pipe.add(mltflite)
    pipe.add(queue_5)
    pipe.add(mlvdetection)
    pipe.add(queue_6)
    pipe.add(capsfilter_postprocess)
    pipe.add(queue_7)
    pipe.add(queue_8)
    pipe.add(overlay)
    pipe.add(display)

    # Link most of the elements
    linked = True

    # Link elements of the first stream
    # qmmfsrc stream 0 will link with capsfilter_preview
    linked = linked and capsfilter_preview.link(queue_0)
    linked = linked and queue_0.link(v4l2h264enc)
    linked = linked and v4l2h264enc.link(queue_1)
    linked = linked and queue_1.link(h264parse)
    linked = linked and h264parse.link(mp4mux)
    linked = linked and mp4mux.link(filesink)

    # Link elements of the second stream
    # qmmfsrc stream 1 will link with capsfilter_video
    linked = linked and capsfilter_video.link(queue_2)
    linked = linked and queue_2.link(tee)

    # tee will link with metamux video pad
    linked = linked and queue_3.link(metamux)

    # tee will link with mlvconverter
    linked = linked and mlvconverter.link(queue_4)
    linked = linked and queue_4.link(mltflite)
    linked = linked and mltflite.link(queue_5)
    linked = linked and queue_5.link(mlvdetection)
    linked = linked and mlvdetection.link(capsfilter_postprocess)
    linked = linked and capsfilter_postprocess.link(queue_6)

    # detection will link with metamux
    linked = linked and metamux.link(queue_7)
    linked = linked and queue_7.link(overlay)
    linked = linked and overlay.link(queue_8)
    linked = linked and queue_8.link(display)

    if not linked:
        print("Failed to link all elements!")
        sys.exit(1)

    # Link qmmfsrc
    qmmf_src_pad_template = qmmfsrc.get_pad_template("video_%u")
    qmmf_preview_src_pad = qmmfsrc.request_pad(
        qmmf_src_pad_template, None, None
    )
    qmmf_video_src_pad = qmmfsrc.request_pad(qmmf_src_pad_template, None, None)

    if not qmmf_preview_src_pad or not qmmf_video_src_pad:
        print("Failed to request pads for qmmfsrc!")
        sys.exit(1)

    Gst.util_set_object_arg(qmmf_preview_src_pad, "type", "preview")
    Gst.util_set_object_arg(qmmf_video_src_pad, "type", "video")

    capsfilter_preview_sink_pad = capsfilter_preview.get_static_pad("sink")
    capsfilter_video_sink_pad = capsfilter_video.get_static_pad("sink")

    if (
        qmmf_preview_src_pad.link(capsfilter_preview_sink_pad)
        != Gst.PadLinkReturn.OK
        or qmmf_video_src_pad.link(capsfilter_video_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link qmmfsrc!")
        sys.exit(1)

    # Link tee and metamux
    # Link tee and mlvconverter
    tee_src_pad_template = tee.get_pad_template("src_%u")
    tee_queue_3_src_pad = tee.request_pad(tee_src_pad_template, None, None)
    tee_mlvconverter_src_pad = tee.request_pad(tee_src_pad_template, None, None)

    if not tee_queue_3_src_pad or not tee_mlvconverter_src_pad:
        print("Failed to request pads for tee!")
        sys.exit(1)

    queue_3_sink_pad = queue_3.get_static_pad("sink")
    mlvconverter_sink_pad = mlvconverter.get_static_pad("sink")

    if (
        tee_queue_3_src_pad.link(queue_3_sink_pad) != Gst.PadLinkReturn.OK
        or tee_mlvconverter_src_pad.link(mlvconverter_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link tee!")
        sys.exit(1)

    # Link detection and metamux
    metamux_sink_pad_template = metamux.get_pad_template("data_%u")
    metamux_metadata_sink_pad = metamux.request_pad(
        metamux_sink_pad_template, None, None
    )

    if not metamux_metadata_sink_pad:
        print("Failed to request pad for metamux!")
        sys.exit(1)

    queue_6_src_pad = queue_6.get_static_pad("src")

    if queue_6_src_pad.link(metamux_metadata_sink_pad) != Gst.PadLinkReturn.OK:
        print("Failed to link metamux!")
        sys.exit(1)

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
