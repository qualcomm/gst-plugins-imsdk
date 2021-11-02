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
 * ​​​​​Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <dlfcn.h>
#include <gst/gstutils.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/ml/gstmlpool.h>
#include <gst/video/gstimagepool.h>

#include "mlvposenet.h"
#include "modules/ml-video-posenet-module.h"

#define GST_CAT_DEFAULT gst_ml_video_posenet_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_video_posenet_debug);

#define gst_ml_video_posenet_parent_class parent_class
G_DEFINE_TYPE (GstMLVideoPosenet, gst_ml_video_posenet,
    GST_TYPE_BASE_TRANSFORM);

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_ML_VIDEO_POSENET_VIDEO_FORMATS \
    "{ BGRA, RGBA, BGRx, xRGB, BGR16 }"

#define GST_ML_VIDEO_POSENET_SRC_CAPS                           \
  "video/x-raw, "                                               \
  "format = (string) " GST_ML_VIDEO_POSENET_VIDEO_FORMATS  "; " \
  "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "              \
  "format = (string) " GST_ML_VIDEO_POSENET_VIDEO_FORMATS

#define GST_ML_VIDEO_POSENET_SINK_CAPS \
    "neural-network/tensors"

#define DEFAULT_PROP_MODULE NULL
#define DEFAULT_PROP_NUM_RESULTS 5
#define DEFAULT_PROP_THRESHOLD 0.5

#define DEFAULT_MIN_BUFFERS 2
#define DEFAULT_MAX_BUFFERS 10
#define DEFAULT_VIDEO_WIDTH 320
#define DEFAULT_VIDEO_HEIGHT 240

#define POSENET_DOT_RADIUS 3.0f
#define POSENET_LINE_WIDTH 2.0f

#define POSENET_WIDTH 641.0
#define POSENET_HEIGHT 481.0

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))

enum
{
  PROP_0,
  PROP_MODULE,
  PROP_NUM_RESULTS,
  PROP_THRESHOLD,
};

// Indices for the keypoints for one segment
typedef struct
{
  size_t a;
  size_t b;
} Segment;
static const Segment segments[] = {
  {POSENET_KP_LEFT_SHOULDER, POSENET_KP_LEFT_ELBOW},
  {POSENET_KP_LEFT_ELBOW, POSENET_KP_LEFT_WRIST},
  {POSENET_KP_LEFT_SHOULDER, POSENET_KP_LEFT_HIP},
  {POSENET_KP_LEFT_HIP, POSENET_KP_LEFT_KNEE},
  {POSENET_KP_LEFT_KNEE, POSENET_KP_LEFT_ANKLE},
  {POSENET_KP_RIGHT_SHOULDER, POSENET_KP_RIGHT_ELBOW},
  {POSENET_KP_RIGHT_ELBOW, POSENET_KP_RIGHT_WRIST},
  {POSENET_KP_RIGHT_SHOULDER, POSENET_KP_RIGHT_HIP},
  {POSENET_KP_RIGHT_HIP, POSENET_KP_RIGHT_KNEE},
  {POSENET_KP_RIGHT_KNEE, POSENET_KP_RIGHT_ANKLE},
  {POSENET_KP_LEFT_SHOULDER, POSENET_KP_RIGHT_SHOULDER},
  {POSENET_KP_LEFT_HIP, POSENET_KP_RIGHT_HIP},
};

static GstStaticCaps gst_ml_video_posenet_static_sink_caps =
  GST_STATIC_CAPS (GST_ML_VIDEO_POSENET_SINK_CAPS);

static GstStaticCaps gst_ml_video_posenet_static_src_caps =
  GST_STATIC_CAPS (GST_ML_VIDEO_POSENET_SRC_CAPS);

/**
 * GstMLModule:
 * @libhandle: the library handle
 * @instance: instance of the tensor processing module
 *
 * @init: Initilizes an instance of the module.
 * @deinit: Deinitilizes the instance of the module.
 * @process: Decode the tensors inside the buffer into poses.
 *
 * Machine learning interface for post-processing module.
 */
