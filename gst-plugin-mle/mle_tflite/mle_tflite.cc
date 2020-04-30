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
*/

#include <json/json.h>
#include <cstring>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mle_tflite.h"
#include "deeplearning_engine/tflite_base.h"

#define GST_CAT_DEFAULT mle_tflite_debug
GST_DEBUG_CATEGORY_STATIC (mle_tflite_debug);

#define gst_mle_tflite_parent_class parent_class
G_DEFINE_TYPE (GstMLETFLite, gst_mle_tflite, GST_TYPE_VIDEO_FILTER);

#define GST_ML_VIDEO_FORMATS "{ NV12, NV21 }"

#define DEFAULT_PROP_MLE_TFLITE_CONF_THRESHOLD 0.5
#define DEFAULT_PROP_MLE_TFLITE_PREPROCESSING_TYPE 0
#define DEFAULT_TFLITE_NUM_THREADS 2
#define GST_MLE_UNUSED(var) ((void)var)

enum {
  PROP_0,
  PROP_MLE_PARSE_CONFIG,
  PROP_MLE_MODEL_FILENAME,
  PROP_MLE_LABELS_FILENAME,
  PROP_MLE_PREPROCESSING_TYPE,
  PROP_MLE_CONF_THRESHOLD,
  PROP_MLE_TFLITE_USE_NNAPI,
  PROP_MLE_TFLITE_NUM_THREADS,
};


static GstStaticCaps gst_mle_tflite_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_ML_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_ML_VIDEO_FORMATS));

static void
gst_mle_tflite_set_property_mask(guint &mask, guint property_id)
{
  mask |= 1 << property_id;
}

static gboolean
gst_mle_check_is_set(guint &mask, guint property_id)
{
  return (mask & 1 << property_id) ? true:false;
}

static void
gst_mle_tflite_set_property(GObject *object, guint property_id,
                            const GValue *value, GParamSpec *pspec)
{
  GstMLETFLite *mle = GST_MLE_TFLITE(object);

  GST_OBJECT_LOCK (mle);
  switch (property_id) {
    case PROP_MLE_PARSE_CONFIG:
      gst_mle_tflite_set_property_mask(mle->property_mask, property_id);
      mle->config_location = g_strdup(g_value_get_string (value));
      break;
    case PROP_MLE_PREPROCESSING_TYPE:
      gst_mle_tflite_set_property_mask(mle->property_mask, property_id);
      mle->preprocessing_type = g_value_get_uint (value);
      break;
    case PROP_MLE_MODEL_FILENAME:
      gst_mle_tflite_set_property_mask(mle->property_mask, property_id);
      mle->model_filename = g_strdup(g_value_get_string (value));
      break;
    case PROP_MLE_LABELS_FILENAME:
      gst_mle_tflite_set_property_mask(mle->property_mask, property_id);
      mle->labels_filename = g_strdup(g_value_get_string (value));
      break;
    case PROP_MLE_CONF_THRESHOLD:
      gst_mle_tflite_set_property_mask(mle->property_mask, property_id);
      mle->conf_threshold = g_value_get_float (value);
      break;
    case PROP_MLE_TFLITE_USE_NNAPI:
      gst_mle_tflite_set_property_mask(mle->property_mask, property_id);
      mle->use_nnapi = g_value_get_uint (value);
      break;
    case PROP_MLE_TFLITE_NUM_THREADS:
      gst_mle_tflite_set_property_mask(mle->property_mask, property_id);
      mle->num_threads = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mle);
}

