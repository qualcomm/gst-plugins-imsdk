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
#include <fstream>
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mle_snpe.h"
#include "deeplearning_engine/snpe_base.h"
#include "deeplearning_engine/snpe_complex.h"
#include "deeplearning_engine/snpe_single_ssd.h"

#define GST_CAT_DEFAULT mle_snpe_debug
GST_DEBUG_CATEGORY_STATIC (mle_snpe_debug);

#define gst_mle_snpe_parent_class parent_class
G_DEFINE_TYPE (GstMLESNPE, gst_mle_snpe, GST_TYPE_VIDEO_FILTER);

#define GST_ML_VIDEO_FORMATS "{ NV12, NV21 }"

#define DEFAULT_PROP_SNPE_INPUT_FORMAT 3 //kBgrFloat
#define DEFAULT_PROP_SNPE_OUTPUT 1 //kMulti
#define DEFAULT_PROP_SNPE_IO_TYPE 0 //kUserBufer
#define DEFAULT_PROP_SNPE_MEAN_VALUE 128.0
#define DEFAULT_PROP_SNPE_SIGMA_VALUE 255.0
#define DEFAULT_PROP_SNPE_USE_NORM 1
#define DEFAULT_PROP_SNPE_RUNTIME 1
#define DEFAULT_PROP_MLE_CONF_THRESHOLD 0.5
#define DEFAULT_PROP_MLE_PREPROCESSING_TYPE 0
#define GST_MLE_UNUSED(var) ((void)var)

enum {
  PROP_0,
  PROP_MLE_PARSE_CONFIG,
  PROP_MLE_FRAMEWORK_TYPE,
  PROP_MLE_MODEL_FILENAME,
  PROP_MLE_LABELS_FILENAME,
  PROP_SNPE_INPUT_FORMAT,
  PROP_SNPE_OUTPUT,
  PROP_SNPE_IO_TYPE,
  PROP_SNPE_MEAN_BLUE,
  PROP_SNPE_MEAN_GREEN,
  PROP_SNPE_MEAN_RED,
  PROP_SNPE_SIGMA_BLUE,
  PROP_SNPE_SIGMA_GREEN,
  PROP_SNPE_SIGMA_RED,
  PROP_SNPE_USE_NORM,
  PROP_SNPE_RUNTIME,
  PROP_SNPE_OUTPUT_LAYERS,
  PROP_SNPE_RESULT_LAYERS,
  PROP_MLE_PREPROCESSING_TYPE,
  PROP_MLE_CONF_THRESHOLD,
};


static GstStaticCaps gst_mle_snpe_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_ML_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_ML_VIDEO_FORMATS));

static void
gst_mle_set_property_mask(guint &mask, guint property_id)
{
  mask |= 1 << property_id;
}

static gboolean
gst_mle_check_is_set(guint &mask, guint property_id)
{
  return (mask & 1 << property_id) ? true:false;
}

static void
gst_mle_snpe_set_property(GObject *object, guint property_id,
                          const GValue *value, GParamSpec *pspec)
{
  GstMLESNPE *mle = GST_MLE_SNPE (object);

  GST_OBJECT_LOCK (mle);
  switch (property_id) {
    case PROP_MLE_PARSE_CONFIG:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->config_location = g_strdup(g_value_get_string (value));
      break;
    case PROP_MLE_PREPROCESSING_TYPE:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->preprocessing_type = g_value_get_uint (value);
      break;
    case PROP_MLE_MODEL_FILENAME:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->model_filename = g_strdup(g_value_get_string (value));
      break;
    case PROP_MLE_LABELS_FILENAME:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->labels_filename = g_strdup(g_value_get_string (value));
      break;
    case PROP_SNPE_INPUT_FORMAT:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->input_format = g_value_get_uint (value);
      break;
    case PROP_SNPE_OUTPUT:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->output = g_value_get_uint (value);
      break;
    case PROP_SNPE_IO_TYPE:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->io_type = g_value_get_uint (value);
      break;
    case PROP_SNPE_MEAN_BLUE:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->blue_mean = g_value_get_float (value);
      break;
    case PROP_SNPE_MEAN_GREEN:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->green_mean = g_value_get_float (value);
      break;
    case PROP_SNPE_MEAN_RED:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->red_mean = g_value_get_float (value);
      break;
    case PROP_SNPE_SIGMA_BLUE:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->blue_sigma = g_value_get_float (value);
      break;
    case PROP_SNPE_SIGMA_GREEN:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->green_sigma = g_value_get_float (value);
      break;
    case PROP_SNPE_SIGMA_RED:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->red_sigma = g_value_get_float (value);
      break;
    case PROP_SNPE_USE_NORM:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->use_norm = g_value_get_uint (value);
      break;
    case PROP_SNPE_RUNTIME:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->runtime = g_value_get_uint (value);
      break;
    case PROP_SNPE_OUTPUT_LAYERS:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->output_layers = g_strdup(g_value_get_string (value));
      break;
    case PROP_SNPE_RESULT_LAYERS:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->result_layers = g_strdup(g_value_get_string (value));
      break;
    case PROP_MLE_CONF_THRESHOLD:
      gst_mle_set_property_mask(mle->property_mask, property_id);
      mle->conf_threshold = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mle);
}

