/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Changes from Qualcomm Technologies, Inc. are provided under the following license:
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlpostprocess.h"
#include "mlpostprocess-utils.h"

#include <stdio.h>
#include <stdlib.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/ml/ml-module-utils.h>
#include <gst/allocators/gstqtiallocator.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstimagepool.h>
#include <gst/memory/gstmempool.h>
#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>
#include <cairo/cairo.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#endif // HAVE_LINUX_DMA_BUF_H

#define GST_CAT_DEFAULT gst_ml_post_process_debug
GST_DEBUG_CATEGORY (gst_ml_post_process_debug);

#define gst_ml_post_process_parent_class parent_class
G_DEFINE_TYPE (GstMLPostProcess, gst_ml_post_process,
    GST_TYPE_BASE_TRANSFORM);

#define GST_DETECTION_TYPE            "object-detection"
#define GST_CLASSIFICATION_TYPE       "image-classification"
#define GST_POSE_TYPE                 "pose-estimation"
#define GST_SEGMENTATION_TYPE         "image-segmentation"
#define GST_SUPER_RESOLUTION_TYPE     "super-resolution"
#define GST_AUDIO_CLASSIFICATION_TYPE "audio-classification"
#define GST_TEXT_GENERATION_TYPE      "text-generation"

#define GST_IS_DETECTION_TYPE(type) \
    (type == g_quark_from_static_string (GST_DETECTION_TYPE))
#define GST_IS_CLASSIFICATION_TYPE(type) \
    (type == g_quark_from_static_string (GST_CLASSIFICATION_TYPE))
#define GST_IS_POSE_TYPE(type) \
    (type == g_quark_from_static_string (GST_POSE_TYPE))
#define GST_IS_SEGMENTATION_TYPE(type) \
    (type == g_quark_from_static_string (GST_SEGMENTATION_TYPE))
#define GST_IS_SUPER_RESOLUTION_TYPE(type) \
    (type == g_quark_from_static_string (GST_SUPER_RESOLUTION_TYPE))
#define GST_IS_AUDIO_CLASSIFICATION_TYPE(type) \
    (type == g_quark_from_static_string (GST_AUDIO_CLASSIFICATION_TYPE))
#define GST_IS_TEXT_GENERATION_TYPE(type) \
    (type == g_quark_from_static_string (GST_TEXT_GENERATION_TYPE))

#define GST_IS_INVALID_TYPE(type) \
    (!GST_IS_DETECTION_TYPE(type) && \
      !GST_IS_CLASSIFICATION_TYPE(type) && \
      !GST_IS_POSE_TYPE(type) && \
      !GST_IS_SEGMENTATION_TYPE(type) && \
      !GST_IS_SUPER_RESOLUTION_TYPE(type) && \
      !GST_IS_AUDIO_CLASSIFICATION_TYPE(type) && \
      !GST_IS_TEXT_GENERATION_TYPE(type))

#define GST_ML_POST_PROCESS_VIDEO_FORMATS \
    "{ BGRA, RGBA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, BGR16 }"

#define GST_ML_POST_PROCESS_TEXT_FORMATS \
    "{ utf8 }"

#define GST_ML_POST_PROCESS_SRC_CAPS                            \
    "video/x-raw, "                                             \
    "format = (string) " GST_ML_POST_PROCESS_VIDEO_FORMATS "; " \
    "text/x-raw, "                                              \
    "format = (string) " GST_ML_POST_PROCESS_TEXT_FORMATS

#define GST_ML_POST_PROCESS_SINK_CAPS \
    "neural-network/tensors"

#define DEFAULT_PROP_MODULE         0
#define DEFAULT_PROP_LABELS         NULL
#define DEFAULT_PROP_NUM_RESULTS    5
#define DEFAULT_PROP_SETTINGS       NULL

#define DEFAULT_MIN_BUFFERS         2
#define DEFAULT_MAX_BUFFERS         10
#define DEFAULT_VIDEO_WIDTH         320
#define DEFAULT_VIDEO_HEIGHT        240

#define DEFAULT_FONT_SIZE           24
#define MAX_TEXT_LENGTH             25

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_LABELS,
  PROP_NUM_RESULTS,
  PROP_SETTINGS,
};

enum
{
  OUTPUT_MODE_VIDEO,
  OUTPUT_MODE_TEXT,
};

static GstStaticCaps gst_ml_post_process_static_sink_caps =
    GST_STATIC_CAPS (GST_ML_POST_PROCESS_SINK_CAPS);

static GstCaps *
gst_ml_post_process_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_post_process_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_post_process_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_ML_POST_PROCESS_SRC_CAPS);

    if (gst_gbm_qcom_backend_is_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_ML_POST_PROCESS_VIDEO_FORMATS));

      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_post_process_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_post_process_sink_caps ());
}

static GstPadTemplate *
gst_ml_post_process_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_post_process_src_caps ());
}

static void
gst_ml_post_process_module_free (GstMLPostProcess * postprocess)
{
  if (!postprocess->module) {
    delete postprocess->module;
    postprocess->module = NULL;
  }

  if (postprocess->handle != NULL) {
    dlclose (postprocess->handle);
    postprocess->handle = NULL;
  }

  GST_INFO_OBJECT (postprocess, "Destroyed module.");
}

static gboolean
gst_ml_post_process_module_new (GstMLPostProcess * postprocess,
    const gchar * name)
{
  gchar *location = NULL;
  NewIModule NewModule;

  location = g_strdup_printf("%s/lib%s%s.so", GST_ML_MODULES_DIR,
      GST_ML_MODULES_PREFIX, name);

  postprocess->handle = dlopen (location, RTLD_NOW);

  g_free (location);

  if (postprocess->handle == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed to open %s library, error: %s!",
        name, dlerror());
    gst_ml_post_process_module_free (postprocess);
    return FALSE;
  }

  NewModule = (NewIModule) dlsym(postprocess->handle,
      ML_POST_PROCESS_MODULE_NEW_FUNC);
  if (NewModule == NULL) {
    GST_ERROR_OBJECT (postprocess,
        "Failed to link library method %s, error: %s!", name, dlerror());
    gst_ml_post_process_module_free (postprocess);
    return FALSE;
  }

  try {
    postprocess->module = NewModule (gst_module_logging);
  } catch (std::exception& e) {
    GST_ERROR_OBJECT (postprocess,
        "Failed to create and init new module, error: %s!", e.what());
    gst_ml_post_process_module_free (postprocess);
    return FALSE;
  }

  GST_INFO_OBJECT (postprocess, "Created %s module.", name);
  return TRUE;
}

