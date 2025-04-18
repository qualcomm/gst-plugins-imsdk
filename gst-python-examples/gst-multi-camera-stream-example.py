#!/usr/bin/env python3

################################################################################
# Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

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
This application Demonstrates in viewing Multicamera Camera Live
on waylandsink or Dumping encoder output i.e. mp4 on device

Usage:
For Preview on Display:
python3 gst-multi-camera-stream-example.py -D 1 --width=1920 --height=1080
For Video Encoder mp4 on device:
python3 gst-multi-camera-stream-example.py --width=1920 --height=1080 -- output_of_primary_cam=<cam_0.mp4> --output_of_secondary_cam==<cam_1.mp4>
Help:
python3 gst-multi-camera-stream-example.py --help
"""

DEFAULT_OUTPUT_FILE_PRIMARY_CAMERA = "/opt/cam_0.mp4"
DEFAULT_OUTPUT_FILE_SECONDARY_CAMERA = "/opt/cam_1.mp4"
DEFAULT_WIDTH = 1280
DEFAULT_HEIGHT = 720
DEFAULT_PRIMARY_CAMERA_ID = 0
DEFAULT_SECONDARY_CAMERA_ID = 1
GST_V4L2_IO_DMABUF = 4
GST_V4L2_IO_DMABUF_IMPORT = 5

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
        "--display", "-D", type=int, default=0, help="Output Sink type as preview else default filesink")
    parser.add_argument(
        "--output_of_primary_cam", type=str,
        default=DEFAULT_OUTPUT_FILE_PRIMARY_CAMERA,
        help="Output file of priamry camera stream"
    )
    parser.add_argument(
        "--output_of_secondary_cam", type=str,
        default=DEFAULT_OUTPUT_FILE_SECONDARY_CAMERA,
        help="Output file of secondary camera stream"
    )

    args = parser.parse_args()

    if(args.display):
        print("Preview Enabled")
        elements = {
            "camsrc_0": create_element("qtiqmmfsrc", "camsrc_0"),
            "camsrc_1": create_element("qtiqmmfsrc", "camsrc_1"),
            "camcaps_0": create_element("capsfilter", "camcaps_0"),
            "camcaps_1": create_element("capsfilter", "camcaps_1"),
            "composer": create_element("qtivcomposer", "composer"),
            "sink": create_element("waylandsink", "sink")
        }

        elements["camsrc_0"].set_property("camera", DEFAULT_PRIMARY_CAMERA_ID)
        capsvalues = "video/x-raw,format=NV12,width=" + str(
            args.width) + ",height=" + str(args.height) + ",framerate=" + str(args.framerate)
        elements["camcaps_0"].set_property("caps", Gst.Caps.from_string(capsvalues))

        elements["camsrc_1"].set_property("camera", DEFAULT_SECONDARY_CAMERA_ID)
        capsvalues = "video/x-raw,format=NV12,width="+str(
            DEFAULT_WIDTH) + ",height="+str(DEFAULT_HEIGHT) + ",framerate="+str(args.framerate)
        elements["camcaps_1"].set_property("caps", Gst.Caps.from_string(capsvalues))

        Gst.util_set_object_arg(elements["sink"], "fullscreen", "true")

        print("Add elements to the pipeline")
        # Add elements to the pipeline
        for element in elements.values():
            pipeline.add(element)

        print("Set composer position")
        # Link elements
        link_order = [
            [
                "camsrc_0", "camcaps_0", "composer", "sink"
            ],
            [
                "camsrc_1", "camcaps_1", "composer"
            ]
        ]

        link_elements(link_order, elements)

        # Set pad properties
        composer_sink_0 = elements["composer"].get_static_pad("sink_0")
        Gst.util_set_object_arg(composer_sink_0, "position", "<0, 0>")
        Gst.util_set_object_arg(composer_sink_0, "dimensions", "<640, 360>")
        composer_sink_0 = None

        composer_sink_1 = elements["composer"].get_static_pad("sink_1")
        Gst.util_set_object_arg(composer_sink_1, "position", "<640, 0>")
        Gst.util_set_object_arg(composer_sink_1, "dimensions", "<640, 360>")
        composer_sink_1 = None
    else:
        print("Filesink Enabled")
        # Create elements
        elements = {
            "camsrc_0": create_element("qtiqmmfsrc", "camsrc_0"),
            "camsrc_1": create_element("qtiqmmfsrc", "camsrc_1"),
            "camcaps_0": create_element("capsfilter", "camcaps_0"),
            "camcaps_1": create_element("capsfilter", "camcaps_1"),
            "encoder_0": create_element("v4l2h264enc", "encoder_0"),
            "parser_0": create_element("h264parse", "parser_0"),
            "mux_0": create_element("mp4mux", "mux_0"),
            "sink_0": create_element("filesink", "sink_0"),
            "encoder_1": create_element("v4l2h264enc", "encoder_1"),
            "parser_1": create_element("h264parse", "parser_1"),
            "mux_1": create_element("mp4mux", "mux_1"),
            "sink_1": create_element("filesink", "sink_1")
        }

        queue_count = 2
        for i in range(queue_count):
            queue_name = f"queue{i}"
            elements[queue_name] = create_element("queue", queue_name)

        # Set properties
        elements["camsrc_0"].set_property("camera", DEFAULT_PRIMARY_CAMERA_ID)
        capsvalues = "video/x-raw,format=NV12,width=" + str(
            args.width) + ",height=" + str(args.height) + ",framerate=" + str(args.framerate)
        elements["camcaps_0"].set_property("caps", Gst.Caps.from_string(capsvalues))

        elements["camsrc_1"].set_property("camera", DEFAULT_SECONDARY_CAMERA_ID)
        capsvalues = "video/x-raw,format=NV12,width="+str(
            DEFAULT_WIDTH) + ",height="+str(DEFAULT_HEIGHT) + ",framerate="+str(args.framerate)
        elements["camcaps_1"].set_property("caps", Gst.Caps.from_string(capsvalues))

        elements["sink_0"].set_property("location", args.output_of_primary_cam)
        elements["sink_1"].set_property("location", args.output_of_secondary_cam)

        elements["encoder_0"].set_property("capture-io-mode", GST_V4L2_IO_DMABUF)
        elements["encoder_0"].set_property("output-io-mode", GST_V4L2_IO_DMABUF_IMPORT)
        elements["mux_0"].set_property("reserved-moov-update-period", 1000000)
        elements["mux_0"].set_property("reserved-bytes-per-sec", 10000)
        elements["mux_0"].set_property("reserved-max-duration",  20000000000)

        elements["encoder_1"].set_property("capture-io-mode", GST_V4L2_IO_DMABUF)
        elements["encoder_1"].set_property("output-io-mode", GST_V4L2_IO_DMABUF_IMPORT)
        elements["mux_1"].set_property("reserved-moov-update-period", 1000000)
        elements["mux_1"].set_property("reserved-bytes-per-sec", 10000)
        elements["mux_1"].set_property("reserved-max-duration",  20000000000)

        # Add elements to the pipeline
        for element in elements.values():
            pipeline.add(element)

        # Link elements
        link_order = [
            [    "camsrc_0", "camcaps_0", "encoder_0", "parser_0",
                "mux_0", "queue0", "sink_0"
            ],
            [    "camsrc_1", "camcaps_1", "encoder_1", "parser_1",
                "mux_1", "queue1", "sink_1"
            ]
        ]
        link_elements(link_order, elements)
        print("Camera 0 Filesink output", args.output_of_primary_cam)
        print("Camera 0 Filesink output", args.output_of_secondary_cam)

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

    # Create the pipelines
    try:
        pipelinecam_id0 = Gst.Pipeline.new("video-recording-pipeline0")
        if not (pipelinecam_id0):
            raise Exception(f"Unable to create pipelines")

        create_pipeline(pipelinecam_id0, DEFAULT_PRIMARY_CAMERA_ID)

    except Exception as e:
        print(f"{e} Exiting...")
        return -1

    # Handle Ctrl+C
    interrupt_watch_id_cam0 = GLib.unix_signal_add(
        GLib.PRIORITY_HIGH, signal.SIGINT, handle_interrupt_signal, pipelinecam_id0, mloop
    )

    # Wait until error or EOS
    bus_cam0 = pipelinecam_id0.get_bus()
    bus_cam0.add_signal_watch()
    bus_cam0.connect("message", handle_bus_message, mloop)

    # Start playing
    print("Setting to PLAYING...")
    pipelinecam_id0.set_state(Gst.State.PLAYING)
    mloop.run()

    GLib.source_remove(interrupt_watch_id_cam0)
    bus_cam0.remove_signal_watch()
    bus_cam0 = None

    print("Setting to NULL...")
    pipelinecam_id0.set_state(Gst.State.NULL)

    mloop = None
    pipelinecam_id0 = None
    Gst.deinit()


if __name__ == "__main__":
    sys.exit(main())
