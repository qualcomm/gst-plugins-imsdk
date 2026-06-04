#!/usr/bin/env python3

################################################################################
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

from gst_utils import gst_run_pipeline, gst_get_sink, nv12_buffer_to_jpeg
import argparse

# ------------------------------------------------------------------------------
# Constants and Configuration
# ------------------------------------------------------------------------------

DESCRIPTION = """
This application sets up a GStreamer pipeline for multi-model video inference.
It processes a video stream using two separate ML models detection and classification.
The pipeline supports various input sources and output types.
Results from both models are postprocessed and overlayed over the output stream.
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

# Alternative outputs (use with -o argument):
# Output type: can be "display", "appsink", or "file"

# Display output (default). Renders video with overlay directly to screen using Wayland.
# -o display

# Video output. Encodes and saves the video to an MP4 file. Default location is "output.mp4"
# -o video

# Appsink output. Sends frames to Python for further processing (e.g., inference, saving, analysis).
# -o appsink


# ------------------------------------------------------------------------------
# Argument Parsing
# ------------------------------------------------------------------------------

parser = argparse.ArgumentParser(description=DESCRIPTION)
parser.add_argument('-s', '--source', type=str, default=VIDEO_SOURCE, help='GStreamer source pipeline string')
parser.add_argument('-o', '--output', type=str, default="video", help='Output type: display, appsink, video')
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

    # Use a tee element to pass the video frame sequentially,
    # first to two mlvconverters, then to metamux.
    'tee name=t '

    # Preprocess the video for inference
    't. ! qtimlvconverter name=detection-preprocess ! queue ! '

    # Run inference using the yolox model
    'qtimltflite name=detection-inference delegate=external '
    'external-delegate-path=libQnnTFLiteDelegate.so '
    'external-delegate-options="QNNExternalDelegate,backend_type=htp;" '
    f'model={MODEL_DIR}/yolox_quantized.tflite ! queue ! '

    # Postprocess inference results
    'qtimlpostprocess name=detection-postprocess results=8 '
    f'module=yolov8 labels={LABEL_DIR}/coco.txt '
    'settings="{\\"confidence\\": 75.0}" ! text/x-raw ! metamux. '

    # Preprocess the video for inference
    't. ! qtimlvconverter name=classification-preprocess ! queue ! '

    # Run inference using the inception_v3 model
    'qtimltflite name=classification-inference delegate=external '
    'external-delegate-path=libQnnTFLiteDelegate.so '
    'external-delegate-options="QNNExternalDelegate,backend_type=htp;" '
    f'model={MODEL_DIR}/resnext101-w8a8.tflite ! queue ! '

    # Postprocess inference results
    'qtimlpostprocess name=classification-postprocess results=1 '
    f'module=mobilenet-softmax labels={LABEL_DIR}/imagenet.txt '
    'settings="{\\"confidence\\": 51.0}" ! text/x-raw ! metamux. '

    # Attch ML result to video frame
    't. ! qtimetamux name=metamux ! '

    # Overlay ML result on top of video frame
    'qtivoverlay ! '

    # Output (e.g. display, appsink, encoded video)
    f'{gst_get_sink(args.output)}'
)

# ------------------------------------------------------------------------------
# Receive the buffer in case of Appsink output.
# Encode the buffer to JPEG and save to a file.
# ------------------------------------------------------------------------------

def on_frame(name, buffer):
    try:
        # For convenience, the Python script overwrites the same file (frame.jpeg)
        # with each inference run. This approach streamlines access to the latest
        # results and eliminates the need to manage multiple output files during testing.
        nv12_buffer_to_jpeg(buffer, "frame.jpeg")
        print(f"JPEG saved.")
    except Exception as e:
        print(f"JPEG error: {e}")

# ------------------------------------------------------------------------------
# Run the Pipeline
# ------------------------------------------------------------------------------

gst_run_pipeline(PIPELINE, on_frame)
