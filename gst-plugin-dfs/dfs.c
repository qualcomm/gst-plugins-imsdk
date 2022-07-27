/*
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

#include "dfs.h"
#include <gst/video/gstimagepool.h>
#include <gst/video/video.h>

#define GST_CAT_DEFAULT gst_dfs_debug
GST_DEBUG_CATEGORY_STATIC (gst_dfs_debug);

#define gst_dfs_parent_class parent_class
G_DEFINE_TYPE (GstDfs, gst_dfs, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_MIN_BUFFERS        2
#define DEFAULT_MAX_BUFFERS        10
#define DEFAULT_CONFIG_PATH "/data/stereo.config"
#define DEFAULT_OUTPUT_MODE OUTPUT_MODE_VIDEO

#define DEFAULT_PROP_MODE MODE_SPEED
#define DEFAULT_PROP_MIN_DISPARITY 1
#define DEFAULT_PROP_NUM_DISPARITY_LEVELS 32
#define DEFAULT_PROP_FILTER_WIDTH 11
#define DEFAULT_PROP_FILTER_HEIGHT 11
#define DEFAULT_PROP_RECTIFICATION FALSE
#define DEFAULT_PROP_GPU_RECT FALSE

#define PLY_HEADER_SIZE 93      //Point Cloud PLY Header size in bytes

enum
{
  PROP_0,
  PROP_MODE,
  PROP_MIN_DISPARITY,
  PROP_NUM_DISPARITY_LEVELS,
  PROP_FILTER_WIDTH,
  PROP_FILTER_HEIGHT,
  PROP_RECTIFICATION,
  PROP_CONFIG_PATH,
};

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#define GST_SINK_VIDEO_FORMATS "{ NV12, NV21 }"

#define GST_SRC_VIDEO_FORMATS "{ RGB, BGR, RGBA, BGRA, RGBx, BGRx, GRAY8 }"

#define GST_SRC_DISPARITY_CAPS "dfs/disparity-map"
#define GST_SRC_POINT_CLOUD_CAPS "dfs/point-cloud"


#define GST_DFS_SRC_CAPS                              \
    "video/x-raw, "                                   \
    "format = (string) " GST_SRC_VIDEO_FORMATS "; "   \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GBM "), "  \
    "format = (string) " GST_SRC_VIDEO_FORMATS "; "   \
    GST_SRC_DISPARITY_CAPS                     "; "   \
    GST_SRC_POINT_CLOUD_CAPS


static GstStaticCaps gst_dfs_static_sink_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_SINK_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_SINK_VIDEO_FORMATS));

static GstStaticCaps gst_dfs_static_src_caps =
GST_STATIC_CAPS (GST_DFS_SRC_CAPS);

static GstCaps *
gst_dfs_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_dfs_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_dfs_src_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_dfs_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_dfs_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_dfs_sink_caps ());
}

static GstPadTemplate *
gst_dfs_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_dfs_src_caps ());
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

static gboolean
gst_dfs_parse_config (gchar * config_location,
    stereoConfiguration * configuration)
{
  gboolean rc = FALSE;
  GstStructure *structure = NULL;
  GValueArray *arrvalue;
  gint intvalue;

  GValue gvalue = G_VALUE_INIT;
  g_value_init (&gvalue, GST_TYPE_STRUCTURE);

  if (!g_file_test (config_location, G_FILE_TEST_EXISTS)) {
    g_print("Failed to find config file\n");
    GST_WARNING ("Failed to find config file");
    return FALSE;
  }

  if (g_file_test (config_location, G_FILE_TEST_IS_REGULAR)) {
    gchar *contents = NULL;
    GError *error = NULL;

    if (!g_file_get_contents (config_location, &contents, NULL, &error)) {
      GST_WARNING ("Failed to get config file contents, error: %s!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    }
    // Remove trailing space and replace new lines with a coma delimeter.
    contents = g_strstrip (contents);
    contents = g_strdelimit (contents, "\n", ',');

    rc = gst_value_deserialize (&gvalue, contents);
    g_free (contents);

    if (!rc) {
      GST_WARNING ("Failed to deserialize config file contents!");
      return rc;
    }
  } else if (!gst_value_deserialize (&gvalue, config_location)) {
    GST_WARNING ("Failed to deserialize the config!");
    return FALSE;
  }

  structure = GST_STRUCTURE (g_value_dup_boxed (&gvalue));
  g_value_unset (&gvalue);

  if ((rc = gst_structure_get_array (structure, "translation", &arrvalue))) {
    for (uint i = 0; i < arrvalue->n_values; i++) {
      configuration->translation[i] = g_value_get_double (arrvalue->values + i);
    }
  }

  if ((rc &= gst_structure_get_array (structure, "rotation", &arrvalue))) {
    for (uint i = 0; i < arrvalue->n_values; i++) {
      configuration->rotation[i] = g_value_get_double (arrvalue->values + i);
    }
  }

  if ((rc &=
          gst_structure_get_array (structure, "camera0_principalPoint",
              &arrvalue))) {
    for (uint i = 0; i < arrvalue->n_values; i++) {
      configuration->camera[0].principalPoint[i] =
          g_value_get_double (arrvalue->values + i);
    }
  }

  if ((rc &=
          gst_structure_get_array (structure, "camera1_principalPoint",
              &arrvalue))) {
    for (uint i = 0; i < arrvalue->n_values; i++) {
      configuration->camera[1].principalPoint[i] =
          g_value_get_double (arrvalue->values + i);
    }
  }

  if ((rc &=
          gst_structure_get_array (structure, "camera0_focalLength",
              &arrvalue))) {
    for (uint i = 0; i < arrvalue->n_values; i++) {
      configuration->camera[0].focalLength[i] =
          g_value_get_double (arrvalue->values + i);
    }
  }

  if ((rc &=
          gst_structure_get_array (structure, "camera1_focalLength",
              &arrvalue))) {
    for (uint i = 0; i < arrvalue->n_values; i++) {
      configuration->camera[1].focalLength[i] =
          g_value_get_double (arrvalue->values + i);
    }
  }

  if ((rc &=
          gst_structure_get_array (structure, "camera0_distortion_coefficient",
              &arrvalue))) {
    for (uint i = 0; i < arrvalue->n_values; i++) {
      configuration->camera[0].distortion[i] =
          g_value_get_double (arrvalue->values + i);
    }
  }

  if ((rc &=
          gst_structure_get_array (structure, "camera1_distortion_coefficient",
              &arrvalue))) {
    for (uint i = 0; i < arrvalue->n_values; i++) {
      configuration->camera[1].distortion[i] =
          g_value_get_double (arrvalue->values + i);
    }
  }

  if ((rc &= gst_structure_get_int (structure, "distortion_model", &intvalue))) {
    configuration->camera[0].distortionModel = intvalue;
    configuration->camera[1].distortionModel = intvalue;
  }

  gst_structure_free (structure);

  return rc;
}

GType
gst_dfs_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue mode[] = {
    {MODE_CVP,
        "CVP hardware mode", "cvp"},
    {MODE_COVERAGE,
        "CPU solution, speed mode", "coverage"},
    {MODE_SPEED,
        "OpenCL solution, speed mode", "speed"},
    {MODE_ACCURACY,
        "CPU solution, accuracy mode", "accuracy"},
    {0, NULL, NULL},
  };
  if (!type)
    type = g_enum_register_static ("DFSMode", mode);

  return type;
}

static GstBufferPool *
gst_dfs_create_pool (GstDfs * dfs, GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstBufferPool *pool = NULL;
  guint size = 0;
  GstVideoInfo info;

  if (gst_structure_has_name (structure, "video/x-raw")) {
    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (dfs, "Invalid caps %" GST_PTR_FORMAT, caps);
      return NULL;
    }
    // If downstream allocation query supports GBM, allocate gbm memory.
    if (caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      GST_INFO_OBJECT (dfs, "Uses GBM memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
    } else {
      GST_INFO_OBJECT (dfs, "Uses ION memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
    }
    if (NULL == pool) {
      GST_ERROR_OBJECT (dfs, "Failed to create buffer pool!");
      return NULL;
    }
    size = GST_VIDEO_INFO_SIZE (&info);

  } else if (gst_structure_has_name (structure, "dfs/disparity-map")) {
    GST_INFO_OBJECT (dfs, "Uses SYSTEM memory");
    pool = gst_buffer_pool_new ();
    guint width = GST_VIDEO_INFO_WIDTH (dfs->ininfo) / 2;
    guint height = GST_VIDEO_INFO_HEIGHT (dfs->ininfo);
    size = width * height * sizeof (float);

  } else if (gst_structure_has_name (structure, "dfs/point-cloud")) {
    GST_INFO_OBJECT (dfs, "Uses SYSTEM memory");
    pool = gst_buffer_pool_new ();
    guint width = GST_VIDEO_INFO_WIDTH (dfs->ininfo) / 2;
    guint height = GST_VIDEO_INFO_HEIGHT (dfs->ininfo);
    // Setting initial size of buffer to worst case size.
    // since point cloud is not known ahead of time and is not constant.
    size = (width * height) + PLY_HEADER_SIZE;
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
    GST_WARNING_OBJECT (dfs, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  return pool;
}

static gboolean
gst_dfs_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (dfs, "Failed to parse the decide_allocation caps!");
    return FALSE;
  }
  // Invalidate the cached pool if there is an allocation_query.
  if (dfs->outpool) {
    gst_buffer_pool_set_active (dfs->outpool, FALSE);
    gst_object_unref (dfs->outpool);
  }
  // Create a new buffer pool.
  pool = gst_dfs_create_pool (dfs, caps);
  dfs->outpool = pool;

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

  return TRUE;
}

static GstFlowReturn
gst_dfs_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstBufferPool *pool = dfs->outpool;
  GstFlowReturn ret = GST_FLOW_OK;

  if (gst_base_transform_is_passthrough (trans)) {
    GST_LOG_OBJECT (dfs, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (dfs, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  ret = gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (dfs, "Failed to create output buffer!");
    return GST_FLOW_ERROR;
  }
  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS),
      0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_dfs_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstCaps *result = NULL;

  GST_DEBUG_OBJECT (dfs, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (dfs, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (trans);
    result = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (trans);
    result = gst_pad_get_pad_template_caps (pad);
  }

  if (filter != NULL) {
    GstCaps *intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (dfs, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_dfs_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstStructure *output = NULL;
  const GValue *value = NULL;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  output = gst_caps_get_structure (outcaps, 0);

  if (gst_structure_has_name (output, "video/x-raw")) {

    // Fixate the output format.
    value = gst_structure_get_value (output, "format");

    if (!gst_value_is_fixed (value)) {
      gst_structure_fixate_field (output, "format");
      value = gst_structure_get_value (output, "format");
    }

    GST_DEBUG_OBJECT (dfs, "Output format fixed to: %s",
        g_value_get_string (value));

    gint width = 0, height = 0;
    GstVideoInfo ininfo;

    if (!gst_video_info_from_caps (&ininfo, incaps))
      GST_ERROR_OBJECT (dfs, "Failed to get input video info from caps!");


    width = GST_VIDEO_INFO_WIDTH (&ininfo) / 2;
    height = GST_VIDEO_INFO_HEIGHT (&ininfo);

    value = gst_structure_get_value (output, "width");
    if ((NULL == value) || !gst_value_is_fixed (value)) {
      gst_structure_set (output, "width", G_TYPE_INT, width, NULL);
      value = gst_structure_get_value (output, "width");
    }
    width = g_value_get_int (value);

    value = gst_structure_get_value (output, "height");
    if ((NULL == value) || !gst_value_is_fixed (value)) {
      gst_structure_set (output, "height", G_TYPE_INT, height, NULL);
      value = gst_structure_get_value (output, "height");
    }
    height = g_value_get_int (value);

    GST_DEBUG_OBJECT (dfs, "Output width and height fixated to: %dx%d",
        width, height);
  } else {
    outcaps = gst_caps_fixate (outcaps);
  }

  GST_DEBUG_OBJECT (dfs, "Fixated caps to %" GST_PTR_FORMAT, outcaps);

  return outcaps;
}

static GstFlowReturn
gst_dfs_transform (GstBaseTransform * trans, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstVideoMeta *vmeta = NULL;
  DfsInitSettings settings;
  GstVideoFrame inframe;
  GstMapInfo out_info0;
  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_STIME_NONE;

  vmeta = gst_buffer_get_video_meta (inbuffer);

  //Check if Engine has been init already
  if (dfs->engine == NULL) {
    settings.mode = dfs->output_mode;
    settings.format = dfs->format;
    settings.stereo_frame_width = vmeta->width;
    settings.stereo_frame_height = vmeta->height;
    settings.stride = vmeta->stride[0]; //Only need Y-plane
    settings.dfs_mode = dfs->dfs_mode;
    settings.min_disparity = dfs->min_disparity;
    settings.num_disparity_levels = dfs->num_disparity_levels;
    settings.filter_width = dfs->filter_width;
    settings.filter_height = dfs->filter_height;
    settings.rectification = dfs->rectification;
    settings.gpu_rect = dfs->gpu_rect;
    settings.stereo_parameter = dfs->stereo_parameter;

    ts_begin = gst_util_get_timestamp ();

    dfs->engine = gst_dfs_engine_new (&settings);
    if (dfs->engine == NULL) {
      GST_ERROR_OBJECT (dfs, "Failed to create DFS engine!");
      return GST_FLOW_ERROR;
    }

    ts_end = gst_util_get_timestamp ();
    tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

    GST_INFO ("DFS init time: %" G_GINT64_FORMAT ".%03"
        G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
        (GST_TIME_AS_USECONDS (tsdelta) % 1000));
  }

  if (!gst_video_frame_map (&inframe, dfs->ininfo, inbuffer,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR_OBJECT (dfs, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map_range (outbuffer, 0, 1, &out_info0, GST_MAP_READWRITE)) {
    gst_video_frame_unmap (&inframe);
    GST_ERROR_OBJECT (dfs, "Failed to map output buffer!");
    return GST_FLOW_ERROR;
  }

  ts_begin = gst_util_get_timestamp ();
  if (!gst_dfs_engine_execute (dfs->engine, &inframe, out_info0.data)) {
    GST_ERROR_OBJECT (dfs, "Failed to execute engine");;
  }

  ts_end = gst_util_get_timestamp ();
  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  GST_INFO ("DFS time: %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  gst_buffer_unmap (outbuffer, &out_info0);
  gst_video_frame_unmap (&inframe);

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_dfs_change_state (GstElement * element, GstStateChange transition)
{
  GstDfs *dfs = GST_DFS_CAST (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (dfs, "Failure");
    return ret;
  }

  return ret;
}


static void
gst_dfs_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDfs *dfs = GST_DFS (object);

  GST_OBJECT_LOCK (dfs);
  switch (property_id) {
    case PROP_MODE:
      dfs->dfs_mode = (DFSMode) g_value_get_enum (value);
      break;
    case PROP_MIN_DISPARITY:
      dfs->min_disparity = g_value_get_int (value);
      break;
    case PROP_NUM_DISPARITY_LEVELS:
      dfs->num_disparity_levels = g_value_get_int (value);
      break;
    case PROP_FILTER_WIDTH:
      dfs->filter_width = g_value_get_int (value);
      break;
    case PROP_FILTER_HEIGHT:
      dfs->filter_height = g_value_get_int (value);
      break;
    case PROP_RECTIFICATION:
      dfs->rectification = g_value_get_boolean (value);
      break;
    case PROP_CONFIG_PATH:
      dfs->config_location = g_strdup(g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (dfs);
}

static void
gst_dfs_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstDfs *dfs = GST_DFS (object);

  GST_OBJECT_LOCK (dfs);
  switch (property_id) {
    case PROP_MODE:
      g_value_set_enum (value, dfs->dfs_mode);
      break;
    case PROP_MIN_DISPARITY:
      g_value_set_int (value, dfs->min_disparity);
      break;
    case PROP_NUM_DISPARITY_LEVELS:
      g_value_set_int (value, dfs->num_disparity_levels);
      break;
    case PROP_FILTER_WIDTH:
      g_value_set_int (value, dfs->filter_width);
      break;
    case PROP_FILTER_HEIGHT:
      g_value_set_int (value, dfs->filter_height);
      break;
    case PROP_RECTIFICATION:
      g_value_set_boolean (value, dfs->rectification);
      break;
    case PROP_CONFIG_PATH:
      g_value_set_string (value, dfs->config_location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (dfs);
}

static gboolean
gst_dfs_set_caps (GstBaseTransform * trans, GstCaps * incaps, GstCaps * outcaps)
{
  GstDfs *dfs = GST_DFS_CAST (trans);
  GstVideoInfo ininfo;
  GstStructure *structure = NULL;

  if (!gst_video_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (dfs, "Failed to get input video info from caps!");
    return FALSE;
  }
  // Get the output caps structure in order to determine the mode.
  structure = gst_caps_get_structure (outcaps, 0);

  if (gst_structure_has_name (structure, "video/x-raw")) {
    dfs->output_mode = OUTPUT_MODE_VIDEO;
    dfs->format =
        gst_video_format_from_string (gst_structure_get_string (structure,
            "format"));
  } else if (gst_structure_has_name (structure, "dfs/disparity-map")) {
    dfs->output_mode = OUTPUT_MODE_DISPARITY;
  } else if (gst_structure_has_name (structure, "dfs/point-cloud")) {
    dfs->output_mode = OUTPUT_MODE_POINT_CLOUD;
  }

  if (dfs->rectification == TRUE && dfs->dfs_mode == MODE_SPEED)
    dfs->gpu_rect = TRUE;

  //Populate stereo parameter values
  dfs->stereo_parameter.camera[0].pixelWidth =
      GST_VIDEO_INFO_WIDTH (&ininfo) / 2;
  dfs->stereo_parameter.camera[0].pixelHeight = GST_VIDEO_INFO_HEIGHT (&ininfo);
  dfs->stereo_parameter.camera[1].pixelWidth =
      GST_VIDEO_INFO_WIDTH (&ininfo) / 2;
  dfs->stereo_parameter.camera[1].pixelHeight = GST_VIDEO_INFO_HEIGHT (&ininfo);
  if (dfs->rectification) {
    if (!gst_dfs_parse_config (dfs->config_location, &dfs->stereo_parameter)) {
      GST_ERROR_OBJECT (dfs, "Error parsing config file");
      return FALSE;
    }
  } else {
    GST_INFO_OBJECT (dfs, "rectification=false. Config file not parsed.");
  }

  dfs->ininfo = gst_video_info_copy (&ininfo);

  gst_base_transform_set_passthrough (trans, FALSE);

  return TRUE;
}

static void
gst_dfs_finalize (GObject * object)
{
  GstDfs *dfs = GST_DFS (object);

  if (dfs->outpool != NULL)
    gst_object_unref (dfs->outpool);

  if (dfs->ininfo != NULL)
    gst_video_info_free (dfs->ininfo);

  if (dfs->engine != NULL)
    gst_dfs_engine_free (dfs->engine);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (dfs));
}

static void
gst_dfs_class_init (GstDfsClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_dfs_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_dfs_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_dfs_finalize);


  g_object_class_install_property (gobject, PROP_MODE,
      g_param_spec_enum ("dfs-mode", "dfs-mode",
          "Select DFS mode", GST_TYPE_DFS_MODE, DEFAULT_PROP_MODE,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_MIN_DISPARITY,
      g_param_spec_int ("min-disparity", "min-disparity",
          "Set min disparity", 0, 240, DEFAULT_PROP_MIN_DISPARITY,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_NUM_DISPARITY_LEVELS,
      g_param_spec_int ("num-disparity-level", "num-disparity-level",
          "Set disparirty level. Distinct disparity levels between"
          "neighboring pixels. Multiples of 16",
          16, 256, DEFAULT_PROP_NUM_DISPARITY_LEVELS,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_FILTER_WIDTH,
      g_param_spec_int ("filter-width", "filter-width",
          "Set filter width. Controls window size for guided filter"
          "used in DFS implementation. Must be odd number. Max value"
          "should be < image width", 1, INT_MAX, DEFAULT_PROP_FILTER_WIDTH,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_FILTER_HEIGHT,
      g_param_spec_int ("filter-height", "filter-height",
          "Set filter height. Controls window size for guided filter"
          "used in DFS implementation. Must be odd number. Max value"
          "should be < image height", 1, INT_MAX, DEFAULT_PROP_FILTER_HEIGHT,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_RECTIFICATION,
      g_param_spec_boolean ("rectification", "rectification",
          "Perform rectification on input frames.", DEFAULT_PROP_RECTIFICATION,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_CONFIG_PATH,
      g_param_spec_string("config", "Path to stereo config file",
          "Path to config file. Eg.: /data/stereo.config", DEFAULT_CONFIG_PATH,
          (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element, "Depth From Stereo",
      "Runs Depth From Stereo (DFS) algorithm",
      "Calculates disparity map a pair of stereo images", "QTI");

  gst_element_class_add_pad_template (element, gst_dfs_sink_template ());
  gst_element_class_add_pad_template (element, gst_dfs_src_template ());

  element->change_state = GST_DEBUG_FUNCPTR (gst_dfs_change_state);

  trans->decide_allocation = GST_DEBUG_FUNCPTR (gst_dfs_decide_allocation);
  trans->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_dfs_prepare_output_buffer);

  trans->transform_caps = GST_DEBUG_FUNCPTR (gst_dfs_transform_caps);
  trans->fixate_caps = GST_DEBUG_FUNCPTR (gst_dfs_fixate_caps);
  trans->set_caps = GST_DEBUG_FUNCPTR (gst_dfs_set_caps);
  trans->transform = GST_DEBUG_FUNCPTR (gst_dfs_transform);
}

static void
gst_dfs_init (GstDfs * dfs)
{
  dfs->ininfo = NULL;
  dfs->outpool = NULL;
  dfs->engine = NULL;
  dfs->config_location = DEFAULT_CONFIG_PATH;
  dfs->output_mode = DEFAULT_OUTPUT_MODE;

  dfs->dfs_mode = DEFAULT_PROP_MODE;
  dfs->min_disparity = DEFAULT_PROP_MIN_DISPARITY;
  dfs->num_disparity_levels = DEFAULT_PROP_NUM_DISPARITY_LEVELS;
  dfs->filter_width = DEFAULT_PROP_FILTER_WIDTH;
  dfs->filter_height = DEFAULT_PROP_FILTER_HEIGHT;
  dfs->rectification = DEFAULT_PROP_RECTIFICATION;
  dfs->gpu_rect = DEFAULT_PROP_GPU_RECT;

  GST_DEBUG_CATEGORY_INIT (gst_dfs_debug, "qtidfs", 0, "DFS");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtidfs", GST_RANK_PRIMARY,
      GST_TYPE_DFS);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtidfs,
    "DFS",
    plugin_init,
    PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_SUMMARY, PACKAGE_ORIGIN)
