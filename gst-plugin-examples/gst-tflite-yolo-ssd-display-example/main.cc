/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
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

/*
* Application:
* GStreamer Single stream with TfLite Yolo/SSD overlay
*
* Description:
* This is an application of object detection with overlay for one stream
* using Yolo or SSD model. These models need to be available in /data
* The output is shown on the display
*
* Usage:
* gst-tflite-yolo-ssd-display-example
*
* Help:
* gst-tflite-yolo-ssd-display-example --help
*/

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

#define DEFAULT_OUTPUT_WIDTH  1920
#define DEFAULT_OUTPUT_HEIGHT 1080
#define TFLITE_YOLO_MODEL  "/data/yolov5m-320x320-int8.tflite"
#define TFLITE_YOLO_LABELS "/data/yolov5m.labels"
#define TFLITE_SSD_MODEL   "/data/ssd-mobilenet_v1_1.tflite"
#define TFLITE_SSD_LABELS  "/data/ssd-mobilenet.labels"

typedef struct _GstAppContext GstAppContext;

enum
{
  POSTPROC_YOLOV5M = 0,
  POSTPROC_YOLOV5S = 1,
  POSTPROC_SSD     = 2,
};

struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // Pointer to the mainloop
  GMainLoop *mloop;
};

static void
build_pad_property (GValue *property, gint values[], int num) {
  GValue val = G_VALUE_INIT;
  g_value_init (&val, G_TYPE_INT);

  for (int idx = 0; idx < num; idx++) {
    g_value_set_int (&val, values[idx]);
    gst_value_array_append_value (property, &val);
  }

  g_value_unset (&val);
}

// Handle interrupt by CTRL+C
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

// Handle state change events for the pipeline
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

// Handle warning events
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

// Handle error events
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

// Handle end of stream event
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

// Unlink and remove all elements
static void
destroy_pipe (GstAppContext *appctx)
{
  gst_object_unref (appctx->pipeline);
}

