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

DEFAULT_RTSP_SRC = "rtsp://127.0.0.1:8900/live"

# Configurations for Detection
DEFAULT_DETECTION_MODEL = "/etc/models/yolox_quantized.tflite"
DEFAULT_DETECTION_MODULE = "yolov8"
DEFAULT_DETECTION_LABELS = "/etc/labels/yolox.json"

DESCRIPTION = f"""
The application receives an RTSP stream as source, decodes it, uses a TFLite
model to identify the object in scene from camera stream and overlay
the bounding boxes over the detected objects. The results are shown on the
display.

The default file paths in the python script are as follows:
- Detection model:  {DEFAULT_DETECTION_MODEL}
- Detection labels: {DEFAULT_DETECTION_LABELS}

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
        "--rtsp", type=str, default=DEFAULT_RTSP_SRC,
        help="RTSP URL"
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

    args = parser.parse_args()

    detection = {
        "model": args.detection_model,
        "module": args.detection_module,
        "labels": args.detection_labels
    }

    # Check if all model and label files are present
    if not os.path.exists(detection["model"]):
        print(f"File {detection['model']} does not exist")
        sys.exit(1)
    if not os.path.exists(detection["labels"]):
        print(f"File {detection['labels']} does not exist")
        sys.exit(1)

    # Create all elements
    # fmt: off
    elements = {
        "rtspsrc":      create_element("rtspsrc", "rtspsrc"),
        "rtph264depay": create_element("rtph264depay", "rtph264depay"),
        "capsfilter_0": create_element("capsfilter", "rtph264depaycaps"),
        "h264parse":    create_element("h264parse", "h264parser"),
        "v4l2h264dec":  create_element("v4l2h264dec", "v4l2h264decoder"),
        "deccaps":      create_element("capsfilter", "deccaps"),
        "tee":          create_element("tee", "split"),
        "mlvconverter": create_element("qtimlvconverter", "converter"),
        "mltflite":     create_element("qtimltflite", "inference"),
        "mlvdetection": create_element("qtimlpostprocess", "detection"),
        "capsfilter_1": create_element("capsfilter", "metamuxmetacaps"),
        "metamux":      create_element("qtimetamux", "metamux"),
        "overlay":      create_element("qtivoverlay", "overlay"),
        "display":      create_element("waylandsink", "display")
    }
    # fmt: on

    queue_count = 6
    for i in range(queue_count):
        queue_name = f"queue_{i}"
        elements[queue_name] = create_element("queue", queue_name)

    # Set element properties
    Gst.util_set_object_arg(elements["rtspsrc"], "location", args.rtsp)

    Gst.util_set_object_arg(
        elements["capsfilter_0"],
        "caps",
        "video/x-h264,colorimetry=bt709",
    )

    Gst.util_set_object_arg(elements["h264parse"], "config-interval", "1")

    Gst.util_set_object_arg(elements["v4l2h264dec"], "capture-io-mode", "dmabuf")
    Gst.util_set_object_arg(elements["v4l2h264dec"], "output-io-mode", "dmabuf")

    Gst.util_set_object_arg(
        elements["deccaps"], "caps", "video/x-raw,format=NV12"
    )

    Gst.util_set_object_arg(elements["mltflite"], "delegate", "external")
    Gst.util_set_object_arg(
        elements["mltflite"],
        "external-delegate-path",
        "libQnnTFLiteDelegate.so",
    )
    Gst.util_set_object_arg(
        elements["mltflite"],
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp,htp_precision=(string)1;",
    )
    Gst.util_set_object_arg(elements["mltflite"], "model", detection["model"])

    threshold = 75.0
    settings = f'{{"confidence": {threshold:.1f}}}'
    Gst.util_set_object_arg(elements["mlvdetection"], "settings", settings)
    Gst.util_set_object_arg(elements["mlvdetection"], "results", "4")
    Gst.util_set_object_arg(
        elements["mlvdetection"], "module", detection["module"]
    )
    Gst.util_set_object_arg(
        elements["mlvdetection"], "labels", detection["labels"]
    )

    Gst.util_set_object_arg(elements["capsfilter_1"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay"], "engine", "gles")

    Gst.util_set_object_arg(elements["display"], "sync", "false")
    Gst.util_set_object_arg(elements["display"], "fullscreen", "true")

    # Add all elements
    for element in elements.values():
        pipe.add(element)

    # Link all elements
    # fmt: off
    link_orders = [
        [
            "rtph264depay", "capsfilter_0", "h264parse", "v4l2h264dec", "deccaps",
            "tee", "queue_0", "metamux", "overlay", "queue_1", "display"
        ],
        [
            "tee", "queue_2", "mlvconverter", "queue_3", "mltflite", "queue_4",
            "mlvdetection", "capsfilter_1", "queue_5", "metamux"
        ]
    ]
    # fmt: on
    link_elements(link_orders, elements)

    def on_pad_added(elem, pad, dest):
        if "rtp" in pad.get_name():
            sink_pad = dest.get_static_pad("sink")
            if (
                not sink_pad.is_linked()
                and pad.link(sink_pad) != Gst.PadLinkReturn.OK
            ):
                raise (
                    f"Failed to link {elem.get_name()} with {dest.get_name()}!"
                )

    elements["rtspsrc"].connect(
        "pad-added", on_pad_added, elements["rtph264depay"]
    )


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
