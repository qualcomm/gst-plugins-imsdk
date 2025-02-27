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
This app sets up GStreamer pipeline for concurrent video playback
composition. Parse mutiple input video files and stream concurrent
videoplay and composite to display.
"""
GST_V4L2_IO_DMABUF = "4"
GST_V4L2_IO_DMABUF_IMPORT = "5"

GST_PIPELINE_CONCURRENT_STREAM = "qtivcomposer name=mixer sink_0::position=\"<0, 0>\" sink_0::dimensions=\"<480, 270>\" sink_1::position=\"<480, 0>\" sink_1::dimensions=\"<480, 270>\" sink_2::position=\"<480, 270>\" sink_2::dimensions=\"<480, 270>\" sink_3::position=\"<0, 270>\" sink_3::dimensions=\"<480, 270>\" sink_4::position=\"<480, 540>\" sink_4::dimensions=\"<480, 270>\" sink_5::position=\"<0, 540>\" sink_5::dimensions=\"<480, 270>\"  sink_6::position=\"<480, 810>\" sink_6::dimensions=\"<480, 270>\" sink_7::position=\"<0, 810>\" sink_7::dimensions=\"<480, 270>\"  sink_8::position=\"<960, 0>\" sink_8::dimensions=\"<480, 270>\" sink_9::position=\"<1440, 0>\" sink_9::dimensions=\"<480, 270>\"  sink_10::position=\"<960, 270>\" sink_10::dimensions=\"<480, 270>\" sink_11::position=\"<1440, 270>\" sink_11::dimensions=\"<480, 270>\" sink_12::position=\"<960, 540>\" sink_12::dimensions=\"<480, 270>\" sink_13::position=\"<1440, 540>\" sink_13::dimensions=\"<480, 270>\" sink_14::position=\"<960, 810>\" sink_14::dimensions=\"<480, 270>\" sink_15::position=\"<1440, 810>\" sink_15::dimensions=\"<480, 270>\" mixer. ! queue ! waylandsink enable-last-sample=false fullscreen=true"


def concurrent_stream(iterations, filename):
    """Concatenate source files"""
    srcfile = ""
    for i in range(iterations):
        file = " filesrc name=source" + str(i) + " location=" + filename + \
            " ! qtdemux ! queue ! h264parse ! v4l2h264dec capture-io-mode=" + GST_V4L2_IO_DMABUF +" output-io-mode=" + GST_V4L2_IO_DMABUF + " ! video/x-raw,format=NV12 ! mixer. "
        srcfile = srcfile + file
    return GST_PIPELINE_CONCURRENT_STREAM + srcfile


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
    Gst.init(sys.argv)
    mloop = GLib.MainLoop()

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
        "--infile", type=str, required=True,
        help="Input file to stream"
    )
    parser.add_argument(
        "-c", "--stream_count", type=int, default=2,
        help="Number of streams"
    )
    args = parser.parse_args()

    pipeline = Gst.parse_launch(
        concurrent_stream(args.stream_count, args.infile))

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
