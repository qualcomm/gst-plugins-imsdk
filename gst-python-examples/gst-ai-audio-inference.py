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
This application sets up a GStreamer pipeline for audio preprocessing and inference.
It captures video input, converts frames into tensors using qtimlaconverter, runs
inference and outputs the data to a file via appsink for further processing.
"""

parser = argparse.ArgumentParser(description=DESCRIPTION)

# ------------------------------------------------------------------------------
# GStreamer Pipeline Definition
# ------------------------------------------------------------------------------

PIPELINE = (
    # Audio source from the microphone
    'pulsesrc ! audio/x-raw,format=S16LE ! '

    # Audio format convert and acumulate enough samples
    'audioconvert ! audioresample ! audiobuffersplit output-buffer-size=31200 ! '

    # Preprocess the audio for inference
    'qtimlaconverter name=preprocess sample-rate=16000 ! queue ! '

    # Run inference using a yamnet model
    'qtimltflite name=inference delegate=xnnpack model=/etc/models/yamnet.tflite ! queue ! '

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
        buffer_to_file(buffer, "/etc/media/tensor.bin")
        print(f"Tensor saved.")
    except Exception as e:
        print(f"Tensor error: {e}")

# ------------------------------------------------------------------------------
# Run the Pipeline
# ------------------------------------------------------------------------------

gst_run_pipeline(PIPELINE, on_frame)
