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
The application records, downscales and rotates and encodes a single camera
stream and dump the output.
"""

DEFAULT_OUTPUT_FILE = "/opt/data/test.mp4"
GST_V4L2_IO_DMABUF = 4
GST_V4L2_IO_DMABUF_IMPORT = 5

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
    parser.add_argument(
        "--output_path",
        type=str,
        default=DEFAULT_OUTPUT_FILE,
        help="Pipeline Output Path",
    )

    args = parser.parse_args()

    # Create all elements
    # fmt: off
    elements = {
        "qmmfsrc":      create_element("qtiqmmfsrc", "camsrc"),
        "capsfilter_0": create_element("capsfilter", "camoutcaps"),
        "vtransform":   create_element("qtivtransform", "transfrom"),
        "capsfilter_1": create_element("capsfilter", "transformcaps"),
        "v4l2h264enc":  create_element("v4l2h264enc", "v4l2h264encoder"),
        "h264parse":    create_element("h264parse", "h264parser"),
        "mp4mux":       create_element("mp4mux", "mp4muxer"),
        "filesink":     create_element("filesink", "filesink")
    }
    # fmt: on

    # Set element properties
    Gst.util_set_object_arg(elements["qmmfsrc"], "camera", "0")

    Gst.util_set_object_arg(
        elements["capsfilter_0"],
        "caps",
        "video/x-raw,format=NV12,\
        width=1920,height=1080,framerate=30/1",
    )

    Gst.util_set_object_arg(elements["vtransform"], "rotate", "90CW")

    Gst.util_set_object_arg(
        elements["capsfilter_1"],
        "caps",
        "video/x-raw,width=480,height=640,colorimetry=bt709",
    )

    Gst.util_set_object_arg(elements["v4l2h264enc"], "capture-io-mode", "GST_V4L2_IO_DMABUF")
    Gst.util_set_object_arg(elements["v4l2h264enc"], "output-io-mode", "GST_V4L2_IO_DMABUF_IMPORT")

    Gst.util_set_object_arg(elements["h264parse"], "config-interval", "1")

    Gst.util_set_object_arg(elements["filesink"], "location", args.output_path)

    # Add all elements
    for element in elements.values():
        pipe.add(element)

    # Link all elements
    # fmt: off
    link_orders = [
        [
            "qmmfsrc", "capsfilter_0", "vtransform", "capsfilter_1",
            "v4l2h264enc", "h264parse", "mp4mux", "filesink"
        ]
    ]
    # fmt: on
    link_elements(link_orders, elements)


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