// Create all elements and link
static gboolean
create_pipe (GstAppContext *appctx, gint width, gint height)
{
  GstElement *qtiqmmfsrc, *tee, *qtivcomposer, *waylandsink;
  GstElement *main_capsfilter;
  GstElement *qtimlvconverter, *qtimltflite, *qtimlvdetection;
  GstElement *detection_filter;
  GstElement *queue1, *queue2, *queue3, *queue4, *queue5, *queue6, *queue7;
  GstPad *composer_sink_1, *composer_sink_2;
  GstCaps *filtercaps;
  gboolean ret = FALSE;

  // Create all elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  main_capsfilter = gst_element_factory_make ("capsfilter", "main_capsfilter");
  tee = gst_element_factory_make ("tee", "tee");
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  qtimlvconverter = gst_element_factory_make ("qtimlvconverter", "qtimlvconverter");
  qtimltflite = gst_element_factory_make ("qtimltflite", "qtimltflite");
  qtimlvdetection = gst_element_factory_make ("qtimlvdetection", "qtimlvdetection");
  detection_filter = gst_element_factory_make ("capsfilter", "detection_filter");
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  queue1 = gst_element_factory_make ("queue", "queue1");
  queue2 = gst_element_factory_make ("queue", "queue2");
  queue3 = gst_element_factory_make ("queue", "queue3");
  queue4 = gst_element_factory_make ("queue", "queue4");
  queue5 = gst_element_factory_make ("queue", "queue5");
  queue6 = gst_element_factory_make ("queue", "queue6");
  queue7 = gst_element_factory_make ("queue", "queue7");

  // Check if all elements are created successfully
  if (!qtiqmmfsrc || !main_capsfilter || !tee || !qtivcomposer ||
      !qtimlvconverter || !qtimltflite || !qtimlvdetection || !waylandsink ||
      !detection_filter || !queue1 || !queue2 || !queue3 || !queue4 ||
      !queue5 || !queue6 || !queue7
      ) {
    g_printerr ("One element could not be created. Exiting.\n");
    return FALSE;
  }

  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);

  // Set qtimltflite properties
  g_object_set (G_OBJECT (qtimltflite), "delegate", 5, NULL); // gpu

  // Set qtimlvdetection properties
  g_object_set (G_OBJECT (qtimlvdetection), "threshold", 49.0, NULL);
  g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 360,
      NULL);
  g_object_set (G_OBJECT (detection_filter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      qtiqmmfsrc, main_capsfilter, tee, qtivcomposer,
      qtimlvconverter, qtimltflite, qtimlvdetection, detection_filter,
      waylandsink, NULL);
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      queue1, queue2, queue3, queue4, queue5, queue6, queue7, NULL);

  g_print ("Linking elements...\n");

  // Linking stream 1
  ret = gst_element_link_many (
      qtiqmmfsrc, main_capsfilter, queue1, tee, queue2, qtivcomposer,
      queue3, waylandsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    gst_object_unref (appctx->pipeline);
    return FALSE;
  }

  // Linking stream 2
  ret = gst_element_link_many (
      tee, queue4, qtimlvconverter, queue5, qtimltflite, queue6,
      qtimlvdetection, detection_filter, queue7, qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    gst_object_unref (appctx->pipeline);
    return FALSE;
  }

  g_print ("All elements are linked successfully\n");

  composer_sink_1 = gst_element_get_static_pad (qtivcomposer, "sink_0");
  composer_sink_2 = gst_element_get_static_pad (qtivcomposer, "sink_1");

  if (composer_sink_1 == NULL || composer_sink_2 == NULL) {
    g_printerr ("One or more sink pads are not ref'ed");
    return FALSE;
  }

  GValue pos1 = G_VALUE_INIT, dim1 = G_VALUE_INIT;
  g_value_init (&pos1, GST_TYPE_ARRAY);
  g_value_init (&dim1, GST_TYPE_ARRAY);

  gint pos1_vals[] = {0, 0};
  gint dim1_vals[] = {DEFAULT_OUTPUT_WIDTH, DEFAULT_OUTPUT_HEIGHT};

  build_pad_property (&pos1, pos1_vals, 2);
  build_pad_property (&dim1, dim1_vals, 2);

  g_object_set_property (G_OBJECT(composer_sink_1), "position", &pos1);
  g_object_set_property (G_OBJECT(composer_sink_1), "dimensions", &dim1);

  g_object_set_property (G_OBJECT(composer_sink_2), "position", &pos1);
  g_object_set_property (G_OBJECT(composer_sink_2), "dimensions", &dim1);

  gst_object_unref (composer_sink_1);
  gst_object_unref (composer_sink_2);

  g_value_unset (&pos1);
  g_value_unset (&dim1);

  return TRUE;

}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstElement *pipeline = NULL;
  gboolean ret = FALSE;
  GstAppContext appctx = {};
  GstElement *element = NULL;
  gint width = DEFAULT_OUTPUT_WIDTH;
  gint height = DEFAULT_OUTPUT_HEIGHT;
  gchar *model = ((gchar *)TFLITE_YOLO_MODEL);
  gchar *labels = ((gchar *)TFLITE_YOLO_LABELS);
  gint postproc = POSTPROC_YOLOV5M;

  // Configure input parameters
  GOptionEntry entries[] = {
    { "width", 'w', 0, G_OPTION_ARG_INT,
      &width,
      "width",
      "image width"
    },
    { "height", 'h', 0, G_OPTION_ARG_INT,
      &height,
      "height",
      "image height"
    },
    { "postproc", 'p', 0, G_OPTION_ARG_INT,
      &postproc,
      "Postprocessing",
      "0 - yolov5m, 1 - yolov5s, 2 - ssd-mobilenet"
    },
    { "model_file", 'm', 0,
      G_OPTION_ARG_FILENAME, &model,
      "Model file - by default takes /data/yolov5m-320x320-int8.tflite"
    },
    { "label_file", 'l', 0,
      G_OPTION_ARG_FILENAME, &labels,
      "Labels file - by default takes /data/yolov5m.labels"
    },
    { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("DESCRIPTION")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    return -EFAULT;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline
  pipeline = gst_pipeline_new ("gst-tflite-yolo-ssd-display-example");
  if (!pipeline) {
    g_printerr ("failed to create pipeline.\n");
    return -1;
  }

  appctx.pipeline = pipeline;

  // Build the pipeline
  ret = create_pipe (&appctx, width, height);
  if (!ret) {
    g_printerr ("failed to create GST pipe.\n");
    return -1;
  }

  element = gst_bin_get_by_name (GST_BIN (appctx.pipeline), "qtimltflite");
  if (element != NULL) {
    g_object_set (G_OBJECT (element), "model", model, NULL);
    gst_object_unref (element);
  } else {
    g_printerr ("Failed to find qtimltflite. Exiting..\n");
    destroy_pipe (&appctx);
    return -1;
  }

  element = gst_bin_get_by_name (GST_BIN (appctx.pipeline), "qtimlvdetection");
  if (element != NULL) {
    g_object_set (G_OBJECT (element), "labels", labels, NULL);
    switch (postproc) {
      case POSTPROC_YOLOV5M : {
        g_object_set (G_OBJECT (element), "module", 3, NULL);
        break;
      }
      case POSTPROC_YOLOV5S : {
        g_object_set (G_OBJECT (element), "module", 4, NULL);
        break;
      }
      case POSTPROC_SSD : {
        g_object_set (G_OBJECT (element), "module", 2, NULL);
        break;
      }
    }
    gst_object_unref (element);
  } else {
    g_printerr ("Failed to find qtimlvdetection. Exiting..\n");
    destroy_pipe (&appctx);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    destroy_pipe (&appctx);
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    destroy_pipe (&appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &appctx);

  g_print ("Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Destory pipeline\n");
  destroy_pipe (&appctx);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
