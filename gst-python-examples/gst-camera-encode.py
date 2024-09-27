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
This app sets up GStreamer pipeline for video recording.
Initializes and links elements for capturing live stream from camera
and saving the encoded video as OUTPUT.
"""
DEFAULT_OUTPUT_FILE = "/opt/data/recording_720p.mp4"

waiting_for_eos = False
def handle_interrupt_signal(pipeline, mloop):
    """Handle Ctrl+C."""
    global waiting_for_eos

    _, state, _ = pipeline.get_state(0)
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

def link_elements(link_order, elements):
    """Link elements in the specified order."""
    src = None  # Initialize src to None at the start of each link_order
    for element in link_order:
        dest = elements[element]
        if src:
            if not src.link(dest):
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
        "--output", type=str, default=DEFAULT_OUTPUT_FILE,
        help="Output File Path"
    )
    args = parser.parse_args()

    # Create elements
    elements = {
        "camsrc" : create_element("qtiqmmfsrc", "camsrc"),
        "camcaps": create_element("capsfilter", "camcaps"),
        "encoder": create_element("v4l2h264enc", "encoder"),
        "parser" : create_element("h264parse", "parser"),
        "mux"    : create_element("mp4mux", "mux"),
        "sink"   : create_element("filesink", "sink")
    }

    queue_count = 2
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

    elements["encoder"].set_property("capture-io-mode", 5)
    elements["encoder"].set_property("output-io-mode", 5)

    elements["sink"].set_property("location", args.output)

    # Add elements to the pipeline
    for element in elements.values():
        pipeline.add(element)

    # Link elements
    link_order = [
        "camsrc", "camcaps", "encoder", "parser", "queue0",
        "mux", "queue1", "sink"
    ]
    link_elements(link_order, elements)

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
        pipeline = Gst.Pipeline.new("video-recording-pipeline")
        if not pipeline:
            raise Exception(f"Unable to create video recording pipeline")
        create_pipeline(pipeline)
    except Exception as e:
        print(f"{e} Exiting...")
        exit(-1)

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