struct _GstMLModule
{
  gpointer libhandle;
  gpointer instance;

  gpointer (*init) ();
  void (*deinit) (gpointer instance);

  gboolean (*process) (gpointer instance, GstBuffer * buffer, GList ** poses);
};

static void
gst_ml_module_free (GstMLModule * module)
{
  if (NULL == module)
    return;

  if (module->instance != NULL)
    module->deinit (module->instance);

  if (module->libhandle != NULL)
    dlclose (module->libhandle);

  g_slice_free (GstMLModule, module);
}

static GstMLModule *
gst_ml_module_new (const gchar * libname)
{
  GstMLModule *module = NULL;
  gchar *location = NULL;

  location = g_strdup_printf ("/usr/lib/gstreamer-1.0/ml/modules/lib%s.so",
      libname);

  module = g_slice_new0 (GstMLModule);
  g_return_val_if_fail (module != NULL, NULL);

  if ((module->libhandle = dlopen (location, RTLD_NOW)) == NULL) {
    GST_ERROR ("Failed to open %s module library, error: %s!",
        libname, dlerror ());

    g_free (location);
    gst_ml_module_free (module);

    return NULL;
  }

  g_free (location);

  module->init = dlsym (module->libhandle, "gst_ml_video_posenet_module_init");
  module->deinit = dlsym (module->libhandle,
      "gst_ml_video_posenet_module_deinit");
  module->process = dlsym (module->libhandle,
      "gst_ml_video_posenet_module_process");

  if (!module->init || !module->deinit || !module->process) {
    GST_ERROR ("Failed to load %s library symbols, error: %s!",
        libname, dlerror ());
    gst_ml_module_free (module);
    return NULL;
  }

  if ((module->instance = module->init ()) == NULL) {
    GST_ERROR ("Failed to initilize %s module library!", libname);
    gst_ml_module_free (module);
    return NULL;
  }

  return module;
}

static void
gst_ml_pose_free (GstPose * pose)
{
  g_free (pose);
}

static GstCaps *
gst_ml_video_posenet_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_posenet_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_video_posenet_src_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_posenet_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_video_posenet_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_video_posenet_sink_caps ());
}

static GstPadTemplate *
gst_ml_video_posenet_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_video_posenet_src_caps ());
}

static gboolean
caps_has_feature (const GstCaps * caps, const gchar * feature)
{
  guint idx = 0;

  while (idx != gst_caps_get_size (caps)) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    // Skip ANY caps and return immediately if feature is present.
    if (!gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;

    idx++;
  }
  return FALSE;
}

