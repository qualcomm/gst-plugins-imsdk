#!/usr/bin/env python3

################################################################################
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

from gst_utils import gst_run_pipeline, buffer_to_file
import argparse
import os

# ------------------------------------------------------------------------------
# Constants and Configuration
# ------------------------------------------------------------------------------

DESCRIPTION = """
This application sets up a GStreamer pipeline for classification using a
quantized ResNet101 model. It does a preprocess, runs inference
and outputs the results in JSON format. It supports various video input sources.
"""

# H.264 offline video input (MP4 format). Resolution is determined by the video. Decoder does not support rescaling.
VIDEO_SOURCE = (
    f"filesrc location={os.environ['HOME']}/media/video.mp4 ! qtdemux ! h264parse ! "
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
parser.add_argument('--model-base-path', type=str, default=os.environ["HOME"], help='Base directory containing models/ and labels/')
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

    # Postprocess inference results and send them to the appsink
    'qtimlpostprocess name=postprocess results=1 module=mobilenet-softmax '
    f'labels={LABEL_DIR}/imagenet.txt settings="{{\\"confidence\\": 51.0}}" ! '
    'text/x-raw ! queue ! '

    # Convert GStreamer ML results to JSON format
    'qtimlmetaparser module=json ! queue ! '

    # Output appsink
    'appsink name=appsink sync=false emit-signals=true'
)

# ------------------------------------------------------------------------------
# Receive the buffer in case of Appsink output.
# Save the buffer to a file.
# ------------------------------------------------------------------------------

def on_frame(name, buffer):
    try:
        # For convenience, the Python script overwrites the same file (result.json)
        # with each inference run. This approach streamlines access to the latest
        # results and eliminates the need to manage multiple output files during testing.
        buffer_to_file(buffer, "result.json")
        print(f"ML result saved.")
    except Exception as e:
        print(f"ML result error: {e}")

# ------------------------------------------------------------------------------
# Run the Pipeline
# ------------------------------------------------------------------------------

gst_run_pipeline(PIPELINE, on_frame)
