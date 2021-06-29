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
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cvp-optclflow.h"
#include <gst/memory/gstionpool.h>

#define GST_CAT_DEFAULT cvp_optclflow_debug
GST_DEBUG_CATEGORY_STATIC (cvp_optclflow_debug);

#define gst_cvp_optclflow_parent_class parent_class
G_DEFINE_TYPE (GstCVPOPTCLFLOW, gst_cvp_optclflow, GST_TYPE_BASE_TRANSFORM);

#define GST_ML_VIDEO_FORMATS "{ GRAT8BIT, NV12, NV21 }"

#define GST_CVP_UNUSED(var) ((void)var)

#define DEFAULT_MIN_BUFFERS 2
#define DEFAULT_MAX_BUFFERS 10

enum {
  PROP_0,
  PROP_CVP_SET_OUTPUT,
  PROP_CVP_STATS_ENABLE
};


static GstStaticCaps gst_cvp_optclflow_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_ML_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_ML_VIDEO_FORMATS));


static GstStaticCaps gst_cvp_optclflow_format_caps_src =
    GST_STATIC_CAPS ("cvp/optiflow");

static void
gst_cvp_optclflow_set_property_mask (guint &mask, guint property_id)
{
  mask |= 1 << property_id;
}

static void
gst_cvp_optclflow_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec *pspec)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW (object);

  GST_OBJECT_LOCK (cvp);
  switch (property_id) {
    case PROP_CVP_SET_OUTPUT:
      gst_cvp_optclflow_set_property_mask (cvp->property_mask, property_id);
      cvp->output_location = g_strdup (g_value_get_string (value));
      break;
    case PROP_CVP_STATS_ENABLE:
      gst_cvp_optclflow_set_property_mask (cvp->property_mask, property_id);
      cvp->stats_enable = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (cvp);
}

static void
gst_cvp_optclflow_get_property (GObject * object, guint property_id,
                            GValue * value, GParamSpec * pspec)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW (object);

  GST_OBJECT_LOCK (cvp);
  switch (property_id) {
    case PROP_CVP_SET_OUTPUT:
      g_value_set_string (value, cvp->output_location);
      break;
    case PROP_CVP_STATS_ENABLE:
      g_value_set_boolean (value, cvp->stats_enable);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (cvp);
}

static void
gst_cvp_optclflow_finalize (GObject * object)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW (object);

  if (cvp->engine) {
    if (cvp->engine->Deinit ())
      GST_ERROR_OBJECT (cvp, "Optical flow engine deinit failed");

    delete (cvp->engine);
    cvp->engine = nullptr;
  }

  if (cvp->output_location)
    g_free(cvp->output_location);

  if (cvp->ininfo != NULL)
    gst_video_info_free (cvp->ininfo);

  if (cvp->outinfo != NULL)
    gst_video_info_free (cvp->outinfo);

  if (cvp->outpool != NULL)
    gst_object_unref (cvp->outpool);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT(cvp));
}

static GstCaps *
gst_cvp_optclflow_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;
  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_cvp_optclflow_format_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_cvp_optclflow_caps_src (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;
  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_cvp_optclflow_format_caps_src);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_cvp_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_cvp_optclflow_caps_src ());
}

static GstPadTemplate *
gst_cvp_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_cvp_optclflow_caps ());
}

static cvp::CVPImageFormat
gst_cvp_get_video_format (GstCVPOPTCLFLOW *cvp, GstVideoFormat &format)
{
  cvp::CVPImageFormat cvp_format = cvp::CVPImageFormat::cvp_format_invalid;
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      cvp_format = cvp::CVPImageFormat::cvp_format_nv12;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      cvp_format = cvp::CVPImageFormat::cvp_format_gray8bit;
      break;
    case GST_VIDEO_FORMAT_NV21:
      cvp_format = cvp::CVPImageFormat::cvp_format_nv21;
      break;
    default:
      cvp_format = cvp::CVPImageFormat::cvp_format_invalid;
      GST_WARNING_OBJECT (cvp, "Invalid image format");
  }
  return cvp_format;
}

static gboolean
gst_cvp_check_is_set (guint &mask, guint property_id)
{
  return (mask & 1 << property_id) ? true:false;
}

