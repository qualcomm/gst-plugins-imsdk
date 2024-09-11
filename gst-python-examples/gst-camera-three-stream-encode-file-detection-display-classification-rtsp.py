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

    v4l2h264enc_0 = Gst.ElementFactory.make("v4l2h264enc", "v4l2h264enc_0")
    Gst.util_set_object_arg(v4l2h264enc_0, "capture-io-mode", "5")
    Gst.util_set_object_arg(v4l2h264enc_0, "output-io-mode", "5")

    # queue after encoder
    queue_1 = Gst.ElementFactory.make("queue")

    h264parse_0 = Gst.ElementFactory.make("h264parse")
    Gst.util_set_object_arg(h264parse_0, "config-interval", "1")

    mp4mux = Gst.ElementFactory.make("mp4mux")

    # Save Camera stream 0 in file
    filesink = Gst.ElementFactory.make("filesink")
    Gst.util_set_object_arg(filesink, "location", "/opt/data/test.mp4")

    # Camera stream 1
    capsfilter_video_0 = Gst.ElementFactory.make("capsfilter", "video_caps_0")
    Gst.util_set_object_arg(
        capsfilter_video_0,
        "caps",
        "video/x-raw\(memory:GBM\),format=NV12,\
        width=640,height=360,framerate=30/1",
    )

    # queue after camera source for video 0
    queue_2 = Gst.ElementFactory.make("queue")

    tee_0 = Gst.ElementFactory.make("tee", "split_0")

    # queue between tee_0 and metamux_0
    queue_3 = Gst.ElementFactory.make("queue")

    metamux_0 = Gst.ElementFactory.make("qtimetamux", "metamux_0")

    mlvconverter_0 = Gst.ElementFactory.make("qtimlvconverter", "converter_0")

    # queue after preprocess
    queue_4 = Gst.ElementFactory.make("queue")

    mltflite_0 = Gst.ElementFactory.make("qtimltflite", "inference_0")
    Gst.util_set_object_arg(mltflite_0, "delegate", "external")
    Gst.util_set_object_arg(
        mltflite_0, "external-delegate-path", "libQnnTFLiteDelegate.so"
    )
    Gst.util_set_object_arg(
        mltflite_0,
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        mltflite_0, "model", "/opt/data/YoloV8N_Detection_Quantized.tflite"
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

    capsfilter_postprocess_0 = Gst.ElementFactory.make(
        "capsfilter", "postproc_caps_0"
    )
    Gst.util_set_object_arg(capsfilter_postprocess_0, "caps", "text/x-raw")

    # queue between postprocess and metamux_0
    queue_6 = Gst.ElementFactory.make("queue")

    # queue after metamux_0
    queue_7 = Gst.ElementFactory.make("queue")

    overlay_0 = Gst.ElementFactory.make("qtioverlay", "overlay_0")
    Gst.util_set_object_arg(overlay_0, "engine", "gles")

    # queue after overlay_0
    queue_8 = Gst.ElementFactory.make("queue")

    display = Gst.ElementFactory.make("waylandsink")
    Gst.util_set_object_arg(display, "sync", "false")
    Gst.util_set_object_arg(display, "async", "false")
    Gst.util_set_object_arg(display, "fullscreen", "true")

    # Camera stream 2
    capsfilter_video_1 = Gst.ElementFactory.make("capsfilter", "video_caps_1")
    Gst.util_set_object_arg(
        capsfilter_video_1,
        "caps",
        "video/x-raw\(memory:GBM\),format=NV12,\
        width=640,height=360,framerate=30/1",
    )

    # queue after camera source for video 1
    queue_9 = Gst.ElementFactory.make("queue")

    tee_1 = Gst.ElementFactory.make("tee", "split_1")

    # queue between tee_1 and metamux_1
    queue_10 = Gst.ElementFactory.make("queue")

    metamux_1 = Gst.ElementFactory.make("qtimetamux", "metamux_1")

    mlvconverter_1 = Gst.ElementFactory.make("qtimlvconverter", "converter_1")

    # queue after preprocess
    queue_11 = Gst.ElementFactory.make("queue")

    mltflite_1 = Gst.ElementFactory.make("qtimltflite", "inference_1")
    Gst.util_set_object_arg(mltflite_1, "delegate", "external")
    Gst.util_set_object_arg(
        mltflite_1, "external-delegate-path", "libQnnTFLiteDelegate.so"
    )
    Gst.util_set_object_arg(
        mltflite_1,
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        mltflite_1, "model", "/opt/data/Resnet101_Quantized.tflite"
    )

    # queue after inference
    queue_12 = Gst.ElementFactory.make("queue")

    mlvclassification = Gst.ElementFactory.make("qtimlvclassification")
    Gst.util_set_object_arg(mlvclassification, "threshold", "51.0")
    Gst.util_set_object_arg(mlvclassification, "results", "5")
    Gst.util_set_object_arg(mlvclassification, "module", "mobilenet")
    Gst.util_set_object_arg(
        mlvclassification, "labels", "/opt/data/resnet101.labels"
    )
    Gst.util_set_object_arg(mlvclassification, "extra-operation", "softmax")
    Gst.util_set_object_arg(
        mlvclassification,
        "constants",
        "Mobilenet,q-offsets=<-82.0>,q-scales=<0.21351955831050873>;",
    )
    capsfilter_postprocess_1 = Gst.ElementFactory.make(
        "capsfilter", "postproc_caps_1"
    )
    Gst.util_set_object_arg(capsfilter_postprocess_1, "caps", "text/x-raw")

    # queue between postprocess and metamux_1
    queue_13 = Gst.ElementFactory.make("queue")

    # queue after metamux_1
    queue_14 = Gst.ElementFactory.make("queue")

    overlay_1 = Gst.ElementFactory.make("qtioverlay", "overlay_1")
    Gst.util_set_object_arg(overlay_1, "engine", "gles")

    # queue after overlay_1
    queue_15 = Gst.ElementFactory.make("queue")

    v4l2h264enc_1 = Gst.ElementFactory.make("v4l2h264enc", "v4l2h264enc_1")
    Gst.util_set_object_arg(v4l2h264enc_1, "capture-io-mode", "5")
    Gst.util_set_object_arg(v4l2h264enc_1, "output-io-mode", "5")

    # queue after encoder
    queue_16 = Gst.ElementFactory.make("queue")

    h264parse_1 = Gst.ElementFactory.make("h264parse", "h264parse_1")
    Gst.util_set_object_arg(h264parse_1, "config-interval", "1")

    queue_17 = Gst.ElementFactory.make("queue")

    rtspbin = Gst.ElementFactory.make("qtirtspbin")

    if (
        not qmmfsrc
        or not capsfilter_preview
        or not queue_0
        or not v4l2h264enc_0
        or not queue_1
        or not h264parse_0
        or not mp4mux
        or not filesink
        or not capsfilter_video_0
        or not queue_2
        or not tee_0
        or not queue_3
        or not metamux_0
        or not mlvconverter_0
        or not queue_4
        or not mltflite_0
        or not queue_5
        or not mlvdetection
        or not capsfilter_postprocess_0
        or not queue_6
        or not queue_7
        or not overlay_0
        or not queue_8
        or not display
        or not capsfilter_video_1
        or not queue_9
        or not tee_1
        or not queue_10
        or not metamux_1
        or not mlvconverter_1
        or not queue_11
        or not mltflite_1
        or not queue_12
        or not mlvclassification
        or not capsfilter_postprocess_1
        or not queue_13
        or not queue_14
        or not overlay_1
        or not queue_15
        or not v4l2h264enc_1
        or not queue_16
        or not h264parse_1
        or not queue_17
        or not rtspbin
    ):
        print("Failed to create all elements!")
        sys.exit(1)

    # Add all elements
    pipe.add(qmmfsrc)
    pipe.add(capsfilter_preview)
    pipe.add(queue_0)
    pipe.add(v4l2h264enc_0)
    pipe.add(queue_1)
    pipe.add(h264parse_0)
    pipe.add(mp4mux)
    pipe.add(filesink)
    pipe.add(capsfilter_video_0)
    pipe.add(queue_2)
    pipe.add(tee_0)
    pipe.add(queue_3)
    pipe.add(metamux_0)
    pipe.add(mlvconverter_0)
    pipe.add(queue_4)
    pipe.add(mltflite_0)
    pipe.add(queue_5)
    pipe.add(mlvdetection)
    pipe.add(queue_6)
    pipe.add(capsfilter_postprocess_0)
    pipe.add(queue_7)
    pipe.add(queue_8)
    pipe.add(overlay_0)
    pipe.add(display)
    pipe.add(capsfilter_video_1)
    pipe.add(queue_9)
    pipe.add(tee_1)
    pipe.add(queue_10)
    pipe.add(metamux_1)
    pipe.add(mlvconverter_1)
    pipe.add(queue_11)
    pipe.add(mltflite_1)
    pipe.add(queue_12)
    pipe.add(mlvclassification)
    pipe.add(capsfilter_postprocess_1)
    pipe.add(queue_13)
    pipe.add(queue_14)
    pipe.add(overlay_1)
    pipe.add(queue_15)
    pipe.add(v4l2h264enc_1)
    pipe.add(queue_16)
    pipe.add(h264parse_1)
    pipe.add(queue_17)
    pipe.add(rtspbin)

    # Link most of the elements
    linked = True

    # Link elements of the first stream
    # qmmfsrc stream 0 will link with capsfilter_preview
    linked = linked and capsfilter_preview.link(queue_0)
    linked = linked and queue_0.link(v4l2h264enc_0)
    linked = linked and v4l2h264enc_0.link(queue_1)
    linked = linked and queue_1.link(h264parse_0)
    linked = linked and h264parse_0.link(mp4mux)
    linked = linked and mp4mux.link(filesink)

    # Link elements of the second stream
    # qmmfsrc stream 1 will link with capsfilter_video_0
    linked = linked and capsfilter_video_0.link(queue_2)
    linked = linked and queue_2.link(tee_0)

    # tee_0 will link with metamux_0 video pad
    linked = linked and queue_3.link(metamux_0)

    # tee_0 will link with mlvconverter_0
    linked = linked and mlvconverter_0.link(queue_4)
    linked = linked and queue_4.link(mltflite_0)
    linked = linked and mltflite_0.link(queue_5)
    linked = linked and queue_5.link(mlvdetection)
    linked = linked and mlvdetection.link(capsfilter_postprocess_0)
    linked = linked and capsfilter_postprocess_0.link(queue_6)

    # detection will link with metamux_0
    linked = linked and metamux_0.link(queue_7)
    linked = linked and queue_7.link(overlay_0)
    linked = linked and overlay_0.link(queue_8)
    linked = linked and queue_8.link(display)

    # Link elements of the third stream
    # qmmfsrc stream 2 will link with capsfilter_video_1
    linked = linked and capsfilter_video_1.link(queue_9)
    linked = linked and queue_9.link(tee_1)

    # tee_1 will link with metamux_1 video pad
    linked = linked and queue_10.link(metamux_1)

    # tee_1 will link with mlvconverter_1
    linked = linked and mlvconverter_1.link(queue_11)
    linked = linked and queue_11.link(mltflite_1)
    linked = linked and mltflite_1.link(queue_12)
    linked = linked and queue_12.link(mlvclassification)
    linked = linked and mlvclassification.link(capsfilter_postprocess_1)
    linked = linked and capsfilter_postprocess_1.link(queue_13)

    # classification will link with metamux_1
    linked = linked and metamux_1.link(queue_14)
    linked = linked and queue_14.link(overlay_1)
    linked = linked and overlay_1.link(queue_15)
    linked = linked and queue_15.link(v4l2h264enc_1)
    linked = linked and v4l2h264enc_1.link(queue_16)
    linked = linked and queue_16.link(h264parse_1)
    linked = linked and h264parse_1.link(queue_17)
    linked = linked and queue_17.link(rtspbin)

    # Link qmmfsrc
    qmmf_src_pad_template = qmmfsrc.get_pad_template("video_%u")
    qmmf_preview_src_pad = qmmfsrc.request_pad(
        qmmf_src_pad_template, None, None
    )
    qmmf_video_0_src_pad = qmmfsrc.request_pad(
        qmmf_src_pad_template, None, None
    )
    qmmf_video_1_src_pad = qmmfsrc.request_pad(
        qmmf_src_pad_template, None, None
    )

    if (
        not qmmf_preview_src_pad
        or not qmmf_video_0_src_pad
        or not qmmf_video_1_src_pad
    ):
        print("Failed to request pads for qmmfsrc!")
        sys.exit(1)

    Gst.util_set_object_arg(qmmf_preview_src_pad, "type", "preview")
    Gst.util_set_object_arg(qmmf_video_0_src_pad, "type", "video")
    Gst.util_set_object_arg(qmmf_video_1_src_pad, "type", "video")

    capsfilter_preview_sink_pad = capsfilter_preview.get_static_pad("sink")
    capsfilter_video_0_sink_pad = capsfilter_video_0.get_static_pad("sink")
    capsfilter_video_1_sink_pad = capsfilter_video_1.get_static_pad("sink")

    if (
        qmmf_preview_src_pad.link(capsfilter_preview_sink_pad)
        != Gst.PadLinkReturn.OK
        or qmmf_video_0_src_pad.link(capsfilter_video_0_sink_pad)
        != Gst.PadLinkReturn.OK
        or qmmf_video_1_src_pad.link(capsfilter_video_1_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link qmmfsrc!")
        sys.exit(1)

    # Link tee_0 and metamux_0
    # Link tee_0 and mlvconverter_0
    tee_0_src_pad_template = tee_0.get_pad_template("src_%u")
    tee_0_queue_3_src_pad = tee_0.request_pad(
        tee_0_src_pad_template, None, None
    )
    tee_0_mlvconverter_0_src_pad = tee_0.request_pad(
        tee_0_src_pad_template, None, None
    )

    if not tee_0_queue_3_src_pad or not tee_0_mlvconverter_0_src_pad:
        print("Failed to request pads for tee_0!")
        sys.exit(1)

    queue_3_sink_pad = queue_3.get_static_pad("sink")
    mlvconverter_0_sink_pad = mlvconverter_0.get_static_pad("sink")

    if (
        tee_0_queue_3_src_pad.link(queue_3_sink_pad) != Gst.PadLinkReturn.OK
        or tee_0_mlvconverter_0_src_pad.link(mlvconverter_0_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link tee_0!")
        sys.exit(1)

    # Link detection and metamux_0
    metamux_0_sink_pad_template = metamux_0.get_pad_template("data_%u")
    metamux_0_metadata_sink_pad = metamux_0.request_pad(
        metamux_0_sink_pad_template, None, None
    )

    if not metamux_0_metadata_sink_pad:
        print("Failed to request pad for metamux_0!")
        sys.exit(1)

    queue_6_src_pad = queue_6.get_static_pad("src")

    if (
        queue_6_src_pad.link(metamux_0_metadata_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link metamux_0!")
        sys.exit(1)

    # Link tee_1 and metamux_1
    # Link tee_1 and mlvconverter_1
    tee_1_src_pad_template = tee_1.get_pad_template("src_%u")
    tee_1_queue_10_src_pad = tee_1.request_pad(
        tee_1_src_pad_template, None, None
    )
    tee_1_mlvconverter_1_src_pad = tee_1.request_pad(
        tee_1_src_pad_template, None, None
    )

    if not tee_1_queue_10_src_pad or not tee_1_mlvconverter_1_src_pad:
        print("Failed to request pads for tee_1!")
        sys.exit(1)

    queue_10_sink_pad = queue_10.get_static_pad("sink")
    mlvconverter_1_sink_pad = mlvconverter_1.get_static_pad("sink")

    if (
        tee_1_queue_10_src_pad.link(queue_10_sink_pad) != Gst.PadLinkReturn.OK
        or tee_1_mlvconverter_1_src_pad.link(mlvconverter_1_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link tee_1!")
        sys.exit(1)

    # Link classification and metamux_1
    metamux_1_sink_pad_template = metamux_1.get_pad_template("data_%u")
    metamux_1_metadata_sink_pad = metamux_1.request_pad(
        metamux_1_sink_pad_template, None, None
    )

    if not metamux_1_metadata_sink_pad:
        print("Failed to request pad for metamux_1!")
        sys.exit(1)

    queue_13_src_pad = queue_13.get_static_pad("src")

    if (
        queue_13_src_pad.link(metamux_1_metadata_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link metamux_1!")
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
