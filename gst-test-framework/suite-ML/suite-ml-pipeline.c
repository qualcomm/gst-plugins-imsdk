/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "suite-ml-pipeline.h"

#include <string.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstbufferstraw.h>
#include <gst/video/video.h>
#include <gst/base/gstbasesink.h>
#include <gst/video/gstvideometa.h>

#define ML_DETECTION_CHECK_BUFFER_COUNT    300

/**
 * Get enum for property nick name
 *
 * @param element Plugin to query the property.
 * @param prop_name Property Name.
 * @param prop_value_nick Property Nick Name.
 */
static gint
get_enum_value (GstElement * element, const gchar * prop_name,
    const gchar * prop_value_nick) {
  GParamSpec **property_specs;
  GObject *obj = G_OBJECT (element);
  GObjectClass *obj_class = G_OBJECT_GET_CLASS (element);
  guint num_properties, i;
  gint ret = -1;

  property_specs = g_object_class_list_properties (obj_class, &num_properties);

  for (i = 0; i < num_properties; i++) {
    GParamSpec *param = property_specs[i];
    GEnumValue *values;
    GType owner_type = param->owner_type;
    guint j = 0;

    // Only need pad properties.
    if (obj == NULL && (owner_type == G_TYPE_OBJECT ||
        owner_type == GST_TYPE_OBJECT || owner_type == GST_TYPE_PAD))
      continue;

    if (strcmp (prop_name, param->name) ||
        !G_IS_PARAM_SPEC_ENUM (param))
      continue;

    values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
    while (values[j].value_name) {
      if (!strcmp (prop_value_nick, values[j].value_nick)) {
        ret = values[j].value;
        break;
      }
      j++;
    }
  }

  g_free (property_specs);
  return ret;
}

static gboolean
ml_video_set_mltflite_property (GstElement *mltflite,
    GstMLDelegate delegate) {
  GstStructure *delegate_options = NULL;
  GstMLTFLiteDelegate tflite_delegate;

  if (mltflite == NULL)
    return FALSE;

  if (delegate == GST_ML_DELEGATE_CPU) {
    tflite_delegate = GST_ML_TFLITE_DELEGATE_NONE;
  } else if (delegate == GST_ML_DELEGATE_GPU) {
    tflite_delegate = GST_ML_TFLITE_DELEGATE_GPU;
  } else if (delegate == GST_ML_DELEGATE_DSP) {
    tflite_delegate = GST_ML_TFLITE_DELEGATE_EXTERNAL;
    delegate_options = gst_structure_from_string (
        "QNNExternalDelegate,backend_type=htp;", NULL);

    g_object_set (G_OBJECT (mltflite),
        "external-delegate-path", "libQnnTFLiteDelegate.so",
        "external-delegate-options", delegate_options, NULL);
  }  else {
    return FALSE;
  }

  g_object_set (G_OBJECT (mltflite), "delegate", tflite_delegate, NULL);

  return TRUE;
}

static gboolean
ml_video_set_mlqnn_property (GstElement *mlqnn,
    GstMLDelegate delegate) {
  gchar *backend = NULL;

  if (mlqnn == NULL)
    return FALSE;

  if (delegate == GST_ML_DELEGATE_CPU)
    backend = TF_ML_QNN_CPU_BACKEND;
  else if (delegate == GST_ML_DELEGATE_GPU)
    backend = TF_ML_QNN_GPU_BACKEND;
  else if (delegate == GST_ML_DELEGATE_DSP)
    backend = TF_ML_QNN_HTP_BACKEND;
  else
    return FALSE;

  g_object_set (G_OBJECT (mlqnn), "backend", backend, NULL);

  return TRUE;
}

static gint
ml_video_get_moduleid (GstElement *postproc,
    GstMLModuleType type) {
  gchar* name = NULL;
  gint moduleid = 0;

  if (postproc == NULL)
    return moduleid;

  switch (type) {
    case GST_ML_MODULE_YOLO_V8:
      name= "yolov8";
      break;
    default:
      break;
  }

  moduleid = get_enum_value (postproc, "module", name);

  return moduleid;
}

