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
#include <ml-meta/ml_meta.h>

/**
 * Get enum for property nick name
 *
 * @param element Plugin to query the property.
 * @param prop_name Property Name.
 * @param prop_value_nick Property Nick Name.
 */
gint
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

    // We need only pad properties
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

gboolean ml_video_detection_check (GstMLVideoInfo* videoinfo,
    gint idx, gint num) {
  gint i = 0;
  gint count = sizeof (videoinfo->frameinfo) / sizeof (videoinfo->frameinfo[0]);

  for (i = 0; i < count; i++) {
    if (videoinfo->frameinfo[i].index == idx &&
        videoinfo->frameinfo[i].metanum != num)
      return FALSE;
  }
  return TRUE;
}

void
ml_video_object_detection_inference_pipeline (GstMLModel model,
    GstMLVideoInfo info) {
  GstElement *pipeline, *filesrc, *demux, *parse, *vdec, *queue0, *tee,
      *mlvconvert, *queue1, *mlelement, *queue2, *mlvdetection, *capsfilter,
      *queue3, *metamux, *queue4, *voverlay, *sink;
  GstPad *srcpad;
  GstStructure *delegate_options = NULL;
  gint i, moduleid;
  GstCaps *caps;
  GstMessage *msg;
  GstBus *bus = NULL;
  GValue layers = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;

  pipeline = gst_pipeline_new (NULL);

  // Create elements.
  filesrc = gst_element_factory_make ("filesrc", "filesrc");
  demux = gst_element_factory_make ("qtdemux", NULL);
  parse = gst_element_factory_make ("h264parse", NULL);
  vdec = gst_element_factory_make ("v4l2h264dec", NULL);
  tee = gst_element_factory_make ("tee", NULL);

  mlvconvert = gst_element_factory_make ("qtimlvconverter", NULL);

  // Create inference plugin TFLITE/QNN by model type.
  switch (model.type) {
    case GST_MODEL_TYPE_TFLITE:
      mlelement = gst_element_factory_make ("qtimltflite", NULL);
      break;
    case GST_MODEL_TYPE_QNN:
      mlelement = gst_element_factory_make ("qtimlqnn", NULL);
      break;
    case GST_MODEL_TYPE_SNPE:
      mlelement = gst_element_factory_make ("qtimlqnn", NULL);
      break;
    default:
      fail ();
      break;
  }

  mlvdetection = gst_element_factory_make ("qtimlvdetection", NULL);
  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  metamux = gst_element_factory_make ("qtimetamux", NULL);
  voverlay = gst_element_factory_make ("qtioverlay", NULL);
  sink = gst_element_factory_make ("waylandsink", NULL);
  queue0 = gst_element_factory_make ("queue", NULL);
  queue1 = gst_element_factory_make ("queue", NULL);
  queue2 = gst_element_factory_make ("queue", NULL);
  queue3 = gst_element_factory_make ("queue", NULL);
  queue4 = gst_element_factory_make ("queue", NULL);

  fail_unless (pipeline && filesrc && demux && parse && vdec && queue0 &&
      tee && mlvconvert && queue1 && mlelement && queue2 && mlvdetection &&
      capsfilter && queue3 && metamux && queue4 && voverlay && sink);

  // Set filesrc property.
  g_object_set (G_OBJECT (filesrc), "location", info.file, NULL);

  // Set vdec properties.
  g_object_set (G_OBJECT (vdec), "capture-io-mode", 4,
      "output-io-mode", 4, NULL);

  // Set ml plugin model path.
  g_object_set (G_OBJECT (mlelement), "model", model.modelpath, NULL);

  g_value_init (&layers, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_STRING);
  // Set ml plugin properties by model type
  if (model.type == GST_MODEL_TYPE_TFLITE) {
    GstMLTFLiteDelegate tflite_delegate;
    if (model.delegate == GST_ML_DELEGATE_DSP) {
      tflite_delegate = GST_ML_TFLITE_DELEGATE_EXTERNAL;
      delegate_options = gst_structure_from_string (
          "QNNExternalDelegate,backend_type=htp;", NULL);
      g_object_set (G_OBJECT (mlelement), "delegate", tflite_delegate,
           "external-delegate-path", "libQnnTFLiteDelegate.so",
           "external-delegate-options", delegate_options, NULL);
    } else if (model.delegate == GST_ML_DELEGATE_CPU) {
      tflite_delegate = GST_ML_TFLITE_DELEGATE_NONE;
      g_object_set (G_OBJECT (mlelement), "delegate", tflite_delegate,
           NULL);
    } else if (model.delegate == GST_ML_DELEGATE_GPU) {
      tflite_delegate = GST_ML_TFLITE_DELEGATE_GPU;
      g_object_set (G_OBJECT (mlelement), "delegate", tflite_delegate, NULL);
    } else {
      fail ();
    }
  } else if (model.type == GST_MODEL_TYPE_QNN) {
    if (model.delegate == GST_ML_DELEGATE_CPU)
      g_object_set (G_OBJECT (mlelement), "backend", "/usr/lib/libQnnCpu.so",
          NULL);
    else if (model.delegate == GST_ML_DELEGATE_GPU)
      g_object_set (G_OBJECT (mlelement), "backend", "/usr/lib/libQnnGpu.so",
          NULL);
    else if (model.delegate == GST_ML_DELEGATE_DSP) {
      g_object_set (G_OBJECT (mlelement), "backend", "/usr/lib/libQnnHtp.so",
          NULL);
    } else {
      fail ();
    }
  } else if (model.type == GST_MODEL_TYPE_SNPE) {
    GstMLSnpeDelegate snpe_delegate;
    if (model.delegate == GST_ML_DELEGATE_DSP) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_DSP;
    } else if (model.delegate == GST_ML_DELEGATE_CPU) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_NONE;
    } else if (model.delegate == GST_ML_DELEGATE_GPU) {
      snpe_delegate = GST_ML_SNPE_DELEGATE_GPU;
    } else {
      fail ();
    }
    g_object_set (G_OBJECT (mlelement),"delegate", snpe_delegate, NULL);

    // Layer property
    g_value_set_string (&value,
        "Postprocessor/BatchMultiClassNonMaxSuppression");
    gst_value_array_append_value (&layers, &value);
    g_object_set_property (G_OBJECT (mlelement), "layers", &layers);
  } else {
    fail ();
  }

  // Set vdetection plugin properties
  if (model.moduletype == GST_YOLO_TYPE_V8)
    moduleid = get_enum_value (mlvdetection, "module", "yolov8");
  else
    fail ();

  if (model.use_constants)
    g_object_set (G_OBJECT (mlvdetection), "threshold", model.threshold,
        "results", 10, "module", moduleid, "labels", model.labelspath,
        "constants", model.constants, NULL);
  else
    g_object_set (G_OBJECT (mlvdetection), "threshold", model.threshold,
        "results", 10, "module", moduleid, "labels", model.labelspath,
        NULL);

  // Set waylandsink plugin properties
  g_object_set (G_OBJECT (sink), "sync", FALSE, NULL);

  // Configure filter caps of vdetection.
  caps = gst_caps_new_simple ("text/x-raw", NULL, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), filesrc, demux, parse, vdec, queue0,
      tee, mlvconvert, queue1, mlelement, queue2, mlvdetection, capsfilter,
      queue3, metamux, queue4, voverlay, sink, NULL);

  gst_element_link (filesrc, demux);
  gst_element_link_many (parse, vdec, queue0,
      tee, metamux, voverlay, sink, NULL);
  g_signal_connect(demux, "pad-added",
      G_CALLBACK (gst_element_on_pad_added), parse);

  gst_element_link_many (tee, mlvconvert, queue1, mlelement, queue2,
      mlvdetection, capsfilter, queue3, metamux, NULL);

  // Get the outputs of metamux which contains meta info.
  srcpad = gst_element_get_static_pad (voverlay, "sink");
  fail_unless (srcpad);

  gst_buffer_straw_start_pipeline (pipeline, srcpad);

  // Checking result
  for (i = 0; i < 100; ++i) {
    GstBuffer *buf;
    guint rois = 0;

    buf = gst_buffer_straw_get_buffer (pipeline, srcpad);
    // Get roi number of current buffer.
    rois = gst_buffer_get_n_meta (buf,
        GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

    // Check if meta num is expected.
    fail_unless (ml_video_detection_check (&info, i, rois));
    gst_buffer_unref (buf);
  }

  gst_buffer_straw_stop_pipeline (pipeline, srcpad);

  gst_object_unref (srcpad);
  gst_object_unref (pipeline);
}