static GstBufferPool *
gst_ml_video_posenet_create_pool (GstMLVideoPosenet * posenet, GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstBufferPool *pool = NULL;
  guint size = 0;

  if (gst_structure_has_name (structure, "video/x-raw")) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (posenet, "Invalid caps %" GST_PTR_FORMAT, caps);
      return NULL;
    }
    // If downstream allocation query supports GBM, allocate gbm memory.
    if (caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      GST_INFO_OBJECT (posenet, "Uses GBM memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
    } else {
      GST_INFO_OBJECT (posenet, "Uses ION memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
    }

    if (NULL == pool) {
      GST_ERROR_OBJECT (posenet, "Failed to create buffer pool!");
      return NULL;
    }

    size = GST_VIDEO_INFO_SIZE (&info);
  }

  structure = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (structure, caps, size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (GST_IS_IMAGE_BUFFER_POOL (pool)) {
    GstAllocator *allocator = gst_fd_allocator_new ();

    gst_buffer_pool_config_set_allocator (structure, allocator, NULL);
    g_object_unref (allocator);

    gst_buffer_pool_config_add_option (structure,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  if (!gst_buffer_pool_set_config (pool, structure)) {
    GST_WARNING_OBJECT (posenet, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  return pool;
}

static gboolean
gst_ml_video_posenet_fill_video_output (GstMLVideoPosenet * posenet,
    GList * poses, GstBuffer * buffer)
{
  GstVideoMeta *vmeta = NULL;
  GstMapInfo memmap;

  cairo_format_t format;
  cairo_surface_t *surface = NULL;
  cairo_t *context = NULL;

  gboolean result = FALSE;

  if (!(vmeta = gst_buffer_get_video_meta (buffer))) {
    GST_ERROR_OBJECT (posenet, "Output buffer has no meta!");
    return FALSE;
  }

  switch (vmeta->format) {
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
      format = CAIRO_FORMAT_ARGB32;
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
      format = CAIRO_FORMAT_RGB24;
      break;
    case GST_VIDEO_FORMAT_BGR16:
      format = CAIRO_FORMAT_RGB16_565;
      break;
    default:
      GST_ERROR_OBJECT (posenet, "Unsupported format: %s!",
          gst_video_format_to_string (vmeta->format));
      return FALSE;
  }

  // Map buffer memory blocks.
  if (!gst_buffer_map_range (buffer, 0, 1, &memmap, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (posenet, "Failed to map buffer memory block!");
    return FALSE;
  }

  surface = cairo_image_surface_create_for_data (memmap.data, format,
      vmeta->width, vmeta->height, vmeta->stride[0]);
  if (surface == NULL) {
    goto fail_with_memmap;
  }

  context = cairo_create (surface);
  if (context == NULL) {
    goto fail_with_surface;
  }
  // Initialize the surface since the memory buffer may contain "random" data
  cairo_set_operator (context, CAIRO_OPERATOR_CLEAR);
  cairo_paint (context);

  // Set operator to draw over the source.
  cairo_set_operator (context, CAIRO_OPERATOR_OVER);

  // Select font.
  cairo_select_font_face (context, "@cairo:Georgia",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

  {
    // Set font options.
    cairo_font_options_t *options = cairo_font_options_create ();
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_DEFAULT);
    cairo_set_font_options (context, options);
    cairo_font_options_destroy (options);
  }

  // Adjust the keypoints with extracted source aspect ratio.
  gdouble x_coeficient = 1.0;
  gdouble y_coeficient = 1.0;
  gdouble ratio = 1.0;
  gst_util_fraction_to_double(posenet->sar_n, posenet->sar_d, &ratio);
  if (posenet->sar_n > posenet->sar_d) {
    x_coeficient = 1.0 / POSENET_WIDTH;
    y_coeficient = 1.0 / (POSENET_WIDTH / ratio);
  } else if (posenet->sar_n < posenet->sar_d) {
    x_coeficient = 1.0 / (POSENET_HEIGHT * ratio);
    y_coeficient = 1.0 / POSENET_HEIGHT;
  } else {
    x_coeficient = 1.0 / POSENET_WIDTH;
    y_coeficient = 1.0 / POSENET_HEIGHT;
  }
  x_coeficient *= vmeta->width;
  y_coeficient *= vmeta->height;

  for (guint idx = 0; idx < g_list_length (poses); ++idx) {
    const GstPose *pose = NULL;

    // Break immediately if we reach the number of results limit.
    if (idx >= posenet->n_results)
      break;

    // Extract the pose data
    pose = g_list_nth_data (poses, idx);

    // Set color.
    cairo_set_source_rgb (context, 0.0, 0.5, 0.0);
    cairo_set_line_width (context, POSENET_LINE_WIDTH);

    // Draw the keypoints
    for (size_t i = 0; i < POSENET_KP_COUNT; ++i) {
      if (pose->keypoint[i].score > posenet->threshold) {
        GST_DEBUG_OBJECT (posenet, "Point %zu at %.2f / %d, %.2f / %d, "
            " (score = %0.4f)", i,
            pose->keypoint[i].x * x_coeficient, vmeta->width,
            pose->keypoint[i].y * y_coeficient, vmeta->height,
            pose->keypoint[i].score);
        cairo_arc (context,
            pose->keypoint[i].x * x_coeficient,
            pose->keypoint[i].y * y_coeficient, POSENET_DOT_RADIUS, 0,
            2 * M_PI);
        cairo_fill (context);
        if (cairo_status (context) != CAIRO_STATUS_SUCCESS) {
          goto fail_with_context;
        }
      } else {
        GST_DEBUG_OBJECT (posenet, "Skipping Point %zu (score = %0.4f)", i,
            pose->keypoint[i].score);
      }
    }

    // Draw the segments
    for (size_t i = 0; i < ARRAY_LENGTH (segments); ++i) {
      if ((pose->keypoint[segments[i].a].score < posenet->threshold) ||
          (pose->keypoint[segments[i].b].score < posenet->threshold)) {
        continue;
      }
      cairo_move_to (context,
          pose->keypoint[segments[i].a].x * x_coeficient,
          pose->keypoint[segments[i].a].y * y_coeficient);
      cairo_line_to (context,
          pose->keypoint[segments[i].b].x * x_coeficient,
          pose->keypoint[segments[i].b].y * y_coeficient);
    }
    cairo_stroke (context);
    if (cairo_status (context) != CAIRO_STATUS_SUCCESS) {
      goto fail_with_context;
    }
    // Flush to ensure all writing to the surface has been done.
    cairo_surface_flush (surface);
  }

  result = TRUE;

fail_with_context:
  cairo_destroy (context);
fail_with_surface:
  cairo_surface_destroy (surface);
fail_with_memmap:

  // Unmap buffer memory blocks.
  gst_buffer_unmap (buffer, &memmap);

  return result;
}

static gboolean
gst_ml_video_posenet_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLVideoPosenet *posenet = GST_ML_VIDEO_POSENET (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (posenet, "Failed to parse the allocation caps!");
    return FALSE;
  }
  // Invalidate the cached pool if there is an allocation_query.
  if (posenet->outpool)
    gst_object_unref (posenet->outpool);

  // Create a new buffer pool.
  if ((pool = gst_ml_video_posenet_create_pool (posenet, caps)) == NULL) {
    GST_ERROR_OBJECT (posenet, "Failed to create buffer pool!");
    return FALSE;
  }

  posenet->outpool = pool;

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
    gst_query_add_allocation_pool (query, pool, size, minbuffers, maxbuffers);

  if (GST_IS_IMAGE_BUFFER_POOL (pool))
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_posenet_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLVideoPosenet *posenet = GST_ML_VIDEO_POSENET (base);
  GstBufferPool *pool = posenet->outpool;
  GstFlowReturn ret = GST_FLOW_OK;

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (posenet, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (posenet, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  ret = gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (posenet, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }
  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_ml_video_posenet_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLVideoPosenet *posenet = GST_ML_VIDEO_POSENET (base);
  GstCaps *result = NULL;
  const GValue *value = NULL;

  GST_DEBUG_OBJECT (posenet, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (posenet, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  }
  // Extract the rate and propagate it to result caps.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0),
      (direction == GST_PAD_SRC) ? "framerate" : "rate");

  if (value != NULL) {
    gint idx = 0, length = 0;

    result = gst_caps_make_writable (result);
    length = gst_caps_get_size (result);

    for (idx = 0; idx < length; idx++) {
      GstStructure *structure = gst_caps_get_structure (result, idx);
      gst_structure_set_value (structure,
          (direction == GST_PAD_SRC) ? "rate" : "framerate", value);
    }
  }

  if (filter != NULL) {
    GstCaps *intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (posenet, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static GstCaps *
gst_ml_video_posenet_fixate_caps (GstBaseTransform * base,
    GstPadDirection G_GNUC_UNUSED direction, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoPosenet *posenet = GST_ML_VIDEO_POSENET (base);
  GstStructure *output = NULL;
  const GValue *value = NULL;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  GST_DEBUG_OBJECT (posenet, "Trying to fixate output caps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  // Fixate the output format.
  value = gst_structure_get_value (output, "format");

  if (!gst_value_is_fixed (value)) {
    gst_structure_fixate_field (output, "format");
    value = gst_structure_get_value (output, "format");
  }

  GST_DEBUG_OBJECT (posenet, "Output format fixed to: %s",
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

    GST_DEBUG_OBJECT (posenet, "Output PAR fixed to: %d/%d", par_n, par_d);

    // Retrieve the output width and height.
    value = gst_structure_get_value (output, "width");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      width = DEFAULT_VIDEO_WIDTH;
      gst_structure_set (output, "width", G_TYPE_INT, width, NULL);
      value = gst_structure_get_value (output, "width");
    }

    width = g_value_get_int (value);
    value = gst_structure_get_value (output, "height");

    if ((NULL == value) || !gst_value_is_fixed (value)) {
      height = DEFAULT_VIDEO_HEIGHT;
      gst_structure_set (output, "height", G_TYPE_INT, height, NULL);
      value = gst_structure_get_value (output, "height");
    }

    height = g_value_get_int (value);

    GST_DEBUG_OBJECT (posenet, "Output width and height fixated to: %dx%d",
        width, height);
  }

  GST_DEBUG_OBJECT (posenet, "Fixated caps to %" GST_PTR_FORMAT, outcaps);

  return outcaps;
}

static gboolean
gst_ml_video_posenet_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoPosenet *posenet = GST_ML_VIDEO_POSENET (base);
  GstMLModule *module = NULL;
  GstStructure *structure = NULL;
  GstMLInfo ininfo;

  if (NULL == posenet->modname) {
    GST_ERROR_OBJECT (posenet, "Module not set!");
    return FALSE;
  }

  module = gst_ml_module_new (posenet->modname);
  if (NULL == module) {
    GST_ERROR_OBJECT (posenet, "Failed to create processing module!");
    return FALSE;
  }

  gst_ml_module_free (posenet->module);
  posenet->module = module;

  if (!gst_ml_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (posenet, "Failed to get input ML info from caps %"
        GST_PTR_FORMAT "!", incaps);
    return FALSE;
  }

  if (posenet->mlinfo != NULL)
    gst_ml_info_free (posenet->mlinfo);

  posenet->mlinfo = gst_ml_info_copy (&ininfo);

  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (outcaps, 0);

  // Get the input caps structure in order to extract source aspect ratio.
  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_has_field (structure, "aspect-ratio")) {
    const GValue *value = gst_structure_get_value (structure, "aspect-ratio");

    posenet->sar_n = gst_value_get_fraction_numerator (value);
    posenet->sar_d = gst_value_get_fraction_denominator (value);
  } else {
    posenet->sar_n = posenet->sar_d = 1;
  }

  gst_base_transform_set_passthrough (base, FALSE);

  GST_DEBUG_OBJECT (posenet, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (posenet, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_posenet_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMLVideoPosenet *posenet = GST_ML_VIDEO_POSENET (base);
  GList *poses = NULL;
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;
  gboolean success = FALSE;
  guint n_blocks = 0;

  g_return_val_if_fail (posenet->module != NULL, GST_FLOW_ERROR);

  n_blocks = gst_buffer_n_memory (inbuffer);
  if (n_blocks != posenet->mlinfo->n_tensors) {
    GST_ERROR_OBJECT (posenet, "Input buffer has %u memory blocks but "
        "negotiated caps require %u!", n_blocks, posenet->mlinfo->n_tensors);
    return GST_FLOW_ERROR;
  }

  n_blocks = gst_buffer_get_n_meta (inbuffer, GST_ML_TENSOR_META_API_TYPE);
  if (n_blocks != posenet->mlinfo->n_tensors) {
    GST_ERROR_OBJECT (posenet, "Input buffer has %u tensor metas but "
        "negotiated caps require %u!", n_blocks, posenet->mlinfo->n_tensors);
    return GST_FLOW_ERROR;
  }

  if (gst_buffer_n_memory (outbuffer) == 0) {
    GST_ERROR_OBJECT (posenet, "Output buffer has no memory blocks!");
    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();

  // Call the submodule process funtion.
  success = posenet->module->process (posenet->module->instance, inbuffer,
      &poses);

  if (!success) {
    GST_ERROR_OBJECT (posenet, "Failed to process tensors!");
    g_list_free_full (poses, (GDestroyNotify) gst_ml_pose_free);
    return GST_FLOW_ERROR;
  }

  success = gst_ml_video_posenet_fill_video_output (posenet, poses, outbuffer);

  g_list_free_full (poses, (GDestroyNotify) gst_ml_pose_free);

  if (!success) {
    GST_ERROR_OBJECT (posenet, "Failed to fill output buffer!");
    return GST_FLOW_ERROR;
  }

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_LOG_OBJECT (posenet, "Object posenet took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_video_posenet_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLVideoPosenet *posenet = GST_ML_VIDEO_POSENET (object);

  switch (prop_id) {
    case PROP_MODULE:
      g_free (posenet->modname);
      posenet->modname = g_strdup (g_value_get_string (value));
      break;
    case PROP_NUM_RESULTS:
      posenet->n_results = g_value_get_uint (value);
      break;
    case PROP_THRESHOLD:
      posenet->threshold = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_posenet_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLVideoPosenet *posenet = GST_ML_VIDEO_POSENET (object);

  switch (prop_id) {
    case PROP_MODULE:
      g_value_set_string (value, posenet->modname);
      break;
    case PROP_NUM_RESULTS:
      g_value_set_uint (value, posenet->n_results);
      break;
    case PROP_THRESHOLD:
      g_value_set_double (value, posenet->threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_posenet_finalize (GObject * object)
{
  GstMLVideoPosenet *posenet = GST_ML_VIDEO_POSENET (object);

  gst_ml_module_free (posenet->module);

  if (posenet->mlinfo != NULL)
    gst_ml_info_free (posenet->mlinfo);

  if (posenet->outpool != NULL)
    gst_object_unref (posenet->outpool);

  g_free (posenet->modname);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (posenet));
}

static void
gst_ml_video_posenet_class_init (GstMLVideoPosenetClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_video_posenet_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_video_posenet_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_video_posenet_finalize);

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_string ("module", "Module",
          "Module name that is going to be used for processing the tensors",
          DEFAULT_PROP_MODULE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_NUM_RESULTS,
      g_param_spec_uint ("results", "Results",
          "Number of results to display", 0, 10, DEFAULT_PROP_NUM_RESULTS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_THRESHOLD,
      g_param_spec_double ("threshold", "Threshold",
          "Confidence threshold", 0.0, 1.0, DEFAULT_PROP_THRESHOLD,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Machine Learning Posenet", "Filter/Effect/Converter",
      "Machine Learning plugin for Posenet", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_video_posenet_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_video_posenet_src_template ());

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_video_posenet_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_posenet_prepare_output_buffer);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_posenet_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_ml_video_posenet_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_video_posenet_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_video_posenet_transform);
}

static void
gst_ml_video_posenet_init (GstMLVideoPosenet * posenet)
{
  posenet->sar_n = 0;
  posenet->sar_d = 0;

  posenet->outpool = NULL;
  posenet->module = NULL;

  posenet->modname = DEFAULT_PROP_MODULE;
  posenet->n_results = DEFAULT_PROP_NUM_RESULTS;
  posenet->threshold = DEFAULT_PROP_THRESHOLD;

  GST_DEBUG_CATEGORY_INIT (gst_ml_video_posenet_debug, "qtimlvposenet", 0,
      "QTI ML Posenet plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlvposenet", GST_RANK_NONE,
      GST_TYPE_ML_VIDEO_POSENET);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlvposenet,
    "QTI Machine Learning plugin for Posenet post-processing",
    plugin_init,
    PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_SUMMARY, PACKAGE_ORIGIN)
