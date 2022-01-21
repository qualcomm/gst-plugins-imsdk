/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "videocomposer.h"

#include <gst/video/gstimagepool.h>

#include "videocomposersinkpad.h"

#define GST_CAT_DEFAULT gst_video_composer_debug
GST_DEBUG_CATEGORY_STATIC (gst_video_composer_debug);

static void gst_video_composer_child_proxy_init (gpointer g_iface, gpointer data);

#define gst_video_composer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVideoComposer, gst_video_composer,
     GST_TYPE_AGGREGATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
         gst_video_composer_child_proxy_init));

#define DEFAULT_VIDEO_WIDTH       640
#define DEFAULT_VIDEO_HEIGHT      480
#define DEFAULT_VIDEO_FPS_NUM     30
#define DEFAULT_VIDEO_FPS_DEN     1

#define DEFAULT_PROP_MIN_BUFFERS  2
#define DEFAULT_PROP_MAX_BUFFERS  10

#define DEFAULT_PROP_BACKGROUND   0xFF808080

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

// Caps video size range.
#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

// Caps FPS range.
#undef GST_VIDEO_FPS_RANGE
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, 255 ]"

// Caps formats.
#define GST_VIDEO_FORMATS "{ BGRA, RGBA, BGR, RGB, NV12, NV21, UYVY }"

static GType gst_converter_request_get_type(void);
#define GST_TYPE_CONVERTER_REQUEST  (gst_converter_request_get_type())
#define GST_CONVERTER_REQUEST(obj) ((GstConverterRequest *) obj)

enum
{
  PROP_0,
  PROP_BACKGROUND,
};

static GstStaticPadTemplate gst_video_composer_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink_%u",
        GST_PAD_SINK,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS))
    );

static GstStaticPadTemplate gst_video_composer_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS))
    );

typedef struct _GstConverterRequest GstConverterRequest;

struct _GstConverterRequest {
  GstMiniObject parent;

  // Request ID.
  gpointer      id;

  // Input frames submitted with provided ID.
  GstVideoFrame *inframes;
  // Number of input frames.
  guint         n_inputs;

  // Output frames submitted with provided ID.
  GstVideoFrame *outframes;
  // Number of output frames.
  guint         n_outputs;

  // Time it took for this request to be processed.
  GstClockTime  time;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstConverterRequest, gst_converter_request);

static void
gst_converter_request_free (GstConverterRequest * request)
{
  GstBuffer *buffer = NULL;
  guint idx = 0;

  for (idx = 0; idx < request->n_inputs; idx++) {
    buffer = request->inframes[idx].buffer;

    if (buffer != NULL) {
      gst_video_frame_unmap (&(request)->inframes[idx]);
      gst_buffer_unref (buffer);
    }
  }

  for (idx = 0; idx < request->n_outputs; idx++) {
    buffer = request->outframes[idx].buffer;

    if (buffer != NULL) {
      gst_video_frame_unmap (&(request)->outframes[idx]);
      gst_buffer_unref (buffer);
    }
  }

  g_free (request->inframes);
  g_free (request->outframes);
  g_free (request);
}

static void
gst_converter_request_init (GstConverterRequest * request)
{
  gst_mini_object_init (GST_MINI_OBJECT (request), 0,
      GST_TYPE_CONVERTER_REQUEST, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_converter_request_free);

  request->id = NULL;
  request->inframes = NULL;
  request->n_inputs = 0;
  request->outframes = NULL;
  request->n_outputs = 0;
  request->time = GST_CLOCK_TIME_NONE;
}

static GstConverterRequest *
gst_converter_request_new ()
{
  GstConverterRequest *request = g_new0 (GstConverterRequest, 1);
  gst_converter_request_init (request);
  return request;
}

static inline void
gst_converter_request_unref (GstConverterRequest * request)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (request));
}

