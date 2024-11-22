# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

from gi.repository import Gst, GLib
import os
import sys
import signal
import argparse

import gi
gi.require_version('Gst', '1.0')
gi.require_version("GLib", "2.0")

# Constants
DESCRIPTION = """
This application Demonstrates in viewing Multicamera Camera Live
on waylandsink or Dumping encoder output i.e. mp4 on device

Usage:
For Preview on Display:
python3 gst-multi-camera-stream-example.py  --width=1920 --height=1080
For Video Encoder mp4 on device:
python3 gst-multi-camera-stream-example.py --file --width=1920 --height=1080
Help:
python3 gst-multi-camera-stream-example.py --help
"""

DEFAULT_OUTPUT_FILE_PRIMARY_CAMERA = "/opt/cam_0.mp4"
DEFAULT_OUTPUT_FILE_SECONDARY_CAMERA = "/opt/cam_1.mp4"
DEFAULT_WIDTH = 1280
DEFAULT_HEIGHT = 720
DEFAULT_PRIMARY_CAMERA_ID = 0
DEFAULT_SECONDARY_CAMERA_ID = 1

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


def create_pipeline(pipeline, cameraid):
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
        "--width", "-W", type=int, default=DEFAULT_WIDTH, help="width")
    parser.add_argument(
        "--height", "-H", type=int, default=DEFAULT_HEIGHT, help="height")
    parser.add_argument(
        "--framerate", "-F", type=str, default='30/1', help="framerate(fraction)")
    parser.add_argument(
        "--display", "-D", action='store_false', help="Output Sink type as wayland")
    args = parser.parse_args()

    capsvalues = "video/x-raw(memory:GBM),format=NV12,width=" + str(
        args.width) + ",height=" + str(args.height) + ",framerate=" + str(args.framerate) + ",compression=ubwc"

    if(args.display):
        elements = {
            "camsrc": create_element("qtiqmmfsrc", "camsrc"),
            "camcaps": create_element("capsfilter", "camcaps"),
            "sink": create_element("waylandsink", "sink")
        }

        if (cameraid == DEFAULT_PRIMARY_CAMERA_ID):
            elements["camsrc"].set_property("camera", DEFAULT_PRIMARY_CAMERA_ID)
            elements["sink"].set_property("x", 0)
            elements["sink"].set_property("y", 0)
            elements["sink"].set_property("width", 640)
            elements["sink"].set_property("height", 480)
        else:
            elements["camsrc"].set_property(
                "camera", DEFAULT_SECONDARY_CAMERA_ID)
            elements["sink"].set_property("x", 640)
            elements["sink"].set_property("y", 0)
            elements["sink"].set_property("width", 640)
            elements["sink"].set_property("height", 480)

            capsvalues = "video/x-raw(memory:GBM),format=NV12,width="+str(
                DEFAULT_WIDTH) + ",height="+str(DEFAULT_HEIGHT) + ",framerate="+str(args.framerate)+",compression=ubwc"

        elements["camcaps"].set_property(
            "caps", Gst.Caps.from_string(capsvalues))
        elements["sink"].set_property("async", False)
        elements["sink"].set_property("sync", False)

        # Add elements to the pipeline
        for element in elements.values():
            pipeline.add(element)

        # Link elements
        link_order = [
            "camsrc", "camcaps", "sink"
        ]
        link_elements(link_order, elements)

    else:
        # Create elements
        elements = {
            "camsrc": create_element("qtiqmmfsrc", "camsrc"),
            "camcaps": create_element("capsfilter", "camcaps"),
            "encoder": create_element("v4l2h264enc", "encoder"),
            "parser": create_element("h264parse", "parser"),
            "mux": create_element("mp4mux", "mux"),
            "sink": create_element("filesink", "sink")
        }

        queue_count = 2
        for i in range(queue_count):
            queue_name = f"queue{i}"
            elements[queue_name] = create_element("queue", queue_name)

        if (cameraid == DEFAULT_PRIMARY_CAMERA_ID):
            elements["camsrc"].set_property("camera", DEFAULT_PRIMARY_CAMERA_ID)
            elements["sink"].set_property(
                "location", DEFAULT_OUTPUT_FILE_PRIMARY_CAMERA)
        else:
            elements["camsrc"].set_property(
                "camera", DEFAULT_SECONDARY_CAMERA_ID)
            elements["sink"].set_property(
                "location", DEFAULT_OUTPUT_FILE_SECONDARY_CAMERA)
            capsvalues = "video/x-raw(memory:GBM),format=NV12,width=" + str(
                DEFAULT_WIDTH) + ",height=" + str(DEFAULT_HEIGHT) + ",framerate="+str(args.framerate)+",compression=ubwc"

        elements["encoder"].set_property("capture-io-mode", 5)
        elements["encoder"].set_property("output-io-mode", 5)
        elements["mux"].set_property("reserved-moov-update-period", 1000000)
        elements["mux"].set_property("reserved-bytes-per-sec", 10000)
        elements["mux"].set_property("reserved-max-duration", 1000000000)

        # Set properties
        elements["camcaps"].set_property(
            "caps", Gst.Caps.from_string(capsvalues))

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

    # Create the pipelines
    try:
        pipelinecam_id0 = Gst.Pipeline.new("video-recording-pipeline0")
        pipelinecam_id1 = Gst.Pipeline.new("video-recording-pipeline1")
        if not (pipelinecam_id0 and pipelinecam_id1):
            raise Exception(f"Unable to create pipelines")

        create_pipeline(pipelinecam_id0, DEFAULT_PRIMARY_CAMERA_ID)
        create_pipeline(pipelinecam_id1, DEFAULT_SECONDARY_CAMERA_ID)

    except Exception as e:
        print(f"{e} Exiting...")
        return -1

    # Handle Ctrl+C
    interrupt_watch_id_cam0 = GLib.unix_signal_add(
        GLib.PRIORITY_HIGH, signal.SIGINT, handle_interrupt_signal, pipelinecam_id0, mloop
    )
    interrupt_watch_id_cam1 = GLib.unix_signal_add(
        GLib.PRIORITY_HIGH, signal.SIGINT, handle_interrupt_signal, pipelinecam_id1, mloop
    )

    # Wait until error or EOS
    bus_cam0 = pipelinecam_id0.get_bus()
    bus_cam1 = pipelinecam_id1.get_bus()
    bus_cam0.add_signal_watch()
    bus_cam1.add_signal_watch()
    bus_cam0.connect("message", handle_bus_message, mloop)
    bus_cam1.connect("message", handle_bus_message, mloop)

    # Start playing
    print("Setting to PLAYING...")
    pipelinecam_id0.set_state(Gst.State.PLAYING)
    pipelinecam_id1.set_state(Gst.State.PLAYING)
    mloop.run()

    GLib.source_remove(interrupt_watch_id_cam0)
    GLib.source_remove(interrupt_watch_id_cam1)
    bus_cam0.remove_signal_watch()
    bus_cam1.remove_signal_watch()
    bus_cam0 = None
    bus_cam1 = None

    print("Setting to NULL...")
    pipelinecam_id0.set_state(Gst.State.NULL)
    pipelinecam_id1.set_state(Gst.State.NULL)

    mloop = None
    pipelinecam_id0 = None
    pipelinecam_id1 = None
    Gst.deinit()


if __name__ == "__main__":
    sys.exit(main())
