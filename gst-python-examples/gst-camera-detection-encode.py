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
This app sets up GStreamer pipeline for object detection.
Initializes and links elements for capturing live stream from camera,
performs inference (object detection) using MODEL and LABELS files,
and encodes the rendered video as OUTPUT.
"""
DEFAULT_MODEL_FILE = "/opt/data/YOLOv8-Detection-Quantized.tflite"
DEFAULT_LABELS_FILE = "/opt/data/yolov8.labels"
DEFAULT_OUTPUT_FILE = "/opt/data/det_recording_720p.mp4"
DELEGATE_PATH = "libQnnTFLiteDelegate.so"

waiting_for_eos = False
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

    if message.type == Gst.MessageType.ERROR:
        error, debug_info = message.parse_error()
        print("ERROR:", message.src.get_name(), " ", error.message)
        if debug_info:
            print("debugging info:", debug_info)
        mloop.quit()
    elif message.type == Gst.MessageType.EOS:
        print("EoS received")
        mloop.quit()
    return True

def create_element(factory_name, name):
    """Create a GStreamer element."""
    element = Gst.ElementFactory.make(factory_name, name)
    if not element:
        raise Exception(f"Unable to create element {name}")
    return element

def link_elements(link_orders, elements):
    """Link elements in the specified order."""
    for link_order in link_orders:
        src = None  # Initialize src to None at the start of each link_order
        for element in link_order:
            dest = elements[element]
            if src and not src.link(dest):
                raise Exception(
                    f"Unable to link element {src.get_name()} to "
                    f"{dest.get_name()}"
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
    parser.add_argument(
        "--model", type=str, default=DEFAULT_MODEL_FILE,
        help="Model File Path"
    )
    parser.add_argument(
        "--labels", type=str, default=DEFAULT_LABELS_FILE,
        help="Labels File Path"
    )
    parser.add_argument(
        "--output", type=str, default=DEFAULT_OUTPUT_FILE,
        help="Output File Path"
    )
    args = parser.parse_args()

    # Create elements
    elements = {
        "camsrc"    : create_element("qtiqmmfsrc", "camsrc"),
        "camcaps"   : create_element("capsfilter", "camcaps"),
        "split"     : create_element("tee", "split"),
        "metamux"   : create_element("qtimetamux", "metamux"),
        "overlay"   : create_element("qtioverlay", "overlay"),
        "encoder"   : create_element("v4l2h264enc", "encoder"),
        "parser"    : create_element("h264parse", "parser"),
        "mux"       : create_element("mp4mux", "mux"),
        "sink"      : create_element("filesink", "sink"),
        "converter" : create_element("qtimlvconverter", "converter"),
        "tflite"    : create_element("qtimltflite", "tflite"),
        "detection" : create_element("qtimlvdetection", "detection"),
        "capsfilter": create_element("capsfilter", "capsfilter")
    }

    queue_count = 8
    for i in range(queue_count):
        queue_name = f"queue{i}"
        elements[queue_name] = create_element("queue", queue_name)

    # Set properties
    elements["camcaps"].set_property(
        "caps", Gst.Caps.from_string(
            "video/x-raw(memory:GBM),format=NV12,width=1280,height=720,"
            "framerate=30/1,compression=ubwc"
        )
    )

    elements["overlay"].set_property("engine", "gles")

    elements["encoder"].set_property("capture-io-mode", 5)
    elements["encoder"].set_property("output-io-mode", 5)

    elements["sink"].set_property("location", args.output)

    elements["tflite"].set_property("delegate", "external")
    elements["tflite"].set_property("external-delegate-path", DELEGATE_PATH)
    elements["tflite"].set_property("model", args.model)

    elements["detection"].set_property("threshold", 75.0)
    elements["detection"].set_property("results", 10)
    elements["detection"].set_property("module", "yolov8")
    elements["detection"].set_property("labels", args.labels)
    elements["detection"].set_property(
        "constants",
        "YOLOv8,q-offsets=<-107.0, -128.0, 0.0>,"
        "q-scales=<3.093529462814331, 0.00390625, 1.0>;"
    )

    elements["capsfilter"].set_property("caps",
        Gst.Caps.from_string("text/x-raw")
    )

    # Create GstStructure for external-delegate-options
    options_structure = Gst.Structure.new_empty("QNNExternalDelegate")
    options_structure.set_value("backend_type", "htp")
    elements["tflite"].set_property("external-delegate-options", options_structure)

    # Add elements to the pipeline
    for element in elements.values():
        pipeline.add(element)

    # Link elements
    link_orders = [
        ["camsrc", "camcaps", "queue0", "split"],
        [
            "split", "queue1", "metamux", "queue2", "overlay", "queue3",
            "encoder", "parser", "queue4", "mux", "queue5", "sink"
        ],
        [
            "split", "queue6", "converter", "queue7", "tflite", "detection",
            "capsfilter", "metamux"
        ]
    ]
    link_elements(link_orders, elements)

def main():
    """Main function to set up and run the GStreamer pipeline."""

    # Set the environment
    os.environ["XDG_RUNTIME_DIR"] = "/dev/socket/weston"
    os.environ["WAYLAND_DISPLAY"] = "wayland-1"

    # Initialize GStreamer
    Gst.init(None)
    mloop = GLib.MainLoop()

    # Create the pipeline
    try:
        pipeline = Gst.Pipeline.new("object-detection-pipeline")
        if not pipeline:
            raise Exception(f"Unable to create object detection pipeline")
        create_pipeline(pipeline)
    except Exception as e:
        print(f"{e} Exiting...")
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

if __name__ == "__main__":
    sys.exit(main())