static void
gst_video_composer_free_queue_item (gpointer data)
{
  GstDataQueueItem *item = data;
  gst_converter_request_unref (GST_CONVERTER_REQUEST (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static GstC2dVideoRotate
gst_video_composer_rotation_to_c2d_rotate (GstVideoComposerRotate rotation)
{
  switch (rotation) {
    case GST_VIDEO_COMPOSER_ROTATE_90_CW:
      return GST_C2D_VIDEO_ROTATE_90_CW;
    case GST_VIDEO_COMPOSER_ROTATE_90_CCW:
      return GST_C2D_VIDEO_ROTATE_90_CCW;
    case GST_VIDEO_COMPOSER_ROTATE_180:
      return GST_C2D_VIDEO_ROTATE_180;
    case GST_VIDEO_COMPOSER_ROTATE_NONE:
      return GST_C2D_VIDEO_ROTATE_NONE;
    default:
      GST_WARNING ("Invalid rotation flag %d!", rotation);
  }
  return GST_C2D_VIDEO_ROTATE_NONE;
}

static gint
gst_video_composer_zorder_compare (const GstVideoComposerSinkPad * lpad,
    const GstVideoComposerSinkPad * rpad)
{
  return lpad->zorder - rpad->zorder;
}

static gint
gst_video_composer_index_compare (const GstVideoComposerSinkPad * pad,
    const guint * index)
{
  return pad->index - (*index);
}

static gboolean
gst_video_composer_caps_has_feature (const GstCaps * caps,
    const gchar * feature)
{
  guint idx = 0;

  for (idx = 0; idx < gst_caps_get_size (caps); idx++) {
    GstCapsFeatures *const features = gst_caps_get_features (caps, idx);

    // Skip ANY caps and return immediately if feature is present.
    if (!gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, feature))
      return TRUE;
  }

  return FALSE;
}

static gboolean
gst_video_composer_caps_has_compression (const GstCaps * caps,
    const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
}

static GstBufferPool *
gst_video_composer_create_pool (GstVideoComposer * vcomposer, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vcomposer, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  // If downstream allocation query supports GBM, allocate gbm memory.
  if (gst_video_composer_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    GST_INFO_OBJECT (vcomposer, "Uses GBM memory");
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
  } else {
    GST_INFO_OBJECT (vcomposer, "Uses ION memory");
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (gst_video_composer_caps_has_compression (caps, "ubwc")) {
    gst_buffer_pool_config_add_option (config,
        GST_IMAGE_BUFFER_POOL_OPTION_UBWC_MODE);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (vcomposer, "Failed to set pool configuration!");
    g_clear_object (&pool);
  }

  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_video_composer_set_opts (GstElement * element, GstPad * pad, gpointer data)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (element);
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (pad);
  GstCaps *caps = gst_pad_get_current_caps (pad);
  GstStructure *options = NULL;
  guint idx = 0;

  GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

  options = gst_structure_new (GST_PAD_NAME (sinkpad),
      GST_C2D_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL, G_TYPE_BOOLEAN,
      sinkpad->flip_h,
      GST_C2D_VIDEO_CONVERTER_OPT_FLIP_VERTICAL, G_TYPE_BOOLEAN,
      sinkpad->flip_v,
      GST_C2D_VIDEO_CONVERTER_OPT_ROTATION, GST_TYPE_C2D_VIDEO_ROTATION,
      gst_video_composer_rotation_to_c2d_rotate (sinkpad->rotation),
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_X, G_TYPE_INT,
      sinkpad->crop.x,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_Y, G_TYPE_INT,
      sinkpad->crop.y,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_WIDTH, G_TYPE_INT,
      sinkpad->crop.w,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_HEIGHT, G_TYPE_INT,
      sinkpad->crop.h,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_X, G_TYPE_INT,
      sinkpad->destination.x,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_Y, G_TYPE_INT,
      sinkpad->destination.y,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_WIDTH, G_TYPE_INT,
      sinkpad->destination.w,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_HEIGHT, G_TYPE_INT,
      sinkpad->destination.h,
      GST_C2D_VIDEO_CONVERTER_OPT_ALPHA, G_TYPE_DOUBLE,
      sinkpad->alpha,
      NULL);


  // Check whether the input caps have ubwc compression.
  if (gst_video_composer_caps_has_compression (caps, "ubwc")) {
    gst_structure_set (options,
        GST_C2D_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN, TRUE,
        NULL);
  }

  GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

  gst_caps_unref (caps);

  GST_OBJECT_LOCK (vcomposer);
  idx = g_list_index (element->sinkpads, pad);
  GST_OBJECT_UNLOCK (vcomposer);

  return gst_c2d_video_converter_set_input_opts (
      vcomposer->c2dconvert, idx, options);
}

static void
gst_video_composer_property_zorder_cb (GObject * object, GParamSpec * pspec,
    gpointer data)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (data);
  gboolean success = FALSE;

  GST_LOG_OBJECT (vcomposer, "Property '%s' of pad %s has changed",
      pspec->name, GST_PAD_NAME (object));

  GST_OBJECT_LOCK (vcomposer);

  GST_ELEMENT (vcomposer)->sinkpads = g_list_sort (
      GST_ELEMENT (vcomposer)->sinkpads,
      (GCompareFunc) gst_video_composer_zorder_compare);

  GST_OBJECT_UNLOCK (vcomposer);

  success = gst_element_foreach_sink_pad (GST_ELEMENT (vcomposer),
      gst_video_composer_set_opts, NULL);

  if (!success)
    GST_ERROR_OBJECT (vcomposer, "Failed to set converter options!");
}

static void
gst_video_composer_property_crop_cb (GObject * object,
    GParamSpec * pspec, gpointer data)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (data);
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (object);
  GstStructure *opts = NULL;
  guint idx = 0;

  GST_LOG_OBJECT (vcomposer, "Property '%s' of pad %s has changed",
      pspec->name, GST_PAD_NAME (sinkpad));

  GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

  opts = gst_structure_new (GST_PAD_NAME (sinkpad),
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_X, G_TYPE_INT,
      sinkpad->crop.x,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_Y, G_TYPE_INT,
      sinkpad->crop.y,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_WIDTH, G_TYPE_INT,
      sinkpad->crop.w,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_HEIGHT, G_TYPE_INT,
      sinkpad->crop.h,
      NULL);

  GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

  GST_OBJECT_LOCK (vcomposer);
  idx = g_list_index (GST_ELEMENT (vcomposer)->sinkpads, sinkpad);
  GST_OBJECT_UNLOCK (vcomposer);

  gst_c2d_video_converter_set_input_opts (vcomposer->c2dconvert, idx, opts);
}

static void
gst_video_composer_property_destination_cb (GObject * object,
    GParamSpec * pspec, gpointer data)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (data);
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (object);
  GstStructure *opts = NULL;
  guint idx = 0;

  GST_LOG_OBJECT (vcomposer, "Property '%s' of pad %s has changed",
      pspec->name, GST_PAD_NAME (sinkpad));

  GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

  opts = gst_structure_new (GST_PAD_NAME (sinkpad),
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_X, G_TYPE_INT,
      sinkpad->destination.x,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_Y, G_TYPE_INT,
      sinkpad->destination.y,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_WIDTH, G_TYPE_INT,
      sinkpad->destination.w,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_HEIGHT, G_TYPE_INT,
      sinkpad->destination.h,
      NULL);

  GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

  GST_OBJECT_LOCK (vcomposer);
  idx = g_list_index (GST_ELEMENT (vcomposer)->sinkpads, sinkpad);
  GST_OBJECT_UNLOCK (vcomposer);

  gst_c2d_video_converter_set_input_opts (vcomposer->c2dconvert, idx, opts);
}

static void
gst_video_composer_property_alpha_cb (GObject * object,
    GParamSpec * pspec, gpointer data)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (data);
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (object);
  GstStructure *opts = NULL;
  guint idx = 0;

  GST_LOG_OBJECT (vcomposer, "Property '%s' of pad %s has changed",
      pspec->name, GST_PAD_NAME (sinkpad));

  GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

  opts = gst_structure_new (GST_PAD_NAME (sinkpad),
      GST_C2D_VIDEO_CONVERTER_OPT_ALPHA, G_TYPE_DOUBLE,
      sinkpad->alpha,
      NULL);

  GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

  GST_OBJECT_LOCK (vcomposer);
  idx = g_list_index (GST_ELEMENT (vcomposer)->sinkpads, sinkpad);
  GST_OBJECT_UNLOCK (vcomposer);

  gst_c2d_video_converter_set_input_opts (vcomposer->c2dconvert, idx, opts);
}

