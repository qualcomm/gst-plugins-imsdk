#!/usr/bin/env python3
################################################################################
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################
import os
import sys
import glob
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
DEFAULT_OUTPUT_FILE = "/etc/media/recording.mp4"

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
    src = None
    for element in link_order:
        dest = elements[element]
        if src and not src.link(dest):
            raise Exception(
                f"Unable to link element {src.get_name()} to "
                f"{dest.get_name()}"
            )
        src = dest


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
        '-c', '--camera', type=int, choices=[0, 1], default=0,
        help='Select (0) for Primary Camera and (1) for Secondary Camera.'
    )
    parser.add_argument(
        '-cw', '--width', type=int, default=1280,
        help='Camera Output Width'
    )
    parser.add_argument(
        '-ch', '--height', type=int, default=720,
        help='Camera Output Height'
    )
    parser.add_argument(
        '-cf', '--framerate', type=str, default='30/1',
        help='Camera Output Framerate (fraction)'
    )
    parser.add_argument(
        "--output", type=str, default=DEFAULT_OUTPUT_FILE,
        help="Output File Path"
    )
    return parser.parse_args()


def is_camx_present():
    v4l_root = "/sys/class/video4linux"
    if not os.path.exists(v4l_root):
        return False
    video_nodes = glob.glob("/dev/video*")
    for video_path in video_nodes:
        video_name = os.path.basename(video_path)
        driver_module_link = os.path.join(v4l_root, video_name, "device", "driver", "module")
        try:
            resolved_path = os.path.realpath(driver_module_link)
            if "camera" in resolved_path:
                return True
        except OSError:
            continue
    return False

def create_camera_source_bin(bin_name=None):
    """
    - CAMX present  → returns a bare  qtiqmmfsrc  element.
    - CAMX absent   → returns a GstBin wrapping  libcamerasrc ! qtivtransform
                       with a ghost 'src' pad, so the caller always sees a
                       uniform single-src element / bin.
    """
    camx_present = is_camx_present()

    if camx_present:
        src = Gst.ElementFactory.make("qtiqmmfsrc", "camera_src")
        if not src:
            print("ERROR: Failed to create qtiqmmfsrc")
            return None
        print("CAMX detected — using qtiqmmfsrc")
        return src

    src_bin = Gst.Bin.new(bin_name if bin_name else "camera_source_bin")
    if not src_bin:
        print("ERROR: Failed to create camera source bin")
        return None

    src           = Gst.ElementFactory.make("libcamerasrc",  "camera_src")
    qtivtransform = Gst.ElementFactory.make("qtivtransform", "camera_transform")

    if not src or not qtivtransform:
        print("ERROR: Failed to create libcamerasrc fallback source chain")
        src_bin = None
        return None

    src_bin.add(src)
    src_bin.add(qtivtransform)

    if not src.link(qtivtransform):
        print("ERROR: Failed to link libcamerasrc -> qtivtransform")
        src_bin = None
        return None

    target_src_pad = qtivtransform.get_static_pad("src")
    if not target_src_pad:
        print("ERROR: Failed to get qtivtransform src pad")
        src_bin = None
        return None

    ghost_src_pad = Gst.GhostPad.new("src", target_src_pad)
    target_src_pad = None

    if not ghost_src_pad:
        print("ERROR: Failed to create ghost src pad for camera source bin")
        src_bin = None
        return None

    if not src_bin.add_pad(ghost_src_pad):
        print("ERROR: Failed to add ghost src pad to camera source bin")
        ghost_src_pad = None
        src_bin = None
        return None

    print("CAMX not detected — using libcamerasrc -> qtivtransform bin")
    return src_bin


# ---------------------------------------------------------------------------
# Pipeline construction
# ---------------------------------------------------------------------------

def create_pipeline(pipeline, args):
    """Initialize and link elements for the GStreamer pipeline."""

    # --- Camera source bin (uniform regardless of backend) ---
    cam_src_bin = create_camera_source_bin("camera_source_bin")
    if not cam_src_bin:
        raise Exception("Unable to create camera source bin")

    # Only qtiqmmfsrc supports the 'camera' property
    if cam_src_bin.get_factory() and             cam_src_bin.get_factory().get_name() == "qtiqmmfsrc":
        cam_src_bin.set_property("camera", args.camera)

    # --- Remaining elements ---
    elements = {
        "camsrc" : cam_src_bin,
        "camcaps": create_element("capsfilter", "camcaps"),
        "encoder": create_element("v4l2h264enc",  "encoder"),
        "parser" : create_element("h264parse",    "parser"),
        "mux"    : create_element("mp4mux",       "mux"),
        "sink"   : create_element("filesink",     "sink"),
    }

    queue_count = 2
    for i in range(queue_count):
        queue_name = f"queue{i}"
        elements[queue_name] = create_element("queue", queue_name)

    # Set properties
    elements["camcaps"].set_property(
        "caps", Gst.Caps.from_string(
            "video/x-raw,format=NV12,"
            f"width={args.width},height={args.height},"
            f"framerate={args.framerate}"
        )
    )
    elements["encoder"].set_property("capture-io-mode",  "dmabuf")
    elements["encoder"].set_property("output-io-mode",   "dmabuf-import")
    elements["sink"].set_property("location", args.output)

    # Add elements to the pipeline
    for element in elements.values():
        pipeline.add(element)

    # Link elements in order
    link_order = [
        "camsrc", "camcaps", "encoder", "parser", "queue0",
        "mux", "queue1", "sink"
    ]
    link_elements(link_order, elements)


def main():
    """Main function to set up and run the GStreamer pipeline."""
    Gst.init(None)
    mloop = GLib.MainLoop()

    args = parse_arguments()

    try:
        pipeline = Gst.Pipeline.new("video-recording-pipeline")
        if not pipeline:
            raise Exception("Unable to create video recording pipeline")
        create_pipeline(pipeline, args)
    except Exception as e:
        print(f"{e} Exiting...")
        return -1

    interrupt_watch_id = GLib.unix_signal_add(
        GLib.PRIORITY_HIGH, signal.SIGINT,
        handle_interrupt_signal, pipeline, mloop
    )

    bus = pipeline.get_bus()
    bus.add_signal_watch()
    bus.connect("message", handle_bus_message, mloop)

    print("Setting to PLAYING...")
    pipeline.set_state(Gst.State.PLAYING)
    mloop.run()

    GLib.source_remove(interrupt_watch_id)
    bus.remove_signal_watch()
    bus      = None
    print("Setting to NULL...")
    pipeline.set_state(Gst.State.NULL)
    mloop    = None
    pipeline = None
    Gst.deinit()

    if eos_received:
        print("App execution successful")


if __name__ == "__main__":
    sys.exit(main())

