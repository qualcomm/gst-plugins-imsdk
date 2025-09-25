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
This application sets up a GStreamer pipeline for audio classification using a
quantized YAMNet model. It captures audio from the microphone, preprocesses it,
runs inference to detect sound events, and outputs the results in JSON format.
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

    # Postprocess inference results
    'qtimlpostprocess name=postprocess module=yamnet labels=/etc/labels/audioset.txt '
    'settings="{\\"confidence\\": 10.0}" ! text/x-raw ! queue ! '

    # Parse results to JSON
    'qtimlmetaparser module=json ! '

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
        buffer_to_file(buffer, "/etc/media/result.json")
        print(f"ML result saved.")
    except Exception as e:
        print(f"ML result error: {e}")

# ------------------------------------------------------------------------------
# Run the Pipeline
# ------------------------------------------------------------------------------

gst_run_pipeline(PIPELINE, on_frame)