static void
gst_video_composer_property_flip_cb (GObject * object,
    GParamSpec * pspec, gpointer data)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (data);
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (object);
  GstStructure *opts = NULL;
  guint idx = 0;

  GST_LOG_OBJECT (vcomposer, "Property '%s' of pad %s has changed",
      pspec->name, GST_PAD_NAME (sinkpad));

  GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

  opts = gst_structure_new (GST_PAD_NAME (sinkpad),
      GST_C2D_VIDEO_CONVERTER_OPT_FLIP_HORIZONTAL, G_TYPE_BOOLEAN,
      sinkpad->flip_h,
      GST_C2D_VIDEO_CONVERTER_OPT_FLIP_VERTICAL, G_TYPE_BOOLEAN,
      sinkpad->flip_v,
      NULL);

  GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

  GST_OBJECT_LOCK (vcomposer);
  idx = g_list_index (GST_ELEMENT (vcomposer)->sinkpads, sinkpad);
  GST_OBJECT_UNLOCK (vcomposer);

  gst_c2d_video_converter_set_input_opts (vcomposer->c2dconvert, idx, opts);
}

static void
gst_video_composer_property_rotate_cb (GObject * object,
    GParamSpec * pspec, gpointer data)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (data);
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (object);
  GstStructure *opts = NULL;
  guint idx = 0;

  GST_LOG_OBJECT (vcomposer, "Property '%s' of pad %s has changed",
      pspec->name, GST_PAD_NAME (sinkpad));

  GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

  opts = gst_structure_new (GST_PAD_NAME (sinkpad),
      GST_C2D_VIDEO_CONVERTER_OPT_ROTATION, GST_TYPE_C2D_VIDEO_ROTATION,
      gst_video_composer_rotation_to_c2d_rotate (sinkpad->rotation),
      NULL);

  GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

  GST_OBJECT_LOCK (vcomposer);
  idx = g_list_index (GST_ELEMENT (vcomposer)->sinkpads, sinkpad);
  GST_OBJECT_UNLOCK (vcomposer);

  gst_c2d_video_converter_set_input_opts (vcomposer->c2dconvert, idx, opts);
}

static gboolean
gst_video_composer_propose_allocation (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * inquery, GstQuery * outquery)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (aggregator);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstVideoInfo info;
  guint idx = 0, n_metas = 0, size = 0;
  gboolean needpool = FALSE;

  GST_DEBUG_OBJECT (vcomposer, "Pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  // No input query, nothing to do.
  if (NULL == inquery)
    return TRUE;

  n_metas = gst_query_get_n_allocation_metas (inquery);

  for (idx = 0; idx < n_metas; idx++) {
    GType gtype;
    const GstStructure *params;

    gtype = gst_query_parse_nth_allocation_meta (inquery, idx, &params);
    GST_DEBUG_OBJECT (vcomposer, "Proposing metadata %s", g_type_name (gtype));
    gst_query_add_allocation_meta (outquery, gtype, params);
  }

  // Extract caps from the query.
  gst_query_parse_allocation (outquery, &caps, &needpool);

  if (NULL == caps) {
    GST_ERROR_OBJECT (vcomposer, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to get video info!");
    return FALSE;
  }

  // Get the size from video info.
  size = GST_VIDEO_INFO_SIZE (&info);

  if (needpool) {
    GstStructure *structure = NULL;

    pool = gst_video_composer_create_pool (vcomposer, caps);
    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (vcomposer, "Failed to set buffer pool configuration!");
      gst_object_unref (pool);
      return FALSE;
    }
  }

  // If upstream does't have a pool requirement, set only size in query.
  gst_query_add_allocation_pool (outquery, needpool ? pool : NULL, size, 0, 0);

  if (pool != NULL)
    gst_object_unref (pool);

  gst_query_add_allocation_meta (outquery, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;
}

static gboolean
gst_video_composer_decide_allocation (GstAggregator * aggregator,
    GstQuery * query)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER_CAST (aggregator);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  guint size, minbuffers, maxbuffers;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (vcomposer, "Failed to parse the decide_allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (vcomposer->outpool) {
    gst_buffer_pool_set_active (vcomposer->outpool, FALSE);
    gst_object_unref (vcomposer->outpool);
  }

  // Create a new buffer pool.
  pool = gst_video_composer_create_pool (vcomposer, caps);
  vcomposer->outpool = pool;

  {
    GstStructure *config = NULL;
    GstAllocator *allocator = NULL;
    GstAllocationParams params;

    // Get the configured pool properties in order to set in query.
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
        &maxbuffers);

    if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
      gst_query_add_allocation_param (query, allocator, &params);

    gst_structure_free (config);
  }

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers,
        maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers,
        maxbuffers);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_video_composer_prepare_input_frame (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (element);
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (pad);
  GstVideoFrame *frames = GST_CONVERTER_REQUEST (userdata)->inframes;
  GstBuffer *buffer = NULL;
  guint idx = 0;

  if (gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (pad)))
    return TRUE;

  buffer = gst_aggregator_pad_peek_buffer (GST_AGGREGATOR_PAD (pad));

  if (buffer == NULL) {
    GST_TRACE_OBJECT (pad, "No buffer available!");
    return FALSE;
  }

  // GAP buffer, nothing to do.
  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
    GST_TRACE_OBJECT (pad, "GAP buffer!");
    return TRUE;
  }

  {
    GstSegment *segment = NULL;
    GstClockTime timestamp, position;

    GST_OBJECT_LOCK (vcomposer);
    segment = &GST_AGGREGATOR_PAD (GST_AGGREGATOR (vcomposer)->srcpad)->segment;

    // Check whether the buffer should be kept in the queue for future reuse.
    timestamp = gst_segment_to_running_time (
        &GST_AGGREGATOR_PAD (pad)->segment, GST_FORMAT_TIME,
        GST_BUFFER_PTS (buffer)) + GST_BUFFER_DURATION (buffer);
    position = gst_segment_to_running_time (segment, GST_FORMAT_TIME,
        segment->position) + vcomposer->duration;

    if (timestamp > position)
      GST_TRACE_OBJECT (pad, "Keeping buffer at least until %"
          GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));
    else
      gst_aggregator_pad_drop_buffer (GST_AGGREGATOR_PAD (pad));

    idx = g_list_index (element->sinkpads, pad);
    GST_OBJECT_UNLOCK (vcomposer);
  }

  if (!gst_video_frame_map (&frames[idx], sinkpad->info, buffer,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (pad, "Failed to map input buffer!");
    return FALSE;
  }

  GST_TRACE_OBJECT (pad, "Buffer: %" GST_PTR_FORMAT, buffer);
  return TRUE;
}

