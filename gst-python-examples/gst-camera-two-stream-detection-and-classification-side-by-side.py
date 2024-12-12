# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

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
- YOLOv8 TFLite model to identify the object in scene from camera stream and
overlay the bounding boxes over the detected objects
- Resnet101 TFLite model to classify scene from camera stream and overlay the
classification labels on the top left corner.
Then the results are shown side by side on the display.

The file paths are hard coded in the python script as follows:
- Detection model: /opt/data/YoloV8N_Detection_Quantized.tflite
- Detection labels: /opt/data/yolov8n.labels
- Classification model: /opt/data/Resnet101_Quantized.tflite
- Classification labels: /opt/data/resnet101.labels
"""

DEFAULT_DETECTION_MODEL = "/opt/data/YoloV8N_Detection_Quantized.tflite"
DEFAULT_DETECTION_LABELS = "/opt/data/yolov8n.labels"
DEFAULT_CLASSIFICATION_MODEL = "/opt/data/Resnet101_Quantized.tflite"
DEFAULT_CLASSIFICATION_LABELS = "/opt/data/resnet101.labels"

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

    parser.add_argument("--tflite_object_detection_model", type=str,
        default=DEFAULT_DETECTION_MODEL,
        help="Path to TfLite object detection model"
    )
    parser.add_argument("--tflite_object_detection_labels", type=str,
        default=DEFAULT_DETECTION_LABELS,
        help="Path to TfLite object detection labels"
    )
    parser.add_argument("--tflite_classification_model", type=str,
        default=DEFAULT_CLASSIFICATION_MODEL,
        help="Path to TfLite classification model"
    )
    parser.add_argument("--tflite_classification_labels", type=str,
        default=DEFAULT_CLASSIFICATION_LABELS,
        help="Path to TfLite classification labels"
    )

    args = parser.parse_args()

    # Create all elements
    # fmt: off
    elements = {
        "qmmfsrc":           create_element("qtiqmmfsrc", "camsrc"),
        # Stream 0
        "capsfilter_0":      create_element("capsfilter", "camout0caps"),
        "queue_0":           create_element("queue", "queue0"),
        "tee_0":             create_element("tee", "split0"),
        "mlvconverter_0":    create_element("qtimlvconverter", "converter0"),
        "queue_1":           create_element("queue", "queue1"),
        "mltflite_0":        create_element("qtimltflite", "inference0"),
        "queue_2":           create_element("queue", "queue2"),
        "mlvdetection":      create_element("qtimlvdetection", "detection"),
        "capsfilter_1":      create_element("capsfilter", "metamux0metacaps"),
        "queue_3":           create_element("queue", "queue3"),
        "metamux_0":         create_element("qtimetamux", "metamux0"),
        "overlay_0":         create_element("qtivoverlay", "overlay0"),
        # Stream 1
        "capsfilter_2":      create_element("capsfilter", "camout1caps"),
        "queue_4":           create_element("queue", "queue4"),
        "tee_1":             create_element("tee", "split1"),
        "mlvconverter_1":    create_element("qtimlvconverter", "converter1"),
        "queue_5":           create_element("queue", "queue5"),
        "mltflite_1":        create_element("qtimltflite", "inference1"),
        "queue_6":           create_element("queue", "queue6"),
        "mlvclassification": create_element("qtimlvclassification", "classification"),
        "capsfilter_3":      create_element("capsfilter", "metamux1metacaps"),
        "queue_7":           create_element("queue", "queue7"),
        "metamux_1":         create_element("qtimetamux", "metamux1"),
        "overlay_1":         create_element("qtivoverlay", "overlay1"),
        # Side by side all streams
        "composer":          create_element("qtivcomposer", "composer"),
        "queue_8":           create_element("queue", "queue8"),
        "display":           create_element("waylandsink", "display")
    }
    # fmt: on

    # Set element properties
    Gst.util_set_object_arg(elements["qmmfsrc"], "camera", "0")

    # Stream 0
    Gst.util_set_object_arg(
        elements["capsfilter_0"],
        "caps",
        "video/x-raw(memory:GBM),format=NV12,compression=ubwc,\
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
        args.tflite_object_detection_model,
    )

    Gst.util_set_object_arg(elements["mlvdetection"], "threshold", "75.0")
    Gst.util_set_object_arg(elements["mlvdetection"], "results", "4")
    Gst.util_set_object_arg(elements["mlvdetection"], "module", "yolov8")
    Gst.util_set_object_arg(
        elements["mlvdetection"], "labels", args.tflite_object_detection_labels
    )
    Gst.util_set_object_arg(
        elements["mlvdetection"],
        "constants",
        "YoloV8,q-offsets=<-107.0,-128.0,0.0>,\
        q-scales=<3.093529462814331,0.00390625,1.0>;",
    )

    Gst.util_set_object_arg(elements["capsfilter_1"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_0"], "engine", "gles")

    # Stream 1
    Gst.util_set_object_arg(
        elements["capsfilter_2"],
        "caps",
        "video/x-raw(memory:GBM),format=NV12,\
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
        elements["mltflite_1"], "model", args.tflite_classification_model
    )

    Gst.util_set_object_arg(elements["mlvclassification"], "threshold", "51.0")
    Gst.util_set_object_arg(elements["mlvclassification"], "results", "5")
    Gst.util_set_object_arg(
        elements["mlvclassification"], "module", "mobilenet"
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"], "labels", args.tflite_classification_labels
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"], "extra-operation", "softmax"
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"],
        "constants",
        "Mobilenet,q-offsets=<-82.0>,q-scales=<0.21351955831050873>;",
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
            "qmmfsrc", "capsfilter_0", "queue_0", "tee_0", "metamux_0",
            "overlay_0", "composer"
        ],
        [
            "tee_0", "mlvconverter_0", "queue_1", "mltflite_0", "queue_2",
            "mlvdetection", "capsfilter_1", "queue_3", "metamux_0"
        ],
        [
            "qmmfsrc", "capsfilter_2", "queue_4", "tee_1", "metamux_1",
            "overlay_1", "composer"
        ],
        [
            "tee_1", "mlvconverter_1", "queue_5", "mltflite_1", "queue_6",
            "mlvclassification", "capsfilter_3", "queue_7", "metamux_1"
        ],
        ["composer", "queue_8", "display"]
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


def main():
    """Main function to set up and run the GStreamer pipeline."""
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
