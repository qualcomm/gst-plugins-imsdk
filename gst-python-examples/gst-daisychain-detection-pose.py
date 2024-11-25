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
DESCRIPTION = """
This app sets up GStreamer pipeline to carry out Object Detection,
Classification, Segmentation and Pose Detection on live stream.
Initializes and links elements for reading, performing inference
using MODEL and LABELS files, and rendering video on display.
"""
DEFAULT_TFLITE_YOLOV8_MODEL = "/opt/YOLOv8-Detection-Quantized.tflite"
DEFAULT_YOLOV8_LABELS = "/opt/yolov8.labels"
DEFAULT_TFLITE_POSE_MODEL = "/opt/hrnet_pose_quantized.tflite"
DEFAULT_POSE_LABELS = "/opt/hrnet_pose.labels"
DELEGATE_PATH = "libQnnTFLiteDelegate.so"

DEFAULT_CONSTANTS_OBJECT_DETECTION = "YOLOv8,q-offsets=<21.0, 0.0, 0.0>,\
    q-scales=<3.093529462814331, 0.00390625, 1.0>;"
DEFAULT_CONSTANTS_POSE_DETECTION = "hrnet,q-offsets=<8.0>,\
    q-scales=<0.0040499246679246426>;"

QUEUE_COUNT = 6

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
    # Check if all model and label files are present
    if not os.path.exists(DEFAULT_TFLITE_YOLOV8_MODEL):
        print(f"File {DEFAULT_TFLITE_YOLOV8_MODEL} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_YOLOV8_LABELS):
        print(f"File {DEFAULT_YOLOV8_LABELS} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_TFLITE_POSE_MODEL):
        print(f"File {DEFAULT_TFLITE_POSE_MODEL} does not exist")
        sys.exit(1)
    if not os.path.exists(DEFAULT_POSE_LABELS):
        print(f"File {DEFAULT_POSE_LABELS} does not exist")
        sys.exit(1)

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
    parser.add_argument("--constants_detection", type=str,
        default=DEFAULT_CONSTANTS_OBJECT_DETECTION,
        help="Constants for Object detection model"
    )
    parser.add_argument("--constants_pose", type=str,
        default=DEFAULT_CONSTANTS_POSE_DETECTION,
        help="Constants for Object detection model"
    )
    args = parser.parse_args()

    if not args.camera and args.file is None and args.rtsp is None:
        args.camera = True

    if args.file:
        if not os.path.exists(args.file):
            print(f"Input file {args.file} does not exist")
            sys.exit(1)

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

    elif args.rtsp:
        # Create rtspsrc for rtsp input
        elements["rtspsrc"] = create_element("rtspsrc", "rtspsrc")

        # Create rtph264depay plugin for rtsp payload parsing
        elements["rtph264depay"] = create_element("rtph264depay", "rtph264depay")

        # Create h264parse element for parsing the stream
        elements["h264parse"] = create_element("h264parse", "h264parse")

        # Create v4l2h264dec element for decoding the stream
        elements["v4l2h264dec"] = create_element("v4l2h264dec", "v4l2h264dec")

    else:
        print("No input source selected, exiting...")
        sys.exit(1)

    # Add tee to split single stream into multiple streams (inference, preview)
    elements["tee0"] = create_element("tee", "tee-0")
    elements["tee1"] = create_element("tee", "tee-1")
    elements["tee2"] = create_element("tee", "tee-2")

    # Add metamux
    elements['qtimetamux0'] = create_element("qtimetamux", "qtimetamux-0")
    elements['qtimetamux1'] = create_element("qtimetamux", "qtimetamux-1")

    # Create qtivcomposer for composing multiple streams into single output stream
    elements["qtivcomposer"] = create_element("qtivcomposer", "qtivcomposer")

    # Create qtivsplit
    elements["qtivsplit"] = create_element("qtivsplit", "qtivsplit")
    # Create queue to decouple processing on sink and source pads
    for i in range(QUEUE_COUNT):
        element_name = "queue" + f"-{i}"
        elements[f"queue{i}"] = create_element("queue", element_name)

    # Create capsfilter
    elements["filter0"] = create_element("capsfilter", "filter-0")
    elements["filter1"] = create_element("capsfilter", "filter-1")

    # Create capsfilter for metamux
    elements["qtimetamux_filter0"] = create_element("capsfilter", "qtimetamux_filter-0")
    elements["qtimetamux_filter1"] = create_element("capsfilter", "qtimetamux_filter-1")

    # Create pre-processing plugin
    elements["qtimlvconverter0"] = create_element("qtimlvconverter", "qtimlvconverter-0")
    elements["qtimlvconverter1"] = create_element("qtimlvconverter", "qtimlvconverter-1")

    # Create ML inferencing plugin
    elements["qtimltflite0"] = create_element("qtimltflite", "qtimltflite-0")
    elements["qtimltflite1"] = create_element("qtimltflite", "qtimltflite-1")

    # Create post-processing plugins
    elements["qtimlvdetection"] = create_element("qtimlvdetection", "qtimlvdetection")
    elements["qtimlvpose"] = create_element("qtimlvpose", "qtimlvpose")

    # Create capsfilter for detection
    elements["detection_filter"] = create_element("capsfilter", "capsfilter")

    # Create qtioverlay
    elements["qtioverlay"] = create_element("qtioverlay", "qtioverlay")

    # Create fpsdisplaysink to display current and average fps
    # as a text overlay
    elements["fpsdisplaysink"] = create_element(
        "fpsdisplaysink", "fpsdisplaysink")

    # Create waylandsink to render output on display
    waylandsink = create_element("waylandsink", "waylandsink")

    # Set properties
    if args.file:
        elements["v4l2h264dec"].set_property("capture-io-mode", 5)
        elements["v4l2h264dec"].set_property("output-io-mode", 5)
        elements["filesrc"].set_property("location", args.file)

    elif args.camera:
        elements["qmmfsrc_caps"].set_property(
            "caps", Gst.Caps.from_string(
                "video/x-raw(memory:GBM),format=NV12,width=1920,height=1080,"
                "framerate=30/1,compression=ubwc"
            )
        )

    elif args.rtsp:
        elements["v4l2h264dec"].set_property("capture-io-mode", 5)
        elements["v4l2h264dec"].set_property("output-io-mode", 5)
        elements["rtspsrc"].set_property("location", args.rtsp)

    else:
        print("No input source selected, exiting...")
        sys.exit(1)

    elements["filter0"].set_property(
        "caps", Gst.Caps.from_string(
            "video/x-raw(memory:GBM),format=RGBA,width=240,height=480"
        )
    )
    elements["filter1"].set_property(
        "caps", Gst.Caps.from_string(
            "video/x-raw(memory:GBM),format=RGBA,width=240,height=480"
        )
    )

    elements["qtimetamux_filter0"].set_property(
        "caps", Gst.Caps.from_string("text/x-raw")
    )
    elements["qtimetamux_filter1"].set_property(
        "caps", Gst.Caps.from_string("text/x-raw")
    )

    # Set properties for detection model
    elements["qtimltflite0"].set_property(
        "delegate", "external")
    elements["qtimltflite0"].set_property(
        "external-delegate-path", DELEGATE_PATH)
    elements["qtimltflite0"].set_property(
        "model", DEFAULT_TFLITE_YOLOV8_MODEL)

    options_structure0 = Gst.Structure.new_empty("QNNExternalDelegate")
    options_structure0.set_value("backend_type", "htp")
    elements["qtimltflite0"].set_property(
        "external-delegate-options", options_structure0)

    # Set properties for pose detection model
    elements["qtimltflite1"].set_property(
        "delegate", "external")
    elements["qtimltflite1"].set_property(
        "external-delegate-path", DELEGATE_PATH)
    elements["qtimltflite1"].set_property(
        "model", DEFAULT_TFLITE_POSE_MODEL)

    options_structure1 = Gst.Structure.new_empty("QNNExternalDelegate")
    options_structure1.set_value("backend_type", "htp")
    elements["qtimltflite1"].set_property(
        "external-delegate-options", options_structure1)

    elements["qtimlvconverter0"].set_property(
        "mode", "image-batch-non-cumulative")
    elements["qtimlvconverter1"].set_property(
        "mode", "roi-batch-cumulative")
    elements["qtimlvconverter1"].set_property(
        "image-disposition", "centre")

    elements["qtimlvdetection"].set_property("module", "yolov8")
    elements["qtimlvdetection"].set_property("threshold", 40.0)
    elements["qtimlvdetection"].set_property("results", 4)
    elements["qtimlvdetection"].set_property("labels", DEFAULT_YOLOV8_LABELS)
    elements["qtimlvdetection"].set_property("constants", args.constants_detection)

    elements["qtimlvpose"].set_property("module", "hrnet")
    elements["qtimlvpose"].set_property("threshold", 51.0)
    elements["qtimlvpose"].set_property("results", 1)
    elements["qtimlvpose"].set_property("labels", DEFAULT_POSE_LABELS)
    elements["qtimlvpose"].set_property("constants", args.constants_pose)

    elements["qtioverlay"].set_property("engine", "gles")

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
            [
                "qtiqmmfsrc", "qmmfsrc_caps", "tee0", "qtimetamux0"
            ]
        ]
    elif args.file:
        link_orders+= [
            [
                "filesrc", "qtdemux", "queue0", "h264parse",
                "v4l2h264dec", "tee0", "qtimetamux0"
            ],
        ]
    elif args.rtsp:
        link_orders+= [
            [
                "rtspsrc", "queue0", "rtph264depay", "h264parse",
                "v4l2h264dec", "tee0", "qtimetamux0"
            ],
        ]
    else:
        print("No input source selected, exiting...")
        sys.exit(1)

    link_orders+= [
        [
            "tee0", "queue1"
        ],
        [
            "queue1", "qtimlvconverter0", "qtimltflite0",
            "qtimlvdetection", "qtimetamux_filter0",
            "qtimetamux0", "tee1", "qtimetamux1"
        ],
        [
            "tee1", "queue2", "qtimlvconverter1", "qtimltflite1",
            "qtimlvpose", "qtimetamux_filter1",
            "qtimetamux1", "tee2"
        ],
        [
            "tee2", "queue3", "qtivcomposer"
        ],
        [
            "tee2", "qtivsplit"
        ],
        [
            "qtivsplit", "filter0", "queue4", "qtivcomposer"
        ],
        [
            "qtivsplit", "filter1", "queue5", "qtivcomposer"
        ]
    ]

    link_orders+= [
        [
            "qtivcomposer", "qtioverlay", "fpsdisplaysink"
        ]
    ]

    link_elements(elements, link_orders)

    qtivsplit_src0 = elements["qtivsplit"].get_static_pad("src_0")
    Gst.util_set_object_arg(
        qtivsplit_src0,
        "mode", "single-roi-meta"
    )
    qtivsplit_src1 = elements["qtivsplit"].get_static_pad("src_1")
    Gst.util_set_object_arg(
        qtivsplit_src1,
        "mode", "single-roi-meta"
    )

    # Set vcomposer sink pad
    qtivcomposer_sink_pad0 = elements["qtivcomposer"].get_static_pad("sink_0")
    Gst.util_set_object_arg(
        qtivcomposer_sink_pad0,
        "position", "<0, 0>"
    )
    Gst.util_set_object_arg(
        qtivcomposer_sink_pad0,
        "dimensions", "<1920, 1080>"
    )

    qtivcomposer_sink_pad1 = elements["qtivcomposer"].get_static_pad("sink_1")
    Gst.util_set_object_arg(
        qtivcomposer_sink_pad1,
        "position", "<0, 0>"
    )
    Gst.util_set_object_arg(
        qtivcomposer_sink_pad1,
        "dimensions", "<240, 480>"
    )

    qtivcomposer_sink_pad2 = elements["qtivcomposer"].get_static_pad("sink_2")
    Gst.util_set_object_arg(
        qtivcomposer_sink_pad2,
        "position", "<1680, 0>"
    )
    Gst.util_set_object_arg(
        qtivcomposer_sink_pad2,
        "dimensions", "<240, 480>"
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
        pipeline = Gst.Pipeline.new("gst-ai-daisychain-detection-pose")
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