static gboolean
gst_video_composer_prepare_output_frame (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (element);
  GstVideoFrame *frames = GST_CONVERTER_REQUEST (userdata)->outframes;
  GstBufferPool *pool = vcomposer->outpool;
  GstBuffer *buffer = NULL;
  guint idx = 0;

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to activate output video buffer pool!");
    return FALSE;
  }

  if (gst_buffer_pool_acquire_buffer (pool, &buffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (vcomposer, "Failed to create output video buffer!");
    return FALSE;
  }

  GST_OBJECT_LOCK (vcomposer);
  idx = g_list_index (element->srcpads, pad);
  GST_OBJECT_UNLOCK (vcomposer);

  if (!gst_video_frame_map (&frames[idx], vcomposer->outinfo, buffer,
          GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to map output buffer!");
    return FALSE;
  }

  GST_BUFFER_DURATION (buffer) = vcomposer->duration;

  {
    GstSegment *s = NULL;

    GST_OBJECT_LOCK (vcomposer);
    s = &GST_AGGREGATOR_PAD (GST_AGGREGATOR (vcomposer)->srcpad)->segment;

    GST_BUFFER_TIMESTAMP (buffer) = (s->position == GST_CLOCK_TIME_NONE ||
        s->position <= s->start) ? s->start : s->position;

    s->position = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    GST_OBJECT_UNLOCK (vcomposer);
  }

  GST_TRACE_OBJECT (vcomposer, "Pad %s %" GST_PTR_FORMAT, GST_PAD_NAME (pad),
      buffer);

  return TRUE;
}

static gboolean
gst_video_composer_sink_query (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * query)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_DEBUG_OBJECT (vcomposer, "Received %s query on pad %s:%s",
      GST_QUERY_TYPE_NAME (query), GST_DEBUG_PAD_NAME (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter = NULL, *caps = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_video_composer_sinkpad_getcaps (pad, aggregator, filter);
      gst_query_set_caps_result (query, caps);

      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_video_composer_sinkpad_acceptcaps (pad, aggregator, caps);
      gst_query_set_accept_caps_result (query, success);

      return TRUE;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (
      aggregator, pad, query);
}

static gboolean
gst_video_composer_sink_event (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstEvent * event)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_DEBUG_OBJECT (vcomposer, "Received %s event on pad %s:%s",
      GST_EVENT_TYPE_NAME (event), GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_event_parse_caps (event, &caps);
      success = gst_video_composer_sinkpad_setcaps (pad, aggregator, caps);

      gst_event_unref (event);
      return success;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_event (
      aggregator, pad, event);
}

static gboolean
gst_video_composer_src_query (GstAggregator * aggregator, GstQuery * query)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_DEBUG_OBJECT (vcomposer, "Received %s query on src pad",
      GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstSegment *segment = &GST_AGGREGATOR_PAD (aggregator->srcpad)->segment;
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (vcomposer, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));
      return TRUE;
    }
    case GST_QUERY_DURATION:
      // TODO
      break;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (aggregator, query);
}

static gboolean
gst_video_composer_src_event (GstAggregator * aggregator, GstEvent * event)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_TRACE_OBJECT (vcomposer, "Received %s event on src pad",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      // TODO
      break;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_event (aggregator, event);
}