static gboolean
gst_cvp_create_engine (GstCVPOPTCLFLOW *cvp) {
  gboolean rc = TRUE;
  cvp::CVPConfig configuration {};

  // Set default configuration values
  configuration.stats_enable = false;
  configuration.output_location = nullptr;
  configuration.source_info = cvp->source_info;
  configuration.source_info.format = cvp::cvp_format_gray8bit;

  // Set configuration values only if property is set
  if (gst_cvp_check_is_set (cvp->property_mask, PROP_CVP_SET_OUTPUT))
    configuration.output_location = cvp->output_location;

  if (gst_cvp_check_is_set (cvp->property_mask, PROP_CVP_STATS_ENABLE))
    configuration.stats_enable = cvp->stats_enable;

  // Print config
  GST_DEBUG_OBJECT (cvp, "==== Configuration Begin ====");
  GST_DEBUG_OBJECT (cvp, "Width  %d", configuration.source_info.width);
  GST_DEBUG_OBJECT (cvp, "Height %d", configuration.source_info.height);
  GST_DEBUG_OBJECT (cvp, "Format %d", configuration.source_info.format);
  GST_DEBUG_OBJECT (cvp, "FPS    %d", configuration.source_info.fps);
  if (configuration.output_location != nullptr)
    GST_DEBUG_OBJECT (cvp, "Output %s", configuration.output_location);
  else
    GST_DEBUG_OBJECT (cvp, "No output location");

  GST_DEBUG_OBJECT (cvp, "Stats %s",
      configuration.stats_enable ? "enabled" : "disabled");

  // Set engine
  cvp->engine = new cvp::OFEngine (configuration);
  if (nullptr == cvp->engine) {
    GST_ERROR_OBJECT (cvp, "Failed to create CVP instance");
    rc = FALSE;
  }

  return rc;
}

