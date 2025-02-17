# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

import os
import sys
import signal
import argparse

import gi
gi.require_version('Gst', '1.0')
gi.require_version("GLib", "2.0")
from gi.repository import Gst, GLib

# Constants
DEFAULT_TFLITE_OBJECT_DETECTION_MODEL = "/etc/models/YOLOv8-Detection-Quantized.tflite"
DEFAULT_OBJECT_DETECTION_LABELS = "/etc/labels/yolov8.labels"
DEFAULT_TFLITE_CLASSIFICATION_MODEL = "/etc/models/inception_v3_quantized.tflite"
DEFAULT_CLASSIFICATION_LABELS = "/etc/labels/classification.labels"
DEFAULT_TFLITE_POSE_DETECTION_MODEL = "/etc/models/hrnet_pose_quantized.tflite"
DEFAULT_POSE_DETECTION_LABELS = "/etc/labels/posenet_mobilenet_v1.labels"
DEFAULT_TFLITE_SEGMENTATION_MODEL = "/etc/models/deeplabv3_plus_mobilenet_quantized.tflite"
DEFAULT_SEGMENTATION_LABELS = "/etc/labels/deeplabv3_resnet50.labels"
DELEGATE_PATH = "libQnnTFLiteDelegate.so"

DEFAULT_CONSTANTS_CLASSIFICATION = "Mobilenet,q-offsets=<38.0>,\
    q-scales=<0.17039915919303894>;"
DEFAULT_CONSTANTS_OBJECT_DETECTION = "YOLOv8,q-offsets=<21.0, 0.0, 0.0>,\
    q-scales=<3.093529462814331, 0.00390625, 1.0>;"
DEFAULT_CONSTANTS_POSE_DETECTION = "Posenet,q-offsets=<8.0>,\
    q-scales=<0.0040499246679246426>;"
DEFAULT_CONSTANTS_SEGMENTATION = "deeplab,q-offsets=<0.0>,\
    q-scales=<1.0>;"

DESCRIPTION = f"""
This app sets up GStreamer pipeline to carry out Object Detection,
Classification, Segmentation and Pose Detection on live stream.
Initializes and links elements for reading, performing inference
using MODEL and LABELS files, and rendering video on display.

Default Object Detection model:    {DEFAULT_TFLITE_OBJECT_DETECTION_MODEL}
Default Object Detection labels:   {DEFAULT_OBJECT_DETECTION_LABELS}
Default Classification model:      {DEFAULT_TFLITE_CLASSIFICATION_MODEL}
Default Classification labels:     {DEFAULT_CLASSIFICATION_LABELS}
Default Pose Detection model:      {DEFAULT_TFLITE_POSE_DETECTION_MODEL}
Default Pose Detection labels:     {DEFAULT_POSE_DETECTION_LABELS}
Default Segmentation model:        {DEFAULT_TFLITE_SEGMENTATION_MODEL}
Default Segmentation Labels:       {DEFAULT_SEGMENTATION_LABELS}
"""

GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX = 1

QUEUE_COUNT = 22
GST_PIPELINE_CNT = 4

GST_OBJECT_DETECTION=0
GST_CLASSIFICATION=1
GST_POSE_DETECTION=2
GST_SEGMENTATION=3

waiting_for_eos = False
eos_received = False
def handle_interrupt_signal(pipeline, mloop):
    """Handle Ctrl+C."""
    global waiting_for_eos

    _, state, _ = pipeline.get_state(Gst.CLOCK_TIME_NONE)
    if state != Gst.State.PLAYING or waiting_for_eos:
        mloop.quit()
        return GLib.SOURCE_CONTINUE

    event = Gst.Event.new_eos()
    if pipeline.send_event(event):
        print("EoS sent to the pipeline")
        waiting_for_eos = True
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

def create_element(factory_name, name):
    """Create a GStreamer element."""
    element = Gst.ElementFactory.make(factory_name, name)
    if not element:
        raise Exception(f"Unable to create element {name}")

    return element