static GstFlowReturn
gst_video_composer_update_src_caps (GstAggregator * aggregator,
    GstCaps * caps, GstCaps ** othercaps)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  gint outwidth = 0, outheight = 0, out_fps_n = 0, out_fps_d = 0;
  guint idx = 0, length = 0;
  gboolean configured = TRUE;


  GST_DEBUG_OBJECT (vcomposer, "Update output caps based on caps %"
      GST_PTR_FORMAT, caps);

  {
    GstVideoComposerSinkPad *sinkpad = NULL;
    GList *list = NULL;

    GST_OBJECT_LOCK (vcomposer);

    // Extrapolate the highest width, height and frame rate from the sink pads.
    for (list = GST_ELEMENT (vcomposer)->sinkpads; list; list = list->next) {
      gint width, height, fps_n, fps_d;
      gdouble fps = 0.0, outfps = 0;

      sinkpad = GST_VIDEO_COMPOSER_SINKPAD_CAST (list->data);

      if (NULL == sinkpad->info) {
        GST_DEBUG_OBJECT (vcomposer, "%s caps not set!", GST_PAD_NAME (sinkpad));
        configured = FALSE;
        continue;
      }

      GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

      width = (sinkpad->destination.w != 0) ?
          sinkpad->destination.w : GST_VIDEO_INFO_WIDTH (sinkpad->info);
      height = (sinkpad->destination.h != 0) ?
          sinkpad->destination.h : GST_VIDEO_INFO_HEIGHT (sinkpad->info);

      fps_n = GST_VIDEO_INFO_FPS_N (sinkpad->info);
      fps_d = GST_VIDEO_INFO_FPS_D (sinkpad->info);

      // Adjust the width & height to take into account the X & Y coordinates.
      width += (width > 0) ? sinkpad->destination.x : 0;
      height += (height > 0) ? sinkpad->destination.y : 0;

      GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

      if (width == 0 || height == 0)
        continue;

      // Take the greater dimensions.
      outwidth = (width > outwidth) ? width : outwidth;
      outheight = (height > outheight) ? height : outheight;

      gst_util_fraction_to_double (fps_n, fps_d, &fps);

      if (out_fps_d != 0)
        gst_util_fraction_to_double (out_fps_n, out_fps_d, &outfps);

      if (outfps < fps) {
        out_fps_n = fps_n;
        out_fps_d = fps_d;
      }
    }

    GST_OBJECT_UNLOCK (vcomposer);
  }

  *othercaps = gst_caps_new_empty ();
  length = gst_caps_get_size (caps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = gst_caps_get_structure (caps, idx);
    GstCapsFeatures *features = gst_caps_get_features (caps, idx);
    const GValue *framerate = NULL;
    gint width = 0, height = 0;

    // If this is already expressed by the existing caps skip this structure.
    if (idx > 0 && gst_caps_is_subset_structure_full (*othercaps, structure, features))
      continue;

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);
    framerate = gst_structure_get_value (structure, "framerate");

    if (!width && !outwidth) {
      gst_structure_set (structure, "width", G_TYPE_INT,
          DEFAULT_VIDEO_WIDTH, NULL);
      GST_DEBUG_OBJECT (vcomposer, "Width not set, using default value: %d",
          DEFAULT_VIDEO_WIDTH);
    } else if (!width) {
      gst_structure_set (structure, "width", G_TYPE_INT, outwidth, NULL);
      GST_DEBUG_OBJECT (vcomposer, "Width not set, using extrapolated width "
          "based on the sinkpads: %d", outwidth);
    } else if (width < outwidth) {
      GST_ERROR_OBJECT (vcomposer, "Set width (%u) is not compatible with the"
          "extrapolated width (%d) from the sinkpads!", width, outwidth);
      return GST_FLOW_NOT_SUPPORTED;
    }

    if (!height && !outheight) {
      gst_structure_set (structure, "height", G_TYPE_INT,
          DEFAULT_VIDEO_HEIGHT, NULL);
      GST_DEBUG_OBJECT (vcomposer, "Height not set, using default value: %d",
          DEFAULT_VIDEO_HEIGHT);
    } else if (!height) {
      gst_structure_set (structure, "height", G_TYPE_INT, outheight, NULL);
      GST_DEBUG_OBJECT (vcomposer, "Height not set, using extrapolated height "
          "based on the sinkpads: %d", outheight);
    } else if (height < outheight) {
      GST_ERROR_OBJECT (vcomposer, "Set height (%u) is not compatible with the"
          "extrapolated height (%d) from the sinkpads!", height, outheight);
      return GST_FLOW_NOT_SUPPORTED;
    }

    if (!gst_value_is_fixed (framerate) && (out_fps_n <= 0 || out_fps_d <= 0)) {
      gst_structure_fixate_field_nearest_fraction (structure, "framerate",
          DEFAULT_VIDEO_FPS_NUM, DEFAULT_VIDEO_FPS_DEN);
      GST_DEBUG_OBJECT (vcomposer, "Frame rate not set, using default value: "
          "%d/%d", DEFAULT_VIDEO_FPS_NUM, DEFAULT_VIDEO_FPS_DEN);
    } else if (!gst_value_is_fixed (framerate)) {
      gst_structure_fixate_field_nearest_fraction (structure, "framerate",
          out_fps_n, out_fps_d);
      GST_DEBUG_OBJECT (vcomposer, "Frame rate not set, using extrapolated "
          "rate (%d/%d) from the sinkpads", out_fps_n, out_fps_d);
    } else {
      gint fps_n = gst_value_get_fraction_numerator (framerate);
      gint fps_d = gst_value_get_fraction_denominator (framerate);
      gdouble fps = 0.0, outfps = 0.0;

      gst_util_fraction_to_double (fps_n, fps_d, &fps);
      gst_util_fraction_to_double (out_fps_n, out_fps_d, &outfps);

      if (fps != outfps) {
        GST_ERROR_OBJECT (vcomposer, "Set framerate (%d/%d) is not compatible"
            " with the extrapolated rate (%d/%d) from the sinkpads!", fps_n,
            fps_d, out_fps_n, out_fps_d);
        return GST_FLOW_NOT_SUPPORTED;
      }
    }

    // TODO optimize that to take into account sink pads format.
    // Fixate the format field in case it wasn't already fixated.
    gst_structure_fixate_field (structure, "format");

    framerate = gst_structure_get_value (structure, "framerate");
    vcomposer->duration = gst_util_uint64_scale_int (GST_SECOND,
        gst_value_get_fraction_denominator (framerate),
        gst_value_get_fraction_numerator (framerate));

    gst_caps_append_structure_full (*othercaps, structure,
        gst_caps_features_copy (features));
  }

  GST_DEBUG_OBJECT (vcomposer, "Updated caps %" GST_PTR_FORMAT, *othercaps);
  return configured ? GST_FLOW_OK : GST_AGGREGATOR_FLOW_NEED_DATA;
}

static GstCaps *
gst_video_composer_fixate_src_caps (GstAggregator * aggregator, GstCaps * caps)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  guint idx = 0;

  // Check caps structures for memory:GBM feature.
  for (idx = 0; idx < gst_caps_get_size (caps); idx++) {
    GstCapsFeatures *features = gst_caps_get_features (caps, idx);

    if (!gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GBM)) {
      // Found caps structure with memory:GBM feature, remove all others.
      GstStructure *structure = gst_caps_steal_structure (caps, idx);

      gst_caps_unref (caps);
      caps = gst_caps_new_empty ();

      gst_caps_append_structure_full (caps, structure,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL));
      break;
    }
  }

  caps = gst_caps_fixate (caps);
  GST_DEBUG_OBJECT (vcomposer, "Fixated output caps to %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_video_composer_negotiated_src_caps (GstAggregator * aggregator,
    GstCaps * caps)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  GstStructure *options = NULL;
  GstVideoInfo info;
  gint dar_n = 0, dar_d = 0;
  gboolean success = FALSE;

  GST_DEBUG_OBJECT (vcomposer, "Negotiated caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to get video info from caps!");
    return FALSE;
  }

  // Iterate over the sink pads and set the converter input options.
  success = gst_element_foreach_sink_pad (GST_ELEMENT (vcomposer),
      gst_video_composer_set_opts, NULL);

  if (!success) {
    GST_ERROR_OBJECT (vcomposer, "Failed to set converter options!");
    return FALSE;
  }

  // Fill the converter output options structure.
  if (NULL == (options = gst_structure_new_empty ("qtivtransform"))) {
    GST_ERROR_OBJECT (vcomposer, "Failed to create options structure!");
    return FALSE;
  }

  // Check whether the output caps have ubwc compression.
  if (gst_video_composer_caps_has_compression (caps, "ubwc")) {
    gst_structure_set (options,
        GST_C2D_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN, TRUE,
        NULL);
  }

  gst_c2d_video_converter_set_output_opts (vcomposer->c2dconvert, options);

  if (!gst_util_fraction_multiply (info.width, info.height,
          info.par_n, info.par_d, &dar_n, &dar_d)) {
    GST_WARNING_OBJECT (vcomposer, "Failed to calculate DAR!");
    dar_n = dar_d = -1;
  }

  GST_DEBUG_OBJECT (vcomposer, "Output %dx%d (PAR: %d/%d, DAR: %d/%d), size"
      " %" G_GSIZE_FORMAT, info.width, info.height, info.par_n, info.par_d,
      dar_n, dar_d, info.size);

  if (vcomposer->outinfo != NULL)
    gst_video_info_free (vcomposer->outinfo);

  vcomposer->outinfo = gst_video_info_copy (&info);

  gst_aggregator_set_latency (aggregator, vcomposer->duration,
      vcomposer->duration);

  return TRUE;
}

