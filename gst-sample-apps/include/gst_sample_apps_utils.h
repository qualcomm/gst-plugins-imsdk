/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * This file provides utility functions for gst applications.
 */

#ifndef GST_SAMPLE_APPS_UTILS_H
#define GST_SAMPLE_APPS_UTILS_H

/**
 * GstAppContext:
 * @pipeline: Pipeline connecting all the elements for Use Case.
 * @plugins : List of all the plugins used in Pipeline.
 * @mloop   : Main Loop for the Application.
 *
 * Application context to pass information between the functions.
 */
typedef struct
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // list of pipeline plugins
  GList      *plugins;
  // Pointer to the mainloop
  GMainLoop  *mloop;
} GstAppContext;

/**
 * ModelType:
 * @MODEL_TYPE_NONE  : Invalid Model Type.
 * @MODEL_TYPE_SNPE  : SNPE DLC Container.
 * @MODEL_TYPE_TFLITE: TFLITE Container.
 *
 * Type of Model container for the Runtime.
 */
typedef enum {
  MODEL_TYPE_NONE,
  MODEL_TYPE_SNPE,
  MODEL_TYPE_TFLITE
} ModelType;

/**
 * YoloModelType:
 * @YOLO_TYPE_NONE: Invalid Model Type.
 * @YOLO_TYPE_V5  : Yolov5 Object Detection Model.
 * @YOLO_TYPE_V8  : Yolov8 Object Detection Model.
 * @YOLO_TYPE_NAS: Yolonas Object Detection Model.
 *
 * Type of Yolo Model.
 */
typedef enum {
  YOLO_TYPE_NONE,
  YOLO_TYPE_V5,
  YOLO_TYPE_V8,
  YOLO_TYPE_NAS
} YoloModelType;

/**
 * GstMLSnpeDelegate:
 * @GST_ML_SNPE_DELEGATE_NONE: CPU is used for all operations
 * @GST_ML_SNPE_DELEGATE_DSP : Hexagon Digital Signal Processor
 * @GST_ML_SNPE_DELEGATE_GPU : Graphics Processing Unit
 * @GST_ML_SNPE_DELEGATE_AIP : Snapdragon AIX + HVX
 *
 * Different delegates for transferring part or all of the model execution.
 */
typedef enum {
  GST_ML_SNPE_DELEGATE_NONE,
  GST_ML_SNPE_DELEGATE_DSP,
  GST_ML_SNPE_DELEGATE_GPU,
  GST_ML_SNPE_DELEGATE_AIP,
} GstMLSnpeDelegate;

/**
 * GstMLTFLiteDelegate:
 * @GST_ML_TFLITE_DELEGATE_NONE     : CPU is used for all operations
 * @GST_ML_TFLITE_DELEGATE_NNAPI_DSP: DSP through Android NN API
 * @GST_ML_TFLITE_DELEGATE_NNAPI_GPU: GPU through Android NN API
 * @GST_ML_TFLITE_DELEGATE_NNAPI_NPU: NPU through Android NN API
 * @GST_ML_TFLITE_DELEGATE_HEXAGON  : Hexagon DSP is used for all operations
 * @GST_ML_TFLITE_DELEGATE_GPU      : GPU is used for all operations
 * @GST_ML_TFLITE_DELEGATE_XNNPACK  : Prefer to delegate nodes to XNNPACK
 * @GST_ML_TFLITE_DELEGATE_EXTERNAL : Use external delegate
 *
 * Different delegates for transferring part or all of the model execution.
 */
typedef enum {
  GST_ML_TFLITE_DELEGATE_NONE,
  GST_ML_TFLITE_DELEGATE_NNAPI_DSP,
  GST_ML_TFLITE_DELEGATE_NNAPI_GPU,
  GST_ML_TFLITE_DELEGATE_NNAPI_NPU,
  GST_ML_TFLITE_DELEGATE_HEXAGON,
  GST_ML_TFLITE_DELEGATE_GPU,
  GST_ML_TFLITE_DELEGATE_XNNPACK,
  GST_ML_TFLITE_DELEGATE_EXTERNAL,
} GstMLTFLiteDelegate;

/**
 * Handles interrupt by CTRL+C.
 *
 * @param userdata pointer to AppContext.
 * @return FALSE if the source should be removed, else TRUE.
 */
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (
      appctx->pipeline, &state, &pending, GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  } else {
    g_main_loop_quit (appctx->mloop);
  }
  return TRUE;
}

/**
 * Handles error events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to Main Loop for Handling Error.
 */
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (mloop);
}

/**
 * Handles warning events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to Main Loop for Handling Error.
 */
static void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

/**
 * Handles End-Of-Stream(eos) Event.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to Main Loop for Handling eos.
 */
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

/**
 * Handles state change events for the pipeline
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to Application Pipeline.
 */
static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);

  if ((new_st == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {

    if (gst_element_set_state (pipeline,
        GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr (
          "\nPipeline doesn't want to transition to PLAYING state!\n");
      return;
    }
  }
}

/**
 * Get enum for property nick name
 *
 * @param element Plugin to query the property.
 * @param prop_name Property Name.
 * @param prop_value_nick Property Nick Name.
 */
static gint
get_enum_value (GstElement * element, const gchar * prop_name,
    const gchar * prop_value_nick)
{
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
    if (obj == NULL && (owner_type == G_TYPE_OBJECT
        || owner_type == GST_TYPE_OBJECT || owner_type == GST_TYPE_PAD))
      continue;

    if (strcmp (prop_name, param->name)) {
      continue;
    }

    if (!G_IS_PARAM_SPEC_ENUM (param)) {
      continue;
    }

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

#endif //GST_SAMPLE_APPS_UTILS_H
