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

# Configurations for Detection
DEFAULT_DETECTION_MODEL = "/etc/models/yolox_quantized.tflite"
DEFAULT_DETECTION_MODULE = "yolov8"
DEFAULT_DETECTION_LABELS = "/etc/labels/yolox.json"

# Configurations for Classification
DEFAULT_CLASSIFICATION_MODEL = "/etc/models/inception_v3_quantized.tflite"
DEFAULT_CLASSIFICATION_MODULE = "mobilenet-softmax"
DEFAULT_CLASSIFICATION_LABELS = "/etc/labels/classification.json"

DESCRIPTION = f"""
The application uses:
- A TFLite model to identify the object in scene from camera stream and
overlay the bounding boxes over the detected objects
- A TFLite model to classify scene from camera stream and overlay the
classification labels on the top left corner.
Then the results are shown side by side on the display.

The default file paths in the python script are as follows:
- Detection model:       {DEFAULT_DETECTION_MODEL}
- Detection labels:      {DEFAULT_DETECTION_LABELS}
- Classification model:  {DEFAULT_CLASSIFICATION_MODEL}
- Classification labels: {DEFAULT_CLASSIFICATION_LABELS}

To override the default settings,
please configure the corresponding module as well.
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

    args = parser.parse_args()

    detection = {
        "model": args.detection_model,
        "module": args.detection_module,
        "labels": args.detection_labels
    }
    classification = {
        "model": args.classification_model,
        "module": args.classification_module,
        "labels": args.classification_labels
    }

    # Create all elements
    # fmt: off
    elements = {
        "qmmfsrc":           create_element("qtiqmmfsrc", "camsrc"),
        # Stream 0
        "capsfilter_0":      create_element("capsfilter", "camout0caps"),
        "tee_0":             create_element("tee", "split0"),
        "mlvconverter_0":    create_element("qtimlvconverter", "converter0"),
        "mltflite_0":        create_element("qtimltflite", "inference0"),
        "mlvdetection":      create_element("qtimlpostprocess", "detection"),
        "capsfilter_1":      create_element("capsfilter", "metamux0metacaps"),
        "metamux_0":         create_element("qtimetamux", "metamux0"),
        "overlay_0":         create_element("qtivoverlay", "overlay0"),
        # Stream 1
        "capsfilter_2":      create_element("capsfilter", "camout1caps"),
        "tee_1":             create_element("tee", "split1"),
        "mlvconverter_1":    create_element("qtimlvconverter", "converter1"),
        "mltflite_1":        create_element("qtimltflite", "inference1"),
        "mlvclassification": create_element("qtimlpostprocess", "classification"),
        "capsfilter_3":      create_element("capsfilter", "metamux1metacaps"),
        "metamux_1":         create_element("qtimetamux", "metamux1"),
        "overlay_1":         create_element("qtivoverlay", "overlay1"),
        # Side by side all streams
        "composer":          create_element("qtivcomposer", "composer"),
        "display":           create_element("waylandsink", "display")
    }
    # fmt: on

    queue_count = 13
    for i in range(queue_count):
        queue_name = f"queue_{i}"
        elements[queue_name] = create_element("queue", queue_name)

    # Set element properties
    Gst.util_set_object_arg(elements["qmmfsrc"], "camera", "0")

    # Stream 0
    Gst.util_set_object_arg(
        elements["capsfilter_0"],
        "caps",
        "video/x-raw,format=NV12_Q08C,\
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
        elements["mltflite_0"], "model", detection["model"],
    )

    detection_threshold = 75.0
    settings = f'{{"confidence": {detection_threshold:.1f}}}'
    Gst.util_set_object_arg(elements["mlvdetection"], "settings", settings)
    Gst.util_set_object_arg(elements["mlvdetection"], "results", "4")
    Gst.util_set_object_arg(elements["mlvdetection"], "module", detection["module"])
    Gst.util_set_object_arg(
        elements["mlvdetection"], "labels", detection["labels"]
    )

    Gst.util_set_object_arg(elements["capsfilter_1"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_0"], "engine", "gles")

    # Stream 1
    Gst.util_set_object_arg(
        elements["capsfilter_2"],
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

    classification_threshold = 75.0
    settings = f'{{"confidence": {classification_threshold:.1f}}}'
    Gst.util_set_object_arg(elements["mlvclassification"], "settings", settings)
    Gst.util_set_object_arg(elements["mlvclassification"], "results", "5")
    Gst.util_set_object_arg(
        elements["mlvclassification"], "module", classification["module"]
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"], "labels", classification["labels"]
    )

    Gst.util_set_object_arg(elements["capsfilter_3"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_1"], "engine", "gles")

    # Side by side all streams
    Gst.util_set_object_arg(elements["composer"], "background", "0")

    Gst.util_set_object_arg(elements["display"], "sync", "false")
    Gst.util_set_object_arg(elements["display"], "fullscreen", "true")

    # Add all elements
    for element in elements.values():
        pipe.add(element)

    # Link all elements
    # fmt: off
    link_orders = [
        [
            "qmmfsrc", "capsfilter_0", "queue_0", "tee_0", "queue_1",
            "metamux_0", "overlay_0", "composer"
        ],
        [
            "tee_0", "queue_2", "mlvconverter_0", "queue_3", "mltflite_0",
            "queue_4", "mlvdetection", "capsfilter_1", "queue_5", "metamux_0"
        ],
        [
            "qmmfsrc", "capsfilter_2", "queue_6", "tee_1", "queue_7",
            "metamux_1", "overlay_1", "composer"
        ],
        [
            "tee_1", "queue_8", "mlvconverter_1", "queue_9", "mltflite_1",
            "queue_10", "mlvclassification", "capsfilter_3", "queue_11", "metamux_1"
        ],
        ["composer", "queue_12", "display"]
    ]
    # fmt: on
    link_elements(link_orders, elements)

    # Set pad properties
    qmmf_video_0 = elements["qmmfsrc"].get_static_pad("video_0")
    qmmf_video_1 = elements["qmmfsrc"].get_static_pad("video_1")
    Gst.util_set_object_arg(qmmf_video_0, "type", "preview")
    Gst.util_set_object_arg(qmmf_video_1, "type", "video")
    qmmf_video_0 = None
    qmmf_video_1 = None

    composer_sink_0 = elements["composer"].get_static_pad("sink_0")
    composer_sink_1 = elements["composer"].get_static_pad("sink_1")
    Gst.util_set_object_arg(composer_sink_0, "position", "<0, 0>")
    Gst.util_set_object_arg(composer_sink_0, "dimensions", "<640, 360>")
    Gst.util_set_object_arg(composer_sink_1, "position", "<640, 0>")
    Gst.util_set_object_arg(composer_sink_1, "dimensions", "<640, 360>")
    composer_sink_0 = None
    composer_sink_1 = None


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
