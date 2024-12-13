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
The application uses:
- YOLOv8 TFLite model to identify the object in scene from video file and
overlay the bounding boxes over the detected objects
- YOLOv8 TFLite model to identify the object in scene from video file and
overlay the bounding boxes over the detected objects
- Resnet101 TFLite model to classify scene from video file and overlay the
classification labels on the top left corner
- FFNet40S TFLite model to produce semantic segmentations for video file
Then the results are shown side by side on the display.

The file paths are hard coded in the python script as follows:
- Detection model (YOLOv8): /opt/data/YoloV8N_Detection_Quantized.tflite
- Detection labels: /opt/data/yolov8n.labels
- Classification model: /opt/data/Resnet101_Quantized.tflite
- Classification labels: /opt/data/resnet101.labels
- Segmentation model: /opt/data/ffnet_40s_quantized.tflite
- Segmentation labels: /opt/data/dv3-argmax.labels
- Input video for detection: /opt/data/Draw_720p_180s_30FPS.mp4
- Input video for classification: /opt/data/Animals_000_720p_180s_30FPS.mp4
- Input video for segmentation: /opt/data/Street_Bridge_720p_180s_30FPS.MOV
"""

DEFAULT_INPUT_DETECTION_0 = "/opt/data/Draw_720p_180s_30FPS.mp4"
DEFAULT_INPUT_DETECTION_1 = "/opt/data/Draw_720p_180s_30FPS.mp4"
DEFAULT_INPUT_CLASSIFICATION = "/opt/data/Animals_000_720p_180s_30FPS.mp4"
DEFAULT_INPUT_SEGMENTATION = "/opt/data/Street_Bridge_720p_180s_30FPS.MOV"
DEFAULT_DETECTION_MODEL_0 = "/opt/data/YoloV8N_Detection_Quantized.tflite"
DEFAULT_DETECTION_LABELS_0 = "/opt/data/yolov8n.labels"
DEFAULT_DETECTION_MODEL_1 = "/opt/data/YoloV8N_Detection_Quantized.tflite"
DEFAULT_DETECTION_LABELS_1 = "/opt/data/yolov8n.labels"
DEFAULT_CLASSIFICATION_MODEL = "/opt/data/Resnet101_Quantized.tflite"
DEFAULT_CLASSIFICATION_LABELS = "/opt/data/resnet101.labels"
DEFAULT_SEGMENTATION_MODEL = "/opt/data/ffnet_40s_quantized.tflite"
DEFAULT_SEGMENTATION_LABELS = "/opt/data/dv3-argmax.labels"

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

    args = parser.parse_args()

    # Create all elements
    # fmt: off
    elements = {
        # Stream 0
        "filesrc_0":         create_element("filesrc", "filesrc0"),
        "qtdemux_0":         create_element("qtdemux", "qtdemux0"),
        "h264parse_0":       create_element("h264parse", "h264parser0"),
        "v4l2h264dec_0":     create_element("v4l2h264dec", "v4l2h264decoder0"),
        "tee_0":             create_element("tee", "split0"),
        "mlvconverter_0":    create_element("qtimlvconverter", "converter0"),
        "queue_0":           create_element("queue", "queue0"),
        "mltflite_0":        create_element("qtimltflite", "inference0"),
        "queue_1":           create_element("queue", "queue1"),
        "mlvdetection_0":    create_element("qtimlvdetection", "detection0"),
        "capsfilter_0":      create_element("capsfilter", "metamux0metacaps"),
        "queue_2":           create_element("queue", "queue2"),
        "metamux_0":         create_element("qtimetamux", "metamux0"),
        "overlay_0":         create_element("qtivoverlay", "overlay0"),
        # Stream 1
        "filesrc_1":         create_element("filesrc", "filesrc1"),
        "qtdemux_1":         create_element("qtdemux", "qtdemux1"),
        "h264parse_1":       create_element("h264parse", "h264parser1"),
        "v4l2h264dec_1":     create_element("v4l2h264dec", "v4l2h264decoder1"),
        "tee_1":             create_element("tee", "split1"),
        "mlvconverter_1":    create_element("qtimlvconverter", "converter1"),
        "queue_3":           create_element("queue", "queue3"),
        "mltflite_1":        create_element("qtimltflite", "inference1"),
        "queue_4":           create_element("queue", "queue4"),
        "mlvdetection_1":    create_element("qtimlvdetection", "detection1"),
        "capsfilter_1":      create_element("capsfilter", "metamux1metacaps"),
        "queue_5":           create_element("queue", "queue5"),
        "metamux_1":         create_element("qtimetamux", "metamux1"),
        "overlay_1":         create_element("qtivoverlay", "overlay1"),
        # Stream 2
        "filesrc_2":         create_element("filesrc", "filesrc2"),
        "qtdemux_2":         create_element("qtdemux", "qtdemux2"),
        "h264parse_2":       create_element("h264parse", "h264parser2"),
        "v4l2h264dec_2":     create_element("v4l2h264dec", "v4l2h264decoder2"),
        "tee_2":             create_element("tee", "split2"),
        "mlvconverter_2":    create_element("qtimlvconverter", "converter2"),
        "queue_6":           create_element("queue", "queue6"),
        "mltflite_2":        create_element("qtimltflite", "inference2"),
        "queue_7":           create_element("queue", "queue7"),
        "mlvclassification": create_element("qtimlvclassification", "classification"),
        "capsfilter_2":      create_element("capsfilter", "metamux2metacaps"),
        "queue_8":           create_element("queue", "queue8"),
        "metamux_2":         create_element("qtimetamux", "metamux2"),
        "overlay_2":         create_element("qtivoverlay", "overlay2"),
        # Stream 3
        "filesrc_3":         create_element("filesrc", "filesrc3"),
        "qtdemux_3":         create_element("qtdemux", "qtdemux3"),
        "h264parse_3":       create_element("h264parse", "h264parser3"),
        "v4l2h264dec_3":     create_element("v4l2h264dec", "v4l2h264decoder3"),
        "tee_3":             create_element("tee", "split3"),
        "queue_9":           create_element("queue", "queue9"),
        "mlvconverter_3":    create_element("qtimlvconverter", "converter3"),
        "queue_10":          create_element("queue", "queue10"),
        "mltflite_3":        create_element("qtimltflite", "inference3"),
        "queue_11":          create_element("queue", "queue11"),
        "mlvsegmentation":   create_element("qtimlvsegmentation", "segmentation"),
        "capsfilter_3":      create_element("capsfilter", "metamux3metacaps"),
        "queue_12":          create_element("queue", "queue12"),
        # Side by side all streams
        "composer":          create_element("qtivcomposer", "composer"),
        "queue_13":          create_element("queue", "queue13"),
        "display":           create_element("waylandsink", "display")
    }
    # fmt: on

    # Set element properties
    # Stream 0
    Gst.util_set_object_arg(
        elements["filesrc_0"], "location", DEFAULT_INPUT_DETECTION_0
    )

    Gst.util_set_object_arg(elements["h264parse_0"], "config-interval", "1")

    Gst.util_set_object_arg(elements["v4l2h264dec_0"], "capture-io-mode", "5")
    Gst.util_set_object_arg(elements["v4l2h264dec_0"], "output-io-mode", "5")

    Gst.util_set_object_arg(elements["mltflite_0"], "delegate", "external")
    Gst.util_set_object_arg(
        elements["mltflite_0"],
        "external-delegate-path",
        "libQnnTFLiteDelegate.so",
    )
    Gst.util_set_object_arg(
        elements["mltflite_0"],
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp,htp_precision=(string)1;",
    )
    Gst.util_set_object_arg(
        elements["mltflite_0"],
        "model",
        DEFAULT_DETECTION_MODEL_0,
    )

    Gst.util_set_object_arg(elements["mlvdetection_0"], "threshold", "75.0")
    Gst.util_set_object_arg(elements["mlvdetection_0"], "results", "4")
    Gst.util_set_object_arg(elements["mlvdetection_0"], "module", "yolov8")
    Gst.util_set_object_arg(
        elements["mlvdetection_0"], "labels", DEFAULT_DETECTION_LABELS_0
    )
    Gst.util_set_object_arg(
        elements["mlvdetection_0"],
        "constants",
        "YoloV8,q-offsets=<-107.0,-128.0,0.0>,\
        q-scales=<3.093529462814331,0.00390625,1.0>;",
    )

    Gst.util_set_object_arg(elements["capsfilter_0"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_0"], "engine", "gles")

    # Stream 1
    Gst.util_set_object_arg(
        elements["filesrc_1"], "location", DEFAULT_INPUT_DETECTION_1
    )

    Gst.util_set_object_arg(elements["h264parse_1"], "config-interval", "1")

    Gst.util_set_object_arg(elements["v4l2h264dec_1"], "capture-io-mode", "5")
    Gst.util_set_object_arg(elements["v4l2h264dec_1"], "output-io-mode", "5")

    Gst.util_set_object_arg(elements["mltflite_1"], "delegate", "external")
    Gst.util_set_object_arg(
        elements["mltflite_1"],
        "external-delegate-path",
        "libQnnTFLiteDelegate.so",
    )
    Gst.util_set_object_arg(
        elements["mltflite_1"],
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp,htp_precision=(string)1;",
    )
    Gst.util_set_object_arg(
        elements["mltflite_1"],
        "model",
        DEFAULT_DETECTION_MODEL_1,
    )

    Gst.util_set_object_arg(elements["mlvdetection_1"], "threshold", "75.0")
    Gst.util_set_object_arg(elements["mlvdetection_1"], "results", "4")
    Gst.util_set_object_arg(elements["mlvdetection_1"], "module", "yolov8")
    Gst.util_set_object_arg(
        elements["mlvdetection_1"], "labels", DEFAULT_DETECTION_LABELS_1
    )
    Gst.util_set_object_arg(
        elements["mlvdetection_1"],
        "constants",
        "YoloV8,q-offsets=<-107.0,-128.0,0.0>,\
        q-scales=<3.093529462814331,0.00390625,1.0>;",
    )

    Gst.util_set_object_arg(elements["capsfilter_1"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_1"], "engine", "gles")

    # Stream 2
    Gst.util_set_object_arg(
        elements["filesrc_2"],
        "location",
        DEFAULT_INPUT_CLASSIFICATION,
    )

    Gst.util_set_object_arg(elements["h264parse_2"], "config-interval", "2")

    Gst.util_set_object_arg(elements["v4l2h264dec_2"], "capture-io-mode", "5")
    Gst.util_set_object_arg(elements["v4l2h264dec_2"], "output-io-mode", "5")

    Gst.util_set_object_arg(elements["mltflite_2"], "delegate", "external")
    Gst.util_set_object_arg(
        elements["mltflite_2"],
        "external-delegate-path",
        "libQnnTFLiteDelegate.so",
    )
    Gst.util_set_object_arg(
        elements["mltflite_2"],
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        elements["mltflite_2"], "model", DEFAULT_CLASSIFICATION_MODEL
    )

    Gst.util_set_object_arg(elements["mlvclassification"], "threshold", "51.0")
    Gst.util_set_object_arg(elements["mlvclassification"], "results", "5")
    Gst.util_set_object_arg(
        elements["mlvclassification"], "module", "mobilenet"
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"], "labels", DEFAULT_CLASSIFICATION_LABELS
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"], "extra-operation", "softmax"
    )
    Gst.util_set_object_arg(
        elements["mlvclassification"],
        "constants",
        "Mobilenet,q-offsets=<-82.0>,q-scales=<0.21351955831050873>;",
    )

    Gst.util_set_object_arg(elements["capsfilter_2"], "caps", "text/x-raw")

    Gst.util_set_object_arg(elements["overlay_2"], "engine", "gles")

    # Stream 3
    Gst.util_set_object_arg(
        elements["filesrc_3"],
        "location",
        DEFAULT_INPUT_SEGMENTATION,
    )

    Gst.util_set_object_arg(elements["h264parse_3"], "config-interval", "2")

    Gst.util_set_object_arg(elements["v4l2h264dec_3"], "capture-io-mode", "5")
    Gst.util_set_object_arg(elements["v4l2h264dec_3"], "output-io-mode", "5")

    Gst.util_set_object_arg(elements["mltflite_3"], "delegate", "external")
    Gst.util_set_object_arg(
        elements["mltflite_3"],
        "external-delegate-path",
        "libQnnTFLiteDelegate.so",
    )
    Gst.util_set_object_arg(
        elements["mltflite_3"],
        "external-delegate-options",
        "QNNExternalDelegate,backend_type=htp;",
    )
    Gst.util_set_object_arg(
        elements["mltflite_3"], "model", DEFAULT_SEGMENTATION_MODEL
    )

    Gst.util_set_object_arg(
        elements["mlvsegmentation"], "module", "deeplab-argmax"
    )
    Gst.util_set_object_arg(
        elements["mlvsegmentation"], "labels", DEFAULT_SEGMENTATION_LABELS
    )
    Gst.util_set_object_arg(
        elements["mlvsegmentation"],
        "constants",
        "FFNet-40S,q-offsets=<50.0>,q-scales=<0.31378185749053955>;",
    )

    # Side by side all streams
    Gst.util_set_object_arg(elements["composer"], "background", "0")

    Gst.util_set_object_arg(elements["display"], "sync", "false")
    Gst.util_set_object_arg(elements["display"], "fullscreen", "true")

    # Add all elements
    for element in elements.values():
        pipe.add(element)

    # Link most of the elements
    # fmt: off
    link_orders = [
        [ "filesrc_0", "qtdemux_0" ],
        [
            "h264parse_0", "v4l2h264dec_0", "tee_0", "metamux_0", "overlay_0",
            "composer"
        ],
        [
            "tee_0", "mlvconverter_0", "queue_0", "mltflite_0", "queue_1",
            "mlvdetection_0", "queue_2", "capsfilter_0", "metamux_0"
        ],
        [ "filesrc_1", "qtdemux_1" ],
        [
            "h264parse_1", "v4l2h264dec_1", "tee_1", "metamux_1", "overlay_1",
            "composer"
        ],
        [
            "tee_1", "mlvconverter_1", "queue_3", "mltflite_1", "queue_4",
            "mlvdetection_1", "queue_5", "capsfilter_1", "metamux_1"
        ],
        [ "filesrc_2", "qtdemux_2" ],
        [
            "h264parse_2", "v4l2h264dec_2", "tee_2", "metamux_2", "overlay_2",
            "composer"
        ],
        [
            "tee_2", "mlvconverter_2", "queue_6", "mltflite_2", "queue_7",
            "mlvclassification", "queue_8", "capsfilter_2", "metamux_2"
        ],
        [ "filesrc_3", "qtdemux_3" ],
        [ "h264parse_3", "v4l2h264dec_3", "tee_3", "queue_9", "composer" ],
        [
            "tee_3", "mlvconverter_3", "queue_10", "mltflite_3", "queue_11",
            "mlvsegmentation", "queue_12", "composer"
        ],
        [ "composer", "queue_13", "display" ]
    ]
    # fmt: on
    link_elements(link_orders, elements)

    # Link qtdemux elements
    def on_pad_added(elem, pad, dest):
        if "video" in pad.get_name():
            sink_pad = dest.get_static_pad("sink")
            if (
                not sink_pad.is_linked()
                and pad.link(sink_pad) != Gst.PadLinkReturn.OK
            ):
                raise (
                    f"Failed to link {elem.get_name()} with {dest.get_name()}!"
                )

    elements["qtdemux_0"].connect(
        "pad-added", on_pad_added, elements["h264parse_0"]
    )
    elements["qtdemux_1"].connect(
        "pad-added", on_pad_added, elements["h264parse_1"]
    )
    elements["qtdemux_2"].connect(
        "pad-added", on_pad_added, elements["h264parse_2"]
    )
    elements["qtdemux_3"].connect(
        "pad-added", on_pad_added, elements["h264parse_3"]
    )

    # Set pad properties
    composer_sink_0 = elements["composer"].get_static_pad("sink_0")
    composer_sink_1 = elements["composer"].get_static_pad("sink_1")
    composer_sink_2 = elements["composer"].get_static_pad("sink_2")
    composer_sink_3 = elements["composer"].get_static_pad("sink_3")
    composer_sink_4 = elements["composer"].get_static_pad("sink_4")
    Gst.util_set_object_arg(composer_sink_0, "position", "<0, 0>")
    Gst.util_set_object_arg(composer_sink_0, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_1, "position", "<1280, 0>")
    Gst.util_set_object_arg(composer_sink_1, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_2, "position", "<0, 720>")
    Gst.util_set_object_arg(composer_sink_2, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_3, "position", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_3, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_4, "position", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_4, "dimensions", "<1280, 720>")
    Gst.util_set_object_arg(composer_sink_4, "alpha", "0.5")
    composer_sink_0 = None
    composer_sink_1 = None
    composer_sink_2 = None
    composer_sink_3 = None
    composer_sink_4 = None


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
