#!/usr/bin/env python3

# Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
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
This app sets up GStreamer pipeline to read the jpg images and display.
Usage:
For Preview on Display:
python3 /usr/bin/gst-jpg-image-decode.py -i /etc/media/imagefiles_%d.jpg"""
DEFAULT_INPUT_FILE = "/etc/media/imagefiles_%d.jpg"

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

def link_elements(link_order, elements):
    """Link elements in the specified order."""
    src = None  # Initialize src to None at the start of each link_order
    for element in link_order:
        dest = elements[element]
        if src and not src.link(dest):
            raise Exception(
                f"Unable to link element {src.get_name()} to "
                f"{dest.get_name()}"
            )
        src = dest  # Update src to the current dest for the next iteration

def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description=DESCRIPTION,
        formatter_class=type(
            'CustomFormatter',
            (argparse.ArgumentDefaultsHelpFormatter, argparse.RawTextHelpFormatter),
            {}
        )
    )

    parser.add_argument(
        '-i', "--filepath", type=str, default=DEFAULT_INPUT_FILE,
        help="Input File Path"
    )

    return parser.parse_args()

def create_pipeline(pipeline, args):
    """Initialize and link elements for the GStreamer pipeline."""
    # Create elements
    elements = {
        "multifilesrc" : create_element("multifilesrc", "multifilesrc"),
        "caps": create_element("capsfilter", "caps"),
        "jpegdec": create_element("jpegdec", "jpegdec"),
        "videoconvert" : create_element("videoconvert", "videoconvert"),
        "sink"   : create_element("waylandsink", "sink")
    }

    # Set properties
    elements["multifilesrc"].set_property('location', args.filepath)
    elements["multifilesrc"].set_property('index', 1)

    # Create the caps filter
    elements["caps"].set_property(
        "caps", Gst.Caps.from_string(
            'image/jpeg,width=1280,height=720,framerate=2/1'
        )
    )

    # Set sink properties
    Gst.util_set_object_arg(elements["sink"], "fullscreen", "true")

    # Add elements to the pipeline
    for element in elements.values():
        pipeline.add(element)

    # Link elements
    link_order = [
        "multifilesrc", "caps", "jpegdec", "videoconvert", "sink",
    ]
    link_elements(link_order, elements)

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

    # Initialize GStreamer
    Gst.init(None)
    mloop = GLib.MainLoop()

    # Parse arguments
    args = parse_arguments()

    # Create the pipeline
    try:
        pipeline = Gst.Pipeline.new("video-recording-pipeline")
        if not pipeline:
            raise Exception(f"Unable to create video recording pipeline")
        create_pipeline(pipeline, args)
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
    if eos_received:
        print("App execution successful")

if __name__ == "__main__":
    sys.exit(main())
