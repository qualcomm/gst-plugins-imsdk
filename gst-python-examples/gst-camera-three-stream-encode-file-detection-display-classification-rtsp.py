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
import re

gi.require_version("Gst", "1.0")
gi.require_version("GLib", "2.0")
from gi.repository import Gst, GLib

# Configurations for Detection
DEFAULT_DETECTION_MODEL = "/etc/models/yolox_quantized.tflite"
DEFAULT_DETECTION_MODULE = "yolov8"
DEFAULT_DETECTION_LABELS = "/etc/labels/yolox.labels"
DEFAULT_DETECTION_CONSTANTS = "YOLOx,q-offsets=<38.0, 0.0, 0.0>,\
    q-scales=<3.6124823093414307, 0.003626860911026597, 1.0>;"

# Configurations for Classification
DEFAULT_CLASSIFICATION_MODEL = "/etc/models/Resnet101_Quantized.tflite"
DEFAULT_CLASSIFICATION_LABELS = "/etc/labels/resnet101.labels"
DEFAULT_CLASSIFICATION_MODULE = "mobilenet"
DEFAULT_CLASSIFICATION_CONSTANTS = "Mobilenet,q-offsets=<-82.0>,\
    q-scales=<0.21351955831050873>;"

DEFAULT_RTSP_LOCATION = "rtsp://127.0.0.1:8900/live"

DEFAULT_OUTPUT_FILE = "/etc/media/test.mp4"

DESCRIPTION = f"""
The application:
- Encodes camera stream and dump the output.
- Uses a TFLite model to identify the object in scene from camera stream
and overlay the bounding boxes over the detected objects. The results are shown
on the display.
- Uses a TFLite model to classify scene from camera stream and overlay
the classification labels on the top left corner. The results are streamed over
RTSP.

Default RTSP location : {DEFAULT_RTSP_LOCATION}

The default file paths in the python script are as follows:
- Detection model:       {DEFAULT_DETECTION_MODEL}
- Detection labels:      {DEFAULT_DETECTION_LABELS}
- Classification model:  {DEFAULT_CLASSIFICATION_MODEL}
- Classification labels: {DEFAULT_CLASSIFICATION_LABELS}

To override the default settings,
please configure the corresponding module and constants as well.
"""

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

def parse_rtsp_location(rtsp_location):
    """Parses an RTSP location string into its components"""
    # Define the regex pattern to match the address, port, and mpoint
    pattern = r'^rtsp://(?P<address>[^:]+):(?P<port>\d+)(?P<mpoint>/.*)$'

    # Match the pattern with the RTSP location string
    match = re.match(pattern, rtsp_location)

    if match:
        # Extract the address, port, and mpoint from the match object
        address = match.group('address')
        port = match.group('port')
        mpoint = match.group('mpoint')
        return {
            "address" : address,
            "port"    : port,
            "mpoint"  : mpoint
        }
    else:
        raise ValueError("Invalid RTSP location")