static void
gst_mle_snpe_get_property(GObject *object, guint property_id,
                          GValue *value, GParamSpec *pspec)
{
  GstMLESNPE *mle = GST_MLE_SNPE (object);

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
    case PROP_SNPE_INPUT_FORMAT:
      g_value_set_uint (value, mle->input_format);
      break;
    case PROP_SNPE_OUTPUT:
      g_value_set_uint (value, mle->output);
      break;
    case PROP_SNPE_IO_TYPE:
      g_value_set_uint (value, mle->io_type);
      break;
    case PROP_SNPE_MEAN_BLUE:
      g_value_set_float (value, mle->blue_mean);
      break;
    case PROP_SNPE_MEAN_GREEN:
      g_value_set_float (value, mle->green_mean);
      break;
    case PROP_SNPE_MEAN_RED:
      g_value_set_float (value, mle->red_mean);
      break;
    case PROP_SNPE_SIGMA_BLUE:
      g_value_set_float (value, mle->blue_sigma);
      break;
    case PROP_SNPE_SIGMA_GREEN:
      g_value_set_float (value, mle->green_sigma);
      break;
    case PROP_SNPE_SIGMA_RED:
      g_value_set_float (value, mle->red_sigma);
      break;
    case PROP_SNPE_USE_NORM:
      g_value_set_uint (value, mle->use_norm);
      break;
    case PROP_SNPE_RUNTIME:
      g_value_set_uint (value, mle->runtime);
      break;
    case PROP_SNPE_OUTPUT_LAYERS:
      g_value_set_string (value, mle->output_layers);
      break;
    case PROP_SNPE_RESULT_LAYERS:
      g_value_set_string (value, mle->result_layers);
      break;
    case PROP_MLE_CONF_THRESHOLD:
      g_value_set_float (value, mle->conf_threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mle);
}