static void
gst_video_composer_task_loop (gpointer userdata)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (vcomposer->requests, &item)) {
    GstConverterRequest *request = NULL;
    GstBuffer *buffer = NULL;
    gboolean success = FALSE;

    // Increase the request reference count to indicate that it is in use.
    request = GST_CONVERTER_REQUEST (gst_mini_object_ref (item->object));
    item->destroy (item);

    GST_TRACE_OBJECT (vcomposer, "Waiting request %p", request->id);
    success = gst_c2d_video_converter_wait_request (
        vcomposer->c2dconvert, request->id);

    if (!success) {
      GST_DEBUG_OBJECT (vcomposer, " Waiting request %p failed!", request->id);
      gst_converter_request_unref (request);
      return;
    }

    // Get time difference between current time and start.
    request->time = GST_CLOCK_DIFF (request->time, gst_util_get_timestamp ());

    GST_LOG_OBJECT (vcomposer, "Request %p took %" G_GINT64_FORMAT ".%03"
        G_GINT64_FORMAT " ms", request->id, GST_TIME_AS_MSECONDS (request->time),
        (GST_TIME_AS_USECONDS (request->time) % 1000));

    // Increase the buffer reference count to indicate that it is in use.
    buffer = gst_buffer_ref (request->outframes[0].buffer);
    gst_converter_request_unref (request);

    gst_aggregator_finish_buffer (GST_AGGREGATOR (vcomposer), buffer);
  } else {
    GST_DEBUG_OBJECT (vcomposer, "Paused worker thread");
    gst_task_pause (vcomposer->worktask);
  }
}

static gboolean
gst_video_composer_start (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  if (vcomposer->worktask != NULL)
    return TRUE;

  vcomposer->worktask =
      gst_task_new (gst_video_composer_task_loop, aggregator, NULL);
  GST_INFO_OBJECT (vcomposer, "Created task %p", vcomposer->worktask);

  gst_task_set_lock (vcomposer->worktask, &vcomposer->worklock);

  if (!gst_task_start (vcomposer->worktask)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to start worker task!");
    return FALSE;
  }

  // Disable requests queue in flushing state to enable normal work.
  gst_data_queue_set_flushing (vcomposer->requests, FALSE);
  return TRUE;
}

static gboolean
gst_video_composer_stop (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  if (NULL == vcomposer->worktask)
    return TRUE;

  // Set the requests queue in flushing state.
  gst_data_queue_set_flushing (vcomposer->requests, TRUE);

  if (!gst_task_stop (vcomposer->worktask))
    GST_WARNING_OBJECT (vcomposer, "Failed to stop worker task!");

  // Make sure task is not running.
  g_rec_mutex_lock (&vcomposer->worklock);
  g_rec_mutex_unlock (&vcomposer->worklock);

  if (!gst_task_join (vcomposer->worktask)) {
    GST_ERROR_OBJECT (vcomposer, "Failed to join worker task!");
    return FALSE;
  }

  // Flush converter and requests queue.
  gst_c2d_video_converter_flush (vcomposer->c2dconvert);
  gst_data_queue_flush (vcomposer->requests);

  GST_INFO_OBJECT (vcomposer, "Removing task %p", vcomposer->worktask);

  gst_object_unref (vcomposer->worktask);
  vcomposer->worktask = NULL;

  return TRUE;
}

static GstClockTime
gst_video_composer_get_next_time (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  GstSegment *segment = &GST_AGGREGATOR_PAD (aggregator->srcpad)->segment;
  GstClockTime nexttime;

  GST_OBJECT_LOCK (vcomposer);
  nexttime = (segment->position == GST_CLOCK_TIME_NONE ||
      segment->position < segment->start) ? segment->start : segment->position;

  if (segment->stop != GST_CLOCK_TIME_NONE && nexttime > segment->stop)
    nexttime = segment->stop;

  nexttime = gst_segment_to_running_time (segment, GST_FORMAT_TIME, nexttime);
  GST_OBJECT_UNLOCK (vcomposer);

  return nexttime;
}

static gboolean
gst_video_composer_is_eos (GstAggregator * aggregator)
{
  GList *list = NULL;
  gboolean eos = TRUE;

  GST_OBJECT_LOCK (aggregator);

  // Iterate over every sink pad and check whether they reached EOS.
  for (list = GST_ELEMENT (aggregator)->sinkpads; list; list = list->next)
    eos &= gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (list->data));

  GST_OBJECT_UNLOCK (aggregator);

  return eos;
}

