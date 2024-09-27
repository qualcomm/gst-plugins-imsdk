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
    capsfilter_0 = Gst.ElementFactory.make("capsfilter", "camout0caps")
    Gst.util_set_object_arg(
        capsfilter_0,
        "caps",
        "video/x-raw(memory:GBM),format=NV12,compression=ubwc,\
        width=640,height=360,framerate=30/1",
    )

    queue_0 = Gst.ElementFactory.make("queue")

    tee_0 = Gst.ElementFactory.make("tee", "split0")

    # Tee 0 video output for metamux 0
    queue_1 = Gst.ElementFactory.make("queue")

    # Tee 0 video output for mlvconverter 0
    mlvconverter_0 = Gst.ElementFactory.make("qtimlvconverter", "converter0")

    queue_2 = Gst.ElementFactory.make("queue")

    mltflite_0 = Gst.ElementFactory.make("qtimltflite", "inference0")
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
        mltflite_0,
        "model",
        "/opt/data/YoloV8N_Detection_Quantized.tflite",
    )

    queue_3 = Gst.ElementFactory.make("queue")

    mlvdetection = Gst.ElementFactory.make("qtimlvdetection")
    Gst.util_set_object_arg(mlvdetection, "threshold", "75.0")
    Gst.util_set_object_arg(mlvdetection, "results", "4")
    Gst.util_set_object_arg(mlvdetection, "module", "yolov8")
    Gst.util_set_object_arg(mlvdetection, "labels", "/opt/data/yolov8n.labels")
    Gst.util_set_object_arg(
        mlvdetection,
        "constants",
        "YoloV8,q-offsets=<-107.0,-128.0,0.0>,\
        q-scales=<3.093529462814331,0.00390625,1.0>;",
    )

    capsfilter_1 = Gst.ElementFactory.make("capsfilter", "metamux0metacaps")
    Gst.util_set_object_arg(capsfilter_1, "caps", "text/x-raw")

    queue_4 = Gst.ElementFactory.make("queue")

    metamux_0 = Gst.ElementFactory.make("qtimetamux", "metamux0")

    overlay_0 = Gst.ElementFactory.make("qtioverlay", "overlay0")
    Gst.util_set_object_arg(overlay_0, "engine", "gles")

    queue_5 = Gst.ElementFactory.make("queue")

    # Camera stream 1
    capsfilter_2 = Gst.ElementFactory.make("capsfilter", "camout1caps")
    Gst.util_set_object_arg(
        capsfilter_2,
        "caps",
        "video/x-raw(memory:GBM),format=NV12,\
        width=640,height=360,framerate=30/1",
    )

    queue_6 = Gst.ElementFactory.make("queue")

    tee_1 = Gst.ElementFactory.make("tee", "split1")

    # Tee 1 video output for metamux 1
    queue_7 = Gst.ElementFactory.make("queue")

    # Tee 1 video output for mlvconverter 1
    mlvconverter_1 = Gst.ElementFactory.make("qtimlvconverter", "converter1")

    queue_8 = Gst.ElementFactory.make("queue")

    mltflite_1 = Gst.ElementFactory.make("qtimltflite", "inference1")
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

    queue_9 = Gst.ElementFactory.make("queue")

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

    capsfilter_3 = Gst.ElementFactory.make("capsfilter", "metamux1metacaps")
    Gst.util_set_object_arg(capsfilter_3, "caps", "text/x-raw")

    queue_10 = Gst.ElementFactory.make("queue")

    metamux_1 = Gst.ElementFactory.make("qtimetamux", "metamux1")

    overlay_1 = Gst.ElementFactory.make("qtioverlay", "overlay1")
    Gst.util_set_object_arg(overlay_1, "engine", "gles")

    queue_11 = Gst.ElementFactory.make("queue")

    # Side by side camera stream 0 and camera stream 1
    composer = Gst.ElementFactory.make("qtivcomposer")
    Gst.util_set_object_arg(composer, "background", "0")

    queue_12 = Gst.ElementFactory.make("queue")

    display = Gst.ElementFactory.make("waylandsink")
    Gst.util_set_object_arg(display, "sync", "false")
    Gst.util_set_object_arg(display, "async", "false")
    Gst.util_set_object_arg(display, "fullscreen", "true")

    if (
        not qmmfsrc
        or not capsfilter_0
        or not queue_0
        or not tee_0
        or not queue_1
        or not mlvconverter_0
        or not queue_2
        or not mltflite_0
        or not queue_3
        or not mlvdetection
        or not capsfilter_1
        or not queue_4
        or not metamux_0
        or not overlay_0
        or not queue_5
        or not capsfilter_2
        or not queue_6
        or not tee_1
        or not queue_7
        or not mlvconverter_1
        or not queue_8
        or not mltflite_1
        or not queue_9
        or not mlvclassification
        or not capsfilter_3
        or not queue_10
        or not metamux_1
        or not overlay_1
        or not queue_11
        or not composer
        or not queue_12
        or not display
    ):
        print("Failed to create all elements!")
        sys.exit(1)

    # Add all elements
    pipe.add(qmmfsrc)
    pipe.add(capsfilter_0)
    pipe.add(queue_0)
    pipe.add(tee_0)
    pipe.add(queue_1)
    pipe.add(mlvconverter_0)
    pipe.add(queue_2)
    pipe.add(mltflite_0)
    pipe.add(queue_3)
    pipe.add(mlvdetection)
    pipe.add(capsfilter_1)
    pipe.add(queue_4)
    pipe.add(metamux_0)
    pipe.add(overlay_0)
    pipe.add(queue_5)
    pipe.add(capsfilter_2)
    pipe.add(queue_6)
    pipe.add(tee_1)
    pipe.add(queue_7)
    pipe.add(mlvconverter_1)
    pipe.add(queue_8)
    pipe.add(mltflite_1)
    pipe.add(queue_9)
    pipe.add(mlvclassification)
    pipe.add(capsfilter_3)
    pipe.add(queue_10)
    pipe.add(metamux_1)
    pipe.add(overlay_1)
    pipe.add(queue_11)
    pipe.add(composer)
    pipe.add(queue_12)
    pipe.add(display)

    # Link most of the elements
    linked = True

    # qmmfsrc stream 0 will link with capsfilter_0
    linked = linked and capsfilter_0.link(queue_0)
    linked = linked and queue_0.link(tee_0)

    # tee_0 will link with metamux_0 video pad
    linked = linked and queue_1.link(metamux_0)

    # tee_0 will link with mlvconverter_0
    linked = linked and mlvconverter_0.link(queue_2)
    linked = linked and queue_2.link(mltflite_0)
    linked = linked and mltflite_0.link(queue_3)
    linked = linked and queue_3.link(mlvdetection)
    linked = linked and mlvdetection.link(capsfilter_1)
    linked = linked and capsfilter_1.link(queue_4)
    # queue_4 will link metamux_0 metadata pad

    linked = linked and metamux_0.link(overlay_0)
    linked = linked and overlay_0.link(queue_5)
    # queue_5 will link with composer

    # qmmfsrc stream 1 will link with capsfilter_2
    linked = linked and capsfilter_2.link(queue_6)
    linked = linked and queue_6.link(tee_1)

    # tee_1 will link with metamux_1 video pad
    linked = linked and queue_7.link(metamux_1)

    # tee_1 will link with mlvconverter_1
    linked = linked and mlvconverter_1.link(queue_8)
    linked = linked and queue_8.link(mltflite_1)
    linked = linked and mltflite_1.link(queue_9)
    linked = linked and queue_9.link(mlvclassification)
    linked = linked and mlvclassification.link(capsfilter_3)
    linked = linked and capsfilter_3.link(queue_10)
    # queue_10 will link metamux_1 metadata pad

    linked = linked and metamux_1.link(overlay_1)
    linked = linked and overlay_1.link(queue_11)
    # queue_11 will link with composer

    linked = linked and composer.link(queue_12)
    linked = linked and queue_12.link(display)

    if not linked:
        print("Failed to link all elements!")
        sys.exit(1)

    # Link qmmfsrc
    qmmf_src_pad_template = qmmfsrc.get_pad_template("video_%u")
    qmmf_video_0_src_pad = qmmfsrc.request_pad(
        qmmf_src_pad_template, None, None
    )
    qmmf_video_1_src_pad = qmmfsrc.request_pad(
        qmmf_src_pad_template, None, None
    )

    if not qmmf_video_0_src_pad or not qmmf_video_1_src_pad:
        print("Failed to request pads for qmmfsrc!")
        sys.exit(1)

    Gst.util_set_object_arg(qmmf_video_0_src_pad, "type", "preview")
    Gst.util_set_object_arg(qmmf_video_1_src_pad, "type", "video")

    capsfilter_0_sink_pad = capsfilter_0.get_static_pad("sink")
    capsfilter_2_sink_pad = capsfilter_2.get_static_pad("sink")

    if (
        qmmf_video_0_src_pad.link(capsfilter_0_sink_pad) != Gst.PadLinkReturn.OK
        or qmmf_video_1_src_pad.link(capsfilter_2_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link qmmfsrc!")
        sys.exit(1)

    # Link tee and metamux for camera stream 0
    tee_0_src_pad_template = tee_0.get_pad_template("src_%u")
    tee_0_mlvconverter_0_src_pad = tee_0.request_pad(
        tee_0_src_pad_template, None, None
    )
    tee_0_queue_1_src_pad = tee_0.request_pad(
        tee_0_src_pad_template, None, None
    )

    if not tee_0_mlvconverter_0_src_pad or not tee_0_queue_1_src_pad:
        print("Failed to request pads for tee_0!")
        sys.exit(1)

    mlvconverter_0_sink_pad = mlvconverter_0.get_static_pad("sink")
    queue_1_sink_pad = queue_1.get_static_pad("sink")

    if (
        tee_0_mlvconverter_0_src_pad.link(mlvconverter_0_sink_pad)
        != Gst.PadLinkReturn.OK
        or tee_0_queue_1_src_pad.link(queue_1_sink_pad) != Gst.PadLinkReturn.OK
    ):
        print("Failed to link tee_0!")
        sys.exit(1)

    metamux_0_sink_pad_template = metamux_0.get_pad_template("data_%u")
    metamux_0_metadata_sink_pad = metamux_0.request_pad(
        metamux_0_sink_pad_template, None, None
    )

    if not metamux_0_metadata_sink_pad:
        print("Failed to request pad for metamux_0!")
        sys.exit(1)

    queue_4_src_pad = queue_4.get_static_pad("src")

    if (
        queue_4_src_pad.link(metamux_0_metadata_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link metamux_0!")
        sys.exit(1)

    # Link tee and metamux for camera stream 1
    tee_1_src_pad_template = tee_1.get_pad_template("src_%u")
    tee_1_mlvconverter_1_src_pad = tee_1.request_pad(
        tee_1_src_pad_template, None, None
    )
    tee_1_queue_7_src_pad = tee_1.request_pad(
        tee_1_src_pad_template, None, None
    )

    if not tee_1_mlvconverter_1_src_pad or not tee_1_queue_7_src_pad:
        print("Failed to request pads for tee_1!")
        sys.exit(1)

    mlvconverter_1_sink_pad = mlvconverter_1.get_static_pad("sink")
    queue_7_sink_pad = queue_7.get_static_pad("sink")

    if (
        tee_1_mlvconverter_1_src_pad.link(mlvconverter_1_sink_pad)
        != Gst.PadLinkReturn.OK
        or tee_1_queue_7_src_pad.link(queue_7_sink_pad) != Gst.PadLinkReturn.OK
    ):
        print("Failed to link tee_1!")
        sys.exit(1)

    metamux_1_sink_pad_template = metamux_1.get_pad_template("data_%u")
    metamux_1_metadata_sink_pad = metamux_1.request_pad(
        metamux_1_sink_pad_template, None, None
    )

    if not metamux_1_metadata_sink_pad:
        print("Failed to request pad for metamux_1!")
        sys.exit(1)

    queue_10_src_pad = queue_10.get_static_pad("src")

    if (
        queue_10_src_pad.link(metamux_1_metadata_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link metamux_1!")
        sys.exit(1)

    # Link composer
    composer_sink_pad_template = composer.get_pad_template("sink_%u")
    composer_sink_0_pad = composer.request_pad(
        composer_sink_pad_template, None, None
    )
    composer_sink_1_pad = composer.request_pad(
        composer_sink_pad_template, None, None
    )

    if not composer_sink_0_pad or not composer_sink_1_pad:
        print("Failed to request pads for composer!")
        sys.exit(1)

    Gst.util_set_object_arg(composer_sink_0_pad, "position", "<0, 0>")
    Gst.util_set_object_arg(composer_sink_0_pad, "dimensions", "<640, 360>")
    Gst.util_set_object_arg(composer_sink_1_pad, "position", "<640, 0>")
    Gst.util_set_object_arg(composer_sink_1_pad, "dimensions", "<640, 360>")

    queue_5_src_pad = queue_5.get_static_pad("src")
    queue_11_src_pad = queue_11.get_static_pad("src")

    if (
        queue_5_src_pad.link(composer_sink_0_pad) != Gst.PadLinkReturn.OK
        or queue_11_src_pad.link(composer_sink_1_pad) != Gst.PadLinkReturn.OK
    ):
        print("Failed to link composer!")
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
