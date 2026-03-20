#!/usr/bin/env python3

################################################################################
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

import os
import sys
import signal
import argparse
import cv2
import time
from multiprocessing import Process, Event

# Constants
DESCRIPTION = """
This application demonstrate live camera stream using opencv api's which include color conversion and resize.
App capture camera frame as an input using cv videocapture and convert to RGBA using downstream plugin
and resize to user specified resolution. Resize output will display on screen.

To enable Qt applications to run using the Wayland display server with EGL rendering.
Use mentioned below recommended Environment Variable:

export QT_QPA_PLATFORM=wayland-egl

usecase:
gst-camera-opencv-resize.py --inwidth 1280 --inheight 720 --outwidth 640 --outheight 480
"""
DEFAULT_INPUT_WIDTH = 1280
DEFAULT_INPUT_HEIGHT = 720
DEFAULT_RESIZE_WIDTH = 640
DEFAULT_RESIZE_HEIGHT = 480


def cam_loop(stop_event, inwidth, inheight, outwidth, outheight):
    source_path = (
        f"qtiqmmfsrc name=camsrc video_0::type=video ! "
        f"video/x-raw,format=NV12,width={inwidth},height={inheight},framerate=30/1 ! "
        "queue ! "
        "qtivtransform ! "
        "video/x-raw,format=RGBA ! "
        "appsink"
    )

    cap = cv2.VideoCapture(source_path, cv2.CAP_GSTREAMER)
    if not cap.isOpened():
        print("Failed to open camera.", flush=True)
        return

    window_created = False

    try:
        while not stop_event.is_set():
            ret, frame = cap.read()
            if not ret:
                print("Frame grab failed", flush=True)
                break

            resized = cv2.resize(frame, (outwidth, outheight))
            cv2.imshow("demo", resized)
            window_created = True

            if cv2.waitKey(1) & 0xFF == ord('q'):
                stop_event.set()
                break

    finally:
        try:
            cap.release()
        except Exception:
            pass
        time.sleep(0.2)
        if window_created:
            try:
                cv2.destroyAllWindows()
            except Exception:
                pass


def main():
    parser = argparse.ArgumentParser(description=DESCRIPTION)
    parser.add_argument("--inwidth", type=int, default=DEFAULT_INPUT_WIDTH, help="Input image width")
    parser.add_argument("--inheight", type=int, default=DEFAULT_INPUT_HEIGHT, help="Input image height")
    parser.add_argument("--outwidth", type=int, default=DEFAULT_RESIZE_WIDTH, help="Output image width")
    parser.add_argument("--outheight", type=int, default=DEFAULT_RESIZE_HEIGHT, help="Output image height")
    args = parser.parse_args()

    stop_event = Event()
    p = Process(target=cam_loop, args=(stop_event, args.inwidth, args.inheight, args.outwidth, args.outheight))
    p.start()

    try:
        # Wait for child process
        while p.is_alive():
            p.join(timeout=0.5)
    except KeyboardInterrupt:
        stop_event.set()
        print("Ctrl+C pressed, terminating camera process...", flush=True)
        p.terminate()
        p.join()


if __name__ == "__main__":
    main()