def construct_pipeline(pipe):
    """Initialize and link elements for the GStreamer pipeline."""
    # Parse arguments
    parser = argparse.ArgumentParser(
        description=DESCRIPTION,
        formatter_class=type(
            'CustomFormatter',
            (argparse.ArgumentDefaultsHelpFormatter, argparse.RawTextHelpFormatter),
            {}
        )
    )

    parser.add_argument(
        "--output_path",
        type=str,
        default=DEFAULT_OUTPUT_FILE,
        help="Pipeline Output Path",
    )
    parser.add_argument(
        "--rtsp",
        type=str,
        default=DEFAULT_RTSP_LOCATION,
        help="RTSP location for streaming",
    )
    parser.add_argument(
        "--detection_model", type=str, default=DEFAULT_DETECTION_MODEL,
        help="Path to TfLite Object Detection Model"
    )
    parser.add_argument(
        "--detection_module", type=str, default=DEFAULT_DETECTION_MODULE,
        help="Object Detection module for post-procesing"
    )
    parser.add_argument(
        "--detection_labels", type=str, default=DEFAULT_DETECTION_LABELS,
        help="Path to TfLite Object Detection Labels"
    )
    parser.add_argument(
        "--detection_constants", type=str, default=DEFAULT_DETECTION_CONSTANTS,
        help="Constants for TfLite Object Detection Model"
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

    args = parser.parse_args()

    detection = {
        "model": args.detection_model,
        "module": args.detection_module,
        "labels": args.detection_labels,
        "constants": args.detection_constants
    }
    classification = {
        "model": args.classification_model,
        "module": args.classification_module,
        "labels": args.classification_labels,
        "constants": args.classification_constants
    }
    rtsp = parse_rtsp_location(args.rtsp)

    # Create all elements
    # fmt: off
    elements = {
        "qmmfsrc":           create_element("qtiqmmfsrc", "camsrc"),
        # Stream 0
        "capsfilter_0":      create_element("capsfilter", "camout0caps"),
        "queue_0":           create_element("queue", "queue0"),
        "v4l2h264enc_0":     create_element("v4l2h264enc", "v4l2h264encoder0"),
        "h264parse_0":       create_element("h264parse", "h264parser0"),
        "mp4mux":            create_element("mp4mux", "mp4muxer"),
        "filesink":          create_element("filesink", "filesink"),
        # Stream 1
        "capsfilter_1":      create_element("capsfilter", "camout1caps"),
        "queue_1":           create_element("queue", "queue1"),
        "tee_0":             create_element("tee", "split0"),
        "mlvconverter_0":    create_element("qtimlvconverter", "converter0"),
        "queue_2":           create_element("queue", "queue2"),
        "mltflite_0":        create_element("qtimltflite", "inference0"),
        "queue_3":           create_element("queue", "queue3"),
        "mlvdetection":      create_element("qtimlvdetection", "detection"),
        "capsfilter_2":      create_element("capsfilter", "metamux0metacaps"),
        "queue_4":           create_element("queue", "queue4"),
        "metamux_0":         create_element("qtimetamux", "metamux0"),
        "overlay_0":         create_element("qtivoverlay", "overlay0"),
        "queue_5":           create_element("queue", "queue5"),
        "display":           create_element("waylandsink", "display"),
        # Stream 2
        "capsfilter_3":      create_element("capsfilter", "camout3caps"),
        "queue_6":           create_element("queue", "queue6"),
        "tee_1":             create_element("tee", "split1"),
        "mlvconverter_1":    create_element("qtimlvconverter", "converter1"),
        "queue_7":           create_element("queue", "queue7"),
        "mltflite_1":        create_element("qtimltflite", "inference1"),
        "queue_8":           create_element("queue", "queue8"),
        "mlvclassification": create_element("qtimlvclassification", "classification"),
        "capsfilter_4":      create_element("capsfilter", "metamux1metacaps"),
        "queue_9":           create_element("queue", "queue9"),
        "metamux_1":         create_element("qtimetamux", "metamux1"),
        "overlay_1":         create_element("qtivoverlay", "overlay1"),
        "v4l2h264enc_1":     create_element("v4l2h264enc", "v4l2h264encoder1"),
        "h264parse_1":       create_element("h264parse", "h264parser1"),
        "rtspbin":           create_element("qtirtspbin", "rtspbin")
    }
    # fmt: on

    # Set element properties
    Gst.util_set_object_arg(elements["qmmfsrc"], "camera", "0")

    # Stream 0
    Gst.util_set_object_arg(
        elements["capsfilter_0"],
        "caps",
        "video/x-raw,format=NV12_Q08C,\
        width=1280,height=720,framerate=30/1,colorimetry=bt709",
    )

    Gst.util_set_object_arg(elements["v4l2h264enc_0"], "capture-io-mode", "dmabuf")
    Gst.util_set_object_arg(elements["v4l2h264enc_0"], "output-io-mode", "dmabuf-import")

    Gst.util_set_object_arg(elements["h264parse_0"], "config-interval", "1")

    Gst.util_set_object_arg(elements["filesink"], "location", args.output_path)

    # Stream 1
    Gst.util_set_object_arg(
        elements["capsfilter_1"],
        "caps",
        "video/x-raw,format=NV12,\
        width=640,height=360,framerate=30/1",
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
        detection["model"],
    )

    Gst.util_set_object_arg(elements["mlvdetection"], "threshold", "75.0")
    Gst.util_set_object_arg(elements["mlvdetection"], "results", "4")
    Gst.util_set_object_arg(elements["mlvdetection"], "module", detection["module"])
    Gst.util_set_object_arg(
        elements["mlvdetection"], "constants", detection["constants"],
    )
    Gst.util_set_object_arg(
        elements["mlvdetection"], "labels", detection["labels"]
    )

    Gst.util_set_object_arg(elements["capsfilter_2"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_0"], "engine", "gles")

    Gst.util_set_object_arg(elements["display"], "sync", "false")
    Gst.util_set_object_arg(elements["display"], "fullscreen", "true")

    # Stream 2
    Gst.util_set_object_arg(
        elements["capsfilter_3"],
        "caps",
        "video/x-raw,format=NV12,\
        width=640,height=360,framerate=30/1",
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
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        elements["mltflite_1"], "model", classification["model"]
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

    Gst.util_set_object_arg(elements["capsfilter_4"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_1"], "engine", "gles")

    Gst.util_set_object_arg(elements["v4l2h264enc_1"], "capture-io-mode", "dmabuf")
    Gst.util_set_object_arg(elements["v4l2h264enc_1"], "output-io-mode", "dmabuf")

    Gst.util_set_object_arg(elements["h264parse_1"], "config-interval", "1")

    Gst.util_set_object_arg(
        elements["rtspbin"], "address", rtsp["address"]
    )
    Gst.util_set_object_arg(elements["rtspbin"], "port", rtsp["port"])
    Gst.util_set_object_arg(elements["rtspbin"], "mpoint", rtsp["mpoint"])

    # Add all elements
    for element in elements.values():
        pipe.add(element)

    # Link all elements
    # fmt: off
    link_orders = [
        [
            "qmmfsrc", "capsfilter_0", "queue_0", "v4l2h264enc_0",
            "h264parse_0", "mp4mux", "filesink"
        ],
        [
            "qmmfsrc", "capsfilter_1", "queue_1", "tee_0", "metamux_0",
            "overlay_0", "queue_5", "display"
        ],
        [
            "tee_0", "mlvconverter_0", "queue_2", "mltflite_0", "queue_3",
            "mlvdetection", "capsfilter_2", "queue_4", "metamux_0"
        ],
        [
            "qmmfsrc", "capsfilter_3", "queue_6", "tee_1", "metamux_1",
            "overlay_1", "v4l2h264enc_1", "h264parse_1", "rtspbin"
        ],
        [
            "tee_1", "mlvconverter_1", "queue_7", "mltflite_1", "queue_8",
            "mlvclassification", "capsfilter_4", "queue_9", "metamux_1"
        ]
    ]
    # fmt: on
    link_elements(link_orders, elements)

    # Set pad properties
    qmmf_video_0 = elements["qmmfsrc"].get_static_pad("video_0")
    qmmf_video_1 = elements["qmmfsrc"].get_static_pad("video_1")
    qmmf_video_2 = elements["qmmfsrc"].get_static_pad("video_2")
    Gst.util_set_object_arg(qmmf_video_0, "type", "preview")
    Gst.util_set_object_arg(qmmf_video_1, "type", "video")
    Gst.util_set_object_arg(qmmf_video_2, "type", "video")
    qmmf_video_0 = None
    qmmf_video_1 = None
    qmmf_video_2 = None


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
        eos_received  = True
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