static GstFlowReturn
gst_video_composer_aggregate (GstAggregator * aggregator, gboolean timeout)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);
  GstConverterRequest *request = NULL;
  GstDataQueueItem *item = NULL;
  gboolean success = FALSE;

  if (timeout && (NULL == vcomposer->outinfo))
    return GST_AGGREGATOR_FLOW_NEED_DATA;

  // Check whether all pads have reached EOS.
  if (gst_video_composer_is_eos (aggregator))
    return GST_FLOW_EOS;

  request = gst_converter_request_new ();
  request->inframes = g_new0 (GstVideoFrame, vcomposer->n_inputs);
  request->n_inputs = vcomposer->n_inputs;
  request->outframes = g_new0 (GstVideoFrame, vcomposer->n_outputs);
  request->n_outputs = vcomposer->n_outputs;

  // Get start time for performance measurements.
  request->time = gst_util_get_timestamp ();

  success = gst_element_foreach_sink_pad (GST_ELEMENT_CAST (vcomposer),
      gst_video_composer_prepare_input_frame, request);

  if (!success) {
    gst_converter_request_unref (request);
    return GST_AGGREGATOR_FLOW_NEED_DATA;
  }

  success = gst_element_foreach_src_pad (GST_ELEMENT_CAST (vcomposer),
      gst_video_composer_prepare_output_frame, request);

  if (!success) {
    GST_WARNING_OBJECT (vcomposer, "Failed to prepare output video frames!");
    gst_converter_request_unref (request);
    return GST_FLOW_ERROR;
  }

  request->id = gst_c2d_video_converter_submit_request (vcomposer->c2dconvert,
      request->inframes, request->n_inputs, request->outframes);

  if (NULL == request->id) {
    GST_WARNING_OBJECT (vcomposer, "Failed to submit request to converter!");
    gst_converter_request_unref (request);
    return GST_FLOW_ERROR;
  }

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (request);
  item->visible = TRUE;
  item->destroy = gst_video_composer_free_queue_item;

  // Push the request into the queue or free it on failure.
  if (!gst_data_queue_push (vcomposer->requests, item)) {
    item->destroy (item);
    return GST_FLOW_OK;
  }

  GST_TRACE_OBJECT (vcomposer, "Submitted request with ID: %p", request->id);
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_video_composer_flush (GstAggregator * aggregator)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (aggregator);

  GST_INFO_OBJECT (vcomposer, "Flushing request queue");

  // Set the requests queue in flushing state.
  gst_data_queue_set_flushing (vcomposer->requests, TRUE);

  // Flush converter and requests queue.
  gst_c2d_video_converter_flush (vcomposer->c2dconvert);
  gst_data_queue_flush (vcomposer->requests);

  return GST_AGGREGATOR_CLASS (parent_class)->flush (aggregator);;
}

static GstPad*
gst_video_composer_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (element);
  GstPad *pad = NULL;

  pad = GST_ELEMENT_CLASS (parent_class)->request_new_pad
      (element, templ, reqname, caps);

  if (pad == NULL) {
    GST_ERROR_OBJECT (element, "Failed to create sink pad!");
    return NULL;
  }

  GST_OBJECT_LOCK (vcomposer);

  // Extract the pad index field from its name.
  GST_VIDEO_COMPOSER_SINKPAD (pad)->index =
      g_ascii_strtoull (&GST_PAD_NAME (pad)[5], NULL, 10);

  // In case Z axis order is not filled use the order of creation.
  if (GST_VIDEO_COMPOSER_SINKPAD (pad)->zorder < 0)
    GST_VIDEO_COMPOSER_SINKPAD (pad)->zorder = element->numsinkpads;

  // Sort sink pads by their Z axis order.
  element->sinkpads = g_list_sort (element->sinkpads,
      (GCompareFunc) gst_video_composer_zorder_compare);

  vcomposer->n_inputs = element->numsinkpads;

  GST_OBJECT_UNLOCK (vcomposer);

  GST_DEBUG_OBJECT (vcomposer, "Created pad: %s", GST_PAD_NAME (pad));

  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  g_signal_connect (pad, "notify::zorder",
      G_CALLBACK (gst_video_composer_property_zorder_cb),
      vcomposer);
  g_signal_connect (pad, "notify::crop",
      G_CALLBACK (gst_video_composer_property_crop_cb),
      vcomposer);
  g_signal_connect (pad, "notify::position",
      G_CALLBACK (gst_video_composer_property_destination_cb),
      vcomposer);
  g_signal_connect (pad, "notify::dimensions",
      G_CALLBACK (gst_video_composer_property_destination_cb),
      vcomposer);
  g_signal_connect (pad, "notify::alpha",
      G_CALLBACK (gst_video_composer_property_alpha_cb),
      vcomposer);
  g_signal_connect (pad, "notify::flip-horizontal",
      G_CALLBACK (gst_video_composer_property_flip_cb),
      vcomposer);
  g_signal_connect (pad, "notify::flip-vertical",
      G_CALLBACK (gst_video_composer_property_flip_cb),
      vcomposer);
  g_signal_connect (pad, "notify::rotate",
      G_CALLBACK (gst_video_composer_property_rotate_cb),
      vcomposer);

  return pad;
}

