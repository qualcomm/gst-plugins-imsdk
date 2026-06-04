#!/usr/bin/env python3

################################################################################
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

from gst_utils import gst_run_pipeline, buffer_to_file
import argparse

# ------------------------------------------------------------------------------
# Constants and Configuration
# ------------------------------------------------------------------------------

DESCRIPTION = """
This application sets up a GStreamer pipeline for video preprocessing and inference.
It captures video input, converts frames into tensors using qtimlvconverter, runs
inference and outputs the data to a file via appsink for further processing.
It supports various video input sources.
"""

# H.264 offline video input (MP4 format). Resolution is determined by the video. Decoder does not support rescaling.
VIDEO_SOURCE = (
    "filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! "
    "v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12"
)

# Alternative sources (use with -s argument):

# H.265 offline video input (MP4 format). Resolution is determined by the video. Decoder does not support rescaling.
# -s "filesrc location=/etc/media/video.mp4 ! qtdemux ! h265parse ! v4l2h265dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12"

# USB camera source. Update "device" property to match with your USB camera device node. You can update width and height if you camera does not support 1080p
# -s "v4l2src device=/dev/video2 ! video/x-raw,width=1920,height=1080"

# Build-in CSI camera. If there is more than one camera attached you can select camera by "camera" property.
# -s "qtiqmmfsrc camera=0 ! video/x-raw,width=1920,height=1080"

# RTSP (Network) camera. You have to provide RTSP camera URL. Resolution is determined by RTSP camera and it cannot be controlled by GStremer pipeline.
# -s "rtspsrc location=rtsp://<user>:<pass>@<ip>:554/Streaming/Channels/101 ! rtph264depay ! h264parse ! v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12"


# ------------------------------------------------------------------------------
# Argument Parsing
# ------------------------------------------------------------------------------

parser = argparse.ArgumentParser(description=DESCRIPTION)
parser.add_argument('-s', '--source', type=str, default=VIDEO_SOURCE, help='GStreamer source pipeline string')
parser.add_argument('--model-base-path', type=str, default="/etc/", help='Base directory containing models/ and labels/')
args = parser.parse_args()
MODEL_BASE_PATH = args.model_base_path.rstrip('/')
MODEL_DIR = f'{MODEL_BASE_PATH}/models' if MODEL_BASE_PATH else '/models'
LABEL_DIR = f'{MODEL_BASE_PATH}/labels' if MODEL_BASE_PATH else '/labels'

# ------------------------------------------------------------------------------
# GStreamer Pipeline Definition
# ------------------------------------------------------------------------------

PIPELINE = (
    # Video source input
    f'{args.source} ! queue ! '

    # Preprocess the video for inference
    'qtimlvconverter name=preprocess ! queue ! '

    # Run inference using the resnext101 model
    'qtimltflite name=inference delegate=external '
    'external-delegate-path=libQnnTFLiteDelegate.so '
    'external-delegate-options="QNNExternalDelegate,backend_type=htp;" '
    f'model={MODEL_DIR}/resnext101-w8a8.tflite ! queue ! '

    # Output appsink
    'appsink name=appsink sync=false emit-signals=true'
)

# ------------------------------------------------------------------------------
# Receive the buffer in case of Appsink output.
# Save the buffer to a file.
# ------------------------------------------------------------------------------

def on_frame(name, buffer):
    try:
        # For convenience, the Python script overwrites the same file (tensor.bin)
        # with each inference run. This approach streamlines access to the latest
        # results and eliminates the need to manage multiple output files during testing.
        buffer_to_file(buffer, "tensor.bin")
        print(f"Tensor saved.")
    except Exception as e:
        print(f"Tensor error: {e}")

# ------------------------------------------------------------------------------
# Run the Pipeline
# ------------------------------------------------------------------------------

gst_run_pipeline(PIPELINE, on_frame)
