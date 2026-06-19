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
import glob
import time
from multiprocessing import Process, Event
import gi
gi.require_version('Gst', '1.0')
gi.require_version("GLib", "2.0")
from gi.repository import Gst, GLib

# Constants
DESCRIPTION = """
This application demonstrates live camera stream using OpenCV APIs with color conversion and resize.
"""
DEFAULT_INPUT_WIDTH = 1280
DEFAULT_INPUT_HEIGHT = 720
DEFAULT_RESIZE_WIDTH = 640
DEFAULT_RESIZE_HEIGHT = 480

def is_camx_present():
    return os.access("/run/cam_server/le_cam_socket", os.F_OK)

def get_camera_source_str(camera_id):
    """Returns the appropriate source string for the detected backend."""
    if is_camx_present():
        # CAMX detected — use qtiqmmfsrc
        return f"qtiqmmfsrc camera={camera_id} name=camsrc"
    else:
        # Fallback — use libcamerasrc and qtivtransform
        print("CAMX not detected — falling back to libcamerasrc")
        return "libcamerasrc name=camsrc ! qtivtransform"

def cam_loop(stop_event, inwidth, inheight, outwidth, outheight, camera_id):
    # Initialize GStreamer inside the child process
    Gst.init(None)

    # Construct the pipeline string for OpenCV
    src_cam = get_camera_source_str(camera_id)
    source_path = (
        f"{src_cam} ! "
        f"video/x-raw,format=NV12,width={inwidth},height={inheight},framerate=30/1 ! "
        "queue ! "
        "qtivtransform ! "
        "video/x-raw,format=RGBA ! "
        "appsink"
    )

    print(f"Opening pipeline: {source_path}")
    cap = cv2.VideoCapture(source_path, cv2.CAP_GSTREAMER)
    if not cap.isOpened():
        print("ERROR: Failed to open camera via GStreamer.", flush=True)
        return

    window_created = False
    try:
        while not stop_event.is_set():
            ret, frame = cap.read()
            if not ret:
                print("Frame grab failed", flush=True)
                break

            resized = cv2.resize(frame, (outwidth, outheight))
            cv2.imshow("Camera Preview", resized)
            window_created = True

            if cv2.waitKey(1) & 0xFF == ord('q'):
                stop_event.set()
                break
    finally:
        cap.release()
        if window_created:
            cv2.destroyAllWindows()

def main():
    parser = argparse.ArgumentParser(description=DESCRIPTION)
    parser.add_argument("--camera", type=int, default=0, help="Camera ID (default: 0)")
    parser.add_argument("--inwidth", type=int, default=DEFAULT_INPUT_WIDTH, help="Input width")
    parser.add_argument("--inheight", type=int, default=DEFAULT_INPUT_HEIGHT, help="Input height")
    parser.add_argument("--outwidth", type=int, default=DEFAULT_RESIZE_WIDTH, help="Output width")
    parser.add_argument("--outheight", type=int, default=DEFAULT_RESIZE_HEIGHT, help="Output height")
    args = parser.parse_args()


    stop_event = Event()
    p = Process(target=cam_loop, args=(
        stop_event, args.inwidth, args.inheight, args.outwidth, args.outheight, args.camera
    ))
    p.start()

    try:
        while p.is_alive():
            p.join(timeout=0.5)
    except KeyboardInterrupt:
        stop_event.set()
        print("Ctrl+C pressed, terminating camera process...", flush=True)
        p.terminate()
        p.join()

if __name__ == "__main__":
    main()
