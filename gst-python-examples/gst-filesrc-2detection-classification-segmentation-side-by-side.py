#!/usr/bin/env python3

################################################################################
# Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

import os
import sys
import signal
import gi
import argparse

gi.require_version("Gst", "1.0")
gi.require_version("GLib", "2.0")
from gi.repository import Gst, GLib

DESCRIPTION = """
The application uses:
- YOLOv8 TFLite model to identify the object in scene from video file and
overlay the bounding boxes over the detected objects
- YOLOv8 TFLite model to identify the object in scene from video file and
overlay the bounding boxes over the detected objects
- Resnet101 TFLite model to classify scene from video file and overlay the
classification labels on the top left corner
- FFNet40S TFLite model to produce semantic segmentations for video file
Then the results are shown side by side on the display.

The default file paths in the python script are as follows:
- Detection model (YOLOv8): /etc/models/YoloV8N_Detection_Quantized.tflite
- Detection labels: /etc/labels/yolov8n.labels
- Classification model: /etc/models/Resnet101_Quantized.tflite
- Classification labels: /etc/labels/resnet101.labels
- Segmentation model: /etc/models/ffnet_40s_quantized.tflite
- Segmentation labels: /etc/labels/dv3-argmax.labels
- Input video for detection: /etc/media/detection_input.mp4
- Input video for classification: /etc/media/classification_input.mp4
- Input video for segmentation: /etc/media/segmentation_input.MOV

To override the default settings,
please configure the corresponding module and constants as well.
"""

# Configurations for Detection (0)
DEFAULT_DETECTION_INPUT_0 = "/etc/media/detection_input.mp4"
DEFAULT_DETECTION_MODEL_0 = "/etc/models/YoloV8N_Detection_Quantized.tflite"
DEFAULT_DETECTION_MODULE_0 = "yolov8"
DEFAULT_DETECTION_LABELS_0 = "/etc/labels/yolov8n.labels"
DEFAULT_DETECTION_CONSTANTS_0 = "YoloV8,q-offsets=<-107.0,-128.0,0.0>,\
    q-scales=<3.093529462814331,0.00390625,1.0>;"

# Configurations for Detection (1)
DEFAULT_DETECTION_INPUT_1 = "/etc/media/detection_input.mp4"
DEFAULT_DETECTION_MODEL_1 = "/etc/models/YoloV8N_Detection_Quantized.tflite"
DEFAULT_DETECTION_MODULE_1 = "yolov8"
DEFAULT_DETECTION_LABELS_1 = "/etc/labels/yolov8n.labels"
DEFAULT_DETECTION_CONSTANTS_1 = "YoloV8,q-offsets=<-107.0,-128.0,0.0>,\
    q-scales=<3.093529462814331,0.00390625,1.0>;"

# Configurations for Classification
DEFAULT_CLASSIFICATION_INPUT = "/etc/media/classification_input.mp4"
DEFAULT_CLASSIFICATION_MODEL = "/etc/models/Resnet101_Quantized.tflite"
DEFAULT_CLASSIFICATION_MODULE = "mobilenet"
DEFAULT_CLASSIFICATION_LABELS = "/etc/labels/resnet101.labels"
DEFAULT_CLASSIFICATION_CONSTANTS = "Mobilenet,q-offsets=<-82.0>,\
    q-scales=<0.21351955831050873>;"

# Configurations for Segmentation
DEFAULT_SEGMENTATION_INPUT = "/etc/media/segmentation_input.MOV"
DEFAULT_SEGMENTATION_MODEL = "/etc/models/ffnet_40s_quantized.tflite"
DEFAULT_SEGMENTATION_MODULE = "deeplab-argmax"
DEFAULT_SEGMENTATION_LABELS = "/etc/labels/dv3-argmax.labels"
DEFAULT_SEGMENTATION_CONSTANTS = "FFNet-40S,q-offsets=<50.0>,\
    q-scales=<0.31378185749053955>;"

eos_received = False
def create_element(factory_name, name):
    """Create a GStreamer element."""
    element = Gst.ElementFactory.make(factory_name, name)
    if not element:
        raise Exception(f"Failed to create {factory_name} named {name}!")
    return element


def link_elements(link_orders, elements):
    """Link elements in the specified orders."""
    for link_order in link_orders:
        src = None
        for element in link_order:
            dest = elements[element]
            if src and not src.link(dest):
                raise Exception(
                    f"Failed to link element\
                    {src.get_name()} with {dest.get_name()}"
                )
            src = dest


