/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/ml/gstmlpool.h>
#include <gst/ml/ml-frame.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#include <math.h>
#include <algorithm>
#include <dlfcn.h>
#include <unistd.h>
#include <cairo/cairo.h>

#include "modules/qti-ml-post-proccess.h"
#include "modules/qti-json-parser.h"

#define DISPLACEMENT_THRESHOLD      0.7F
#define POSITION_THRESHOLD          0.04F

#define SUPPORTED_TENSORS_IDENTATION "                                "
#define CAPS_IDENTATION              "                                  "

#define GST_TYPE_ML_MODULES (gst_ml_modules_get_type())
#define GST_ML_MODULES_PREFIX "ml-postprocess-"

/* NewIModule
 *
 * Helper function pointer for creating new module instance.
 *
 * return: Pointer to new module instance.
 **/
typedef IModule *(*NewIModule)(LogCallback logger);

/* gst_module_logging
 *
 * Helper function module logging.
 *
 * return: None.
 **/
void
gst_module_logging (uint32_t level, const char * msg);

/* gst_structure_from_dictionary
 *
 * Helper function to get GstStructure from dictionary.
 *
 * return: Pointer to GstStructure.
 **/
GstStructure*
gst_structure_from_dictionary (const Dictionary& dict);

/* gst_ml_structure_to_module_params
 *
 * Helper function to transform ML GstStructure to ML Dictionary params for
 * submodule process.
 *
 * return: Dictionary with ML params.
 **/
Dictionary
gst_ml_structure_to_module_params (const GstStructure * structure);

/* gst_ml_caps_from_json
 *
 * Helper function to get GstCaps from JSON.
 *
 * return: Pointer to GstCaps.
 **/
GstCaps *
gst_ml_caps_from_json (const std::string& json);

/* gst_ml_module_get_type
 *
 * Helper function to get the type of the module.
 *
 * return: None.
 **/
void
gst_ml_module_get_type (GstStructure * structure, GString * result);

/* gst_ml_module_get_dimensions
 *
 * Helper function to get the dimensions of the module.
 *
 * return: None.
 **/
void
gst_ml_module_get_dimensions (GstStructure * structure, GString * result);

/* gst_ml_module_parse_caps
 *
 * Helper function to parse module caps.
 *
 * return: char string.
 **/
gchar *
gst_ml_module_parse_caps (const GstCaps *caps);

/* gst_ml_enumarate_modules
 *
 * Helper function to enumerate all modules.
 *
 * return: Pointer to GEnumValue.
 **/
GEnumValue *
gst_ml_enumarate_modules (const gchar * type);

/* gst_ml_modules_get_type
 *
 * Helper function to get the modules type.
 *
 * return: GType.
 **/
GType
gst_ml_modules_get_type (void);

/* gst_ml_post_process_boxes_intersection_score
 *
 * Helper function to get intersection score between two boxes.
 *
 * return: Score.
 **/
gfloat
gst_ml_post_process_boxes_intersection_score (ObjectDetection& l_box,
                                              ObjectDetection& r_box);

/* gst_ml_post_process_box_displacement_correction
 *
 * Helper function to do displacement correction of two boxes.
 *
 * return: None.
 **/
void
gst_ml_post_process_box_displacement_correction (ObjectDetection &l_box,
                                                 ObjectDetections& boxes);

/* gst_ml_module_caps_get_type
 *
 * Helper function to get module type from JSON.
 *
 * return: GQuark.
 **/
GQuark
gst_ml_module_caps_get_type (const std::string& json);

/* gst_ml_object_detections_sort_and_push
 *
 * Helper function to sort and push object detections.
 *
 * return: None.
 **/
void
gst_ml_object_detections_sort_and_push (std::any& output, std::any& predictions);

/* gst_ml_image_classifications_sort_and_push
 *
 * Helper function to sort and push image classifications.
 *
 * return: None.
 **/
void
gst_ml_image_classifications_sort_and_push (std::any& output, std::any& predictions);

/* gst_ml_audio_classifications_sort_and_push
 *
 * Helper function to sort and push audio classifications.
 *
 * return: None.
 **/
void
gst_ml_audio_classifications_sort_and_push (std::any& output, std::any& predictions);

/* gst_ml_pose_estimation_sort_and_push
 *
 * Helper function to sort and push pose estimation.
 *
 * return: None.
 **/
void
gst_ml_pose_estimation_sort_and_push (std::any& output, std::any& predictions);

/* gst_ml_text_generation_sort_and_push
 *
 * Helper function to sort and push text generation.
 *
 * return: None.
 **/
void
gst_ml_text_generation_sort_and_push (std::any& output, std::any& predictions);

/* gst_video_frame_to_module_frame
 *
 * Helper function to convert GstVideoFrame to VideoFrame.
 *
 * return: Success.
 **/
gboolean
gst_video_frame_to_module_frame (const GstVideoFrame &vframe, VideoFrame &frame);

/* gst_cairo_draw_setup
 *
 * Helper function to prepare cairo for draw.
 *
 * return: Success.
 **/
gboolean
gst_cairo_draw_setup (GstVideoFrame * frame, cairo_surface_t ** surface,
    cairo_t ** context);

/* gst_cairo_draw_cleanup
 *
 * Helper function for cairo cleanup.
 *
 * return: None.
 **/
void
gst_cairo_draw_cleanup (GstVideoFrame * frame, cairo_surface_t * surface,
    cairo_t * context);

/* gst_ml_tensors_convert
 *
 * Helper function to convert GstMLFrame to Tensors.
 *
 * return: Success.
 **/
gboolean
gst_ml_tensors_convert (const GstMLFrame& mlframe, GstBuffer * buffer,
    Tensors& tensors);

/* gst_is_valid_protection_meta
 *
 * Helper function to check if necessary fields are present in protection meta.
 *
 * return: Success.
 **/
gboolean
gst_is_valid_protection_meta (const GstProtectionMeta *pmeta);
