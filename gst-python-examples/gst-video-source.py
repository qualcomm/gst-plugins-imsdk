#!/usr/bin/env python3

################################################################################
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

from gst_utils import gst_run_pipeline, nv12_buffer_to_jpeg
import argparse

# ------------------------------------------------------------------------------
# Constants and Configuration
# ------------------------------------------------------------------------------

DESCRIPTION = """
This application sets up a GStreamer pipeline for capturing video frames.
"""

VIDEO_SOURCE = (
    "filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! "
    "v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12"
)

# ------------------------------------------------------------------------------
# Argument Parsing
# ------------------------------------------------------------------------------

parser = argparse.ArgumentParser(description=DESCRIPTION)
parser.add_argument('-s', '--source', type=str, default=VIDEO_SOURCE, help='GStreamer source pipeline string')
args = parser.parse_args()

# ------------------------------------------------------------------------------
# GStreamer Pipeline Definition
# ------------------------------------------------------------------------------

PIPELINE = (
    # Video source input
    f'{args.source} ! queue ! '

    # Output appsink
    'appsink name=appsink sync=false emit-signals=true'
)

# ------------------------------------------------------------------------------
# Receive the buffer from Appsink.
# Encode the buffer to JPEG and save to a file.
# ------------------------------------------------------------------------------

def on_frame(name, buffer):
    try:
        # For convenience, the Python script overwrites the same file (frame.jpeg)
        # with each inference run. This approach streamlines access to the latest
        # results and eliminates the need to manage multiple output files during testing.
        nv12_buffer_to_jpeg(buffer, "/etc/media/frame.jpeg")
        print(f"JPEG saved.")
    except Exception as e:
        print(f"JPEG error: {e}")

# ------------------------------------------------------------------------------
# Run the Pipeline
# ------------------------------------------------------------------------------

gst_run_pipeline(PIPELINE, on_frame)
