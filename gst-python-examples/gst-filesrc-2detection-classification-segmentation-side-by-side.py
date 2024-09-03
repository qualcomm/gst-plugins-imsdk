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
    # Stream 0
    filesrc_0 = Gst.ElementFactory.make("filesrc", "filesrc0")
    Gst.util_set_object_arg(
        filesrc_0, "location", "/opt/data/Draw_720p_180s_30FPS.mp4"
    )

    qtdemux_0 = Gst.ElementFactory.make("qtdemux", "qtdemux0")

    queue_0 = Gst.ElementFactory.make("queue")

    h264parse_0 = Gst.ElementFactory.make("h264parse", "h264parse0")
    Gst.util_set_object_arg(h264parse_0, "config-interval", "1")

    v4l2h264dec_0 = Gst.ElementFactory.make("v4l2h264dec", "v4l2h264dec0")
    Gst.util_set_object_arg(v4l2h264dec_0, "capture-io-mode", "5")
    Gst.util_set_object_arg(v4l2h264dec_0, "output-io-mode", "5")

    queue_1 = Gst.ElementFactory.make("queue")

    tee_0 = Gst.ElementFactory.make("tee", "split0")

    # Tee 0 video output for metamux 0
    queue_2 = Gst.ElementFactory.make("queue")

    # Tee 0 video output for mlvconverter 0
    mlvconverter_0 = Gst.ElementFactory.make("qtimlvconverter", "converter0")

    queue_3 = Gst.ElementFactory.make("queue")

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
        mltflite_0, "model", "/opt/data/YoloV8N_Detection_Quantized.tflite"
    )

    queue_4 = Gst.ElementFactory.make("queue")

    mlvdetection_0 = Gst.ElementFactory.make("qtimlvdetection", "mlvdetection0")
    Gst.util_set_object_arg(mlvdetection_0, "threshold", "75.0")
    Gst.util_set_object_arg(mlvdetection_0, "results", "4")
    Gst.util_set_object_arg(mlvdetection_0, "module", "yolov8")
    Gst.util_set_object_arg(
        mlvdetection_0, "labels", "/opt/data/yolov8n.labels"
    )
    Gst.util_set_object_arg(
        mlvdetection_0,
        "constants",
        "YoloV8,q-offsets=<-107.0,-128.0,0.0>,\
        q-scales=<3.093529462814331,0.00390625,1.0>;",
    )

    capsfilter_0 = Gst.ElementFactory.make("capsfilter", "metamux0metacaps")
    Gst.util_set_object_arg(capsfilter_0, "caps", "text/x-raw")

    queue_5 = Gst.ElementFactory.make("queue")

    metamux_0 = Gst.ElementFactory.make("qtimetamux", "metamux0")

    overlay_0 = Gst.ElementFactory.make("qtioverlay", "overlay0")
    Gst.util_set_object_arg(overlay_0, "engine", "gles")

    queue_6 = Gst.ElementFactory.make("queue")

    # Stream 1
    filesrc_1 = Gst.ElementFactory.make("filesrc", "filesrc1")
    Gst.util_set_object_arg(
        filesrc_1, "location", "/opt/data/Draw_720p_180s_30FPS.mp4"
    )

    qtdemux_1 = Gst.ElementFactory.make("qtdemux", "qtdemux1")

    queue_7 = Gst.ElementFactory.make("queue")

    h264parse_1 = Gst.ElementFactory.make("h264parse", "h264parse1")
    Gst.util_set_object_arg(h264parse_1, "config-interval", "1")

    v4l2h264dec_1 = Gst.ElementFactory.make("v4l2h264dec", "v4l2h264dec1")
    Gst.util_set_object_arg(v4l2h264dec_1, "capture-io-mode", "5")
    Gst.util_set_object_arg(v4l2h264dec_1, "output-io-mode", "5")

    queue_8 = Gst.ElementFactory.make("queue")

    tee_1 = Gst.ElementFactory.make("tee", "split1")

    # Tee 1 video output for metamux 1
    queue_9 = Gst.ElementFactory.make("queue")

    # Tee 1 video output for mlvconverter 1
    mlvconverter_1 = Gst.ElementFactory.make("qtimlvconverter", "converter1")

    queue_10 = Gst.ElementFactory.make("queue")

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
        mltflite_1, "model", "/opt/data/yolov7_quantized.tflite"
    )

    queue_11 = Gst.ElementFactory.make("queue")

    mlvdetection_1 = Gst.ElementFactory.make("qtimlvdetection", "mlvdetection1")
    Gst.util_set_object_arg(mlvdetection_1, "threshold", "50.0")
    Gst.util_set_object_arg(mlvdetection_1, "results", "5")
    Gst.util_set_object_arg(mlvdetection_1, "module", "yolov8")
    Gst.util_set_object_arg(
        mlvdetection_1, "labels", "/opt/data/yolov8n.labels"
    )
    Gst.util_set_object_arg(
        mlvdetection_1,
        "constants",
        "YOLOv8,q-offsets=<-93.0, -128.0, 0.0>,\
        q-scales=<3.4220554, 0.0023370725102722645, 1.0>;",
    )

    capsfilter_1 = Gst.ElementFactory.make("capsfilter", "metamux1metacaps")
    Gst.util_set_object_arg(capsfilter_1, "caps", "text/x-raw")

    queue_12 = Gst.ElementFactory.make("queue")

    metamux_1 = Gst.ElementFactory.make("qtimetamux", "metamux1")

    overlay_1 = Gst.ElementFactory.make("qtioverlay", "overlay1")
    Gst.util_set_object_arg(overlay_1, "engine", "gles")

    queue_13 = Gst.ElementFactory.make("queue")

    # Stream 2
    filesrc_2 = Gst.ElementFactory.make("filesrc", "filesrc2")
    Gst.util_set_object_arg(
        filesrc_2, "location", "/opt/data/Animals_000_720p_180s_30FPS.mp4"
    )

    qtdemux_2 = Gst.ElementFactory.make("qtdemux", "qtdemux2")

    queue_14 = Gst.ElementFactory.make("queue")

    h264parse_2 = Gst.ElementFactory.make("h264parse", "h264parse2")
    Gst.util_set_object_arg(h264parse_2, "config-interval", "2")

    v4l2h264dec_2 = Gst.ElementFactory.make("v4l2h264dec", "v4l2h264dec2")
    Gst.util_set_object_arg(v4l2h264dec_2, "capture-io-mode", "5")
    Gst.util_set_object_arg(v4l2h264dec_2, "output-io-mode", "5")

    queue_15 = Gst.ElementFactory.make("queue")

    tee_2 = Gst.ElementFactory.make("tee", "split2")

    # Tee 2 video output for metamux 2
    queue_16 = Gst.ElementFactory.make("queue")

    # Tee 2 video output for mlvconverter 2
    mlvconverter_2 = Gst.ElementFactory.make("qtimlvconverter", "converter2")

    queue_17 = Gst.ElementFactory.make("queue")

    mltflite_2 = Gst.ElementFactory.make("qtimltflite", "inference2")
    Gst.util_set_object_arg(mltflite_2, "delegate", "external")
    Gst.util_set_object_arg(
        mltflite_2, "external-delegate-path", "libQnnTFLiteDelegate.so"
    )
    Gst.util_set_object_arg(
        mltflite_2,
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        mltflite_2, "model", "/opt/data/Resnet101_Quantized.tflite"
    )

    queue_18 = Gst.ElementFactory.make("queue")

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

    capsfilter_2 = Gst.ElementFactory.make("capsfilter", "metamux2metacaps")
    Gst.util_set_object_arg(capsfilter_2, "caps", "text/x-raw")

    queue_19 = Gst.ElementFactory.make("queue")

    metamux_2 = Gst.ElementFactory.make("qtimetamux", "metamux2")

    overlay_2 = Gst.ElementFactory.make("qtioverlay", "overlay2")
    Gst.util_set_object_arg(overlay_2, "engine", "gles")

    queue_20 = Gst.ElementFactory.make("queue")

    # Stream 3
    filesrc_3 = Gst.ElementFactory.make("filesrc", "filesrc3")
    Gst.util_set_object_arg(
        filesrc_3, "location", "/opt/data/Street_Bridge_720p_180s_30FPS.MOV"
    )

    qtdemux_3 = Gst.ElementFactory.make("qtdemux", "qtdemux3")

    queue_21 = Gst.ElementFactory.make("queue")

    h264parse_3 = Gst.ElementFactory.make("h264parse", "h264parse3")
    Gst.util_set_object_arg(h264parse_3, "config-interval", "2")

    v4l2h264dec_3 = Gst.ElementFactory.make("v4l2h264dec", "v4l2h264dec3")
    Gst.util_set_object_arg(v4l2h264dec_3, "capture-io-mode", "5")
    Gst.util_set_object_arg(v4l2h264dec_3, "output-io-mode", "5")

    queue_22 = Gst.ElementFactory.make("queue")

    tee_3 = Gst.ElementFactory.make("tee", "split3")

    # Tee 3 video output for composer
    queue_23 = Gst.ElementFactory.make("queue")

    # Tee 3 video output for mlvconverter 3
    mlvconverter_3 = Gst.ElementFactory.make("qtimlvconverter", "converter3")

    queue_24 = Gst.ElementFactory.make("queue")

    mltflite_3 = Gst.ElementFactory.make("qtimltflite", "inference3")
    Gst.util_set_object_arg(mltflite_3, "delegate", "external")
    Gst.util_set_object_arg(
        mltflite_3, "external-delegate-path", "libQnnTFLiteDelegate.so"
    )
    Gst.util_set_object_arg(
        mltflite_3,
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        mltflite_3, "model", "/opt/data/ffnet_40s_quantized.tflite"
    )

    queue_25 = Gst.ElementFactory.make("queue")

    mlvsegmentation = Gst.ElementFactory.make("qtimlvsegmentation")
    Gst.util_set_object_arg(mlvsegmentation, "module", "deeplab-argmax")
    Gst.util_set_object_arg(
        mlvsegmentation, "labels", "/opt/data/dv3-argmax.labels"
    )
    Gst.util_set_object_arg(
        mlvsegmentation,
        "constants",
        "FFNet-40S,q-offsets=<50.0>,q-scales=<0.31378185749053955>;",
    )

    queue_26 = Gst.ElementFactory.make("queue")

    # Side by side stream 0, stream 1, stream 2, stream 3
    composer = Gst.ElementFactory.make("qtivcomposer")
    Gst.util_set_object_arg(composer, "background", "0")

    queue_27 = Gst.ElementFactory.make("queue")

    display = Gst.ElementFactory.make("waylandsink")
    Gst.util_set_object_arg(display, "sync", "false")
    Gst.util_set_object_arg(display, "async", "false")
    Gst.util_set_object_arg(display, "fullscreen", "true")

    if (  # Stream 0
        not filesrc_0
        or not qtdemux_0
        or not queue_0
        or not h264parse_0
        or not v4l2h264dec_0
        or not queue_1
        or not tee_0
        or not queue_2
        or not mlvconverter_0
        or not queue_3
        or not mltflite_0
        or not queue_4
        or not mlvdetection_0
        or not capsfilter_0
        or not queue_5
        or not metamux_0
        or not overlay_0
        or not queue_6
        or
        # Stream 1
        not filesrc_1
        or not qtdemux_1
        or not queue_7
        or not h264parse_1
        or not v4l2h264dec_1
        or not queue_8
        or not tee_1
        or not queue_9
        or not mlvconverter_1
        or not queue_10
        or not mltflite_1
        or not queue_11
        or not mlvdetection_1
        or not capsfilter_1
        or not queue_12
        or not metamux_1
        or not overlay_1
        or not queue_13
        or
        # Stream 2
        not filesrc_2
        or not qtdemux_2
        or not queue_14
        or not h264parse_2
        or not v4l2h264dec_2
        or not queue_15
        or not tee_2
        or not queue_16
        or not mlvconverter_2
        or not queue_17
        or not mltflite_2
        or not queue_18
        or not mlvclassification
        or not capsfilter_2
        or not queue_19
        or not metamux_2
        or not overlay_2
        or not queue_20
        or
        # Stream 3
        not filesrc_3
        or not qtdemux_3
        or not queue_21
        or not h264parse_3
        or not v4l2h264dec_3
        or not queue_22
        or not tee_3
        or not queue_23
        or not mlvconverter_3
        or not queue_24
        or not mltflite_3
        or not queue_25
        or not mlvsegmentation
        or not queue_26
        or
        # Side by side
        not composer
        or not queue_27
        or not display
    ):
        print("Failed to create all elements!")
        sys.exit(1)

    # Add all elements
    # Stream 0
    pipe.add(filesrc_0)
    pipe.add(qtdemux_0)
    pipe.add(queue_0)
    pipe.add(h264parse_0)
    pipe.add(v4l2h264dec_0)
    pipe.add(queue_1)
    pipe.add(tee_0)
    pipe.add(queue_2)
    pipe.add(mlvconverter_0)
    pipe.add(queue_3)
    pipe.add(mltflite_0)
    pipe.add(queue_4)
    pipe.add(mlvdetection_0)
    pipe.add(capsfilter_0)
    pipe.add(queue_5)
    pipe.add(metamux_0)
    pipe.add(overlay_0)
    pipe.add(queue_6)
    # Stream 1
    pipe.add(filesrc_1)
    pipe.add(qtdemux_1)
    pipe.add(queue_7)
    pipe.add(h264parse_1)
    pipe.add(v4l2h264dec_1)
    pipe.add(queue_8)
    pipe.add(tee_1)
    pipe.add(queue_9)
    pipe.add(mlvconverter_1)
    pipe.add(queue_10)
    pipe.add(mltflite_1)
    pipe.add(queue_11)
    pipe.add(mlvdetection_1)
    pipe.add(capsfilter_1)
    pipe.add(queue_12)
    pipe.add(metamux_1)
    pipe.add(overlay_1)
    pipe.add(queue_13)
    # Stream 2
    pipe.add(filesrc_2)
    pipe.add(qtdemux_2)
    pipe.add(queue_14)
    pipe.add(h264parse_2)
    pipe.add(v4l2h264dec_2)
    pipe.add(queue_15)
    pipe.add(tee_2)
    pipe.add(queue_16)
    pipe.add(mlvconverter_2)
    pipe.add(queue_17)
    pipe.add(mltflite_2)
    pipe.add(queue_18)
    pipe.add(mlvclassification)
    pipe.add(capsfilter_2)
    pipe.add(queue_19)
    pipe.add(metamux_2)
    pipe.add(overlay_2)
    pipe.add(queue_20)
    # Stream 3
    pipe.add(filesrc_3)
    pipe.add(qtdemux_3)
    pipe.add(queue_21)
    pipe.add(h264parse_3)
    pipe.add(v4l2h264dec_3)
    pipe.add(queue_22)
    pipe.add(tee_3)
    pipe.add(queue_23)
    pipe.add(mlvconverter_3)
    pipe.add(queue_24)
    pipe.add(mltflite_3)
    pipe.add(queue_25)
    pipe.add(mlvsegmentation)
    pipe.add(queue_26)
    # Side by side
    pipe.add(composer)
    pipe.add(queue_27)
    pipe.add(display)

    # Link most of the elements
    linked = True

    # Stream 0
    linked = linked and filesrc_0.link(qtdemux_0)
    # qtdemux_0 video pad will link with h264parse_0
    linked = linked and queue_0.link(h264parse_0)
    linked = linked and h264parse_0.link(v4l2h264dec_0)
    linked = linked and v4l2h264dec_0.link(queue_1)
    linked = linked and queue_1.link(tee_0)

    # tee_0 will link with metamux_0 video pad
    linked = linked and queue_2.link(metamux_0)

    # tee_0 will link with mlvconverter_0
    linked = linked and mlvconverter_0.link(queue_3)
    linked = linked and queue_3.link(mltflite_0)
    linked = linked and mltflite_0.link(queue_4)
    linked = linked and queue_4.link(mlvdetection_0)
    linked = linked and mlvdetection_0.link(capsfilter_0)
    linked = linked and capsfilter_0.link(queue_5)
    # queue_5 will link metamux_0 metadata pad

    linked = linked and metamux_0.link(overlay_0)
    linked = linked and overlay_0.link(queue_6)
    # queue_6 will link with composer

    # Stream 1
    linked = linked and filesrc_1.link(qtdemux_1)
    # qtdemux_1 video pad will link with h264parse_1
    linked = linked and queue_7.link(h264parse_1)
    linked = linked and h264parse_1.link(v4l2h264dec_1)
    linked = linked and v4l2h264dec_1.link(queue_8)
    linked = linked and queue_8.link(tee_1)

    # tee_1 will link with metamux_1 video pad
    linked = linked and queue_9.link(metamux_1)

    # tee_1 will link with mlvconverter_1
    linked = linked and mlvconverter_1.link(queue_10)
    linked = linked and queue_10.link(mltflite_1)
    linked = linked and mltflite_1.link(queue_11)
    linked = linked and queue_11.link(mlvdetection_1)
    linked = linked and mlvdetection_1.link(capsfilter_1)
    linked = linked and capsfilter_1.link(queue_12)
    # queue_12 will link metamux_1 metadata pad

    linked = linked and metamux_1.link(overlay_1)
    linked = linked and overlay_1.link(queue_13)
    # queue_13 will link with composer

    # Stream 2
    linked = linked and filesrc_2.link(qtdemux_2)
    # qtdemux_2 video pad will link with h264parse_2
    linked = linked and queue_14.link(h264parse_2)
    linked = linked and h264parse_2.link(v4l2h264dec_2)
    linked = linked and v4l2h264dec_2.link(queue_15)
    linked = linked and queue_15.link(tee_2)

    # tee_2 will link with metamux_2 video pad
    linked = linked and queue_16.link(metamux_2)

    # tee_2 will link with mlvconverter_2
    linked = linked and mlvconverter_2.link(queue_17)
    linked = linked and queue_17.link(mltflite_2)
    linked = linked and mltflite_2.link(queue_18)
    linked = linked and queue_18.link(mlvclassification)
    linked = linked and mlvclassification.link(capsfilter_2)
    linked = linked and capsfilter_2.link(queue_19)
    # queue_19 will link metamux_2 metadata pad

    linked = linked and metamux_2.link(overlay_2)
    linked = linked and overlay_2.link(queue_20)
    # queue_20 will link with composer

    # Stream 3
    linked = linked and filesrc_3.link(qtdemux_3)
    # qtdemux_2 video pad will link with h264parse_2
    linked = linked and queue_21.link(h264parse_3)
    linked = linked and h264parse_3.link(v4l2h264dec_3)
    linked = linked and v4l2h264dec_3.link(queue_22)
    linked = linked and queue_22.link(tee_3)

    # tee_3 will link with composer

    # tee_3 will link with mlvconverter_3
    linked = linked and mlvconverter_3.link(queue_24)
    linked = linked and queue_24.link(mltflite_3)
    linked = linked and mltflite_3.link(queue_25)
    linked = linked and queue_25.link(mlvsegmentation)
    linked = linked and mlvsegmentation.link(queue_26)
    # mlvsegmentation will link with composer

    # Side by side
    linked = linked and composer.link(queue_27)
    linked = linked and queue_27.link(display)

    if not linked:
        print("Failed to link all elements!")
        sys.exit(1)

    # Link qtdemux for stream 0
    def on_pad_added_0(elem, pad):
        if "video" in pad.get_name():
            queue_0_sink_pad = queue_0.get_static_pad("sink")
            if pad.link(queue_0_sink_pad) != Gst.PadLinkReturn.OK:
                print("Failed to link qtdemux_0!")
                sys.exit(1)

    qtdemux_0.connect("pad-added", on_pad_added_0)

    # Link tee for stream 0
    tee_0_src_pad_template = tee_0.get_pad_template("src_%u")
    tee_0_mlvconverter_0_src_pad = tee_0.request_pad(
        tee_0_src_pad_template, None, None
    )
    tee_0_queue_2_src_pad = tee_0.request_pad(
        tee_0_src_pad_template, None, None
    )

    if not tee_0_mlvconverter_0_src_pad or not tee_0_queue_2_src_pad:
        print("Failed to request pads for tee_0!")
        sys.exit(1)

    mlvconverter_0_sink_pad = mlvconverter_0.get_static_pad("sink")
    queue_2_sink_pad = queue_2.get_static_pad("sink")

    if (
        tee_0_mlvconverter_0_src_pad.link(mlvconverter_0_sink_pad)
        != Gst.PadLinkReturn.OK
        or tee_0_queue_2_src_pad.link(queue_2_sink_pad) != Gst.PadLinkReturn.OK
    ):
        print("Failed to link tee_0!")
        sys.exit(1)

    # Link metamux for stream 0
    metamux_0_sink_pad_template = metamux_0.get_pad_template("data_%u")
    metamux_0_metadata_sink_pad = metamux_0.request_pad(
        metamux_0_sink_pad_template, None, None
    )

    if not metamux_0_metadata_sink_pad:
        print("Failed to request pad for metamux_0!")
        sys.exit(1)

    queue_5_src_pad = queue_5.get_static_pad("src")

    if (
        queue_5_src_pad.link(metamux_0_metadata_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link metamux_0!")
        sys.exit(1)

    # Link qtdemux for stream 1
    def on_pad_added_1(elem, pad):
        if "video" in pad.get_name():
            queue_7_sink_pad = queue_7.get_static_pad("sink")
            if pad.link(queue_7_sink_pad) != Gst.PadLinkReturn.OK:
                print("Failed to link qtdemux_1!")
                sys.exit(1)

    qtdemux_1.connect("pad-added", on_pad_added_1)

    # Link tee for stream 1
    tee_1_src_pad_template = tee_1.get_pad_template("src_%u")
    tee_1_mlvconverter_1_src_pad = tee_1.request_pad(
        tee_1_src_pad_template, None, None
    )
    tee_1_queue_9_src_pad = tee_1.request_pad(
        tee_1_src_pad_template, None, None
    )

    if not tee_1_mlvconverter_1_src_pad or not tee_1_queue_9_src_pad:
        print("Failed to request pads for tee_1!")
        sys.exit(1)

    mlvconverter_1_sink_pad = mlvconverter_1.get_static_pad("sink")
    queue_9_sink_pad = queue_9.get_static_pad("sink")

    if (
        tee_1_mlvconverter_1_src_pad.link(mlvconverter_1_sink_pad)
        != Gst.PadLinkReturn.OK
        or tee_1_queue_9_src_pad.link(queue_9_sink_pad) != Gst.PadLinkReturn.OK
    ):
        print("Failed to link tee_1!")
        sys.exit(1)

    # Link metamux for stream 1
    metamux_1_sink_pad_template = metamux_1.get_pad_template("data_%u")
    metamux_1_metadata_sink_pad = metamux_1.request_pad(
        metamux_1_sink_pad_template, None, None
    )

    if not metamux_1_metadata_sink_pad:
        print("Failed to request pad for metamux_1!")
        sys.exit(1)

    queue_12_src_pad = queue_12.get_static_pad("src")

    if (
        queue_12_src_pad.link(metamux_1_metadata_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link metamux_1!")
        sys.exit(1)

    # Link qtdemux for stream 2
    def on_pad_added_2(elem, pad):
        if "video" in pad.get_name():
            queue_14_sink_pad = queue_14.get_static_pad("sink")
            if pad.link(queue_14_sink_pad) != Gst.PadLinkReturn.OK:
                print("Failed to link qtdemux_2!")
                sys.exit(1)

    qtdemux_2.connect("pad-added", on_pad_added_2)

    # Link tee for stream 2
    tee_2_src_pad_template = tee_2.get_pad_template("src_%u")
    tee_2_mlvconverter_2_src_pad = tee_2.request_pad(
        tee_2_src_pad_template, None, None
    )
    tee_2_queue_16_src_pad = tee_2.request_pad(
        tee_2_src_pad_template, None, None
    )

    if not tee_2_mlvconverter_2_src_pad or not tee_2_queue_16_src_pad:
        print("Failed to request pads for tee_2!")
        sys.exit(1)

    mlvconverter_2_sink_pad = mlvconverter_2.get_static_pad("sink")
    queue_16_sink_pad = queue_16.get_static_pad("sink")

    if (
        tee_2_mlvconverter_2_src_pad.link(mlvconverter_2_sink_pad)
        != Gst.PadLinkReturn.OK
        or tee_2_queue_16_src_pad.link(queue_16_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link tee_2!")
        sys.exit(1)

    # Link metamux for stream 2
    metamux_2_sink_pad_template = metamux_2.get_pad_template("data_%u")
    metamux_2_metadata_sink_pad = metamux_2.request_pad(
        metamux_2_sink_pad_template, None, None
    )

    if not metamux_2_metadata_sink_pad:
        print("Failed to request pad for metamux_2!")
        sys.exit(1)

    queue_19_src_pad = queue_19.get_static_pad("src")

    if (
        queue_19_src_pad.link(metamux_2_metadata_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link metamux_2!")
        sys.exit(1)

    # Link qtdemux for stream 3
    def on_pad_added_3(elem, pad):
        if "video" in pad.get_name():
            queue_21_sink_pad = queue_21.get_static_pad("sink")
            if pad.link(queue_21_sink_pad) != Gst.PadLinkReturn.OK:
                print("Failed to link qtdemux_3!")
                sys.exit(1)

    qtdemux_3.connect("pad-added", on_pad_added_3)

    # Link tee for stream 3
    tee_3_src_pad_template = tee_3.get_pad_template("src_%u")
    tee_3_mlvconverter_3_src_pad = tee_3.request_pad(
        tee_3_src_pad_template, None, None
    )
    tee_2_queue_23_src_pad = tee_3.request_pad(
        tee_3_src_pad_template, None, None
    )

    if not tee_3_mlvconverter_3_src_pad or not tee_2_queue_23_src_pad:
        print("Failed to request pads for tee_3!")
        sys.exit(1)

    mlvconverter_3_sink_pad = mlvconverter_3.get_static_pad("sink")
    queue_23_sink_pad = queue_23.get_static_pad("sink")

    if (
        tee_3_mlvconverter_3_src_pad.link(mlvconverter_3_sink_pad)
        != Gst.PadLinkReturn.OK
        or tee_2_queue_23_src_pad.link(queue_23_sink_pad)
        != Gst.PadLinkReturn.OK
    ):
        print("Failed to link tee_3!")
        sys.exit(1)

    # Link composer
    composer_sink_pad_template = composer.get_pad_template("sink_%u")
    composer_sink_0_pad = composer.request_pad(
        composer_sink_pad_template, None, None
    )
    composer_sink_1_pad = composer.request_pad(
        composer_sink_pad_template, None, None
    )
    composer_sink_2_pad = composer.request_pad(
        composer_sink_pad_template, None, None
    )
    composer_sink_3_pad = composer.request_pad(
        composer_sink_pad_template, None, None
    )
    composer_sink_4_pad = composer.request_pad(
        composer_sink_pad_template, None, None
    )

    if (
        not composer_sink_0_pad
        or not composer_sink_1_pad
        or not composer_sink_2_pad
        or not composer_sink_3_pad
        or not composer_sink_4_pad
    ):
        print("Failed to request pads for composer!")
        sys.exit(1)

    Gst.util_set_object_arg(composer_sink_0_pad, "position", "<0, 0>")
    Gst.util_set_object_arg(composer_sink_0_pad, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_1_pad, "position", "<1280, 0>")
    Gst.util_set_object_arg(composer_sink_1_pad, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_2_pad, "position", "<0, 720>")
    Gst.util_set_object_arg(composer_sink_2_pad, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_3_pad, "position", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_3_pad, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_4_pad, "position", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_4_pad, "dimensions", "<1280, 720>")

    queue_6_src_pad = queue_6.get_static_pad("src")
    queue_13_src_pad = queue_13.get_static_pad("src")
    queue_20_src_pad = queue_20.get_static_pad("src")
    queue_23_src_pad = queue_23.get_static_pad("src")
    queue_26_src_pad = queue_26.get_static_pad("src")

    if (
        queue_6_src_pad.link(composer_sink_0_pad) != Gst.PadLinkReturn.OK
        or queue_13_src_pad.link(composer_sink_1_pad) != Gst.PadLinkReturn.OK
        or queue_20_src_pad.link(composer_sink_2_pad) != Gst.PadLinkReturn.OK
        or queue_23_src_pad.link(composer_sink_3_pad) != Gst.PadLinkReturn.OK
        or queue_26_src_pad.link(composer_sink_4_pad) != Gst.PadLinkReturn.OK
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