static void
gst_mle_tflite_get_property(GObject *object, guint property_id,
                            GValue *value, GParamSpec *pspec)
{
  GstMLETFLite *mle = GST_MLE_TFLITE (object);

  GST_OBJECT_LOCK (mle);
  switch (property_id) {
    case PROP_MLE_PARSE_CONFIG:
      g_value_set_string (value, mle->config_location);
      break;
    case PROP_MLE_PREPROCESSING_TYPE:
      g_value_set_uint (value, mle->preprocessing_type);
      break;
    case PROP_MLE_MODEL_FILENAME:
      g_value_set_string (value, mle->model_filename);
      break;
    case PROP_MLE_LABELS_FILENAME:
      g_value_set_string (value, mle->labels_filename);
      break;
    case PROP_MLE_CONF_THRESHOLD:
      g_value_set_float (value, mle->conf_threshold);
      break;
    case PROP_MLE_TFLITE_USE_NNAPI:
      g_value_set_uint (value, mle->use_nnapi);
      break;
    case PROP_MLE_TFLITE_NUM_THREADS:
      g_value_set_uint (value, mle->num_threads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mle);
}

static void
gst_mle_tflite_finalize(GObject * object)
{
  GstMLETFLite *mle = GST_MLE_TFLITE (object);

  if (mle->engine) {
    mle->engine->Deinit();
    mle->engine = nullptr;
  }
  if (mle->model_filename) {
    g_free(mle->model_filename);
  }
  if (mle->labels_filename) {
    g_free(mle->labels_filename);
  }

  G_OBJECT_CLASS(parent_class)->finalize(G_OBJECT(mle));
}

static GstCaps *
gst_mle_tflite_caps(void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;
  if (g_once_init_enter(&inited)) {
    caps = gst_static_caps_get(&gst_mle_tflite_format_caps);
    g_once_init_leave(&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_mle_src_template(void)
{
  return gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_mle_tflite_caps());
}

static GstPadTemplate *
gst_mle_sink_template (void)
{
  return gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_mle_tflite_caps ());
}

static gboolean
gst_mle_tflite_parse_config(gchar *config_location,
                            mle::MLConfig &configuration) {
  gboolean rc = FALSE;
  std::ifstream in(config_location, std::ios::in | std::ios::binary);
  if (in) {
    Json::Reader reader;
    Json::Value val;
    if (reader.parse(in, val)) {
      configuration.conf_threshold = val.get("ConfThreshold", 0.0).asFloat();
      configuration.model_file = val.get("MODEL_FILENAME", "").asString();
      configuration.labels_file = val.get("LABELS_FILENAME", "").asString();
      configuration.number_of_threads = val.get("NUM_THREADS", 2).asInt();
      configuration.use_nnapi = val.get("USE_NNAPI", 0).asInt();
      rc = TRUE;
    }
    in.close();

  }
  return rc;
}

static gboolean
gst_mle_create_engine(GstMLETFLite *mle) {
  gboolean rc = TRUE;
  gboolean parse = TRUE;

  // Configuration structure for MLE
  // The order of priority is: default values < configuration file < property
  mle::MLConfig configuration {};

  // Set default configuration values
  configuration.conf_threshold = mle->conf_threshold;
  configuration.use_nnapi = mle->use_nnapi;
  configuration.number_of_threads = mle->num_threads;
  configuration.preprocess_mode =
      (mle::PreprocessingMode)mle->preprocessing_type;

  // Set configuration values from json config file
  if (mle->config_location) {
    parse = gst_mle_tflite_parse_config(mle->config_location, configuration);
    if (FALSE == parse) {
      GST_DEBUG_OBJECT(mle, "Parsing configuration failed.");
    } else {
      GST_DEBUG_OBJECT(mle, "Parsing from file is successful!");
    }
  }

  // Set configuration values only if property is set
  if (gst_mle_check_is_set(mle->property_mask, PROP_MLE_MODEL_FILENAME)) {
    configuration.model_file = mle->model_filename;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_MLE_LABELS_FILENAME)) {
    configuration.labels_file = mle->labels_filename;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_MLE_CONF_THRESHOLD)) {
    configuration.conf_threshold = mle->conf_threshold;
  }

  if (gst_mle_check_is_set(mle->property_mask, PROP_MLE_TFLITE_NUM_THREADS)) {
    configuration.number_of_threads = mle->num_threads;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_MLE_TFLITE_USE_NNAPI)) {
    configuration.use_nnapi = mle->use_nnapi;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_MLE_PREPROCESSING_TYPE)) {
    configuration.preprocess_mode =
        (mle::PreprocessingMode)mle->preprocessing_type;
  }
  mle->engine = new mle::TFLBase(configuration);
  if (nullptr == mle->engine) {
    GST_ERROR_OBJECT (mle, "Failed to create TFLite instance.");
    rc = FALSE;
  }

  return rc;
}