static void
gst_mle_snpe_finalize(GObject * object)
{
  GstMLESNPE *mle = GST_MLE_SNPE (object);

  if (mle->engine) {
    mle->engine->Deinit();
    mle->engine = nullptr;
  }
  if (mle->output_layers) {
    g_free(mle->output_layers);
  }
  if (mle->result_layers) {
    g_free(mle->result_layers);
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
gst_mle_snpe_caps(void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;
  if (g_once_init_enter(&inited)) {
    caps = gst_static_caps_get(&gst_mle_snpe_format_caps);
    g_once_init_leave(&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_mle_src_template(void)
{
  return gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_mle_snpe_caps());
}

static GstPadTemplate *
gst_mle_sink_template (void)
{
  return gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_mle_snpe_caps ());
}

static gboolean
gst_mle_snpe_parse_config(gchar *config_location,
                          mle::MLConfig &configuration) {
  gboolean rc = FALSE;
  std::ifstream in(config_location, std::ios::in | std::ios::binary);
  if (in) {
    Json::Reader reader;
    Json::Value val;
    if (reader.parse(in, val)) {
      configuration.engine_output = (mle::EngineOutput)val.get("EngineOutput", 0).asInt();
      configuration.io_type = (mle::NetworkIO)val.get("NetworkIO", 0).asInt();
      configuration.input_format =
          (mle::InputFormat)val.get("InputFormat", 3).asInt();
      configuration.blue_mean = val.get("BlueMean", 0).asFloat();
      configuration.blue_sigma = val.get("BlueSigma", 255).asFloat();
      configuration.green_mean = val.get("GreenMean", 0).asFloat();
      configuration.green_sigma = val.get("GreenSigma", 255).asFloat();
      configuration.red_mean = val.get("RedMean", 0).asFloat();
      configuration.red_sigma = val.get("RedSigma", 255).asFloat();
      configuration.use_norm = val.get("UseNorm", 0).asInt();
      configuration.conf_threshold = val.get("ConfThreshold", 0.0).asFloat();
      configuration.model_file = val.get("MODEL_FILENAME", "").asString();
      configuration.labels_file = val.get("LABELS_FILENAME", "").asString();
      for (size_t i = 0; i < val["OutputLayers"].size(); i++) {
        configuration.output_layers.push_back(val["OutputLayers"][i].asString());
      }
      for (size_t i = 0; i < val["ResultLayers"].size(); i++) {
        configuration.result_layers.push_back(val["ResultLayers"][i].asString());
      }
      configuration.runtime = (mle::RuntimeType)val.get("Runtime", 0).asInt();
      rc = TRUE;
    }
    in.close();
  }
  return rc;
}

static void
gst_mle_parse_snpe_layers(gchar *src, std::vector<std::string> &dst)
{
  gchar *pch;
  gchar *saveptr;

  if (src) {
    pch = strtok_r(src, " ,", &saveptr);
    while (pch != NULL) {
      dst.push_back(pch);
      pch = strtok_r(NULL, " ,", &saveptr);
    }
  }
}

static gboolean
gst_mle_create_engine(GstMLESNPE *mle) {
  gboolean rc = TRUE;
  gboolean parse = TRUE;

  // Configuration structure for MLE
  // The order of priority is: default values < configuration file < property
  mle::MLConfig configuration {};

  // Set default configuration values
  configuration.blue_mean = configuration.green_mean = configuration.red_mean =
      DEFAULT_PROP_SNPE_MEAN_VALUE;
  configuration.blue_sigma = configuration.green_sigma =
      configuration.red_sigma = DEFAULT_PROP_SNPE_SIGMA_VALUE;
  configuration.engine_output = (mle::EngineOutput)mle->output;
  configuration.input_format = (mle::InputFormat)mle->input_format;
  configuration.use_norm = mle->use_norm;
  configuration.runtime = (mle::RuntimeType)mle->runtime;
  configuration.preprocess_mode =
      (mle::PreprocessingMode)mle->preprocessing_type;
  configuration.conf_threshold = DEFAULT_PROP_MLE_CONF_THRESHOLD;

  // Set configuration values from json config file
  if (mle->config_location) {
    parse = gst_mle_snpe_parse_config(mle->config_location, configuration);
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

  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_OUTPUT)) {
    configuration.engine_output = mle::EngineOutput(mle->output);
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_INPUT_FORMAT)) {
    configuration.input_format = (mle::InputFormat)mle->input_format;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_IO_TYPE)) {
    configuration.io_type = (mle::NetworkIO) mle->io_type;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_MEAN_BLUE)) {
    configuration.blue_mean = mle->blue_mean;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_MEAN_GREEN)) {
    configuration.green_mean = mle->green_mean;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_MEAN_RED)) {
    configuration.red_mean = mle->red_mean;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_SIGMA_BLUE)) {
    configuration.blue_sigma = mle->blue_sigma;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_SIGMA_GREEN)) {
    configuration.green_sigma = mle->green_sigma;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_SIGMA_RED)) {
    configuration.red_sigma = mle->red_sigma;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_USE_NORM)) {
    configuration.use_norm = mle->use_norm;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_SNPE_RUNTIME)) {
    configuration.runtime = (mle::RuntimeType) mle->runtime;
  }
  if (gst_mle_check_is_set(mle->property_mask, PROP_MLE_PREPROCESSING_TYPE)) {
    configuration.preprocess_mode =
        (mle::PreprocessingMode)mle->preprocessing_type;
  }
  gst_mle_parse_snpe_layers(mle->output_layers, configuration.output_layers);
  gst_mle_parse_snpe_layers(mle->result_layers, configuration.result_layers);
  switch (configuration.engine_output) {
    case mle::EngineOutput::kSingle: {
      mle->engine = new mle::SNPEBase(configuration);
      if (nullptr == mle->engine) {
        GST_ERROR_OBJECT (mle, "Failed to create SNPE instance.");
        rc = FALSE;
      }
      break;
    }
    case mle::EngineOutput::kMulti: {
      mle->engine = new mle::SNPEComplex(configuration);
      if (nullptr == mle->engine) {
        GST_ERROR_OBJECT (mle, "Failed to create SNPE instance.");
        rc = FALSE;
      }
      break;
    }
    case mle::EngineOutput::kSingleSSD: {
      mle->engine = new mle::SNPESingleSSD(configuration);
      if (nullptr == mle->engine) {
        GST_ERROR_OBJECT (mle, "Failed to create SNPE instance.");
        rc = FALSE;
      }
      break;
    }
    default: {
      GST_ERROR_OBJECT (mle, "Unknown SNPE output type.");
      rc = FALSE;
    }
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
gst_mle_snpe_set_info(GstVideoFilter *filter, GstCaps *in,
                      GstVideoInfo *ininfo, GstCaps *out,
                      GstVideoInfo *outinfo)
{
  GST_MLE_UNUSED(in);
  GST_MLE_UNUSED(out);
  GST_MLE_UNUSED(outinfo);

  gboolean rc = TRUE;
  GstMLESNPE *mle = GST_MLE_SNPE (filter);
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

static GstFlowReturn gst_mle_snpe_transform_frame_ip(GstVideoFilter * filter,
                                                     GstVideoFrame * frame)
{
  GstMLESNPE *mle = GST_MLE_SNPE (filter);

  mle->source_frame.frame_data[0] = (uint8_t*)GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  mle->source_frame.frame_data[1] = (uint8_t*)GST_VIDEO_FRAME_PLANE_DATA (frame, 1);

  gint ret = mle->engine->Process(&mle->source_frame, frame->buffer);
  if (ret) {
    GST_ERROR_OBJECT (mle, "MLE Process failed.");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static void
gst_mle_snpe_class_init (GstMLESNPEClass * klass)
{
  GObjectClass *gobject            = G_OBJECT_CLASS (klass);
  GstElementClass *element         = GST_ELEMENT_CLASS (klass);
  GstVideoFilterClass *filter      = GST_VIDEO_FILTER_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR(gst_mle_snpe_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR(gst_mle_snpe_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR(gst_mle_snpe_finalize);

  g_object_class_install_property(
      gobject,
      PROP_MLE_PARSE_CONFIG,
      g_param_spec_string(
          "config",
          "Path to config file",
          "Path to JSON file. Eg.: /data/misc/camera/mle_snpe_config.json",
          NULL,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS )));

  g_object_class_install_property(
      gobject,
      PROP_MLE_MODEL_FILENAME,
      g_param_spec_string(
          "model",
          "Model file",
          "Model .dlc file",
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
      PROP_SNPE_INPUT_FORMAT,
      g_param_spec_uint(
          "input-format",
          "SNPE input format",
          "0 - RGB; 1 - BGR; 2 - RGBFloat; 3 - BGRFloat",
          0,
          3,
          DEFAULT_PROP_SNPE_INPUT_FORMAT,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_OUTPUT,
      g_param_spec_uint(
          "output",
          "SNPE output",
          "Model output type: Eg.: 0 - classification; 1 - SSD",
          0,
          3,
          DEFAULT_PROP_SNPE_OUTPUT,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_IO_TYPE,
      g_param_spec_uint(
          "io-type",
          "SNPE IO type",
          "UserBuffer or ITensor",
          0,
          1,
          DEFAULT_PROP_SNPE_IO_TYPE,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_MEAN_BLUE,
      g_param_spec_float(
          "blue-mean",
          "SNPE Blue mean",
          "Blue mean value",
          0,
          255,
          DEFAULT_PROP_SNPE_MEAN_VALUE,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_MEAN_GREEN,
      g_param_spec_float(
          "green-mean",
          "SNPE Green mean",
          "Green mean value",
          0,
          255,
          DEFAULT_PROP_SNPE_MEAN_VALUE,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_MEAN_RED,
      g_param_spec_float(
          "red-mean",
          "SNPE Red mean",
          "Red mean value",
          0,
          255,
          DEFAULT_PROP_SNPE_MEAN_VALUE,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_SIGMA_BLUE,
      g_param_spec_float(
          "blue-sigma",
          "Blue sigma",
          "Divisor of blue channel for norm",
          0,
          255,
          DEFAULT_PROP_SNPE_SIGMA_VALUE,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_SIGMA_GREEN,
      g_param_spec_float(
          "green-sigma",
          "Green sigma",
          "Divisor of green channel for norm",
          0,
          255,
          DEFAULT_PROP_SNPE_SIGMA_VALUE,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_SIGMA_RED,
      g_param_spec_float(
          "red-sigma",
          "Red sigma",
          "Divisor of red channel for norm",
          0.0,
          255.0,
          DEFAULT_PROP_SNPE_SIGMA_VALUE,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_USE_NORM,
      g_param_spec_uint(
          "norm",
          "Use normalization",
          "0 - do not use; 1 - use normalization",
          0,
          1,
          DEFAULT_PROP_SNPE_USE_NORM,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_RUNTIME,
      g_param_spec_uint(
          "runtime",
          "SNPE Runtime",
          "0 - CPU; 1 - DSP",
          0,
          1,
          DEFAULT_PROP_SNPE_RUNTIME,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_OUTPUT_LAYERS,
      g_param_spec_string(
          "output-layers",
          "SNPE output layers",
          "Model output layers, comma separated",
          NULL,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
      gobject,
      PROP_SNPE_RESULT_LAYERS,
      g_param_spec_string(
          "result-layers",
          "SNPE result layers",
          "Model result layers, comma separated",
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
          DEFAULT_PROP_MLE_PREPROCESSING_TYPE,
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
          DEFAULT_PROP_MLE_CONF_THRESHOLD,
          static_cast<GParamFlags>(G_PARAM_READWRITE |
                                   G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata(
      element, "MLE SNPE", "Execute SNPE NN models",
      "Pre-process, execute NN model, post-process", "QTI");

  gst_element_class_add_pad_template(element,
                                     gst_mle_sink_template());
  gst_element_class_add_pad_template(element,
                                     gst_mle_src_template());

  filter->set_info = GST_DEBUG_FUNCPTR (gst_mle_snpe_set_info);
  filter->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_mle_snpe_transform_frame_ip);
}

static void
gst_mle_snpe_init (GstMLESNPE * mle)
{
  mle->engine = nullptr;
  mle->config_location = nullptr;
  mle->is_init = FALSE;
  mle->input_format = DEFAULT_PROP_SNPE_INPUT_FORMAT;
  mle->output = DEFAULT_PROP_SNPE_OUTPUT;
  mle->io_type = DEFAULT_PROP_SNPE_IO_TYPE;
  mle->blue_mean = mle->green_mean = mle->red_mean =
      DEFAULT_PROP_SNPE_MEAN_VALUE;
  mle->blue_sigma = mle->green_sigma = mle->red_sigma =
      DEFAULT_PROP_SNPE_SIGMA_VALUE;
  mle->use_norm = 0;
  mle->output_layers = nullptr;
  mle->result_layers = nullptr;
  mle->runtime = DEFAULT_PROP_SNPE_RUNTIME;
  mle->preprocessing_type = DEFAULT_PROP_MLE_PREPROCESSING_TYPE;
  mle->conf_threshold = DEFAULT_PROP_MLE_CONF_THRESHOLD;

  GST_DEBUG_CATEGORY_INIT (mle_snpe_debug, "mlesnpe", 0,
      "QTI Machine Learning Engine");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "mlesnpe", GST_RANK_PRIMARY,
                               GST_TYPE_MLE_SNPE);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mlesnpe,
    "Machine Learning Engine SNPE",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