def on_pad_added(elem, pad, target):
    """Link dynamic pads from demuxer to target element."""
    if "video" in pad.get_name() or "rtp" in pad.get_name():
        sink_pad = target.get_static_pad("sink")
        if not sink_pad.is_linked():
            if pad.link(sink_pad) != Gst.PadLinkReturn.OK:
                raise Exception(f"Failed linking {elem.get_name()} to {target.get_name()}")

def link_elements(elements, link_orders):
    """Link elements in the specified order."""
    for link_order in link_orders:
        src = None  # Initialize src to None at the start of each link_order
        for element in link_order:
            dest = elements[element]
            if src:
                if "qtdemux" in elements.keys() and src == elements["qtdemux"]:
                    src.connect("pad-added", on_pad_added, dest)
                    print(f"{src.get_name()} linked to {dest.get_name()}")
                elif "rtspsrc" in elements.keys() and src == elements["rtspsrc"]:
                    src.connect("pad-added", on_pad_added, dest)
                    print(f"{src.get_name()} linked to {dest.get_name()}")
                elif not src.link(dest):
                    raise Exception(
                        f"Unable to link element "
                        f"{src.get_name()} to {dest.get_name()}"
                    )
            src = dest  # Update src to the current dest for the next iteration

def create_pipeline(pipeline):
    # Parse command line arguments
    parser = argparse.ArgumentParser(
        description=DESCRIPTION,
        formatter_class=type(
            'CustomFormatter',
            (argparse.ArgumentDefaultsHelpFormatter, argparse.RawTextHelpFormatter),
            {}
        )
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--camera", action="store_true",
        help="Camera input"
    )
    group.add_argument(
        "--file", type=str, default=None,
        help="File input"
    )
    group.add_argument(
        "--rtsp", type=str, default=None,
        help="RTSP URL"
    )
    parser.add_argument("--constants-detection", type=str,
        default=DEFAULT_CONSTANTS_OBJECT_DETECTION,
        help="Constants for Object detection model"
    )
    parser.add_argument("--constants-classification", type=str,
        default=DEFAULT_CONSTANTS_CLASSIFICATION,
        help="Constants for Object detection model"
    )
    parser.add_argument("--constants-pose", type=str,
        default=DEFAULT_CONSTANTS_POSE_DETECTION,
        help="Constants for Object detection model"
    )
    parser.add_argument("--constants-segmentation", type=str,
        default=DEFAULT_CONSTANTS_SEGMENTATION,
        help="Constants for Object detection model"
    )
    parser.add_argument("--tflite-object-detection-model", type=str,
        default=DEFAULT_TFLITE_OBJECT_DETECTION_MODEL,
        help="Path to TfLite object detection model"
    )
    parser.add_argument("--object-detection-labels", type=str,
        default=DEFAULT_OBJECT_DETECTION_LABELS,
        help="Path to TfLite object detection labels"
    )
    parser.add_argument("--tflite-classification-model", type=str,
        default=DEFAULT_TFLITE_CLASSIFICATION_MODEL,
        help="Path to TfLite classification model"
    )
    parser.add_argument("--tflite-classification-labels", type=str,
        default=DEFAULT_CLASSIFICATION_LABELS,
        help="Path to TfLite classification labels"
    )
    parser.add_argument("--tflite-pose-detection-model", type=str,
        default=DEFAULT_TFLITE_POSE_DETECTION_MODEL,
        help="Path to TfLite pose detection model"
    )
    parser.add_argument("--tflite-pose-detection-labels", type=str,
        default=DEFAULT_POSE_DETECTION_LABELS,
        help="Path to TfLite pose detection labels"
    )
    parser.add_argument("--tflite-segmentation-model", type=str,
        default=DEFAULT_TFLITE_SEGMENTATION_MODEL,
        help="Path to TfLite segmentation model"
    )
    parser.add_argument("--tflite-segmentation-labels", type=str,
        default=DEFAULT_SEGMENTATION_LABELS,
        help="Path to TfLite segmentation labels"
    )

    args = parser.parse_args()

    # Check if all model and label files are present
    if not os.path.exists(args.tflite_object_detection_model):
        print(f"File {args.tflite_object_detection_model} does not exist")
        sys.exit(1)
    if not os.path.exists(args.object_detection_labels):
        print(f"File {args.object_detection_labels} does not exist")
        sys.exit(1)
    if not os.path.exists(args.tflite_classification_model):
        print(f"File {args.tflite_classification_model} does not exist")
        sys.exit(1)
    if not os.path.exists(args.tflite_classification_labels):
        print(f"File {args.tflite_classification_labels} does not exist")
        sys.exit(1)
    if not os.path.exists(args.tflite_pose_detection_model):
        print(f"File {args.tflite_pose_detection_model} does not exist")
        sys.exit(1)
    if not os.path.exists(args.tflite_pose_detection_labels):
        print(f"File {args.tflite_pose_detection_labels} does not exist")
        sys.exit(1)
    if not os.path.exists(args.tflite_segmentation_model):
        print(f"File {args.tflite_segmentation_model} does not exist")
        sys.exit(1)
    if not os.path.exists(args.tflite_segmentation_labels):
        print(f"File {args.tflite_segmentation_labels} does not exist")
        sys.exit(1)

    if not args.camera and args.file is None and args.rtsp is None:
        args.camera = True

    # Check if all model and label files are present
    if not os.path.exists(DEFAULT_TFLITE_OBJECT_DETECTION_MODEL):
        print(f"File {DEFAULT_TFLITE_OBJECT_DETECTION_MODEL} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_OBJECT_DETECTION_LABELS):
        print(f"File {DEFAULT_OBJECT_DETECTION_LABELS} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_TFLITE_CLASSIFICATION_MODEL):
        print(f"File {DEFAULT_TFLITE_CLASSIFICATION_MODEL} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_CLASSIFICATION_LABELS):
        print(f"File {DEFAULT_CLASSIFICATION_LABELS} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_TFLITE_POSE_DETECTION_MODEL):
        print(f"File {DEFAULT_TFLITE_POSE_DETECTION_MODEL} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_POSE_DETECTION_LABELS):
        print(f"File {DEFAULT_POSE_DETECTION_LABELS} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_TFLITE_SEGMENTATION_MODEL):
        print(f"File {DEFAULT_TFLITE_SEGMENTATION_MODEL} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_SEGMENTATION_LABELS):
        print(f"File {DEFAULT_SEGMENTATION_LABELS} does not exist")
        sys.exit(1)
    if args.file:
        if not os.path.exists(args.file):
            print(f"Input file {args.file} does not exist")
            sys.exit(1)

    pipeline_data = [
        {
            "model": args.tflite_object_detection_model,
            "labels": args.object_detection_labels,
            "preproc": "qtimlvconverter",
            "mlframework": "qtimltflite",
            "postproc": "qtimlvdetection",
            "delegate": "external"
        },
        {
            "model": args.tflite_classification_model,
            "labels": args.tflite_classification_labels,
            "preproc": "qtimlvconverter",
            "mlframework": "qtimltflite",
            "postproc": "qtimlvclassification",
            "delegate": "external"
        },
        {
            "model": args.tflite_pose_detection_model,
            "labels": args.tflite_pose_detection_labels,
            "preproc": "qtimlvconverter",
            "mlframework": "qtimltflite",
            "postproc": "qtimlvpose",
            "delegate": "external"
        },
        {
            "model": args.tflite_segmentation_model,
            "labels": args.tflite_segmentation_labels,
            "preproc": "qtimlvconverter",
            "mlframework": "qtimltflite",
            "postproc": "qtimlvsegmentation",
            "delegate": "external"
        }
    ]

    composer_coords = [
        [
            0, 0, 960, 540
        ],
        [
            960, 0, 960, 540
        ],
        [
            0, 540, 960, 540
        ],
        [
            960, 540, 960, 540
        ]
    ]

    elements = {}

    if args.camera:
        # Create qtiqmmfsrc plugin for camera stream
        elements["qtiqmmfsrc"] = create_element("qtiqmmfsrc", "qtiqmmfsrc")

        # Create capsfilter to define camera output settings
        elements["qmmfsrc_caps"] = create_element("capsfilter", "qmmfsrc_caps")

    elif args.file:
        # Create file source element for file stream
        elements["filesrc"] = create_element("filesrc", "filesrc")

        # Create qtdemux for demuxing the filesrc
        elements["qtdemux"] = create_element("qtdemux", "qtdemux")

        # Create h264parse for parsing the stream
        elements["h264parse"] = create_element("h264parse", "h264parse")

        # Create v4l2h264dec element for decoding the stream
        elements["v4l2h264dec"] = create_element("v4l2h264dec", "v4l2h264dec")

        # Create caps for v4l2h264dec
        elements["v4l2h264dec_caps"] = create_element(
            "capsfilter", "v4l2h264dec_caps")

    elif args.rtsp:
        # Create rtspsrc for rtsp input
        elements["rtspsrc"] = create_element("rtspsrc", "rtspsrc")

        # Create rtph264depay plugin for rtsp payload parsing
        elements["rtph264depay"] = create_element("rtph264depay", "rtph264depay")

        # Create h264parse element for parsing the stream
        elements["h264parse"] = create_element("h264parse", "h264parse")

        # Create v4l2h264dec element for decoding the stream
        elements["v4l2h264dec"] = create_element("v4l2h264dec", "v4l2h264dec")

        # Create caps for v4l2h264dec
        elements["v4l2h264dec_caps"] = create_element(
            "capsfilter", "v4l2h264dec_caps")

    else:
        print("No input source selected, exiting...")
        sys.exit(1)

    # Add tee to split single stream into multiple streams (inference, preview)
    elements["tee"] = create_element("tee", "tee")

    # Create qtivcomposer for composing multiple streams into single output stream
    elements["qtivcomposer"] = create_element("qtivcomposer", "qtivcomposer")

    for i in range(GST_PIPELINE_CNT):
        # Create qtimlvconverter for pre-processing
        element_name = pipeline_data[i]["preproc"] + f"-{i}"
        elements[f"qtimlvconverter{i}"] = create_element(
            pipeline_data[i]["preproc"], element_name)

        # Create qtimltflite for ML inferencing
        element_name = pipeline_data[i]["mlframework"] + f"-{i}"
        elements[f"qtimlelement{i}"] = create_element(
            pipeline_data[i]["mlframework"], element_name)

        # Create post-processing plugin
        element_name = pipeline_data[i]["postproc"] + f"-{i}"
        elements[f"qtimlvpostproc{i}"] = create_element(
            pipeline_data[i]["postproc"], element_name)

        # Create capsfilter to get matching params of ML post proc o/p and qtivcomposer
        element_name = "capsfilter" + f"-{i}"
        elements[f"capsfilter{i}"] = create_element("capsfilter", element_name)

    # Create queue to decouple processing on sink and source pads
    for i in range(QUEUE_COUNT):
        element_name = "queue" + f"-{i}"
        elements[f"queue{i}"] = create_element("queue", element_name)

    # Create fpsdisplaysink to display current and average fps
    # as a text overlay
    elements["fpsdisplaysink"] = create_element(
        "fpsdisplaysink", "fpsdisplaysink")

    # Create waylandsink to render output on display
    waylandsink = create_element("waylandsink", "waylandsink")

    # Set properties
    if args.file:
        elements["v4l2h264dec"].set_property("capture-io-mode", "dmabuf")
        elements["v4l2h264dec"].set_property("output-io-mode", "dmabuf")
        elements["filesrc"].set_property("location", args.file)
        elements["v4l2h264dec_caps"].set_property(
            "caps", Gst.Caps.from_string("video/x-raw,format=NV12")
        )

    elif args.rtsp:
        elements["v4l2h264dec"].set_property("capture-io-mode", "dmabuf")
        elements["v4l2h264dec"].set_property("output-io-mode", "dmabuf")
        elements["rtspsrc"].set_property("location", args.rtsp)
        elements["v4l2h264dec_caps"].set_property(
            "caps", Gst.Caps.from_string("video/x-raw,format=NV12")
        )

    elif args.camera:
        elements["qmmfsrc_caps"].set_property(
            "caps", Gst.Caps.from_string(
                "video/x-raw,format=NV12,width=1920,height=1080,"
                "framerate=30/1"
            )
        )

    else:
        print("No input source selected, exiting...")
        sys.exit(1)

    for i in range(GST_PIPELINE_CNT):
        elements[f"qtimlelement{i}"].set_property(
            "delegate", pipeline_data[i]["delegate"])
        elements[f"qtimlelement{i}"].set_property(
            "external-delegate-path", DELEGATE_PATH)
        elements[f"qtimlelement{i}"].set_property(
            "model", pipeline_data[i]["model"])

        options_structure = Gst.Structure.new_empty("QNNExternalDelegate")
        options_structure.set_value("backend_type", "htp")
        options_structure.set_value("htp_device_id", "0")
        options_structure.set_value("htp_performance_mode", "2")
        elements[f"qtimlelement{i}"].set_property(
            "external-delegate-options", options_structure)

        elements[f"qtimlvpostproc{i}"].set_property(
            "labels", pipeline_data[i]["labels"])
        if i == GST_OBJECT_DETECTION:
            elements[f"qtimlvpostproc{i}"].set_property("module", "yolov8")
            elements[f"qtimlvpostproc{i}"].set_property("threshold", 40.0)
            elements[f"qtimlvpostproc{i}"].set_property("results", 10)
            elements[f"qtimlvpostproc{i}"].set_property(
                "constants", args.constants_detection)

        elif i == GST_CLASSIFICATION:
            elements[f"qtimlvpostproc{i}"].set_property("module", "mobilenet")
            elements[f"qtimlvpostproc{i}"].set_property("threshold", 40.0)
            elements[f"qtimlvpostproc{i}"].set_property("results", 2)
            elements[f"qtimlvpostproc{i}"].set_property(
                "extra-operation", GST_VIDEO_CLASSIFICATION_OPERATION_SOFTMAX)
            elements[f"qtimlvpostproc{i}"].set_property(
                "constants", args.constants_classification)

        elif i == GST_POSE_DETECTION:
            elements[f"qtimlvpostproc{i}"].set_property("module", "hrnet")
            elements[f"qtimlvpostproc{i}"].set_property("threshold", 40.0)
            elements[f"qtimlvpostproc{i}"].set_property("results", 2)
            elements[f"qtimlvpostproc{i}"].set_property(
                "constants", args.constants_pose)

        elif i == GST_SEGMENTATION:
            elements[f"qtimlvpostproc{i}"].set_property(
                "module", "deeplab-argmax")
            elements[f"qtimlvpostproc{i}"].set_property(
                "constants", args.constants_segmentation)

    for i in range(GST_PIPELINE_CNT):
        if i == GST_SEGMENTATION:
            elements[f"capsfilter{i}"].set_property(
                "caps", Gst.Caps.from_string(
                    "video/x-raw,width=256,height=144,"
                )
            )
        else:
            elements[f"capsfilter{i}"].set_property(
                "caps", Gst.Caps.from_string(
                    "video/x-raw,format=BGRA,width=640,height=360"
                )
            )

    # Set sync to False to override default value
    waylandsink.set_property("sync", True)
    waylandsink.set_property("fullscreen", True)

    # Set sync to False to override default value
    elements["fpsdisplaysink"].set_property("sync", True)
    elements["fpsdisplaysink"].set_property("signal-fps-measurements", True)
    elements["fpsdisplaysink"].set_property("text-overlay", True)
    elements["fpsdisplaysink"].set_property(
        "video-sink", waylandsink)

    for k in elements.keys():
        pipeline.add(elements[k])

    # Link all elements
    link_orders = []

    if args.camera:
        link_orders+= [
            ["qtiqmmfsrc", "qmmfsrc_caps", "queue0", "tee"]
        ]
    elif args.file:
        link_orders+= [
            [
                "filesrc", "qtdemux", "queue0", "h264parse",
                "v4l2h264dec", "v4l2h264dec_caps", "tee"
            ],
        ]
    elif args.rtsp:
        link_orders+= [
            [
                "rtspsrc", "queue0", "rtph264depay", "h264parse"
                "v4l2h264dec", "v4l2h264dec_caps", "tee",
            ],
        ]
    else:
        print("No input source selected, exiting...")
        sys.exit(1)

    for i in range(GST_PIPELINE_CNT):
        link_orders+= [
            [
                "tee", f"queue{5 * i + 1}", "qtivcomposer"
            ],
            [
                "tee", f"queue{5 * i + 2}", f"qtimlvconverter{i}", f"queue{5 * i + 3}",
                f"qtimlelement{i}", f"queue{5 * i + 4}", f"qtimlvpostproc{i}",
                f"capsfilter{i}", f"queue{5 * i + 5}", "qtivcomposer"
            ]
        ]

    link_orders+= [
        [
            "qtivcomposer", "queue21", "fpsdisplaysink"
        ]
    ]

    link_elements(elements, link_orders)

    # Set position and dimension for each output stream
    composer_sink_pads = [elements["qtivcomposer"].get_static_pad(f"sink_{i}") for i in range(2 * GST_PIPELINE_CNT)]

    for i in range(GST_PIPELINE_CNT):
        Gst.util_set_object_arg(
            composer_sink_pads[2 * i],
            "position",
            f"<{composer_coords[i][0]}, {composer_coords[i][1]}>"
        )
        Gst.util_set_object_arg(
            composer_sink_pads[2 * i],
            "dimensions",
            f"<{composer_coords[i][2]}, {composer_coords[i][3]}>"
        )

        if i == GST_CLASSIFICATION:
            Gst.util_set_object_arg(
                composer_sink_pads[2 * i + 1],
                "position",
                f"<{composer_coords[i][0] + 30}, {composer_coords[i][1] + 45}>"
            )
            Gst.util_set_object_arg(
                composer_sink_pads[2 * i + 1],
                "dimensions",
                f"<{composer_coords[i][2] // 3}, {composer_coords[i][3] // 3}>"
            )
        elif i == GST_SEGMENTATION:
            alpha = 0.5
            composer_sink_pads[2 * i + 1].set_property("alpha", alpha)
            Gst.util_set_object_arg(
                composer_sink_pads[2 * i + 1],
                "position",
                f"<{composer_coords[i][0]}, {composer_coords[i][1]}>"
            )
            Gst.util_set_object_arg(
                composer_sink_pads[2 * i + 1],
                "dimensions",
                f"<{composer_coords[i][2]}, {composer_coords[i][3]}>"
            )
        else:
            Gst.util_set_object_arg(
                composer_sink_pads[2 * i + 1],
                "position",
                f"<{composer_coords[i][0]}, {composer_coords[i][1]}>"
            )
            Gst.util_set_object_arg(
                composer_sink_pads[2 * i + 1],
                "dimensions",
                f"<{composer_coords[i][2]}, {composer_coords[i][3]}>"
            )

def main():
    """Main function to set up and run the GStreamer pipeline."""

    os.environ["XDG_RUNTIME_DIR"] = "/dev/socket/weston"
    os.environ["WAYLAND_DISPLAY"] = "wayland-1"

    # Initialize GStreamer
    Gst.init(None)
    mloop = GLib.MainLoop()

    # Create the pipeline
    try:
        pipeline = Gst.Pipeline.new("gst-ai-parallel-inference")
        if not pipeline:
            raise Exception(f"Unable to create pipeline")
        create_pipeline(pipeline)
    except Exception as e:
        print(f"{e}, exiting...")
        return -1

    # Handle Ctrl+C
    interrupt_watch_id = GLib.unix_signal_add(
        GLib.PRIORITY_HIGH, signal.SIGINT, handle_interrupt_signal, pipeline, mloop
    )

    # Wait until error or EOS
    bus = pipeline.get_bus()
    bus.add_signal_watch()
    bus.connect("message", handle_bus_message, mloop)

    # Start playing
    print("Setting to PLAYING...")
    pipeline.set_state(Gst.State.PLAYING)
    mloop.run()

    GLib.source_remove(interrupt_watch_id)
    bus.remove_signal_watch()
    bus = None

    print("Setting to NULL...")
    pipeline.set_state(Gst.State.NULL)

    mloop = None
    pipeline = None
    Gst.deinit()
    if eos_received:
        print("App execution successful")

if __name__ == "__main__":
    sys.exit(main())