static mle::MLEImageFormat
gst_mle_get_video_format(GstVideoFormat &format)
{
  mle::MLEImageFormat mle_format = mle::MLEImageFormat::mle_format_invalid;
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      mle_format = mle::MLEImageFormat::mle_format_nv12;
      break;
    case GST_VIDEO_FORMAT_NV21:
      mle_format = mle::MLEImageFormat::mle_format_nv21;
      break;
    default:
      mle_format = mle::MLEImageFormat::mle_format_invalid;
  }
  return mle_format;
}

static gboolean
gst_mle_tflite_set_info(GstVideoFilter *filter, GstCaps *in,
                        GstVideoInfo *ininfo, GstCaps *out,
                        GstVideoInfo *outinfo)
{
  GST_MLE_UNUSED(in);
  GST_MLE_UNUSED(out);
  GST_MLE_UNUSED(outinfo);

  gboolean rc = TRUE;
  GstMLETFLite *mle = GST_MLE_TFLITE (filter);
  GstVideoFormat video_format = GST_VIDEO_INFO_FORMAT(ininfo);

  if (mle->engine && mle->is_init) {
    if ((gint)mle->source_info.width != GST_VIDEO_INFO_WIDTH(ininfo) ||
        (gint)mle->source_info.height != GST_VIDEO_INFO_HEIGHT(ininfo) ||
        (gint)mle->source_info.stride != GST_VIDEO_INFO_PLANE_STRIDE(ininfo, 0) ||
        mle->source_info.format != gst_mle_get_video_format(video_format)) {
      mle->engine->Deinit();
      mle->engine = nullptr;
      mle->is_init = FALSE;
    } else {
      return TRUE;
    }
  }

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);

  mle->source_info.width = GST_VIDEO_INFO_WIDTH(ininfo);
  mle->source_info.height = GST_VIDEO_INFO_HEIGHT(ininfo);
  mle->source_info.stride = GST_VIDEO_INFO_PLANE_STRIDE(ininfo, 0);
  mle->source_info.scanline = GST_VIDEO_INFO_HEIGHT(ininfo);
  mle->source_info.format = gst_mle_get_video_format(video_format);
  if (mle->source_info.format != mle::MLEImageFormat::mle_format_nv12 &&
      mle->source_info.format != mle::MLEImageFormat::mle_format_nv21) {
    GST_ERROR_OBJECT (mle, "Video format not supported %d", video_format);
    return FALSE;
  }

  rc = gst_mle_create_engine(mle);
  if (FALSE == rc) {
    GST_ERROR_OBJECT (mle, "Failed to create MLE instance.");
    return rc;
  }

  gint ret = mle->engine->Init(&mle->source_info);
  if (ret) {
    GST_ERROR_OBJECT (mle, "MLE init failed.");
    rc = FALSE;
  } else {
    GST_DEBUG_OBJECT (mle, "MLE instance created addr %p", mle->engine);
    mle->is_init = TRUE;
  }

  return rc;
}

