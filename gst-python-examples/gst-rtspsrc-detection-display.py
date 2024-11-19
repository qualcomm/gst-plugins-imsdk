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
The application receives an RTSP stream as source, decodes it, uses YOLOv8
TFLite model to identify the object in scene from camera stream and overlay
the bounding boxes over the detected objects. The results are shown on the
display.

The file paths are hard coded in the python script as follows:
- Detection model (YOLOv8): /opt/data/YoloV8N_Detection_Quantized.tflite
- Detection labels: /opt/data/yolov8n.labels
"""

DEFAULT_RTSP_SRC = "rtsp://127.0.0.1:8900/live"
DEFAULT_DETECTION_MODEL = "/opt/data/YoloV8N_Detection_Quantized.tflite"
DEFAULT_DETECTION_LABELS = "/opt/data/yolov8n.labels"


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

    args = parser.parse_args()

    # Create all elements
    # fmt: off
    elements = {
        "rtspsrc":      create_element("rtspsrc", "rtspsrc"),
        "rtph264depay": create_element("rtph264depay", "rtph264depay"),
        "capsfilter_0": create_element("capsfilter", "rtph264depaycaps"),
        "h264parse":    create_element("h264parse", "h264parser"),
        "v4l2h264dec":  create_element("v4l2h264dec", "v4l2h264decoder"),
        "tee":          create_element("tee", "split"),
        "mlvconverter": create_element("qtimlvconverter", "converter"),
        "queue_0":      create_element("queue", "queue0"),
        "mltflite":     create_element("qtimltflite", "inference"),
        "queue_1":      create_element("queue", "queue1"),
        "mlvdetection": create_element("qtimlvdetection", "detection"),
        "capsfilter_1": create_element("capsfilter", "metamuxmetacaps"),
        "queue_2":      create_element("queue", "queue2"),
        "metamux":      create_element("qtimetamux", "metamux"),
        "overlay":      create_element("qtivoverlay", "overlay"),
        "queue_4":      create_element("queue", "queue4"),
        "display":      create_element("waylandsink", "display")
    }
    # fmt: on

    # Set element properties
    Gst.util_set_object_arg(elements["rtspsrc"], "location", DEFAULT_RTSP_SRC)

    Gst.util_set_object_arg(
        elements["capsfilter_0"],
        "caps",
        "video/x-h264,colorimetry=bt709",
    )

    Gst.util_set_object_arg(elements["h264parse"], "config-interval", "1")

    Gst.util_set_object_arg(elements["v4l2h264dec"], "capture-io-mode", "5")
    Gst.util_set_object_arg(elements["v4l2h264dec"], "output-io-mode", "5")

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
    Gst.util_set_object_arg(
        elements["mltflite"],
        "model",
        DEFAULT_DETECTION_MODEL,
    )

    Gst.util_set_object_arg(elements["mlvdetection"], "threshold", "75.0")
    Gst.util_set_object_arg(elements["mlvdetection"], "results", "4")
    Gst.util_set_object_arg(elements["mlvdetection"], "module", "yolov8")
    Gst.util_set_object_arg(
        elements["mlvdetection"],
        "constants",
        "YoloV8,q-offsets=<-107.0,-128.0,0.0>,\
        q-scales=<3.093529462814331,0.00390625,1.0>;",
    )
    Gst.util_set_object_arg(
        elements["mlvdetection"], "labels", DEFAULT_DETECTION_LABELS
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
            "rtph264depay", "capsfilter_0", "h264parse", "v4l2h264dec",
            "tee", "metamux", "overlay", "queue_4", "display"
        ],
        [
            "tee", "mlvconverter", "queue_0", "mltflite", "queue_1",
            "mlvdetection", "capsfilter_1", "queue_2", "metamux"
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
    message_type = message.type
    if message_type == Gst.MessageType.EOS:
        print("EoS received!")
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

    return 0


if __name__ == "__main__":
    sys.exit(main())