static gboolean
ml_video_detection_check (GstMLVideoInfo* vinfo,
    GstBuffer *buf, gint idx) {
  gint i = 0, count = 0, rois = 0;

  if (vinfo == NULL || buf == NULL)
    return FALSE;

  count = sizeof (vinfo->frameinfo) / sizeof (vinfo->frameinfo[0]);
  // Get roi number from buffer.
  rois = gst_buffer_get_n_meta (buf,
        GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

  for (i = 0; i < count; i++) {
    if (idx == vinfo->frameinfo[i].index  &&
        rois != vinfo->frameinfo[i].metanum)
      return FALSE;
  }
  return TRUE;
}

void
ml_video_inference_pipeline (GstMLModelInfo *minfo,
    GstMLVideoInfo *vinfo) {
  GstElement *pipeline, *filesrc, *demux, *parse, *vdec, *queue0, *tee,
      *mlvconvert, *queue1, *inference, *queue2, *postproc, *capsfilter,
      *queue3, *metamux, *queue4, *voverlay, *sink;
  GList *plugins = NULL;
  GstPad *srcpad;
  GstStructure *delegate_options = NULL;
  gint i, moduleid;
  GstCaps *caps;
  GstMessage *msg;

  pipeline = gst_pipeline_new (NULL);

  // Create elements.
  filesrc = gst_element_factory_make ("filesrc", NULL);
  demux = gst_element_factory_make ("qtdemux", NULL);
  parse = gst_element_factory_make ("h264parse", NULL);
  vdec = gst_element_factory_make ("v4l2h264dec", NULL);
  tee = gst_element_factory_make ("tee", NULL);

  mlvconvert = gst_element_factory_make ("qtimlvconverter", NULL);
  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  metamux = gst_element_factory_make ("qtimetamux", "metamux");
  voverlay = gst_element_factory_make ("qtivoverlay", NULL);
  sink = gst_element_factory_make ("waylandsink", NULL);
  queue0 = gst_element_factory_make ("queue", NULL);
  queue1 = gst_element_factory_make ("queue", NULL);
  queue2 = gst_element_factory_make ("queue", NULL);
  queue3 = gst_element_factory_make ("queue", NULL);
  queue4 = gst_element_factory_make ("queue", NULL);

  fail_unless (pipeline && filesrc && demux && parse && vdec &&
      queue0 && tee && mlvconvert && queue1 && queue2 &&
      capsfilter && queue3 && metamux && queue4 && voverlay && sink);

  // Add to GList.
  plugins = g_list_append (plugins, filesrc);
  plugins = g_list_append (plugins, demux);
  plugins = g_list_append (plugins, parse);
  plugins = g_list_append (plugins, vdec);
  plugins = g_list_append (plugins, tee);
  plugins = g_list_append (plugins, mlvconvert);
  plugins = g_list_append (plugins, capsfilter);
  plugins = g_list_append (plugins, metamux);
  plugins = g_list_append (plugins, voverlay);
  plugins = g_list_append (plugins, sink);
  plugins = g_list_append (plugins, queue0);
  plugins = g_list_append (plugins, queue1);
  plugins = g_list_append (plugins, queue2);
  plugins = g_list_append (plugins, queue3);
  plugins = g_list_append (plugins, queue4);

  if (minfo->inferencetype == GST_ML_OBJECT_DETECTION)
    postproc = gst_element_factory_make ("qtimlvdetection", NULL);
  else if (minfo->inferencetype == GST_ML_CLASSIFICATION)
    postproc = gst_element_factory_make ("qtimlvclassification", NULL);
  else if (minfo->inferencetype == GST_ML_POSE_DETECTION)
    postproc = gst_element_factory_make ("qtimlvpose", NULL);
  else
    fail ();
  fail_unless (postproc);

  plugins = g_list_append (plugins, postproc);

  // Create inference plugin and set properties.
  if (minfo->type == GST_ML_MODEL_TFLITE) {
    inference = gst_element_factory_make ("qtimltflite", NULL);
    fail_unless (inference);
    fail_unless (ml_video_set_mltflite_property (inference, minfo->delegate));
  } else if (minfo->type == GST_ML_MODEL_QNN) {
    inference = gst_element_factory_make ("qtimlqnn", NULL);
    fail_unless (inference);
    fail_unless (ml_video_set_mlqnn_property (inference, minfo->delegate));
  }

  plugins = g_list_append (plugins, inference);
  g_object_set (G_OBJECT (inference), "model", minfo->modelpath, NULL);

  // Set filesrc location.
  g_object_set (G_OBJECT (filesrc), "location", vinfo->file, NULL);

  g_object_set (G_OBJECT (vdec), "capture-io-mode", 4,
      "output-io-mode", 4, NULL);

  // Set post processing plugin properties.
  moduleid = ml_video_get_moduleid(postproc, minfo->moduletype);
  g_object_set (G_OBJECT (postproc),
      "threshold", minfo->threshold, "results", minfo->results,
      "module", moduleid, "labels", minfo->labelspath, NULL);
  if (minfo->useconstants && minfo->constants)
    g_object_set (G_OBJECT (postproc), "constants", minfo->constants, NULL);

  g_object_set (G_OBJECT (sink), "sync", FALSE, NULL);

  // Configure filter caps.
  caps = gst_caps_new_simple ("text/x-raw", NULL, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), filesrc, demux, parse, vdec,
      queue0, tee, mlvconvert, queue1, inference, queue2,  postproc,
      capsfilter, queue3, metamux, queue4, voverlay, sink, NULL);

  fail_unless (gst_element_link (filesrc, demux));
  fail_unless (gst_element_link_many (parse, vdec, queue0,
      tee, metamux, voverlay, sink, NULL));
  g_signal_connect(demux, "pad-added",
      G_CALLBACK (gst_element_on_pad_added), parse);

  fail_unless (gst_element_link_many (tee, mlvconvert, queue1,
      inference, queue2, postproc, capsfilter, queue3, metamux, NULL));

  // Get the outputs of metamux which contains meta info.
  srcpad = gst_element_get_static_pad (voverlay, "sink");
  fail_unless (srcpad);

  gst_buffer_straw_start_pipeline (pipeline, srcpad);

  for (i = 0; i < ML_DETECTION_CHECK_BUFFER_COUNT; ++i) {
    GstBuffer *buf;
    buf = gst_buffer_straw_get_buffer (pipeline, srcpad);

    // Check if meta num is expected.
    fail_unless (ml_video_detection_check (vinfo, buf, i));
    gst_buffer_unref (buf);
  }

  gst_buffer_straw_stop_pipeline (pipeline, srcpad);
  gst_object_unref (srcpad);
  gst_destroy_pipeline (&pipeline, &plugins);
}