static GstFlowReturn gst_mle_tflite_transform_frame_ip(GstVideoFilter *filter,
                                                       GstVideoFrame *frame)
{
  GstMemory *memory = NULL;
  GstMLETFLite *mle = GST_MLE_TFLITE (filter);

  memory = gst_buffer_peek_memory (frame->buffer, 0);
  if (!gst_is_fd_memory (memory)) {
    return GST_FLOW_ERROR;
  }

  mle->source_frame.fd = gst_fd_memory_get_fd (memory);
  mle->source_frame.frame_data[0] = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  mle->source_frame.frame_data[1] = GST_VIDEO_FRAME_COMP_DATA (frame, 1);

  gint ret = mle->engine->Process(&mle->source_frame, frame->buffer);
  if (ret) {
    GST_ERROR_OBJECT (mle, "MLE Process failed.");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static void
gst_mle_tflite_class_init (GstMLETFLiteClass * klass)
{
  GObjectClass *gobject            = G_OBJECT_CLASS (klass);
  GstElementClass *element         = GST_ELEMENT_CLASS (klass);
  GstVideoFilterClass *filter      = GST_VIDEO_FILTER_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR(gst_mle_tflite_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR(gst_mle_tflite_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR(gst_mle_tflite_finalize);

  g_object_class_install_property(
      gobject,
      PROP_MLE_PARSE_CONFIG,
      g_param_spec_string(
          "config",
          "Path to config file",
          "Path to JSON file. Eg.: /data/misc/camera/mle_tflite_config.json",
          NULL,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS )));

  g_object_class_install_property(
      gobject,
      PROP_MLE_MODEL_FILENAME,
      g_param_spec_string(
          "model",
          "Model file",
          "Model .tflite file",
          NULL,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_MLE_LABELS_FILENAME,
      g_param_spec_string(
          "labels",
          "Labels filename",
          ".txt labels file",
          NULL,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_MLE_PREPROCESSING_TYPE,
      g_param_spec_uint(
          "maintain-ar",
          "Maintain AR",
          "Pre-processing AR maintenance",
          0,
          2,
          DEFAULT_PROP_MLE_TFLITE_PREPROCESSING_TYPE,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_MLE_CONF_THRESHOLD,
      g_param_spec_float(
          "conf-threshold",
          "ConfThreshold",
          "Confidence Threshold value",
          0.0,
          1.0,
          DEFAULT_PROP_MLE_TFLITE_CONF_THRESHOLD,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_MLE_TFLITE_USE_NNAPI,
      g_param_spec_uint(
          "use-nnapi",
          "TFLite NN API",
          "USE NN API",
          0,
          1,
          0,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_MLE_TFLITE_NUM_THREADS,
      g_param_spec_uint(
          "num-threads",
          "TFLite number of threads",
          "Number of threads",
          0,
          10,
          DEFAULT_TFLITE_NUM_THREADS,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata(
      element, "MLE TFLite", "Execute TFLite NN models",
      "Pre-process, execute NN model, post-process", "QTI");

  gst_element_class_add_pad_template(element,
                                     gst_mle_sink_template());
  gst_element_class_add_pad_template(element,
                                     gst_mle_src_template());

  filter->set_info = GST_DEBUG_FUNCPTR (gst_mle_tflite_set_info);
  filter->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_mle_tflite_transform_frame_ip);
}

static void
gst_mle_tflite_init (GstMLETFLite * mle)
{
  mle->engine = nullptr;
  mle->config_location = nullptr;
  mle->is_init = FALSE;

  mle->preprocessing_type = DEFAULT_PROP_MLE_TFLITE_PREPROCESSING_TYPE;
  mle->conf_threshold = DEFAULT_PROP_MLE_TFLITE_CONF_THRESHOLD;
  mle->num_threads = DEFAULT_TFLITE_NUM_THREADS;
  mle->use_nnapi = 0;

  GST_DEBUG_CATEGORY_INIT (mle_tflite_debug, "mletflite", 0,
      "QTI Machine Learning Engine");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "mletflite", GST_RANK_PRIMARY,
                               GST_TYPE_MLE_TFLITE);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mletflite,
    "Machine Learning Engine TFLite",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
