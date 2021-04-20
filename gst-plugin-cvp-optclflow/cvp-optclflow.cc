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

#define GST_CAT_DEFAULT cvp_optclflow_debug
GST_DEBUG_CATEGORY_STATIC (cvp_optclflow_debug);

#define gst_cvp_optclflow_parent_class parent_class
G_DEFINE_TYPE (GstCVPOPTCLFLOW, gst_cvp_optclflow, GST_TYPE_VIDEO_FILTER);

#define GST_ML_VIDEO_FORMATS "{ GRAT8BIT, NV12, NV21 }"

#define DEFAULT_PROP_CVP_FPS 30

#define GST_CVP_UNUSED(var) ((void)var)

enum {
  PROP_0,
  PROP_CVP_SET_OUTPUT,
  PROP_CVP_STATS_ENABLE,
  PROP_CVP_FPS
};


static GstStaticCaps gst_cvp_optclflow_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_ML_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_ML_VIDEO_FORMATS));

static void
gst_cvp_optclflow_set_property_mask(guint &mask, guint property_id)
{
  mask |= 1 << property_id;
}

static void
gst_cvp_optclflow_set_property(GObject *object, guint property_id,
                            const GValue *value, GParamSpec *pspec)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW(object);

  GST_OBJECT_LOCK (cvp);
  switch (property_id) {
    case PROP_CVP_SET_OUTPUT:
      gst_cvp_optclflow_set_property_mask(cvp->property_mask, property_id);
      cvp->output_location = g_strdup(g_value_get_string (value));
      break;
    case PROP_CVP_STATS_ENABLE:
      gst_cvp_optclflow_set_property_mask(cvp->property_mask, property_id);
      cvp->stats_enable = g_value_get_boolean (value);
      break;
    case PROP_CVP_FPS:
      gst_cvp_optclflow_set_property_mask(cvp->property_mask, property_id);
      cvp->fps = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (cvp);
}

static void
gst_cvp_optclflow_get_property(GObject *object, guint property_id,
                            GValue *value, GParamSpec *pspec)
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
    case PROP_CVP_FPS:
      g_value_set_uint (value, cvp->fps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (cvp);
}

static void
gst_cvp_optclflow_finalize(GObject * object)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW (object);

  if (cvp->engine) {
    cvp->engine->Deinit();
    delete (cvp->engine);
    cvp->engine = nullptr;
  }

  if (cvp->output_location) {
    g_free(cvp->output_location);
  }

  G_OBJECT_CLASS(parent_class)->finalize(G_OBJECT(cvp));
}

static GstCaps *
gst_cvp_optclflow_caps(void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;
  if (g_once_init_enter(&inited)) {
    caps = gst_static_caps_get(&gst_cvp_optclflow_format_caps);
    g_once_init_leave(&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_cvp_src_template(void)
{
  return gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_cvp_optclflow_caps());
}

static GstPadTemplate *
gst_cvp_sink_template (void)
{
  return gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_cvp_optclflow_caps ());
}

static cvp::CVPImageFormat
gst_cvp_get_video_format(GstVideoFormat &format)
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
  }
  return cvp_format;
}

static gboolean
gst_cvp_check_is_set(guint &mask, guint property_id)
{
  return (mask & 1 << property_id) ? true:false;
}

static gboolean
gst_cvp_create_engine(GstCVPOPTCLFLOW *cvp) {
  gboolean rc = TRUE;
  cvp::CVPConfig configuration {};

  // Set default configuration values
  configuration.fps = DEFAULT_PROP_CVP_FPS;
  configuration.stats_enable = false;
  configuration.output_location = nullptr;
  configuration.source_info = cvp->source_info;
  configuration.source_info.format = cvp::  cvp_format_gray8bit;

  // Set configuration values only if property is set
  if (gst_cvp_check_is_set(cvp->property_mask, PROP_CVP_SET_OUTPUT)) {
    configuration.output_location = cvp->output_location;
  }
  if (gst_cvp_check_is_set(cvp->property_mask, PROP_CVP_STATS_ENABLE)) {
    configuration.stats_enable = cvp->stats_enable;
  }
  if (gst_cvp_check_is_set(cvp->property_mask, PROP_CVP_FPS)) {
     configuration.fps = cvp->fps;
  }

  // Print config
  GST_DEBUG_OBJECT(cvp, "==== Configuration Begin ====");
  GST_DEBUG_OBJECT(cvp, "Width  %d", configuration.source_info.width);
  GST_DEBUG_OBJECT(cvp, "Height %d", configuration.source_info.height);
  GST_DEBUG_OBJECT(cvp, "Format %d", configuration.source_info.format);
  GST_DEBUG_OBJECT(cvp, "FPS    %d", configuration.fps);
  if (configuration.output_location != nullptr) {
    GST_DEBUG_OBJECT(cvp, "Output %s", configuration.output_location);
  }
  else {
    GST_DEBUG_OBJECT(cvp, "No output location");
  }
  if (configuration.stats_enable) {
    GST_DEBUG_OBJECT(cvp, "Enable stats");
  }
  else {
    GST_DEBUG_OBJECT(cvp, "Disable stats");
  }

  // Set engine
  cvp->engine = new cvp::OFEngine(configuration);
  if (nullptr == cvp->engine) {
    GST_ERROR_OBJECT (cvp, "Failed to create CVP instance");
    rc = FALSE;
  }

  return rc;
}

