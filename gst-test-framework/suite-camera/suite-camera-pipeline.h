/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_SUITE_CAMERA_PIPELINE_H__
#define __GST_SUITE_CAMERA_PIPELINE_H__

#include "suite-utils.h"

#define CAMERA_FILE_PREFIX       "/etc/media"

G_BEGIN_DECLS

/**
 * camera_pipeline:
 * @params1: Caps parameters for preview stream.
 * @params2: Caps parameters for video stream.
 * @rawparams: Caps parameters for raw snapshot stream.
 * @jpegparams: Caps parameters for jpeg snapshot stream.
 * @duration: The pipeline running time in seconds.
 *
 * Function for creating the camera pipeline with the provided parameters.
 *
 * return: None
 */
void
camera_pipeline (GstCapsParameters * params1,
    GstCapsParameters * params2, GstCapsParameters * rawparams,
    GstCapsParameters * jpegparams, guint duration);

/**
 * camera_display_encode_pipeline:
 * @params1: Caps parameters for display stream.
 * @params2: Caps parameters for encoder stream.
 * @duration: The pipeline running time in seconds.
 *
 * Function for creating the camera display and camera encode
 * pipeline with the provided parameters.
 *
 * return: None
 */
void
camera_display_encode_pipeline (GstCapsParameters * params1,
    GstCapsParameters * params2, guint duration);

/**
 * camera_transform_display_pipeline:
 * @params1: Caps parameters for camera output.
 * @params2: Caps parameters for vtransform output.
 * @duration: The pipeline running time in seconds.
 *
 * Function for creating the camera vtransform and display
 * pipeline with the provided parameters.
 *
 * return: None
 */
void
camera_transform_display_pipeline (GstCapsParameters * params1,
    GstCapsParameters * params2, guint duration);

/**
 * camera_transform_display_pipeline:
 * @params1: Caps parameters for camera output.
 * @filename: File name, it should be Mp4 file.
 * @duration: The pipeline running time in seconds.
 *
 * Function for creating the camera and MP4 decode videos
 * composer to display pipeline with the provided parameters.
 *
 * return: None
 */
void
camera_composer_display_pipeline (GstCapsParameters * params1,
    gchar *filename, guint duration);

/**
 * camera_decoder_display_pipeline:
 * @filename: File name, it should be Mp4 file.
 * @duration: The pipeline running time in seconds.
 *
 * Function for decode MP4 videos to display.
 *
 * return: None
 */
void
camera_decoder_display_pipeline (gchar *filename, guint duration);

G_END_DECLS

#endif /* __GST_SUITE_CAMERA_PIPELINES_H__ */
