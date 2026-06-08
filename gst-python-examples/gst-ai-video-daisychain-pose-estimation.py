#!/usr/bin/env python3

################################################################################
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
################################################################################

from gst_utils import gst_run_pipeline, gst_get_sink, nv12_buffer_to_jpeg
import argparse
import os

# ------------------------------------------------------------------------------
# Constants and Configuration
# ------------------------------------------------------------------------------

DESCRIPTION = """
This application sets up a GStreamer pipeline for multi-stage machine learning inference
on video streams. It performs object detection followed by pose estimation.
The pipeline merges ML results with the video stream and overlays them
for visualization. It supports various input sources and output types.
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

    # Use a tee element to pass the video frame sequentially,
    # first to mlvconverter, then to metamux.
    'tee name=t1 '

    # Stage 1: Preprocess the video for inference
    't1. ! qtimlvconverter name=detection-preprocess ! queue ! '

    # Run inference using yolox model to detect people in the video
    'qtimltflite name=detection-inference delegate=external '
    'external-delegate-path=libQnnTFLiteDelegate.so '
    'external-delegate-options="QNNExternalDelegate,backend_type=htp;" '
    f'model={MODEL_DIR}/yolox_quantized.tflite ! queue ! '

    # Postprocess inference results
    'qtimlpostprocess name=detection-postprocess results=8 module=yolov8 '
    f'labels={LABEL_DIR}/coco.txt settings="{{\\"confidence\\": 75.0}}" ! '
    'text/x-raw ! metamux_1. '

    # Attch ML result to video frame
    't1. ! qtimetamux name=metamux_1 ! queue ! '

    # Use a tee element to pass the video frame sequentially,
    # first to mlvconverter, then to metamux.
    'tee name=t2 '

    # Stage 2: Preprocessing generates a tensor for each result from Stage 1
    't2. ! qtimlvconverter name=pose-preprocess mode=roi-batch-cumulative '
    'image-disposition=centre ! queue ! '

    # Run inference using the hrnet model
    'qtimltflite name=pose-inference delegate=external '
    'external-delegate-path=libQnnTFLiteDelegate.so '
    'external-delegate-options="QNNExternalDelegate,backend_type=htp,htp_performance_mode=(string)2;" '
    f'model={MODEL_DIR}/hrnet_pose_quantized.tflite ! queue ! '

    # Postprocess inference results
    'qtimlpostprocess name=pose-postprocess results=1 module=hrnet '
    f'labels={LABEL_DIR}/coco_pose.txt '
    f'settings={LABEL_DIR}/hrnet_pose_settings.json ! text/x-raw ! metamux_2. '

    # Attch ML result to video frame
    't2. ! qtimetamux name=metamux_2 ! '

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
