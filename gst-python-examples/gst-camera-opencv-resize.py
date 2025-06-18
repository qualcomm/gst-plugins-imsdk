#!/usr/bin/env python3

# Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

from gi.repository import Gst, GLib
import os
import sys
import signal
import argparse
import cv2
import time

import gi
gi.require_version('Gst', '1.0')
gi.require_version("GLib", "2.0")

# Constants
DESCRIPTION = """
This application demonstrate live camera stream using opencv api's which include color conversion and resize.
App capture camera frame as an input using cv videocapture and convert to RGBA using downstream plugin
and resize to user specified resolution. Resize output will display on screen.
"""

DEFAULT_INPUT_WIDTH = 1280
DEFAULT_INPUT_HEIGHT = 720
DEFAULT_RESIZE_WIDTH = 640
DEFAULT_RESIZE_HEIGHT = 480


def read_cam():
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
        "--inwidth", type=int, default=DEFAULT_INPUT_WIDTH,
        help="Input camera stream width"
    )
    parser.add_argument(
        "--inheight", type=int, default=DEFAULT_INPUT_HEIGHT,
        help="Input camera stream height"
    )
    parser.add_argument(
        "--outwidth", type=int, default=DEFAULT_RESIZE_WIDTH,
        help="Resize camera stream width"
    )
    parser.add_argument(
        "--outheight", type=int, default=DEFAULT_RESIZE_HEIGHT,
        help="Resize camera stream height"
    )
    args = parser.parse_args()
    print("Input and Output resolution: ", args.inwidth,
          args.inheight, args.outwidth, args.outheight)

    source_path = (
        f"qtiqmmfsrc name=camsrc video_0::type=video ! "
        f"video/x-raw,format=NV12,width={args.inwidth},height={args.inheight},framerate=30/1 ! "
        "queue ! "
        "qtivtransform ! "
        "video/x-raw,format=RGBA ! "
        "appsink"
    )

    cap = cv2.VideoCapture(source_path)

    if cap.isOpened():
        print("Capture API success")

        width = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
        height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
        fps = cap.get(cv2.CAP_PROP_FPS)
        print("Caps properties: ", width, height, fps)

        try:
            while True:
                ret_val, frame = cap.read()
                if not ret_val:
                    break
                resize = cv2.resize(frame, (args.outwidth, args.outheight))
                cv2.imshow('demo', resize)
                cv2.waitKey(1)
        except KeyboardInterrupt:
            print("\nInterrupted by user. Exiting gracefully...")
        finally:
            cap.release()
            cv2.destroyAllWindows()
    else:
        print("Capture API failed")



def main():
    """Main function to set up and run the OpenCV with color conversion and resize  plugins."""

    # Set the environment
    os.environ["XDG_RUNTIME_DIR"] = "/dev/socket/weston"
    os.environ["WAYLAND_DISPLAY"] = "wayland-1"

    read_cam()
    print("App execution successful")


if __name__ == '__main__':
    sys.exit(main())