static GstBufferPool *
gst_cvp_optclflow_create_pool (GstCVPOPTCLFLOW * cvp, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GValue memoryblocks = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;

  gint width_alligned = (cvp->source_info.width + 64-1) & ~(64-1);
  gint height_alligned = (cvp->source_info.height + 64-1) & ~(64-1);
  gint size = width_alligned * height_alligned / 64;

  // If downstream allocation query supports GBM, allocate gbm memory.
  pool = gst_ion_buffer_pool_new ();
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps,
      sizeof (cvpMotionVector) * size +
      sizeof (cvpOFStats) * size, DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  g_value_init (&memoryblocks, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_UINT);

  // Set memory block 1
  g_value_set_uint (&value, sizeof (cvpMotionVector) * size);
  gst_value_array_append_value (&memoryblocks, &value);
  // Set memory block 2
  g_value_set_uint (&value, sizeof (cvpOFStats) * size);
  gst_value_array_append_value (&memoryblocks, &value);

  gst_structure_set_value (config, "memory-blocks", &memoryblocks);

  allocator = gst_fd_allocator_new ();
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (cvp, "Failed to set pool configuration!");
    gst_structure_free (config);
    g_object_unref (pool);
    pool = NULL;
  }

  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_cvp_optclflow_propose_allocation (GstBaseTransform * trans,
    GstQuery * inquery, GstQuery * outquery)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW_CAST (trans);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstVideoInfo info;
  guint size = 0;
  gboolean needpool = FALSE;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (
        trans, inquery, outquery))
    return FALSE;

  // No input query, nothing to do.
  if (NULL == inquery)
    return TRUE;

  // Extract caps from the query.
  gst_query_parse_allocation (outquery, &caps, &needpool);

  if (NULL == caps) {
    GST_ERROR_OBJECT (cvp, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (cvp, "Failed to get video info!");
    return FALSE;
  }

  // Get the size from video info.
  size = GST_VIDEO_INFO_SIZE (&info);

  if (needpool) {
    GstStructure *structure = NULL;

    pool = gst_cvp_optclflow_create_pool (cvp, caps);
    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (cvp, "Failed to set buffer pool configuration!");
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
gst_cvp_optclflow_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW_CAST (trans);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (cvp, "Failed to parse the decide_allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (cvp->outpool) {
    gst_buffer_pool_set_active (cvp->outpool, FALSE);
    gst_object_unref (cvp->outpool);
  }

  // Create a new buffer pool.
  pool = gst_cvp_optclflow_create_pool (cvp, caps);
  cvp->outpool = pool;

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

  return TRUE;
}

static GstFlowReturn
gst_cvp_optclflow_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW_CAST (trans);
  GstBufferPool *pool = cvp->outpool;
  GstFlowReturn ret = GST_FLOW_OK;

  if (gst_base_transform_is_passthrough (trans)) {
    GST_LOG_OBJECT (cvp, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (cvp, "Failed to activate output video buffer pool!");
    return GST_FLOW_ERROR;
  }

  ret = gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (cvp, "Failed to create output video buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS),
      0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_cvp_optclflow_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW_CAST (trans);
  GstCaps *result = NULL;

  GST_DEBUG_OBJECT (cvp, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (cvp, "Filter caps: %" GST_PTR_FORMAT, filter);

  if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (trans);
    result = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (trans);
    result = gst_pad_get_pad_template_caps (pad);
  }

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (cvp, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_cvp_optclflow_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  (void)outcaps;
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW_CAST (trans);
  GstVideoInfo ininfo, outinfo;

  if (!gst_video_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (cvp, "Failed to get input video info from caps!");
    return FALSE;
  }

  gst_base_transform_set_passthrough (trans, FALSE);

  if (cvp->ininfo != NULL)
    gst_video_info_free (cvp->ininfo);

  cvp->ininfo = gst_video_info_copy (&ininfo);

  if (cvp->outinfo != NULL)
    gst_video_info_free (cvp->outinfo);

  cvp->outinfo = gst_video_info_copy (&outinfo);

  GstVideoFormat video_format = GST_VIDEO_INFO_FORMAT (&ininfo);


  cvp->source_info.width = GST_VIDEO_INFO_WIDTH (&ininfo);
  cvp->source_info.height = GST_VIDEO_INFO_HEIGHT (&ininfo);
  cvp->source_info.format = gst_cvp_get_video_format (cvp, video_format);
  cvp->source_info.stride = GST_VIDEO_INFO_PLANE_STRIDE (cvp->ininfo, 0);
  cvp->source_info.ininfo = cvp->ininfo;
  cvp->source_info.fps =
      GST_VIDEO_INFO_FPS_N (cvp->ininfo)/GST_VIDEO_INFO_FPS_D (cvp->ininfo);

  if (cvp->engine && cvp->is_init) {
    if ((gint) cvp->source_info.width != GST_VIDEO_INFO_WIDTH (&ininfo) ||
        (gint) cvp->source_info.height != GST_VIDEO_INFO_HEIGHT (&ininfo) ||
        cvp->source_info.format != gst_cvp_get_video_format (cvp, video_format)) {
      GST_DEBUG_OBJECT (cvp, "Reinitializing due to source change.");
      cvp->engine->Deinit ();
      delete (cvp->engine);
      cvp->engine = nullptr;
      cvp->is_init = FALSE;
    } else {
      GST_DEBUG_OBJECT (cvp, "Already initialized.");
      return TRUE;
    }
  }

  if (cvp->source_info.format != cvp::CVPImageFormat::cvp_format_nv12 &&
      cvp->source_info.format != cvp::CVPImageFormat::cvp_format_gray8bit) {
    GST_ERROR_OBJECT (cvp, "Video format not supported %d", video_format);
    return FALSE;
  }

  if (!gst_cvp_create_engine (cvp)) {
    GST_ERROR_OBJECT (cvp, "Failed to create CVP instance.");
    return FALSE;
  }

  if (cvp->engine->Init ()) {
    GST_ERROR_OBJECT (cvp, "CVP init failed.");
    delete (cvp->engine);
    cvp->engine = nullptr;
    return FALSE;
  } else {
    GST_DEBUG_OBJECT (cvp, "CVP instance created addr %p", cvp->engine);
    cvp->is_init = TRUE;
  }

  return TRUE;
}

static GstCaps *
gst_cvp_optclflow_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW_CAST (trans);
  (void)direction;

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  GST_DEBUG_OBJECT (cvp, "Trying to fixate output caps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, outcaps, incaps);

  GST_DEBUG_OBJECT (cvp, "Fixated caps to %" GST_PTR_FORMAT, outcaps);

  return outcaps;
}

static GstFlowReturn
gst_cvp_optclflow_transform (GstBaseTransform * trans, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW_CAST (trans);

  GstVideoFrame frame;
  guint instride = 0;
  gst_video_frame_map (&frame, cvp->source_info.ininfo, inbuffer, GST_MAP_READ);
  instride = GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0);
  gst_video_frame_unmap (&frame);

  // Needed when is using GBM.
  // The stride is not available before start stream.
  if (instride != cvp->source_info.stride) {
    CVP_LOGI ("%s: Stride is different (%d), reinit CVP", __func__, instride);
    cvp->source_info.stride = instride;

    cvp->engine->Deinit ();
    delete (cvp->engine);
    cvp->engine = nullptr;
    cvp->is_init = FALSE;

    if (!gst_cvp_create_engine (cvp)) {
      GST_ERROR_OBJECT (cvp, "Failed to create CVP instance.");
      return GST_FLOW_ERROR;
    }

    if (cvp->engine->Init ()) {
      GST_ERROR_OBJECT (cvp, "CVP init failed.");
      delete (cvp->engine);
      cvp->engine = nullptr;
      return GST_FLOW_ERROR;
    } else {
      GST_DEBUG_OBJECT (cvp, "CVP instance created addr %p", cvp->engine);
      cvp->is_init = TRUE;
    }
  }

  gint ret = cvp->engine->Process (inbuffer, outbuffer);
  if (ret) {
    GST_ERROR_OBJECT (cvp, "CVP Process failed.");
    return GST_FLOW_ERROR;
  }
  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_cvp_optclflow_change_state (GstElement * element, GstStateChange transition)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW_CAST (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (cvp, "Failure");
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      // Flush cvp engine
      cvp->engine->Flush ();
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_cvp_optclflow_class_init (GstCVPOPTCLFLOWClass * klass)
{
  GObjectClass *gobject            = G_OBJECT_CLASS (klass);
  GstElementClass *element         = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_finalize);

  g_object_class_install_property (
      gobject,
      PROP_CVP_SET_OUTPUT,
      g_param_spec_string (
          "output",
          "Path to store output file",
          "An existing Path to store output file. Eg.: /data/output",
          NULL,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS )));
  g_object_class_install_property (
      gobject,
      PROP_CVP_STATS_ENABLE,
      g_param_spec_boolean (
          "stats-enable",
          "Enable stats",
          "Enable stats for additional motion vector info",
          false,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (
      element, "CVP Optical Flow", "Runs optical flow from CVP",
      "Calculate motion vector from current image and previous image", "QTI");

  gst_element_class_add_pad_template (element, gst_cvp_sink_template());
  gst_element_class_add_pad_template (element, gst_cvp_src_template());

  element->change_state = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_change_state);
  trans->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_cvp_optclflow_propose_allocation);
  trans->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_cvp_optclflow_decide_allocation);
  trans->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_cvp_optclflow_prepare_output_buffer);
  trans->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cvp_optclflow_transform_caps);
  trans->fixate_caps = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_fixate_caps);
  trans->set_caps = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_set_caps);
  trans->transform = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_transform);
}

static void
gst_cvp_optclflow_init (GstCVPOPTCLFLOW * cvp)
{
  cvp->engine = nullptr;
  cvp->is_init = FALSE;

  cvp->output_location = nullptr;
  cvp->stats_enable = false;

  GST_DEBUG_CATEGORY_INIT (cvp_optclflow_debug, "qticvpoptclflow", 0,
      "QTI Computer Vision Processor Optical Flow");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qticvpoptclflow", GST_RANK_PRIMARY,
                               GST_TYPE_CVP_OPTCLFLOW);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  qticvpoptclflow,
  "Computer Vision Processor Optical Flow",
  plugin_init,
  PACKAGE_VERSION,
  PACKAGE_LICENSE,
  PACKAGE_SUMMARY,
  PACKAGE_ORIGIN
)
