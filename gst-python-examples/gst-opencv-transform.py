#!/usr/bin/env python3

# Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

import os
import sys
import signal
import argparse
import cv2
import time

import gi
gi.require_version('Gst', '1.0')
gi.require_version("GLib", "2.0")
from gi.repository import Gst, GLib

# Constants
DESCRIPTION = """
This app demonstrate video playback using opencv api's.
Parse input video file and capture frame using cv videocapture and
stream videoplay and composite to display using waylandsink.
"""


def read_file():
    print(cv2.getBuildInformation())

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
    args = parser.parse_args()
    print("file name = ", args.infile)

    source_path = "filesrc location=" + args.infile + \
        " ! qtdemux ! queue ! h264parse ! v4l2h264dec capture-io-mode=dmabuf output-io-mode=dmabuf ! video/x-raw, format=NV12 ! qtivtransform ! video/x-raw, width=640, height=480, format=BGR ! appsink"
    cap = cv2.VideoCapture(source_path)

    if cap.isOpened():
        print("Capture API success")

        width = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
        height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
        fps = cap.get(cv2.CAP_PROP_FPS)
        print("Caps properties: ", width, height, fps)

        pipeline = "appsrc ! videoconvert ! video/x-raw, format=NV12 ! waylandsink fullscreen=true"

        out = cv2.VideoWriter(pipeline, cv2.CAP_GSTREAMER, 0, int(
            fps), (int(width), int(height)), True)
        if out.isOpened():
            print("videowriter open success")

        try:
            while True:
                ret_val, frame = cap.read()
                if not ret_val:
                    break
                out.write(frame)
                cv2.waitKey(1)
        except KeyboardInterrupt:
            print("\nInterrupted by user. Exiting gracefully...")
        finally:
            cap.release()
            out.release()
            cv2.destroyAllWindows()

    else:
        print("Capture API failed")



def main():
    """Main function to set up and run the OpenCV with GStreamer plugins."""

    # Set the environment
    os.environ["XDG_RUNTIME_DIR"] = "/dev/socket/weston"
    os.environ["WAYLAND_DISPLAY"] = "wayland-1"

    read_file()
    print("App execution successful")


if __name__ == '__main__':
    sys.exit(main())