def construct_pipeline(pipe):
    """Initialize and link elements for the GStreamer pipeline."""
    # Parse arguments
    parser = argparse.ArgumentParser(
        add_help=False,
        formatter_class=type(
            "CustomFormatter",
            (
                argparse.ArgumentDefaultsHelpFormatter,
                argparse.RawTextHelpFormatter,
            ),
            {},
        ),
    )

    parser.add_argument(
        "-h",
        "--help",
        action="help",
        default=argparse.SUPPRESS,
        help=DESCRIPTION,
    )
    parser.add_argument(
        "--detection_input_0", type=str, default=DEFAULT_DETECTION_INPUT_0,
        help="Input File Path for Detection (0)"
    )
    parser.add_argument(
        "--detection_model_0", type=str, default=DEFAULT_DETECTION_MODEL_0,
        help="Path to TfLite Object Detection Model (0)"
    )
    parser.add_argument(
        "--detection_module_0", type=str, default=DEFAULT_DETECTION_MODULE_0,
        help="Object Detection module for post-procesing (0)"
    )
    parser.add_argument(
        "--detection_labels_0", type=str, default=DEFAULT_DETECTION_LABELS_0,
        help="Path to TfLite Object Detection Labels (0)"
    )
    parser.add_argument(
        "--detection_constants_0", type=str, default=DEFAULT_DETECTION_CONSTANTS_0,
        help="Constants for TfLite Object Detection Model (0)"
    )
    parser.add_argument(
        "--detection_input_1", type=str, default=DEFAULT_DETECTION_INPUT_1,
        help="Input File Path for Detection (1)"
    )
    parser.add_argument(
        "--detection_model_1", type=str, default=DEFAULT_DETECTION_MODEL_1,
        help="Path to TfLite Object Detection Model (1)"
    )
    parser.add_argument(
        "--detection_module_1", type=str, default=DEFAULT_DETECTION_MODULE_1,
        help="Object Detection module for post-procesing (1)"
    )
    parser.add_argument(
        "--detection_labels_1", type=str, default=DEFAULT_DETECTION_LABELS_1,
        help="Path to TfLite Object Detection Labels (1)"
    )
    parser.add_argument(
        "--detection_constants_1", type=str, default=DEFAULT_DETECTION_CONSTANTS_1,
        help="Constants for TfLite Object Detection Model (1)"
    )
    parser.add_argument(
        "--classification_input", type=str, default=DEFAULT_CLASSIFICATION_INPUT,
        help="Input File Path for Classification"
    )
    parser.add_argument(
        "--classification_model", type=str, default=DEFAULT_CLASSIFICATION_MODEL,
        help="Path to TfLite Classification Model"
    )
    parser.add_argument(
        "--classification_module", type=str, default=DEFAULT_CLASSIFICATION_MODULE,
        help="Classification module for post-procesing"
    )
    parser.add_argument(
        "--classification_labels", type=str, default=DEFAULT_CLASSIFICATION_LABELS,
        help="Path to TfLite Classification Labels"
    )
    parser.add_argument(
        "--classification_constants", type=str, default=DEFAULT_CLASSIFICATION_CONSTANTS,
        help="Constants for TfLite Classification Model"
    )
    parser.add_argument(
        "--segmentation_input", type=str, default=DEFAULT_SEGMENTATION_INPUT,
        help="Input File Path for Segmentation"
    )
    parser.add_argument(
        "--segmentation_model", type=str, default=DEFAULT_SEGMENTATION_MODEL,
        help="Path to TfLite Segmentation Model"
    )
    parser.add_argument(
        "--segmentation_module", type=str, default=DEFAULT_SEGMENTATION_MODULE,
        help="Segmentation module for post-procesing"
    )
    parser.add_argument(
        "--segmentation_labels", type=str, default=DEFAULT_SEGMENTATION_LABELS,
        help="Path to TfLite Segmentation Labels"
    )
    parser.add_argument(
        "--segmentation_constants", type=str, default=DEFAULT_SEGMENTATION_CONSTANTS,
        help="Constants for TfLite Segmentation Model"
    )

    args = parser.parse_args()

    detection = [
        {
        "input": args.detection_input_0,
        "model": args.detection_model_0,
        "module": args.detection_module_0,
        "labels": args.detection_labels_0,
        "constants": args.detection_constants_0
        },
        {
        "input": args.detection_input_1,
        "model": args.detection_model_1,
        "module": args.detection_module_1,
        "labels": args.detection_labels_1,
        "constants": args.detection_constants_1
        }
    ]
    classification = {
        "input": args.classification_input,
        "model": args.classification_model,
        "module": args.classification_module,
        "labels": args.classification_labels,
        "constants": args.classification_constants
    }
    segmentation = {
        "input": args.segmentation_input,
        "model": args.segmentation_model,
        "module": args.segmentation_module,
        "labels": args.segmentation_labels,
        "constants": args.segmentation_constants
    }

    # Create all elements
    # fmt: off
    elements = {
        # Stream 0
        "filesrc_0":         create_element("filesrc", "filesrc0"),
        "qtdemux_0":         create_element("qtdemux", "qtdemux0"),
        "h264parse_0":       create_element("h264parse", "h264parser0"),
        "v4l2h264dec_0":     create_element("v4l2h264dec", "v4l2h264decoder0"),
        "deccaps_0":         create_element("capsfilter", "deccaps0"),
        "tee_0":             create_element("tee", "split0"),
        "mlvconverter_0":    create_element("qtimlvconverter", "converter0"),
        "queue_0":           create_element("queue", "queue0"),
        "mltflite_0":        create_element("qtimltflite", "inference0"),
        "queue_1":           create_element("queue", "queue1"),
        "mlvdetection_0":    create_element("qtimlvdetection", "detection0"),
        "capsfilter_0":      create_element("capsfilter", "metamux0metacaps"),
        "queue_2":           create_element("queue", "queue2"),
        "metamux_0":         create_element("qtimetamux", "metamux0"),
        "overlay_0":         create_element("qtivoverlay", "overlay0"),
        # Stream 1
        "filesrc_1":         create_element("filesrc", "filesrc1"),
        "qtdemux_1":         create_element("qtdemux", "qtdemux1"),
        "h264parse_1":       create_element("h264parse", "h264parser1"),
        "v4l2h264dec_1":     create_element("v4l2h264dec", "v4l2h264decoder1"),
        "deccaps_1":         create_element("capsfilter", "deccaps1"),
        "tee_1":             create_element("tee", "split1"),
        "mlvconverter_1":    create_element("qtimlvconverter", "converter1"),
        "queue_3":           create_element("queue", "queue3"),
        "mltflite_1":        create_element("qtimltflite", "inference1"),
        "queue_4":           create_element("queue", "queue4"),
        "mlvdetection_1":    create_element("qtimlvdetection", "detection1"),
        "capsfilter_1":      create_element("capsfilter", "metamux1metacaps"),
        "queue_5":           create_element("queue", "queue5"),
        "metamux_1":         create_element("qtimetamux", "metamux1"),
        "overlay_1":         create_element("qtivoverlay", "overlay1"),
        # Stream 2
        "filesrc_2":         create_element("filesrc", "filesrc2"),
        "qtdemux_2":         create_element("qtdemux", "qtdemux2"),
        "h264parse_2":       create_element("h264parse", "h264parser2"),
        "v4l2h264dec_2":     create_element("v4l2h264dec", "v4l2h264decoder2"),
        "deccaps_2":         create_element("capsfilter", "deccaps2"),
        "tee_2":             create_element("tee", "split2"),
        "mlvconverter_2":    create_element("qtimlvconverter", "converter2"),
        "queue_6":           create_element("queue", "queue6"),
        "mltflite_2":        create_element("qtimltflite", "inference2"),
        "queue_7":           create_element("queue", "queue7"),
        "mlvclassification": create_element("qtimlvclassification", "classification"),
        "capsfilter_2":      create_element("capsfilter", "metamux2metacaps"),
        "queue_8":           create_element("queue", "queue8"),
        "metamux_2":         create_element("qtimetamux", "metamux2"),
        "overlay_2":         create_element("qtivoverlay", "overlay2"),
        # Stream 3
        "filesrc_3":         create_element("filesrc", "filesrc3"),
        "qtdemux_3":         create_element("qtdemux", "qtdemux3"),
        "h264parse_3":       create_element("h264parse", "h264parser3"),
        "v4l2h264dec_3":     create_element("v4l2h264dec", "v4l2h264decoder3"),
        "deccaps_3":         create_element("capsfilter", "deccaps3"),
        "tee_3":             create_element("tee", "split3"),
        "queue_9":           create_element("queue", "queue9"),
        "mlvconverter_3":    create_element("qtimlvconverter", "converter3"),
        "queue_10":          create_element("queue", "queue10"),
        "mltflite_3":        create_element("qtimltflite", "inference3"),
        "queue_11":          create_element("queue", "queue11"),
        "mlvsegmentation":   create_element("qtimlvsegmentation", "segmentation"),
        "capsfilter_3":      create_element("capsfilter", "metamux3metacaps"),
        "queue_12":          create_element("queue", "queue12"),
        # Side by side all streams
        "composer":          create_element("qtivcomposer", "composer"),
        "queue_13":          create_element("queue", "queue13"),
        "display":           create_element("waylandsink", "display")
    }
    # fmt: on

    # Set element properties
    # Stream 0
    Gst.util_set_object_arg(
        elements["filesrc_0"], "location", detection[0]["input"]
    )

    Gst.util_set_object_arg(elements["h264parse_0"], "config-interval", "1")

    Gst.util_set_object_arg(elements["v4l2h264dec_0"], "capture-io-mode", "dmabuf")
    Gst.util_set_object_arg(elements["v4l2h264dec_0"], "output-io-mode", "dmabuf")

    Gst.util_set_object_arg(
        elements["deccaps_0"], "caps", "video/x-raw,format=NV12"
    )

    Gst.util_set_object_arg(elements["mltflite_0"], "delegate", "external")
    Gst.util_set_object_arg(
        elements["mltflite_0"],
        "external-delegate-path",
        "libQnnTFLiteDelegate.so",
    )
    Gst.util_set_object_arg(
        elements["mltflite_0"],
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp,htp_precision=(string)1;",
    )
    Gst.util_set_object_arg(
        elements["mltflite_0"],
        "model",
        detection[0]["model"],
    )

    Gst.util_set_object_arg(elements["mlvdetection_0"], "threshold", "75.0")
    Gst.util_set_object_arg(elements["mlvdetection_0"], "results", "4")
    Gst.util_set_object_arg(
        elements["mlvdetection_0"], "module", detection[0]["module"]
    )
    Gst.util_set_object_arg(
        elements["mlvdetection_0"], "labels", detection[0]["labels"]
    )
    Gst.util_set_object_arg(
        elements["mlvdetection_0"], "constants", detection[0]["constants"],
    )

    Gst.util_set_object_arg(elements["capsfilter_0"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_0"], "engine", "gles")

    # Stream 1
    Gst.util_set_object_arg(
        elements["filesrc_1"], "location", detection[1]["input"]
    )

    Gst.util_set_object_arg(elements["h264parse_1"], "config-interval", "1")

    Gst.util_set_object_arg(elements["v4l2h264dec_1"], "capture-io-mode", "dmabuf")
    Gst.util_set_object_arg(elements["v4l2h264dec_1"], "output-io-mode", "dmabuf")

    Gst.util_set_object_arg(
        elements["deccaps_1"], "caps", "video/x-raw,format=NV12"
    )

    Gst.util_set_object_arg(elements["mltflite_1"], "delegate", "external")
    Gst.util_set_object_arg(
        elements["mltflite_1"],
        "external-delegate-path",
        "libQnnTFLiteDelegate.so",
    )
    Gst.util_set_object_arg(
        elements["mltflite_1"],
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp,htp_precision=(string)1;",
    )
    Gst.util_set_object_arg(
        elements["mltflite_1"],
        "model",
        detection[1]["model"],
    )

    Gst.util_set_object_arg(elements["mlvdetection_1"], "threshold", "75.0")
    Gst.util_set_object_arg(elements["mlvdetection_1"], "results", "4")
    Gst.util_set_object_arg(elements["mlvdetection_1"], "module", detection[1]["module"])
    Gst.util_set_object_arg(
        elements["mlvdetection_1"], "labels", detection[1]["labels"]
    )
    Gst.util_set_object_arg(
        elements["mlvdetection_1"], "constants", detection[1]["constants"],
    )

    Gst.util_set_object_arg(elements["capsfilter_1"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_1"], "engine", "gles")

    # Stream 2
    Gst.util_set_object_arg(
        elements["filesrc_2"], "location", classification["input"],
    )

    Gst.util_set_object_arg(elements["h264parse_2"], "config-interval", "2")

    Gst.util_set_object_arg(elements["v4l2h264dec_2"], "capture-io-mode", "dmabuf")
    Gst.util_set_object_arg(elements["v4l2h264dec_2"], "output-io-mode", "dmabuf")

    Gst.util_set_object_arg(
        elements["deccaps_2"], "caps", "video/x-raw,format=NV12"
    )

    Gst.util_set_object_arg(elements["mltflite_2"], "delegate", "external")
    Gst.util_set_object_arg(
        elements["mltflite_2"],
        "external-delegate-path",
        "libQnnTFLiteDelegate.so",
    )
    Gst.util_set_object_arg(
        elements["mltflite_2"],
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        elements["mltflite_2"], "model", classification["model"]
    )

    Gst.util_set_object_arg(elements["mlvclassification"], "threshold", "51.0")
    Gst.util_set_object_arg(elements["mlvclassification"], "results", "5")
    Gst.util_set_object_arg(
        elements["mlvclassification"], "module", classification["module"]
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"], "labels", classification["labels"]
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"], "extra-operation", "softmax"
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"], "constants", classification["constants"],
    )

    Gst.util_set_object_arg(elements["capsfilter_2"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_2"], "engine", "gles")

    # Stream 3
    Gst.util_set_object_arg(
        elements["filesrc_3"], "location", segmentation["input"],
    )

    Gst.util_set_object_arg(elements["h264parse_3"], "config-interval", "2")

    Gst.util_set_object_arg(elements["v4l2h264dec_3"], "capture-io-mode", "dmabuf")
    Gst.util_set_object_arg(elements["v4l2h264dec_3"], "output-io-mode", "dmabuf")

    Gst.util_set_object_arg(
        elements["deccaps_3"], "caps", "video/x-raw,format=NV12"
    )

    Gst.util_set_object_arg(elements["mltflite_3"], "delegate", "external")
    Gst.util_set_object_arg(
        elements["mltflite_3"],
        "external-delegate-path",
        "libQnnTFLiteDelegate.so",
    )
    Gst.util_set_object_arg(
        elements["mltflite_3"],
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        elements["mltflite_3"], "model", segmentation["model"]
    )

    Gst.util_set_object_arg(
        elements["mlvsegmentation"], "module", segmentation["module"]
    )
    Gst.util_set_object_arg(
        elements["mlvsegmentation"], "labels", segmentation["labels"]
    )
    Gst.util_set_object_arg(
        elements["mlvsegmentation"], "constants", segmentation["constants"],
    )

    # Side by side all streams
    Gst.util_set_object_arg(elements["composer"], "background", "0")

    Gst.util_set_object_arg(elements["display"], "sync", "false")
    Gst.util_set_object_arg(elements["display"], "fullscreen", "true")

    # Add all elements
    for element in elements.values():
        pipe.add(element)

    # Link most of the elements
    # fmt: off
    link_orders = [
        [ "filesrc_0", "qtdemux_0" ],
        [
            "h264parse_0", "v4l2h264dec_0", "deccaps_0", "tee_0", "metamux_0",
            "overlay_0", "composer"
        ],
        [
            "tee_0", "mlvconverter_0", "queue_0", "mltflite_0", "queue_1",
            "mlvdetection_0", "queue_2", "capsfilter_0", "metamux_0"
        ],
        [ "filesrc_1", "qtdemux_1" ],
        [
            "h264parse_1", "v4l2h264dec_1", "deccaps_1", "tee_1", "metamux_1",
            "overlay_1", "composer"
        ],
        [
            "tee_1", "mlvconverter_1", "queue_3", "mltflite_1", "queue_4",
            "mlvdetection_1", "queue_5", "capsfilter_1", "metamux_1"
        ],
        [ "filesrc_2", "qtdemux_2" ],
        [
            "h264parse_2", "v4l2h264dec_2", "deccaps_2", "tee_2", "metamux_2",
            "overlay_2", "composer"
        ],
        [
            "tee_2", "mlvconverter_2", "queue_6", "mltflite_2", "queue_7",
            "mlvclassification", "queue_8", "capsfilter_2", "metamux_2"
        ],
        [ "filesrc_3", "qtdemux_3" ],
        [
            "h264parse_3", "v4l2h264dec_3", "deccaps_3", "tee_3", "queue_9",
            "composer"
        ],
        [
            "tee_3", "mlvconverter_3", "queue_10", "mltflite_3", "queue_11",
            "mlvsegmentation", "queue_12", "composer"
        ],
        [ "composer", "queue_13", "display" ]
    ]
    # fmt: on
    link_elements(link_orders, elements)

    # Link qtdemux elements
    def on_pad_added(elem, pad, dest):
        if "video" in pad.get_name():
            sink_pad = dest.get_static_pad("sink")
            if (
                not sink_pad.is_linked()
                and pad.link(sink_pad) != Gst.PadLinkReturn.OK
            ):
                raise (
                    f"Failed to link {elem.get_name()} with {dest.get_name()}!"
                )

    elements["qtdemux_0"].connect(
        "pad-added", on_pad_added, elements["h264parse_0"]
    )
    elements["qtdemux_1"].connect(
        "pad-added", on_pad_added, elements["h264parse_1"]
    )
    elements["qtdemux_2"].connect(
        "pad-added", on_pad_added, elements["h264parse_2"]
    )
    elements["qtdemux_3"].connect(
        "pad-added", on_pad_added, elements["h264parse_3"]
    )

    # Set pad properties
    composer_sink_0 = elements["composer"].get_static_pad("sink_0")
    composer_sink_1 = elements["composer"].get_static_pad("sink_1")
    composer_sink_2 = elements["composer"].get_static_pad("sink_2")
    composer_sink_3 = elements["composer"].get_static_pad("sink_3")
    composer_sink_4 = elements["composer"].get_static_pad("sink_4")
    Gst.util_set_object_arg(composer_sink_0, "position", "<0, 0>")
    Gst.util_set_object_arg(composer_sink_0, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_1, "position", "<1280, 0>")
    Gst.util_set_object_arg(composer_sink_1, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_2, "position", "<0, 720>")
    Gst.util_set_object_arg(composer_sink_2, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_3, "position", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_3, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_4, "position", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_4, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_4, "alpha", "0.5")
    composer_sink_0 = None
    composer_sink_1 = None
    composer_sink_2 = None
    composer_sink_3 = None
    composer_sink_4 = None


def quit_mainloop(loop):
    """Quit the mainloop if it is running."""
    if loop.is_running():
        print("Quitting mainloop!")
        loop.quit()
    else:
        print("Loop is not running!")


def bus_call(_, message, loop):
    """Handle bus messages."""
    global eos_received

    message_type = message.type
    if message_type == Gst.MessageType.EOS:
        print("EoS received!")
        eos_received = True
        quit_mainloop(loop)
    elif message_type == Gst.MessageType.ERROR:
        error, debug_info = message.parse_error()
        print("ERROR:", message.src.get_name(), " ", error.message)
        if debug_info:
            print("debugging info:", debug_info)
        quit_mainloop(loop)
    return True


def handle_interrupt_signal(pipe, loop):
    """Handle ctrl+C signal."""
    _, state, _ = pipe.get_state(Gst.CLOCK_TIME_NONE)
    if state == Gst.State.PLAYING:
        event = Gst.Event.new_eos()
        if pipe.send_event(event):
            print("EoS sent!")
        else:
            print("Failed to send EoS event to the pipeline!")
            quit_mainloop(loop)
    else:
        print("Pipeline is not playing, terminating!")
        quit_mainloop(loop)
    return GLib.SOURCE_CONTINUE

def is_linux():
    try:
        with open("/etc/os-release") as f:
            for line in f:
                if "Linux" in line:
                    return True
    except FileNotFoundError:
        return False
    return False

def main():
    """Main function to set up and run the GStreamer pipeline."""

    # Set the environment
    if is_linux():
        os.environ["XDG_RUNTIME_DIR"] = "/dev/socket/weston"
        os.environ["WAYLAND_DISPLAY"] = "wayland-1"

    Gst.init(None)

    try:
        pipe = Gst.Pipeline()
        if not pipe:
            raise Exception("Failed to create pipeline!")
        construct_pipeline(pipe)
    except Exception as e:
        print(f"{e}")
        Gst.deinit()
        return 1

    loop = GLib.MainLoop()

    bus = pipe.get_bus()
    bus.add_signal_watch()
    bus.connect("message", bus_call, loop)

    interrupt_watch_id = GLib.unix_signal_add(
        GLib.PRIORITY_HIGH, signal.SIGINT, handle_interrupt_signal, pipe, loop
    )

    pipe.set_state(Gst.State.PLAYING)
    loop.run()

    GLib.source_remove(interrupt_watch_id)
    bus.remove_signal_watch()
    bus = None

    pipe.set_state(Gst.State.NULL)
    loop = None
    pipe = None

    Gst.deinit()
    if eos_received:
        print("App execution successful")

    return 0


if __name__ == "__main__":
    sys.exit(main())