static void
gst_video_composer_release_pad (GstElement * element, GstPad * pad)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (element);
  guint idx = 0;

  GST_DEBUG_OBJECT (vcomposer, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_OBJECT_LOCK (vcomposer);
  idx = g_list_index (GST_ELEMENT (vcomposer)->sinkpads, pad);
  vcomposer->n_inputs = element->numsinkpads - 1;
  GST_OBJECT_UNLOCK (vcomposer);

  gst_c2d_video_converter_set_input_opts (vcomposer->c2dconvert, idx, NULL);

  if (0 == vcomposer->n_inputs) {
    GstSegment *segment =
        &GST_AGGREGATOR_PAD (GST_AGGREGATOR (vcomposer)->srcpad)->segment;
    segment->position = GST_CLOCK_TIME_NONE;
  }

  gst_child_proxy_child_removed (GST_CHILD_PROXY (vcomposer), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (parent_class)->release_pad (GST_ELEMENT (vcomposer), pad);

  gst_pad_mark_reconfigure (GST_AGGREGATOR_SRC_PAD (vcomposer));
}

static void
gst_video_composer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (object);

  GST_VIDEO_COMPOSER_LOCK (vcomposer);

  switch (prop_id) {
    case PROP_BACKGROUND:
    {
      GstStructure *opts = NULL;

      vcomposer->background = g_value_get_uint (value);
      opts = gst_structure_new (gst_element_get_name (vcomposer),
          GST_C2D_VIDEO_CONVERTER_OPT_BACKGROUND, G_TYPE_UINT,
          vcomposer->background, NULL);

      gst_c2d_video_converter_set_output_opts (vcomposer->c2dconvert, opts);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_VIDEO_COMPOSER_UNLOCK (vcomposer);
}

static void
gst_video_composer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (object);

  GST_VIDEO_COMPOSER_LOCK (vcomposer);

  switch (prop_id) {
    case PROP_BACKGROUND:
      g_value_set_uint (value, vcomposer->background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_VIDEO_COMPOSER_UNLOCK (vcomposer);
}

static void
gst_video_composer_finalize (GObject * object)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (object);

  if (vcomposer->c2dconvert != NULL)
    gst_c2d_video_converter_free (vcomposer->c2dconvert);

  if (vcomposer->requests != NULL) {
    gst_data_queue_set_flushing (vcomposer->requests, TRUE);
    gst_data_queue_flush (vcomposer->requests);
    gst_object_unref (GST_OBJECT_CAST(vcomposer->requests));
  }

  if (vcomposer->outpool != NULL)
    gst_object_unref (vcomposer->outpool);

  if (vcomposer->outinfo != NULL)
    gst_video_info_free (vcomposer->outinfo);

  g_rec_mutex_clear (&vcomposer->worklock);
  g_mutex_clear (&vcomposer->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (vcomposer));
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
                  guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
gst_video_composer_class_init (GstVideoComposerClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *aggregator = GST_AGGREGATOR_CLASS (klass);

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_video_composer_finalize);
  gobject->set_property = GST_DEBUG_FUNCPTR (gst_video_composer_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_video_composer_get_property);

  g_object_class_install_property (gobject, PROP_BACKGROUND,
      g_param_spec_uint ("background", "Background",
          "Background color", 0, 0xFFFFFFFF, DEFAULT_PROP_BACKGROUND,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  gst_element_class_set_static_metadata (element,
      "Video composer", "Filter/Editor/Video/Compositor/Scaler",
      "Mix together multiple video streams", "QTI");

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_video_composer_sink_template, GST_TYPE_VIDEO_COMPOSER_SINKPAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_video_composer_src_template, GST_TYPE_AGGREGATOR_PAD);

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_video_composer_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_video_composer_release_pad);

  aggregator->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_video_composer_propose_allocation);
  aggregator->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_video_composer_decide_allocation);
  aggregator->sink_query = GST_DEBUG_FUNCPTR (gst_video_composer_sink_query);
  aggregator->sink_event = GST_DEBUG_FUNCPTR (gst_video_composer_sink_event);
  aggregator->src_event = GST_DEBUG_FUNCPTR (gst_video_composer_src_event);
  aggregator->src_query = GST_DEBUG_FUNCPTR (gst_video_composer_src_query);
  aggregator->update_src_caps =
      GST_DEBUG_FUNCPTR (gst_video_composer_update_src_caps);
  aggregator->fixate_src_caps =
      GST_DEBUG_FUNCPTR (gst_video_composer_fixate_src_caps);
  aggregator->negotiated_src_caps =
      GST_DEBUG_FUNCPTR (gst_video_composer_negotiated_src_caps);
  aggregator->start = GST_DEBUG_FUNCPTR (gst_video_composer_start);
  aggregator->stop = GST_DEBUG_FUNCPTR (gst_video_composer_stop);
  aggregator->get_next_time =
      GST_DEBUG_FUNCPTR (gst_video_composer_get_next_time);
  aggregator->aggregate = GST_DEBUG_FUNCPTR (gst_video_composer_aggregate);
  aggregator->flush = GST_DEBUG_FUNCPTR (gst_video_composer_flush);
}

static void
gst_video_composer_init (GstVideoComposer * vcomposer)
{
  g_mutex_init (&vcomposer->lock);
  g_rec_mutex_init (&vcomposer->worklock);

  vcomposer->n_inputs = 0;
  vcomposer->n_outputs = 1;

  vcomposer->outinfo = NULL;
  vcomposer->outpool = NULL;

  vcomposer->duration = GST_CLOCK_TIME_NONE;

  vcomposer->worktask = NULL;
  vcomposer->requests =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, vcomposer);

  vcomposer->c2dconvert = gst_c2d_video_converter_new ();

  vcomposer->background = DEFAULT_PROP_BACKGROUND;

  GST_AGGREGATOR_PAD (GST_AGGREGATOR (vcomposer)->srcpad)->segment.position =
      GST_CLOCK_TIME_NONE;

  GST_DEBUG_CATEGORY_INIT (gst_video_composer_debug, "qtivcomposer", 0,
      "QTI video composer");
}


static GObject *
gst_video_composer_child_proxy_get_child_by_index (GstChildProxy * proxy,
    guint index)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (proxy);
  GList *list = NULL;
  GObject *gobject = NULL;

  GST_OBJECT_LOCK (vcomposer);

  list = g_list_find_custom (GST_ELEMENT_CAST (vcomposer)->sinkpads, &index,
      (GCompareFunc) gst_video_composer_index_compare);

  if (list != NULL)
    gobject = gst_object_ref (list->data);

  GST_OBJECT_UNLOCK (vcomposer);

  return gobject;
}

static guint
gst_video_composer_child_proxy_get_children_count (GstChildProxy * proxy)
{
  GstVideoComposer *vcomposer = GST_VIDEO_COMPOSER (proxy);
  guint count = 0;

  GST_OBJECT_LOCK (vcomposer);
  count = GST_ELEMENT_CAST (vcomposer)->numsinkpads;
  GST_OBJECT_UNLOCK (vcomposer);

  return count;
}

static void
gst_video_composer_child_proxy_init (gpointer g_iface, gpointer data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index = gst_video_composer_child_proxy_get_child_by_index;
  iface->get_children_count = gst_video_composer_child_proxy_get_children_count;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtivcomposer", GST_RANK_PRIMARY,
          GST_TYPE_VIDEO_COMPOSER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtivcomposer,
    "QTI Video composer",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