static gboolean
gst_cvp_optclflow_set_info(GstVideoFilter *filter, GstCaps *in,
                        GstVideoInfo *ininfo, GstCaps *out,
                        GstVideoInfo *outinfo)
{
  GST_CVP_UNUSED(in);
  GST_CVP_UNUSED(out);
  GST_CVP_UNUSED(outinfo);

  gboolean rc = TRUE;

  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW (filter);
  GstVideoFormat video_format = GST_VIDEO_INFO_FORMAT(ininfo);

  cvp->source_info.width = GST_VIDEO_INFO_WIDTH(ininfo);
  cvp->source_info.height = GST_VIDEO_INFO_HEIGHT(ininfo);
  cvp->source_info.format = gst_cvp_get_video_format(video_format);

  if (cvp->engine && cvp->is_init) {
    if ((gint)cvp->source_info.width != GST_VIDEO_INFO_WIDTH(ininfo) ||
        (gint)cvp->source_info.height != GST_VIDEO_INFO_HEIGHT(ininfo) ||
        cvp->source_info.format != gst_cvp_get_video_format(video_format)) {
      GST_DEBUG_OBJECT(cvp, "Reinitializing due to source change.");
      cvp->engine->Deinit();
      delete (cvp->engine);
      cvp->engine = nullptr;
      cvp->is_init = FALSE;
    } else {
      GST_DEBUG_OBJECT(cvp, "Already initialized.");
      return TRUE;
    }
  }

  if (cvp->source_info.format != cvp::CVPImageFormat::cvp_format_nv12 &&
      cvp->source_info.format != cvp::CVPImageFormat::cvp_format_gray8bit) {
    GST_ERROR_OBJECT (cvp, "Video format not supported %d", video_format);
    return FALSE;
  }

  rc = gst_cvp_create_engine(cvp);
  if (FALSE == rc) {
    GST_ERROR_OBJECT (cvp, "Failed to create CVP instance.");
    return rc;
  }

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);

  gint ret = cvp->engine->Init();
  if (ret) {
    GST_ERROR_OBJECT  (cvp, "CVP init failed.");
    delete (cvp->engine);
    cvp->engine = nullptr;
    rc = FALSE;
  } else {
    GST_DEBUG_OBJECT (cvp, "CVP instance created addr %p", cvp->engine);
    cvp->is_init = TRUE;
  }

  return rc;
}

static GstFlowReturn gst_cvp_optclflow_transform_frame_ip(GstVideoFilter *filter,
                                                       GstVideoFrame *frame)
{
  GstCVPOPTCLFLOW *cvp = GST_CVP_OPTCLFLOW (filter);
  gint ret = cvp->engine->Process(frame);
  if (ret) {
    GST_ERROR_OBJECT (cvp, "CVP Process failed.");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static void
gst_cvp_optclflow_class_init (GstCVPOPTCLFLOWClass * klass)
{
  GObjectClass *gobject            = G_OBJECT_CLASS (klass);
  GstElementClass *element         = GST_ELEMENT_CLASS (klass);
  GstVideoFilterClass *filter      = GST_VIDEO_FILTER_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR(gst_cvp_optclflow_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR(gst_cvp_optclflow_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR(gst_cvp_optclflow_finalize);

  g_object_class_install_property(
      gobject,
      PROP_CVP_SET_OUTPUT,
      g_param_spec_string(
          "output",
          "Path to store output file",
          "An existing Path to store output file. Eg.: /data/output",
          NULL,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS )));
  g_object_class_install_property(
      gobject,
      PROP_CVP_STATS_ENABLE,
      g_param_spec_boolean(
          "stats-enable",
          "Enable stats",
          "Enable stats for additional motion vector info",
          false,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_CVP_FPS,
      g_param_spec_uint(
          "fps",
          "Frame per second",
          "Input frame per second for video",
          1,
          60,
          DEFAULT_PROP_CVP_FPS,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata(
      element, "CVP Optical Flow", "Runs optical flow from CVP",
      "Calculate motion vector from current image and previous image", "QTI");

  gst_element_class_add_pad_template(element,
                                     gst_cvp_sink_template());
  gst_element_class_add_pad_template(element,
                                     gst_cvp_src_template());

  filter->set_info = GST_DEBUG_FUNCPTR (gst_cvp_optclflow_set_info);
  filter->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_cvp_optclflow_transform_frame_ip);
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