static gboolean
gst_ml_post_process_module_set_opts (GstMLPostProcess * postprocess,
    gchar * labels, gchar * settings)
{
  std::string labels_contents;
  std::string settings_contents;

  if (labels)
    labels_contents = std::string(labels);

  if (settings && g_file_test (settings, G_FILE_TEST_IS_REGULAR)) {
    GError *error = NULL;
    gchar *file_content = NULL;

    if (!g_file_get_contents (settings, &file_content, NULL, &error)) {
      GST_ERROR_OBJECT (postprocess,
          "Failed to get settings file contents, error: %s!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    }

    settings_contents = std::string(file_content);
    g_free (file_content);
  } else if (settings) {
    settings_contents = std::string(settings);
  }

  return postprocess->module->Configure (labels_contents, settings_contents);
}

static gboolean
gst_ml_post_process_module_execute (GstMLPostProcess * postprocess,
    GstBuffer * buffer, std::any& output)
{
  GstMLFrame mlframe;
  gboolean needproc = TRUE;

  g_return_val_if_fail (output.has_value(), FALSE);

  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP))
    needproc = FALSE;

  if (needproc &&
      !gst_ml_frame_map (&mlframe, postprocess->mlinfo, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (postprocess, "Failed to map buffer!");
    return FALSE;
  }

  // Iterate all batches and execute the process
  for (guint idx = 0; idx <
      GST_ML_INFO_TENSOR_DIM (postprocess->mlinfo, 0, 0); ++idx) {

    GstProtectionMeta *pmeta = NULL;
    Dictionary mlparams;
    Tensors tensors;

    pmeta = gst_buffer_get_protection_meta_id (buffer,
        gst_batch_channel_name (idx));

    g_ptr_array_add (postprocess->info, pmeta->info);

    if (needproc) {

      mlparams = gst_ml_structure_to_module_params (pmeta->info);

      for (guint num = 0; num < GST_ML_FRAME_N_TENSORS (&mlframe); ++num) {
        Tensor tensor;
        guint size = 1;
        GstMLTensorMeta *mlmeta = NULL;

        mlmeta = gst_buffer_get_ml_tensor_meta_id (buffer, num);

        if (mlmeta == NULL) {
          GST_ERROR_OBJECT (postprocess, "Invalid tensor meta: %p", mlmeta);
          gst_ml_frame_unmap (&mlframe);
          return FALSE;
        }

        switch (GST_ML_FRAME_TYPE (&mlframe)) {
          case GST_ML_TYPE_INT8:
            tensor.type = kInt8;
            break;
          case GST_ML_TYPE_UINT8:
            tensor.type = kUint8;
            break;
          case GST_ML_TYPE_INT32:
            tensor.type = kInt32;
            break;
          case GST_ML_TYPE_UINT32:
            tensor.type = kUint32;
            break;
          case GST_ML_TYPE_FLOAT16:
            tensor.type = kFloat16;
            break;
          case GST_ML_TYPE_FLOAT32:
            tensor.type = kFloat32;
            break;
          default:
            GST_ERROR_OBJECT (postprocess, "Unsupported ML type!");
            gst_ml_frame_unmap (&mlframe);
            return FALSE;
        }

        const char *name = g_quark_to_string (mlmeta->name);
        tensor.name = std::string (name != NULL ? name : "");

        // Always set batch index to 1, the postprocess will not process batching
        tensor.dimensions.push_back(1);

        for (guint pos = 1; pos < GST_ML_FRAME_N_DIMENSIONS (&mlframe, num); ++pos) {
          tensor.dimensions.push_back(GST_ML_FRAME_DIM (&mlframe, num, pos));
          size *= GST_ML_FRAME_DIM (&mlframe, num, pos);
        }

        // Increment the pointer with the size of single batch and current index.
        tensor.data = GST_ML_FRAME_BLOCK_DATA (&mlframe, num) + (idx * size);
        tensors.push_back(tensor);
      }
    }

    std::any predictions;
    if (GST_IS_DETECTION_TYPE (postprocess->type))
      predictions = ObjectDetections();
    else if (GST_IS_CLASSIFICATION_TYPE (postprocess->type))
      predictions = ImageClassifications();
    else if (GST_IS_AUDIO_CLASSIFICATION_TYPE (postprocess->type))
      predictions = AudioClassifications();
    else if (GST_IS_POSE_TYPE (postprocess->type))
      predictions = PoseEstimations();
    else
      predictions = output;

    if (needproc &&
        !postprocess->module->Process (tensors, mlparams, predictions)) {
      GST_ERROR_OBJECT (postprocess, "Failed to execute process!");
      gst_ml_frame_unmap (&mlframe);
      return FALSE;
    }

    // Sorting entries
    if (GST_IS_DETECTION_TYPE (postprocess->type))
      gst_ml_object_detections_sort_and_push (output, predictions);
    else if (GST_IS_CLASSIFICATION_TYPE (postprocess->type))
      gst_ml_image_classifications_sort_and_push (output, predictions);
    else if (GST_IS_AUDIO_CLASSIFICATION_TYPE (postprocess->type))
      gst_ml_audio_classifications_sort_and_push (output, predictions);
    else if (GST_IS_POSE_TYPE (postprocess->type))
      gst_ml_pose_estimation_sort_and_push (output, predictions);
  }

  if (needproc)
    gst_ml_frame_unmap (&mlframe);

  return TRUE;
}

static GstBufferPool *
gst_ml_post_process_create_pool (GstMLPostProcess * postprocess,
    GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;

  GstVideoInfo info = {0,};
  GstVideoAlignment align = {0,};

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (postprocess, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if ((pool = gst_image_buffer_pool_new ()) == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed to create image pool!");
    return NULL;
  }

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    allocator = gst_fd_allocator_new ();
    GST_INFO_OBJECT (postprocess, "Buffer pool uses GBM memory");
  } else {
    allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    GST_INFO_OBJECT (postprocess, "Buffer pool uses DMA memory");
  }

  if (allocator == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed to create allocator");
    gst_clear_object (&pool);
    return NULL;
  }

  structure = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_allocator (structure, allocator, NULL);
  g_object_unref (allocator);

  gst_buffer_pool_config_add_option (structure,
      GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (structure,
      GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  if (!gst_video_retrieve_gpu_alignment (&info, &align)) {
    GST_ERROR_OBJECT (postprocess, "Failed to get alignment!");
    gst_clear_object (&pool);
    return NULL;
  }

  gst_buffer_pool_config_add_option (structure,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (structure, &align);

  gst_buffer_pool_config_set_params (structure, caps, info.size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (!gst_buffer_pool_set_config (pool, structure)) {
    GST_WARNING_OBJECT (postprocess, "Failed to set pool configuration!");
    gst_clear_object (&pool);
  }

  return pool;
}

static gboolean
gst_ml_video_detection_fill_video_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer * buffer, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  gdouble x = 0.0, y = 0.0, width = 0.0, height = 0.0;
  gdouble fontsize = 12.0, borderwidth = 2.0, radius = 2.0;
  guint idx = 0, num = 0, mrk = 0, n_entries = 0, color = 0, length = 0;
  gboolean success = FALSE;

  auto& predictions =
      std::any_cast<DetectionPrediction&>(output);

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  // Set rectangle borders width and label font size.
  cairo_set_line_width (context, borderwidth);
  cairo_set_font_size (context, fontsize);

  for (idx = 0; idx < predictions.size(); idx++) {
    auto& detections = predictions[idx];
    GstVideoRectangle region = { 0, };
    GstStructure *info = NULL;

    n_entries = (detections.size() < postprocess->n_results) ?
        detections.size() : postprocess->n_results;

    // No decoded poses, nothing to do.
    if (n_entries == 0)
      continue;

    // Get saved info for the current batch
    info = (GstStructure *) g_ptr_array_index (postprocess->info, idx);

    // Extract the source tensor region with actual data.
    gst_ml_structure_get_source_region (info, &region);

    // Recalculate the region dimensions depending on the ratios.
    if ((region.w * GST_VIDEO_FRAME_HEIGHT (vframe)) >
        (region.h * GST_VIDEO_FRAME_WIDTH (vframe))) {

      region.h = gst_util_uint64_scale_int (
          GST_VIDEO_FRAME_WIDTH (vframe), region.h, region.w);
      region.w = GST_VIDEO_FRAME_WIDTH (vframe);
    } else if ((region.w * GST_VIDEO_FRAME_HEIGHT (vframe)) <
        (region.h * GST_VIDEO_FRAME_WIDTH (vframe))) {

      region.w = gst_util_uint64_scale_int (
          GST_VIDEO_FRAME_HEIGHT (vframe), region.w, region.h);
      region.h = GST_VIDEO_FRAME_HEIGHT (vframe);
    } else {
      region.w = GST_VIDEO_FRAME_WIDTH (vframe);
      region.h = GST_VIDEO_FRAME_HEIGHT (vframe);
    }

    // Additional overwrite of X and Y axis for centred image disposition.
    region.x = (GST_VIDEO_FRAME_WIDTH (vframe) - region.w) / 2;
    region.y = (GST_VIDEO_FRAME_HEIGHT (vframe) - region.h) / 2;

    for (num = 0; num < n_entries; num++) {
      ObjectDetection& entry = detections[num];

      // Set the bounding box parameters based on the output buffer dimensions.
      x = region.x + (ABS (entry.left) * region.w);
      y = region.y + (ABS (entry.top) * region.h);
      width  = ABS (entry.right - entry.left) * region.w;
      height = ABS (entry.bottom - entry.top) * region.h;

      // Clip width and height if it outside the frame limits.
      width = ((x + width) > GST_VIDEO_FRAME_WIDTH (vframe)) ?
          (GST_VIDEO_FRAME_WIDTH (vframe) - x) : width;
      height = ((y + height) > GST_VIDEO_FRAME_HEIGHT (vframe)) ?
          (GST_VIDEO_FRAME_HEIGHT (vframe) - y) : height;

      color = entry.color.value();

      // Set color.
      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      // Draw rectangle
      cairo_rectangle (context, x, y, width, height);
      cairo_stroke (context);
      g_return_val_if_fail (
          CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

      // Draw landmarks if present.
      if (entry.landmarks) {
        length = entry.landmarks.value().size();
        for (mrk = 0; mrk < length; mrk++) {
          Keypoint& kp = entry.landmarks.value()[mrk];

          GST_TRACE_OBJECT (postprocess, "Landmark [%.2f x %.2f]", kp.x, kp.y);

          // Adjust coordinates based on the output buffer dimensions.
          kp.x = kp.x * GST_VIDEO_FRAME_WIDTH (vframe);
          kp.y = kp.y * GST_VIDEO_FRAME_HEIGHT (vframe);

          cairo_arc (context, kp.x, kp.y, radius, 0, 2 * G_PI);
          cairo_close_path (context);

          cairo_fill (context);
          g_return_val_if_fail (
              CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);
        }
      }

      // Set the width and height of the label background rectangle.
      width = ceil (entry.name.size() * fontsize * 3.0F / 5.0F);
      height = ceil (fontsize);

      // Calculate the X and Y position of the label.
      if ((y -= height) < 0.0)
        y = region.y + region.h;

      if ((x + width - 1) > (gdouble) region.w)
        x = region.x + region.w - width;

      cairo_rectangle (context, (x - 1), y, width, height);
      cairo_fill (context);

      // Choose the best contrasting color to the background.
      color = EXTRACT_ALPHA_COLOR (color);
      color += ((EXTRACT_RED_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 8;
      color += ((EXTRACT_GREEN_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 16;
      color += ((EXTRACT_BLUE_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 24;

      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      // Set the starting position of the label text.
      cairo_move_to (context, x, (y + (fontsize * 4.0F / 5.0F)));

      // Draw text string.
      cairo_show_text (context, entry.name.c_str());
      g_return_val_if_fail (
          CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

      GST_TRACE_OBJECT (postprocess, "Batch: %u, label: %s, confidence: %.1f%%, "
          "[%.2f %.2f %.2f %.2f]", idx, entry.name.c_str(),
          entry.confidence, entry.top, entry.left, entry.bottom, entry.right);

      // Flush to ensure all writing to the surface has been done.
      cairo_surface_flush (surface);
    }
  }

  gst_cairo_draw_cleanup (vframe, surface, context);

  return TRUE;
}

static gboolean
gst_ml_video_detection_fill_text_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer * buffer)
{
  GstStructure *structure = NULL;
  GstMemory *mem = NULL;
  gchar *string = NULL, *name = NULL;
  GValue list = G_VALUE_INIT, bboxes = G_VALUE_INIT;
  GValue array = G_VALUE_INIT, value = G_VALUE_INIT;
  guint idx = 0, num = 0, mrk = 0, n_entries = 0, sequence_idx = 0, id = 0;
  gfloat x = 0.0, y = 0.0, width = 0.0, height = 0.0;
  gsize length = 0;

  auto& predictions =
      std::any_cast<DetectionPrediction&>(output);

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&bboxes, GST_TYPE_ARRAY);
  g_value_init (&array, GST_TYPE_ARRAY);

  for (idx = 0; idx < predictions.size(); idx++) {
    auto& detections = predictions[idx];
    GstStructure *info = NULL;
    const GValue *val = NULL;

    n_entries = (detections.size() < postprocess->n_results) ?
        detections.size() : postprocess->n_results;

    // Get saved info for the current batch
    info = (GstStructure *) g_ptr_array_index (postprocess->info, idx);

    if (gst_structure_has_field (info, "sequence-index"))
        gst_structure_get_uint (info, "sequence-index", &sequence_idx);

    for (num = 0; num < n_entries; num++) {
      ObjectDetection& entry = detections[num];

      id = GST_META_ID (postprocess->stage_id, sequence_idx, num);

      x = entry.left;
      y = entry.top;
      width = entry.right - entry.left;
      height = entry.bottom - entry.top;

      GST_TRACE_OBJECT (postprocess, "Batch: %u, ID: %X, Label: %s, Confidence: "
          "%.1f%%, Box [%.2f %.2f %.2f %.2f]", idx, id,
          entry.name.c_str(), entry.confidence, x, y, width, height);

      // Replace empty spaces otherwise subsequent stream parse call will fail.
      name = g_strdup (entry.name.c_str());
      name = g_strdelimit (name, " ", '.');

      structure = gst_structure_new (name, "id", G_TYPE_UINT, id, "confidence",
          G_TYPE_DOUBLE, entry.confidence,
          "color", G_TYPE_UINT, entry.color.value(), NULL);

      g_free (name);
      g_value_init (&value, G_TYPE_FLOAT);

      g_value_set_float (&value, x);
      gst_value_array_append_value (&array, &value);

      g_value_set_float (&value, y);
      gst_value_array_append_value (&array, &value);

      g_value_set_float (&value, width);
      gst_value_array_append_value (&array, &value);

      g_value_set_float (&value, height);
      gst_value_array_append_value (&array, &value);

      gst_structure_set_value (structure, "rectangle", &array);
      g_value_reset (&array);

      g_value_unset (&value);
      g_value_init (&value, GST_TYPE_STRUCTURE);

      if (entry.landmarks && entry.landmarks.value().size() != 0) {
        GstStructure *substructure = NULL;

        for (mrk = 0; mrk < entry.landmarks.value().size(); mrk++) {
          Keypoint& lndmark = entry.landmarks.value()[mrk];

          GST_TRACE_OBJECT (postprocess, "Landmark %s [%.2f x %.2f]",
              lndmark.name.c_str(), lndmark.x, lndmark.y);

          // Replace empty spaces otherwise subsequent structure call will fail.
          name = g_strdup (lndmark.name.c_str());
          name = g_strdelimit (name, " ", '.');

          substructure = gst_structure_new (name, "x", G_TYPE_DOUBLE,
              lndmark.x, "y", G_TYPE_DOUBLE, lndmark.y, NULL);
          g_free (name);

          g_value_take_boxed (&value, substructure);
          gst_value_array_append_value (&array, &value);
          g_value_reset (&value);
        }

        gst_structure_set_value (structure, "landmarks", &array);
        g_value_reset (&array);
      }

      if (entry.xtraparams.has_value ()) {
        GstStructure *xtraparams =
            gst_structure_from_dictionary (entry.xtraparams.value ());

        GValue value = G_VALUE_INIT;
        g_value_init (&value, GST_TYPE_STRUCTURE);
        g_value_take_boxed (&value, xtraparams);

        gst_structure_set_value (structure, "xtraparams", &value);
        g_value_unset (&value);
      }

      g_value_take_boxed (&value, structure);
      gst_value_array_append_value (&bboxes, &value);
      g_value_unset (&value);
    }

    structure = gst_structure_new_empty ("ObjectDetection");

    gst_structure_set_value (structure, "bounding-boxes", &bboxes);
    g_value_reset (&bboxes);

    val = gst_structure_get_value (info, "timestamp");
    gst_structure_set_value (structure, "timestamp", val);

    val = gst_structure_get_value (info, "sequence-index");
    gst_structure_set_value (structure, "sequence-index", val);

    val = gst_structure_get_value (info, "sequence-num-entries");
    gst_structure_set_value (structure, "sequence-num-entries", val);

    if ((val = gst_structure_get_value (info, "stream-id")))
      gst_structure_set_value (structure, "stream-id", val);

    if ((val = gst_structure_get_value (info, "stream-timestamp")))
      gst_structure_set_value (structure, "stream-timestamp", val);

    if ((val = gst_structure_get_value (info, "parent-id")))
      gst_structure_set_value (structure, "parent-id", val);

    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, structure);

    gst_value_list_append_value (&list, &value);
    g_value_unset (&value);
  }

  g_value_unset (&array);
  g_value_unset (&bboxes);

  // Serialize the predictions list into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed serialize predictions structure!");
    return FALSE;
  }

  // Increase the length by 1 byte for the '\0' character.
  length = strlen (string) + 1;

  mem = gst_memory_new_wrapped ((GstMemoryFlags) 0, string,
      length, 0, length, string, g_free);
  gst_buffer_append_memory (buffer, mem);

  return TRUE;
}

static gboolean
gst_ml_video_classification_fill_video_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer *buffer, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  guint idx = 0, num = 0, n_entries = 0, color = 0;
  gdouble width = 0.0, height = 0.0;
  gboolean success = FALSE;

  auto& predictions =
      std::any_cast<ImageClassPrediction&>(output);

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  // Set the most appropriate font size based on number of results.
  cairo_set_font_size (context, DEFAULT_FONT_SIZE);

  height = DEFAULT_FONT_SIZE;

  for (idx = 0; idx < predictions.size(); idx++) {
    auto& classifications = predictions[idx];

    n_entries = (classifications.size() < postprocess->n_results) ?
        classifications.size() : postprocess->n_results;

    for (num = 0; num < n_entries; num++) {
      ImageClassification& entry = classifications[num];

      // Check whether there is enough pixel space for this label entry.
      if (((num + 1) * height) > GST_VIDEO_FRAME_HEIGHT (vframe))
        break;

      GST_TRACE_OBJECT (postprocess, "Batch: %u, label: %s, confidence: "
          "%.1f%%", idx, entry.name.c_str(), entry.confidence);

      color = entry.color.value();

      // Set text background color.
      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      width = ceil (entry.name.size() * DEFAULT_FONT_SIZE * 3.0F / 5.0F);

      cairo_rectangle (context, 0, (num * height), width, height);
      cairo_fill (context);

      // Choose the best contrasting color to the background.
      color = EXTRACT_ALPHA_COLOR (color);
      color += ((EXTRACT_RED_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 8;
      color += ((EXTRACT_GREEN_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 16;
      color += ((EXTRACT_BLUE_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 24;

      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      // (0,0) is at top left corner of the buffer.
      cairo_move_to (context, 0.0, (DEFAULT_FONT_SIZE * (num + 1) * 4.0F / 5.0F));

      // Draw text string.
      cairo_show_text (context, entry.name.c_str());
      g_return_val_if_fail (
          CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

      // Flush to ensure all writing to the surface has been done.
      cairo_surface_flush (surface);
    }
  }

  gst_cairo_draw_cleanup (vframe, surface, context);

  return TRUE;
}

static gboolean
gst_ml_video_classification_fill_text_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer *buffer)
{
  GstStructure *structure = NULL;
  GstMemory *mem = NULL;
  gchar *string = NULL, *name = NULL;
  GValue list = G_VALUE_INIT, labels = G_VALUE_INIT, value = G_VALUE_INIT;
  guint idx = 0, num = 0, n_entries = 0, sequence_idx = 0, id = 0;
  gsize length = 0;

  auto& predictions =
      std::any_cast<ImageClassPrediction&>(output);

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&labels, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_STRUCTURE);

  for (idx = 0; idx < predictions.size(); idx++) {
    auto& classifications = predictions[idx];
    GstStructure *info = NULL;
    const GValue *val = NULL;

    n_entries = (classifications.size() < postprocess->n_results) ?
        classifications.size() : postprocess->n_results;

    // Get saved info for the current batch
    info = (GstStructure *) g_ptr_array_index (postprocess->info, idx);

    if (gst_structure_has_field (info, "sequence-index"))
        gst_structure_get_uint (info, "sequence-index", &sequence_idx);

    id = GST_META_ID (postprocess->stage_id, sequence_idx, 0);

    for (num = 0; num < n_entries; num++) {
      ImageClassification& entry = classifications[num];

      GST_TRACE_OBJECT (postprocess, "Batch: %u, ID: %X, Label: %s, "
          "Confidence: %.1f%%", idx, id, entry.name.c_str(),
          entry.confidence);

      // Replace empty spaces otherwise subsequent stream parse call will fail.
      name = g_strdup (entry.name.c_str());
      name = g_strdelimit (name, " ", '.');

      structure = gst_structure_new (name, "id", G_TYPE_UINT, id, "confidence",
          G_TYPE_DOUBLE,  entry.confidence,
          "color", G_TYPE_UINT, entry.color.value(), NULL);
      g_free (name);

      if (entry.xtraparams.has_value ()) {
        GstStructure *xtraparams =
            gst_structure_from_dictionary (entry.xtraparams.value ());

        GValue value = G_VALUE_INIT;
        g_value_init (&value, GST_TYPE_STRUCTURE);
        g_value_take_boxed (&value, xtraparams);

        gst_structure_set_value (structure, "xtraparams", &value);
        g_value_unset (&value);
      }

      g_value_take_boxed (&value, structure);
      gst_value_array_append_value (&labels, &value);
      g_value_reset (&value);
    }

    structure = gst_structure_new_empty ("ImageClassification");

    gst_structure_set_value (structure, "labels", &labels);
    g_value_reset (&labels);

    val = gst_structure_get_value (info, "timestamp");
    gst_structure_set_value (structure, "timestamp", val);

    val = gst_structure_get_value (info, "sequence-index");
    gst_structure_set_value (structure, "sequence-index", val);

    val = gst_structure_get_value (info, "sequence-num-entries");
    gst_structure_set_value (structure, "sequence-num-entries", val);

    if ((val = gst_structure_get_value (info, "stream-id")))
      gst_structure_set_value (structure, "stream-id", val);

    if ((val = gst_structure_get_value (info, "stream-timestamp")))
      gst_structure_set_value (structure, "stream-timestamp", val);

    if ((val = gst_structure_get_value (info, "parent-id")))
      gst_structure_set_value (structure, "parent-id", val);

    g_value_take_boxed (&value, structure);
    gst_value_list_append_value (&list, &value);
    g_value_reset (&value);
  }

  g_value_unset (&labels);
  g_value_unset (&value);

  // Serialize the predictions list into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed serialize predictions structure!");
    return FALSE;
  }

  // Increase the length by 1 byte for the '\0' character.
  length = strlen (string) + 1;

  mem = gst_memory_new_wrapped ((GstMemoryFlags) 0, string,
      length, 0, length, string, g_free);
  gst_buffer_append_memory (buffer, mem);

  return TRUE;
}

static gboolean
gst_ml_audio_classification_fill_video_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer *buffer, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  guint idx = 0, num = 0, n_entries = 0, color = 0;
  gdouble width = 0.0, height = 0.0;
  gboolean success = FALSE;

  auto& predictions =
      std::any_cast<AudioClassPrediction&>(output);

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  // Set the most appropriate font size based on number of results.
  cairo_set_font_size (context, DEFAULT_FONT_SIZE);

  height = DEFAULT_FONT_SIZE;

  for (idx = 0; idx < predictions.size(); idx++) {
    auto& classifications = predictions[idx];

    n_entries = (classifications.size() < postprocess->n_results) ?
        classifications.size() : postprocess->n_results;

    for (num = 0; num < n_entries; num++) {
      AudioClassification& entry = classifications[num];

      // Check whether there is enough pixel space for this label entry.
      if (((num + 1) * height) > GST_VIDEO_FRAME_HEIGHT (vframe))
        break;

      GST_TRACE_OBJECT (postprocess, "Batch: %u, label: %s, confidence: "
          "%.1f%%", idx, entry.name.c_str(), entry.confidence);

      color = entry.color.value();

      // Set text background color.
      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      width = ceil (entry.name.size() * DEFAULT_FONT_SIZE * 3.0F / 5.0F);

      cairo_rectangle (context, 0, (num * height), width, height);
      cairo_fill (context);

      // Choose the best contrasting color to the background.
      color = EXTRACT_ALPHA_COLOR (color);
      color += ((EXTRACT_RED_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 8;
      color += ((EXTRACT_GREEN_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 16;
      color += ((EXTRACT_BLUE_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 24;

      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      // (0,0) is at top left corner of the buffer.
      cairo_move_to (context, 0.0, (DEFAULT_FONT_SIZE * (num + 1) * 4.0F / 5.0F));

      // Draw text string.
      cairo_show_text (context, entry.name.c_str());
      g_return_val_if_fail (
          CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

      // Flush to ensure all writing to the surface has been done.
      cairo_surface_flush (surface);
    }
  }

  gst_cairo_draw_cleanup (vframe, surface, context);

  return TRUE;
}

static gboolean
gst_ml_audio_classification_fill_text_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer *buffer)
{
  GstStructure *structure = NULL;
  GstMemory *mem = NULL;
  gchar *string = NULL, *name = NULL;
  GValue list = G_VALUE_INIT, labels = G_VALUE_INIT, value = G_VALUE_INIT;
  guint idx = 0, num = 0, n_entries = 0, sequence_idx = 0, id = 0;
  gsize length = 0;

  auto& predictions =
      std::any_cast<AudioClassPrediction&>(output);

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&labels, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_STRUCTURE);

  for (idx = 0; idx < predictions.size(); idx++) {
    auto& classifications = predictions[idx];
    GstStructure *info = NULL;
    const GValue *val = NULL;

    n_entries = (classifications.size() < postprocess->n_results) ?
        classifications.size() : postprocess->n_results;

    // Get saved info for the current batch
    info = (GstStructure *) g_ptr_array_index (postprocess->info, idx);

    if (gst_structure_has_field (info, "sequence-index"))
        gst_structure_get_uint (info, "sequence-index", &sequence_idx);

    id = GST_META_ID (postprocess->stage_id, sequence_idx, 0);

    for (num = 0; num < n_entries; num++) {
      AudioClassification& entry = classifications[num];

      GST_TRACE_OBJECT (postprocess, "Batch: %u, ID: %X, Label: %s, "
          "Confidence: %.1f%%", idx, id, entry.name.c_str(),
          entry.confidence);

      // Replace empty spaces otherwise subsequent stream parse call will fail.
      name = g_strdup (entry.name.c_str());
      name = g_strdelimit (name, " ", '.');

      structure = gst_structure_new (name, "id", G_TYPE_UINT, id, "confidence",
          G_TYPE_DOUBLE,  entry.confidence,
          "color", G_TYPE_UINT, entry.color.value(), NULL);
      g_free (name);

      if (entry.xtraparams.has_value ()) {
        GstStructure *xtraparams =
            gst_structure_from_dictionary (entry.xtraparams.value ());

        GValue value = G_VALUE_INIT;
        g_value_init (&value, GST_TYPE_STRUCTURE);
        g_value_take_boxed (&value, xtraparams);

        gst_structure_set_value (structure, "xtraparams", &value);
        g_value_unset (&value);
      }

      g_value_take_boxed (&value, structure);
      gst_value_array_append_value (&labels, &value);
      g_value_reset (&value);
    }

    structure = gst_structure_new_empty ("AudioClassification");

    gst_structure_set_value (structure, "labels", &labels);
    g_value_reset (&labels);

    val = gst_structure_get_value (info, "timestamp");
    gst_structure_set_value (structure, "timestamp", val);

    val = gst_structure_get_value (info, "sequence-index");
    gst_structure_set_value (structure, "sequence-index", val);

    val = gst_structure_get_value (info, "sequence-num-entries");
    gst_structure_set_value (structure, "sequence-num-entries", val);

    if ((val = gst_structure_get_value (info, "stream-id")))
      gst_structure_set_value (structure, "stream-id", val);

    if ((val = gst_structure_get_value (info, "stream-timestamp")))
      gst_structure_set_value (structure, "stream-timestamp", val);

    if ((val = gst_structure_get_value (info, "parent-id")))
      gst_structure_set_value (structure, "parent-id", val);

    g_value_take_boxed (&value, structure);
    gst_value_list_append_value (&list, &value);
    g_value_reset (&value);
  }

  g_value_unset (&labels);
  g_value_unset (&value);

  // Serialize the predictions list into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed serialize predictions structure!");
    return FALSE;
  }

  // Increase the length by 1 byte for the '\0' character.
  length = strlen (string) + 1;

  mem = gst_memory_new_wrapped ((GstMemoryFlags) 0, string,
      length, 0, length, string, g_free);
  gst_buffer_append_memory (buffer, mem);

  return TRUE;
}

static gboolean
gst_ml_video_pose_fill_video_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer * buffer, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  gdouble borderwidth = 0.0, radius = 0.0;
  guint idx = 0, num = 0, m = 0, n_entries = 0;
  gboolean success = FALSE;

  auto& predictions =
      std::any_cast<PosePrediction&>(output);

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  // TODO: Set the most appropriate border size based on the bbox dimensions.
  borderwidth = 1.0;

  // TODO: Set the most appropriate border size based on the bbox dimensions.
  radius = 2.0;

  // Set skeleton line width.
  cairo_set_line_width (context, borderwidth);

  for (idx = 0; idx < predictions.size(); idx++) {
    auto& estimations = predictions[idx];
    GstVideoRectangle region = { 0, };
    GstStructure *info = NULL;

    n_entries = (estimations.size() < postprocess->n_results) ?
        estimations.size() : postprocess->n_results;

    // No decoded poses, nothing to do.
    if (n_entries == 0)
      continue;

    // Get saved info for the current batch
    info = (GstStructure *) g_ptr_array_index (postprocess->info, idx);

    // Extract the source tensor region with actual data.
    gst_ml_structure_get_source_region (info, &region);

    // Recalculate the region dimensions depending on the ratios.
    if ((region.w * GST_VIDEO_FRAME_HEIGHT (vframe)) >
        (region.h * GST_VIDEO_FRAME_WIDTH (vframe))) {

      region.h = gst_util_uint64_scale_int (
          GST_VIDEO_FRAME_WIDTH (vframe), region.h, region.w);
      region.w = GST_VIDEO_FRAME_WIDTH (vframe);
    } else if ((region.w * GST_VIDEO_FRAME_HEIGHT (vframe)) <
        (region.h * GST_VIDEO_FRAME_WIDTH (vframe))) {

      region.w = gst_util_uint64_scale_int (
          GST_VIDEO_FRAME_HEIGHT (vframe), region.w, region.h);
      region.h = GST_VIDEO_FRAME_HEIGHT (vframe);
    } else {
      region.w = GST_VIDEO_FRAME_WIDTH (vframe);
      region.h = GST_VIDEO_FRAME_HEIGHT (vframe);
    }

    // Additional overwrite of X and Y axis for centred image disposition.
    region.x = (GST_VIDEO_FRAME_WIDTH (vframe) - region.w) / 2;
    region.y = (GST_VIDEO_FRAME_HEIGHT (vframe) - region.h) / 2;

    for (num = 0; num < n_entries; num++) {
      PoseEstimation& entry = estimations[num];

      GST_TRACE_OBJECT (postprocess, "Batch: %u, confidence: %.2f", idx,
          entry.confidence);

      // Draw pose keypoints.
      for (m = 0; m < entry.keypoints.size(); ++m) {
        Keypoint& kp = entry.keypoints[m];

        // Adjust coordinates based on the output buffer dimensions.
        kp.x = region.x + (kp.x * region.w);
        kp.y = region.y + (kp.y * region.h);

        GST_TRACE_OBJECT (postprocess, "Keypoint: '%s' [%.0f x %.0f], confidence %.2f",
            kp.name.c_str(), kp.x, kp.y, kp.confidence);

        // Set color.
        cairo_set_source_rgba (context,
            EXTRACT_FLOAT_BLUE_COLOR (kp.color.value()),
            EXTRACT_FLOAT_GREEN_COLOR (kp.color.value()),
            EXTRACT_FLOAT_RED_COLOR (kp.color.value()),
            EXTRACT_FLOAT_ALPHA_COLOR (kp.color.value()));

        cairo_arc (context, kp.x, kp.y, radius, 0, 2 * M_PI);
        cairo_close_path (context);
      }

      cairo_fill (context);
      g_return_val_if_fail (
          CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

      // Draw pose skeleton.
      for (m = 0; m < entry.links.value().size(); ++m) {
        KeypointLink& connection = entry.links.value()[m];

        // Adjust coordinates based on the output buffer dimensions.
        connection.l_kp.x = region.x + (connection.l_kp.x * region.w);
        connection.l_kp.y = region.y + (connection.l_kp.y * region.h);

        connection.r_kp.x = region.x + (connection.r_kp.x * region.w);
        connection.r_kp.y = region.y + (connection.r_kp.y * region.h);

        GST_TRACE_OBJECT (postprocess,
            "Link: '%s' [%.0f x %.0f] <--> '%s' [%.0f x %.0f]",
            connection.l_kp.name.c_str(), connection.l_kp.x, connection.l_kp.y,
            connection.r_kp.name.c_str(), connection.r_kp.x, connection.r_kp.y);

        cairo_move_to (context, connection.l_kp.x, connection.l_kp.y);
        cairo_line_to (context, connection.r_kp.x, connection.r_kp.y);

        cairo_stroke (context);
        g_return_val_if_fail (
            CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);
      }
    }
  }

  gst_cairo_draw_cleanup (vframe, surface, context);

  return TRUE;
}

static gboolean
gst_ml_video_pose_fill_text_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer * buffer)
{
  GstStructure *structure = NULL;
  GstMemory *mem = NULL;
  gchar *string = NULL, *name = NULL;
  GValue list = G_VALUE_INIT, poses = G_VALUE_INIT, value = G_VALUE_INIT;
  GValue keypoints = G_VALUE_INIT, links = G_VALUE_INIT, link = G_VALUE_INIT;
  guint idx = 0, num = 0, seqnum = 0, n_entries = 0, sequence_idx = 0, id = 0;
  gsize length = 0;

  auto& predictions =
      std::any_cast<PosePrediction&>(output);

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&poses, GST_TYPE_ARRAY);
  g_value_init (&keypoints, GST_TYPE_ARRAY);
  g_value_init (&links, GST_TYPE_ARRAY);
  g_value_init (&link, GST_TYPE_ARRAY);

  for (idx = 0; idx < predictions.size(); idx++) {
    auto& estimations = predictions[idx];
    GstStructure *info = NULL;
    const GValue *val = NULL;

    n_entries = (estimations.size() < postprocess->n_results) ?
        estimations.size() : postprocess->n_results;

    // Get saved info for the current batch
    info = (GstStructure *) g_ptr_array_index (postprocess->info, idx);

    if (gst_structure_has_field (info, "sequence-index"))
        gst_structure_get_uint (info, "sequence-index", &sequence_idx);

    for (num = 0; num < n_entries; num++) {
      PoseEstimation& entry = estimations[num];

      g_value_init (&value, GST_TYPE_STRUCTURE);

      // Extract the keypoints from the entry and place them in a structure.
      for (seqnum = 0; seqnum < entry.keypoints.size(); seqnum++) {
        Keypoint& kp = entry.keypoints[seqnum];

        GST_TRACE_OBJECT (postprocess,
            "Keypoint: '%s' [%.2f x %.2f], confidence %.2f",
            kp.name.c_str(), kp.x, kp.y, kp.confidence);

        // Replace empty spaces otherwise subsequent stream parse call will fail.
        name = g_strdup (kp.name.c_str());
        name = g_strdelimit (name, " ", '.');

        structure = gst_structure_new (name, "confidence", G_TYPE_DOUBLE,
            kp.confidence, "x", G_TYPE_DOUBLE, kp.x, "y", G_TYPE_DOUBLE, kp.y,
            "color", G_TYPE_UINT, kp.color, NULL);
        g_free (name);

        g_value_take_boxed (&value, structure);
        gst_value_array_append_value (&keypoints, &value);
        g_value_reset (&value);
      }

      g_value_unset (&value);
      g_value_init (&value, G_TYPE_STRING);

      length = (entry.links.has_value()) ? entry.links.value().size() : 0;

      // Extract the connections from the entry and place them in a structure.
      for (seqnum = 0; seqnum < length; seqnum++) {
        KeypointLink& connection = entry.links.value()[seqnum];

        GST_TRACE_OBJECT (postprocess, "Link: '%s' <--> '%s'",
            connection.l_kp.name.c_str(), connection.r_kp.name.c_str());

        g_value_set_string (&value, connection.l_kp.name.c_str());
        gst_value_array_append_value (&link, &value);
        g_value_reset (&value);

        g_value_set_string (&value, connection.r_kp.name.c_str());
        gst_value_array_append_value (&link, &value);
        g_value_reset (&value);

        gst_value_array_append_value (&links, &link);
        g_value_reset (&link);
      }

      id = GST_META_ID (postprocess->stage_id, sequence_idx, num);

      structure = gst_structure_new ("pose", "id", G_TYPE_UINT, id,
          "confidence", G_TYPE_DOUBLE, entry.confidence, NULL);

      GST_TRACE_OBJECT (postprocess, "Batch: %u, ID: %X, Confidence: %.1f%%",
          idx, id, entry.confidence);

      gst_structure_set_value (structure, "keypoints", &keypoints);
      gst_structure_set_value (structure, "connections", &links);

      g_value_reset (&keypoints);
      g_value_reset (&links);

      g_value_unset (&value);
      g_value_init (&value, GST_TYPE_STRUCTURE);

      if (entry.xtraparams.has_value ()) {
        GstStructure *xtraparams =
            gst_structure_from_dictionary (entry.xtraparams.value ());

        GValue value = G_VALUE_INIT;
        g_value_init (&value, GST_TYPE_STRUCTURE);
        g_value_take_boxed (&value, xtraparams);

        gst_structure_set_value (structure, "xtraparams", &value);
        g_value_unset (&value);
      }

      g_value_take_boxed (&value, structure);
      gst_value_array_append_value (&poses, &value);
      g_value_unset (&value);
    }

    structure = gst_structure_new_empty ("PoseEstimation");

    gst_structure_set_value (structure, "poses", &poses);
    g_value_reset (&poses);

    val = gst_structure_get_value (info, "timestamp");
    gst_structure_set_value (structure, "timestamp", val);

    val = gst_structure_get_value (info, "sequence-index");
    gst_structure_set_value (structure, "sequence-index", val);

    val = gst_structure_get_value (info, "sequence-num-entries");
    gst_structure_set_value (structure, "sequence-num-entries", val);

    if ((val = gst_structure_get_value (info, "stream-id")))
      gst_structure_set_value (structure, "stream-id", val);

    if ((val = gst_structure_get_value (info, "stream-timestamp")))
      gst_structure_set_value (structure, "stream-timestamp", val);

    if ((val = gst_structure_get_value (info, "parent-id")))
      gst_structure_set_value (structure, "parent-id", val);

    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, structure);

    gst_value_list_append_value (&list, &value);
    g_value_unset (&value);
  }

  g_value_unset (&link);
  g_value_unset (&links);
  g_value_unset (&keypoints);
  g_value_unset (&poses);

  // Serialize the predictions list into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed serialize predictions structure!");
    return FALSE;
  }

  // Increase the length by 1 byte for the '\0' character.
  length = strlen (string) + 1;

  mem = gst_memory_new_wrapped ((GstMemoryFlags) 0, string,
      length, 0, length, string, g_free);
  gst_buffer_append_memory (buffer, mem);

  return TRUE;
}

static gboolean
gst_ml_text_generation_fill_video_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer *buffer, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  guint idx = 0, num = 0, n_entries = 0, color = 0;
  gdouble width = 0.0, height = 0.0;
  gboolean success = FALSE;

  auto& predictions =
      std::any_cast<TextPrediction&>(output);

  success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  // Set the most appropriate font size based on number of results.
  cairo_set_font_size (context, DEFAULT_FONT_SIZE);

  height = DEFAULT_FONT_SIZE;

  for (idx = 0; idx < predictions.size(); idx++) {
    auto& entries = predictions[idx];

    n_entries = (entries.size() < postprocess->n_results) ?
        entries.size() : postprocess->n_results;

    for (num = 0; num < n_entries; num++) {
      TextGeneration& entry = entries[num];

      // Check whether there is enough pixel space for this label entry.
      if (((num + 1) * height) > GST_VIDEO_FRAME_HEIGHT (vframe))
        break;

      GST_TRACE_OBJECT (postprocess, "Batch: %u, contents: %s, "
          "confidence: %.1f%%", idx, entry.contents.c_str(),
          entry.confidence);

      color = entry.color.value();

      // Set text background color.
      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      width = ceil (entry.contents.size() * DEFAULT_FONT_SIZE * 3.0F / 5.0F);

      cairo_rectangle (context, 0, (num * height), width, height);
      cairo_fill (context);

      // Choose the best contrasting color to the background.
      color = EXTRACT_ALPHA_COLOR (color);
      color += ((EXTRACT_RED_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 8;
      color += ((EXTRACT_GREEN_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 16;
      color += ((EXTRACT_BLUE_COLOR (entry.color.value()) > 0x7F) ? 0x00 : 0xFF) << 24;

      cairo_set_source_rgba (context, EXTRACT_FLOAT_BLUE_COLOR (color),
          EXTRACT_FLOAT_GREEN_COLOR (color), EXTRACT_FLOAT_RED_COLOR (color),
          EXTRACT_FLOAT_ALPHA_COLOR (color));

      // (0,0) is at top left corner of the buffer.
      cairo_move_to (context, 0.0, (DEFAULT_FONT_SIZE * (num + 1) * 4.0F / 5.0F));

      // Draw text string.
      cairo_show_text (context, entry.contents.c_str());
      g_return_val_if_fail (
          CAIRO_STATUS_SUCCESS == cairo_status (context), FALSE);

      // Flush to ensure all writing to the surface has been done.
      cairo_surface_flush (surface);
    }
  }

  gst_cairo_draw_cleanup (vframe, surface, context);

  return TRUE;
}

static gboolean
gst_ml_text_generation_fill_text_output (GstMLPostProcess * postprocess,
    std::any& output, GstBuffer *buffer)
{
  GstStructure *structure = NULL;
  GstMemory *memory = NULL;
  gchar *string = NULL;
  GValue list = G_VALUE_INIT, labels = G_VALUE_INIT, value = G_VALUE_INIT;
  guint idx = 0, num = 0, n_entries = 0, id = 0;
  gsize length = 0;

  auto& predictions =
      std::any_cast<TextPrediction&>(output);

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&labels, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_STRUCTURE);

  for (idx = 0; idx < predictions.size(); idx++) {
    TextGenerations& entries = predictions[idx];
    GstStructure *info = NULL;
    const GValue *val = NULL;

    n_entries = (entries.size() < postprocess->n_results) ?
        entries.size() : postprocess->n_results;

    for (num = 0; num < n_entries; num++) {
      TextGeneration& entry = entries[num];

      id = GST_META_ID (postprocess->stage_id, idx, num);

      GST_TRACE_OBJECT (postprocess, "Batch: %u, ID: %X, "
          "Contents: %s,  Confidence: %.1f%%", idx, id,
          entry.contents.c_str(), entry.confidence);

      structure = gst_structure_new ("text", "id", G_TYPE_UINT, id,
          "contents", G_TYPE_STRING, entry.contents.c_str(), "confidence",
          G_TYPE_DOUBLE, entry.confidence, "color", G_TYPE_UINT,
          entry.color.value(), NULL);

      if (entry.xtraparams.has_value ()) {
        GstStructure *xtraparams =
            gst_structure_from_dictionary (entry.xtraparams.value ());

        GValue value = G_VALUE_INIT;
        g_value_init (&value, GST_TYPE_STRUCTURE);
        g_value_take_boxed (&value, xtraparams);

        gst_structure_set_value (structure, "xtraparams", &value);
        g_value_unset (&value);
      }

      g_value_take_boxed (&value, structure);
      gst_value_array_append_value (&labels, &value);
      g_value_reset (&value);
    }

    structure = gst_structure_new_empty ("TextGeneration");

    gst_structure_set_value (structure, "texts", &labels);
    g_value_reset (&labels);

    // Get saved info for the current batch
    info = (GstStructure *) g_ptr_array_index (postprocess->info, idx);

    val = gst_structure_get_value (info, "timestamp");
    gst_structure_set_value (structure, "timestamp", val);

    val = gst_structure_get_value (info, "sequence-index");
    gst_structure_set_value (structure, "sequence-index", val);

    val = gst_structure_get_value (info, "sequence-num-entries");
    gst_structure_set_value (structure, "sequence-num-entries", val);

    g_value_take_boxed (&value, structure);
    gst_value_list_append_value (&list, &value);
    g_value_reset (&value);
  }

  g_value_unset (&labels);
  g_value_unset (&value);

  // Serialize the predictions list into string format.
  string = gst_value_serialize (&list);
  g_value_unset (&list);

  if (string == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed serialize predictions structure!");
    return FALSE;
  }

  // Increase the length by 1 byte for the '\0' character.
  length = strlen (string) + 1;

  memory = gst_memory_new_wrapped ((GstMemoryFlags) 0, string, length, 0,
      length, string, g_free);
  gst_buffer_append_memory (buffer, memory);

  return TRUE;
}

static gboolean
gst_ml_post_process_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_clear_object (&(postprocess->outpool));

  if (postprocess->mode != OUTPUT_MODE_VIDEO)
    return TRUE;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (postprocess, "Failed to parse the allocation caps!");
    return FALSE;
  }

  // Create a new buffer pool.
  if ((pool = gst_ml_post_process_create_pool (postprocess, caps)) == NULL) {
    GST_ERROR_OBJECT (postprocess, "Failed to create buffer pool!");
    return FALSE;
  }

  postprocess->outpool = pool;

  // Get the configured pool properties in order to set in query.
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (config);

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers,
        maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers,
        maxbuffers);

  if (GST_IS_IMAGE_BUFFER_POOL (pool))
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_ml_post_process_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstBufferPool *pool = postprocess->outpool;

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (postprocess, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  if (postprocess->mode == OUTPUT_MODE_VIDEO) {
    if (!gst_buffer_pool_is_active (pool) &&
        !gst_buffer_pool_set_active (pool, TRUE)) {
      GST_ERROR_OBJECT (postprocess, "Failed to activate output buffer pool!");
      return GST_FLOW_ERROR;
    }

    // Input is marked as GAP, nothing to process. Create a GAP output buffer.
    if ((gst_buffer_get_size (inbuffer) == 0) &&
        GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
      *outbuffer = gst_buffer_new ();
      GST_BUFFER_FLAG_SET (*outbuffer, GST_BUFFER_FLAG_GAP);
    }

    if ((*outbuffer == NULL) &&
        gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (postprocess, "Failed to create output buffer!");
      return GST_FLOW_ERROR;
    }
  } else {
    *outbuffer = gst_buffer_new ();
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static gboolean
gst_ml_post_process_sink_event (GstBaseTransform * base, GstEvent * event)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      if (!GST_IS_DETECTION_TYPE (postprocess->type))
        break;

      const GstStructure *structure = gst_event_get_structure (event);

      // Not a supported custom event, pass it to the default handling function.
      if ((structure == NULL) ||
          !gst_structure_has_name (structure, "ml-detection-information"))
        break;

      // Consume downstream information from previous postprocess stage.
      gst_event_unref (event);
      return TRUE;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (base, event);
}

static GstCaps *
gst_ml_post_process_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstCaps *tmplcaps = NULL, *result = NULL;
  guint idx = 0, num = 0, length = 0;

  GST_DEBUG_OBJECT (postprocess, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (postprocess, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    if (NULL == postprocess->module) {
      GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);
      tmplcaps = gst_pad_get_pad_template_caps (pad);
    } else {
      tmplcaps = gst_ml_caps_from_json (postprocess->module->Caps ());
    }
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    tmplcaps = gst_pad_get_pad_template_caps (pad);
  }

  result = gst_caps_new_empty ();
  length = gst_caps_get_size (tmplcaps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = NULL;
    GstCapsFeatures *features = NULL;

    for (num = 0; num < gst_caps_get_size (caps); num++) {
      const GValue *value = NULL;

      structure = gst_caps_get_structure (tmplcaps, idx);
      features = gst_caps_get_features (tmplcaps, idx);

      // Make a copy that will be modified.
      structure = gst_structure_copy (structure);

      // Extract the rate from incoming caps and propagate it to result caps.
      value = gst_structure_get_value (gst_caps_get_structure (caps, num),
          (direction == GST_PAD_SRC) ? "framerate" : "rate");

      // Skip if there is no value or if current caps structure is text.
      if (value != NULL && !gst_structure_has_name (structure, "text/x-raw")) {
        gst_structure_set_value (structure,
            (direction == GST_PAD_SRC) ? "rate" : "framerate", value);
      }

      // If this is already expressed by the existing caps skip this structure.
      if (gst_caps_is_subset_structure_full (result, structure, features)) {
        gst_structure_free (structure);
        continue;
      }

      gst_caps_append_structure_full (result, structure,
          gst_caps_features_copy (features));
    }
  }

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (postprocess, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static GstCaps *
gst_ml_post_process_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstStructure *output = NULL;
  const GValue *value = NULL;
  GstMLInfo mlinfo;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  GST_DEBUG_OBJECT (postprocess, "Trying to fixate output caps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  // Fixate the output format.
  value = gst_structure_get_value (output, "format");

  if (!gst_value_is_fixed (value)) {
    gst_structure_fixate_field (output, "format");
    value = gst_structure_get_value (output, "format");
  }

  GST_DEBUG_OBJECT (postprocess, "Output format fixed to: %s",
      g_value_get_string (value));

  if (gst_structure_has_name (output, "video/x-raw")) {
    gint width = 0, height = 0, par_n = 0, par_d = 0;

    // Fixate output PAR if not already fixated..
    value = gst_structure_get_value (output, "pixel-aspect-ratio");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      gst_structure_set (output, "pixel-aspect-ratio",
          GST_TYPE_FRACTION, 1, 1, NULL);
      value = gst_structure_get_value (output, "pixel-aspect-ratio");
    }

    par_d = gst_value_get_fraction_denominator (value);
    par_n = gst_value_get_fraction_numerator (value);

    GST_DEBUG_OBJECT (postprocess, "Output PAR fixed to: %d/%d", par_n, par_d);

    gst_ml_info_from_caps (&mlinfo, incaps);

    // Retrieve the output width and height.
    value = gst_structure_get_value (output, "width");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      if (GST_IS_DETECTION_TYPE (postprocess->type) ||
          GST_IS_POSE_TYPE (postprocess->type)) {
        width = DEFAULT_VIDEO_WIDTH;
      } else if (GST_IS_CLASSIFICATION_TYPE (postprocess->type) ||
          GST_IS_AUDIO_CLASSIFICATION_TYPE (postprocess->type) ||
          GST_IS_TEXT_GENERATION_TYPE (postprocess->type)) {
        width = GST_ROUND_UP_4 (DEFAULT_FONT_SIZE * MAX_TEXT_LENGTH * 3 / 5);
      } else if (GST_IS_SEGMENTATION_TYPE (postprocess->type) ||
          GST_IS_SUPER_RESOLUTION_TYPE (postprocess->type)) {
        // 2nd dimension correspond to height, 3rd dimension correspond to width.
        width = GST_ROUND_DOWN_16 (mlinfo.tensors[0][2]);
      }
      gst_structure_set (output, "width", G_TYPE_INT, width, NULL);
      value = gst_structure_get_value (output, "width");
    }

    width = g_value_get_int (value);
    value = gst_structure_get_value (output, "height");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      if (GST_IS_DETECTION_TYPE (postprocess->type) ||
          GST_IS_POSE_TYPE (postprocess->type)) {
        height = DEFAULT_VIDEO_HEIGHT;
      } else if (GST_IS_CLASSIFICATION_TYPE (postprocess->type) ||
          GST_IS_AUDIO_CLASSIFICATION_TYPE (postprocess->type) ||
          GST_IS_TEXT_GENERATION_TYPE (postprocess->type)) {
        height = GST_ROUND_UP_4 (DEFAULT_FONT_SIZE * postprocess->n_results);
      } else if (GST_IS_SEGMENTATION_TYPE (postprocess->type) ||
          GST_IS_SUPER_RESOLUTION_TYPE (postprocess->type)) {
        // 2nd dimension correspond to height, 3rd dimension correspond to width.
        height = mlinfo.tensors[0][1];
      }
      gst_structure_set (output, "height", G_TYPE_INT, height, NULL);
      value = gst_structure_get_value (output, "height");
    }

    height = g_value_get_int (value);

    GST_DEBUG_OBJECT (postprocess, "Output width and height fixated to: %dx%d",
        width, height);
  }

  // Fixate any remaining fields.
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (postprocess, "Fixated caps to %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_ml_post_process_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstCaps *modulecaps = NULL;
  GstQuery *query = NULL;
  GstStructure *structure = NULL;
  GstMLInfo ininfo;
  gboolean success = FALSE;
  guint idx = 0;
  GstVideoInfo outinfo;

  modulecaps = gst_ml_caps_from_json (postprocess->module->Caps ());

  if (!gst_caps_can_intersect (incaps, modulecaps)) {
    GST_ELEMENT_ERROR (postprocess, RESOURCE, FAILED, (NULL),
        ("Module caps %" GST_PTR_FORMAT " do not intersect with the "
         "negotiated caps %" GST_PTR_FORMAT "!", modulecaps, incaps));
    return FALSE;
  }

  // Query upstream pre-process plugin about the inference parameters.
  query = gst_query_new_custom (GST_QUERY_CUSTOM,
      gst_structure_new_empty ("ml-preprocess-information"));

  if (gst_pad_peer_query (base->sinkpad, query)) {
    const GstStructure *s = gst_query_get_structure (query);

    gst_structure_get_uint (s, "stage-id", &(postprocess->stage_id));
    GST_DEBUG_OBJECT (postprocess, "Queried stage ID: %u", postprocess->stage_id);
  } else {
    // TODO: Temporary workaround. Need to be addressed proerly.
    // In case of daisycahin it is possible to negotiate wrong stage-id without
    // thrwing an error.
    GST_WARNING_OBJECT (postprocess, "Failed to receive preprocess information!");
  }

  // Free the query instance as it is no longer needed and we are the owners.
  gst_query_unref (query);

  success = gst_ml_post_process_module_set_opts (postprocess,
      postprocess->labels, postprocess->settings);

  if (!success) {
    GST_ELEMENT_ERROR (postprocess, RESOURCE, FAILED, (NULL),
        ("Failed to set module options!"));
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&ininfo, incaps)) {
    GST_ELEMENT_ERROR (postprocess, CORE, CAPS, (NULL),
        ("Failed to get input ML info from caps %" GST_PTR_FORMAT "!", incaps));
    return FALSE;
  }

  if (postprocess->mlinfo != NULL)
    gst_ml_info_free (postprocess->mlinfo);

  postprocess->mlinfo = gst_ml_info_copy (&ininfo);

  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (outcaps, 0);

  if (gst_structure_has_name (structure, "video/x-raw")) {
    postprocess->mode = OUTPUT_MODE_VIDEO;

    if (!gst_video_info_from_caps (&outinfo, outcaps)) {
      GST_ERROR_OBJECT (postprocess, "Failed to get output video info from caps"
          " %" GST_PTR_FORMAT "!", outcaps);
      return FALSE;
    }

    if (postprocess->vinfo != NULL)
      gst_video_info_free (postprocess->vinfo);

    postprocess->vinfo = gst_video_info_copy (&outinfo);
  } else if (gst_structure_has_name (structure, "text/x-raw")) {
    postprocess->mode = OUTPUT_MODE_TEXT;
  }

  if ((postprocess->mode == OUTPUT_MODE_VIDEO) &&
      (GST_ML_INFO_TENSOR_DIM (postprocess->mlinfo, 0, 0) > 1)) {
    GST_ELEMENT_ERROR (postprocess, CORE, FAILED, (NULL),
        ("Batched input tensors with video output is not supported!"));
    return FALSE;
  }

  if (GST_IS_DETECTION_TYPE (postprocess->type)) {
    // Inform any ML pre-process downstream about it's ROI stage ID.
    structure = gst_structure_new ("ml-detection-information", "stage-id",
        G_TYPE_UINT, postprocess->stage_id, NULL);

    GST_DEBUG_OBJECT (postprocess, "Send stage ID %u", postprocess->stage_id);

    success = gst_pad_push_event (GST_BASE_TRANSFORM_SRC_PAD (postprocess),
        gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, structure));

    if (!success) {
      // TODO: Temporary workaround. Need to be addressed proerly.
      // In case of daisycahin it is possible to negotiate wrong stage-id without
      // thrwing an error.
      GST_WARNING_OBJECT (postprocess, "Failed to send ML info downstream!");
    }
  }

  GST_DEBUG_OBJECT (postprocess, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (postprocess, "Output caps: %" GST_PTR_FORMAT, outcaps);

  gst_base_transform_set_passthrough (base, FALSE);
  return TRUE;
}

static GstStateChangeReturn
gst_ml_video_post_process_change_state (GstElement * element,
    GstStateChange transition)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (element);
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      if (DEFAULT_PROP_MODULE == postprocess->mdlenum) {
        GST_ELEMENT_ERROR (postprocess, RESOURCE, NOT_FOUND, (NULL),
            ("Module name not set, automatic module pick up not supported!"));
        return GST_STATE_CHANGE_FAILURE;
      }

      eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_ML_MODULES));
      evalue = g_enum_get_value (eclass, postprocess->mdlenum);

      gst_ml_post_process_module_free (postprocess);

      if (!gst_ml_post_process_module_new (postprocess, evalue->value_nick)) {
        GST_ELEMENT_ERROR (postprocess, RESOURCE, FAILED, (NULL),
            ("Module creation failed!"));
        return GST_STATE_CHANGE_FAILURE;
      }

      postprocess->type =
          gst_ml_module_caps_get_type (postprocess->module->Caps ());
      if (GST_IS_INVALID_TYPE (postprocess->type)) {
        GST_ELEMENT_ERROR (postprocess, RESOURCE, FAILED, (NULL),
            ("Failed to get module type!"));
        return GST_STATE_CHANGE_FAILURE;
      }

      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    GST_ERROR_OBJECT (postprocess, "Failure");
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ml_post_process_module_free (postprocess);
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_ml_post_process_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (base);
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;
  GstVideoFrame vframe = { 0, };
  std::any output;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  // Perform pre-processing on the input buffer.
  time = gst_util_get_timestamp ();

  // Clear previously stored values.
  g_ptr_array_remove_range (postprocess->info, 0, postprocess->info->len);

  // Set the maximum number of predictions based on the batch size.
  if (GST_IS_DETECTION_TYPE (postprocess->type)) {
    output = DetectionPrediction();
  } else if (GST_IS_CLASSIFICATION_TYPE (postprocess->type)) {
    output = ImageClassPrediction();
  } else if (GST_IS_AUDIO_CLASSIFICATION_TYPE (postprocess->type)) {
    output = AudioClassPrediction();
  } else if (GST_IS_POSE_TYPE (postprocess->type)) {
    output = PosePrediction();
  } else if (GST_IS_TEXT_GENERATION_TYPE (postprocess->type)) {
    output = TextPrediction();
  } else if (GST_IS_SEGMENTATION_TYPE (postprocess->type) ||
      GST_IS_SUPER_RESOLUTION_TYPE (postprocess->type)) {

    if (!gst_video_frame_map (&vframe, postprocess->vinfo, outbuffer,
          (GstMapFlags) (GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
      GST_ERROR_OBJECT (postprocess, "Failed to map output buffer!");
      return GST_FLOW_ERROR;
    }

#ifdef HAVE_LINUX_DMA_BUF_H
    if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
      struct dma_buf_sync bufsync;
      gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

      bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

      if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
        GST_WARNING_OBJECT (postprocess, "DMA IOCTL SYNC START failed!");
    }
#endif // HAVE_LINUX_DMA_BUF_H

    VideoFrame frame;

    if (gst_video_frame_to_module_frame (vframe, frame)) {
      output = frame;
    } else {
      GST_ERROR_OBJECT (postprocess, "Convert video frame failed!");
      return GST_FLOW_ERROR;
    }
  }

  // Call the submodule process funtion.
  success = gst_ml_post_process_module_execute (postprocess, inbuffer, output);

  if (GST_IS_SEGMENTATION_TYPE (postprocess->type) ||
      GST_IS_SUPER_RESOLUTION_TYPE (postprocess->type)) {
#ifdef HAVE_LINUX_DMA_BUF_H
    if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
      struct dma_buf_sync bufsync;
      gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));

      bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

      if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
        GST_WARNING_OBJECT (postprocess, "DMA IOCTL SYNC END failed!");
    }
#endif // HAVE_LINUX_DMA_BUF_H

    gst_video_frame_unmap (&vframe);
  }

  if (!success) {
    GST_ERROR_OBJECT (postprocess, "Failed to process tensors!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (postprocess, "Processing took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  time = gst_util_get_timestamp ();

  if (postprocess->mode == OUTPUT_MODE_VIDEO) {
    GstVideoFrame vframe = {0};

    if (!gst_video_frame_map (&vframe, postprocess->vinfo, outbuffer,
          (GstMapFlags) (GST_MAP_READWRITE  | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
      GST_ERROR_OBJECT (postprocess, "Failed to map output video buffer!");
      return GST_FLOW_ERROR;
    }

    if (GST_IS_DETECTION_TYPE (postprocess->type)) {
      success = gst_ml_video_detection_fill_video_output (postprocess,
          output, outbuffer, &vframe);
    } else if (GST_IS_CLASSIFICATION_TYPE (postprocess->type)) {
      success = gst_ml_video_classification_fill_video_output (postprocess,
          output, outbuffer, &vframe);
    } else if (GST_IS_AUDIO_CLASSIFICATION_TYPE (postprocess->type)) {
      success = gst_ml_audio_classification_fill_video_output (postprocess,
          output, outbuffer, &vframe);
    } else if (GST_IS_POSE_TYPE (postprocess->type)) {
      success = gst_ml_video_pose_fill_video_output (postprocess,
          output, outbuffer, &vframe);
    } else if (GST_IS_TEXT_GENERATION_TYPE (postprocess->type)) {
      success = gst_ml_text_generation_fill_video_output (postprocess,
          output, outbuffer, &vframe);
    }

    gst_video_frame_unmap (&vframe);

  } else if (postprocess->mode == OUTPUT_MODE_TEXT) {
    if (GST_IS_DETECTION_TYPE (postprocess->type)) {
      success = gst_ml_video_detection_fill_text_output (postprocess,
          output, outbuffer);
    } else if (GST_IS_CLASSIFICATION_TYPE (postprocess->type)) {
      success = gst_ml_video_classification_fill_text_output (postprocess,
          output, outbuffer);
    } else if (GST_IS_AUDIO_CLASSIFICATION_TYPE (postprocess->type)) {
      success = gst_ml_audio_classification_fill_text_output (postprocess,
          output, outbuffer);
    } else if (GST_IS_POSE_TYPE (postprocess->type)) {
      success = gst_ml_video_pose_fill_text_output (postprocess,
          output, outbuffer);
    } else if (GST_IS_TEXT_GENERATION_TYPE (postprocess->type)) {
      success = gst_ml_text_generation_fill_text_output (postprocess,
          output, outbuffer);
    }
  }

  if (!success) {
    GST_ERROR_OBJECT (postprocess, "Failed to fill output buffer!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (postprocess, "Postprocess took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_post_process_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (object);

  switch (prop_id) {
    case PROP_MODULE:
      postprocess->mdlenum = g_value_get_enum (value);
      break;
    case PROP_LABELS:
      g_free (postprocess->labels);
      postprocess->labels = g_strdup (g_value_get_string (value));
      break;
    case PROP_NUM_RESULTS:
      postprocess->n_results = g_value_get_uint (value);
      break;
    case PROP_SETTINGS:
      g_free (postprocess->settings);
      postprocess->settings = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_post_process_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (object);

  switch (prop_id) {
     case PROP_MODULE:
      g_value_set_enum (value, postprocess->mdlenum);
      break;
    case PROP_LABELS:
      g_value_set_string (value, postprocess->labels);
      break;
    case PROP_NUM_RESULTS:
      g_value_set_uint (value, postprocess->n_results);
      break;
    case PROP_SETTINGS:
      g_value_set_string (value, postprocess->settings);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_post_process_finalize (GObject * object)
{
  GstMLPostProcess *postprocess = GST_ML_POST_PROCESS (object);
  guint idx = 0;

  g_ptr_array_free (postprocess->info, TRUE);

  gst_ml_post_process_module_free (postprocess);

  if (postprocess->mlinfo != NULL)
    gst_ml_info_free (postprocess->mlinfo);

  if (postprocess->vinfo != NULL)
    gst_video_info_free (postprocess->vinfo);

  if (postprocess->outpool != NULL)
    gst_object_unref (postprocess->outpool);

  if (postprocess->labels)
    g_free (postprocess->labels);

  if (postprocess->settings)
    g_free (postprocess->settings);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (postprocess));
}

static void
gst_ml_post_process_class_init (GstMLPostProcessClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_ml_post_process_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_ml_post_process_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_ml_post_process_finalize);

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_enum ("module", "Module",
          "Module name that is going to be used for processing the tensors",
          GST_TYPE_ML_MODULES, DEFAULT_PROP_MODULE, static_cast <GParamFlags> (
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject, PROP_LABELS,
      g_param_spec_string ("labels", "Labels",
          "Labels filename", DEFAULT_PROP_LABELS, static_cast <GParamFlags> (
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject, PROP_NUM_RESULTS,
      g_param_spec_uint ("results", "Results",
          "Number of results to display", 0, 50, DEFAULT_PROP_NUM_RESULTS,
          static_cast <GParamFlags> (
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject, PROP_SETTINGS,
      g_param_spec_string ("settings", "Settings",
          "Settings used by the chosen module for post-processing. "
          "Applicable only for some modules.",
          DEFAULT_PROP_SETTINGS, static_cast <GParamFlags> (
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element,
      "Machine Learning postprocess", "Filter/Effect/Converter",
      "Machine Learning plugin for postprocess", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_post_process_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_post_process_src_template ());

  element->change_state =
      GST_DEBUG_FUNCPTR (gst_ml_video_post_process_change_state);

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_post_process_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_post_process_prepare_output_buffer);

  base->sink_event = GST_DEBUG_FUNCPTR (gst_ml_post_process_sink_event);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_post_process_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_ml_post_process_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_post_process_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_post_process_transform);

  GST_DEBUG_CATEGORY_INIT (gst_ml_post_process_debug, "qtimlpostprocess", 0,
      "QTI ML post process plugin");
}

static void
gst_ml_post_process_init (GstMLPostProcess * postprocess)
{
  postprocess->mode = OUTPUT_MODE_VIDEO;

  postprocess->outpool = NULL;
  postprocess->vinfo = NULL;
  postprocess->mlinfo = NULL;

  postprocess->module = NULL;
  postprocess->handle = NULL;

  postprocess->stage_id = 0;

  postprocess->info = g_ptr_array_new ();

  postprocess->mdlenum = DEFAULT_PROP_MODULE;
  postprocess->labels = DEFAULT_PROP_LABELS;
  postprocess->n_results = DEFAULT_PROP_NUM_RESULTS;
  postprocess->settings = DEFAULT_PROP_SETTINGS;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (postprocess), TRUE);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlpostprocess", GST_RANK_NONE,
      GST_TYPE_ML_POST_PROCESS);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlpostprocess,
    "QTI Machine Learning plugin for post processing",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
