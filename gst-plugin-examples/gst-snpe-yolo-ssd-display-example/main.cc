/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
* GStreamer Single stream with SNPE Yolo/SSD overlay
*
* Description:
* This application creates SNPE inference with overlay for one stream
* using Yolo or SSD model.
* The output is shown on the display
*
* Usage:
* gst-snpe-yolo-ssd-display-example
*
* Help:
* gst-snpe-yolo-ssd-display-example --help
*/

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

#define DEFAULT_OUTPUT_WIDTH 1920
#define DEFAULT_OUTPUT_HEIGHT 1080
#define DEFAULT_MODEL 0
#define SNPEv1_YOLO_MODEL "/data/yolov5s_relu_finetune_quantized_cle_bc.dlc"
#define SNPEv1_SSD_MODEL "/data/tensorflow_mobilenet_v1_ssd_2017_quantized.dlc"
#define SNPEv2_SSD_MODEL "/data/tf11_public_cnns_cnns_mobilenet_v2_ssd_quant_aware_batch_1_quant.dlc"
#define SNPE_YOLO_LABELS "/data/yolov5s.labels"
#define SNPE_SSD_LABELS "/data/ssd-mobilenet.labels"

typedef struct _GstAppContext GstAppContext;
struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // list of pipeline plugins
  GList *plugins;
  // Pointer to the mainloop
  GMainLoop *mloop;
};

// Handle interrupt by CTRL+C
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  guint idx = 0;
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
  static guint eoscnt = 0;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

// Unlink and remove all elements
static void
destroy_pipe (GstAppContext *appctx)
{
  GstElement * element_1 = (GstElement *) appctx->plugins->data;
  GstElement * element_2;

  GList *list = appctx->plugins->next;
  for ( ; list != NULL; list = list->next) {
    element_2 = (GstElement *) list->data;
    gst_element_unlink (element_1, element_2);
    gst_bin_remove (GST_BIN (appctx->pipeline), element_1);
    element_1 = element_2;
  }
  gst_bin_remove (GST_BIN (appctx->pipeline), element_1);

  g_list_free (appctx->plugins);
  appctx->plugins = NULL;
  gst_object_unref (appctx->pipeline);
}

// Create all elements and link
static gboolean
create_pipe (GstAppContext *appctx, gint width, gint height, gint model)
{
  GstElement *qtiqmmfsrc, *tee, *qtivcomposer, *waylandsink;
  GstElement *main_capsfilter;
  GstElement *qtimlvconverter, *qtimlsnpe, *qtimlvdetection;
  GstElement *queue1, *queue2, *queue3, *queue4, *queue5, *queue6, *queue7;
  GstCaps *filtercaps;
  gboolean ret = FALSE;
  GValue layers = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;

  // Create all elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  main_capsfilter = gst_element_factory_make ("capsfilter", "main_capsfilter");
  tee = gst_element_factory_make ("tee", "tee");
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  qtimlvconverter = gst_element_factory_make ("qtimlvconverter", "qtimlvconverter");
  qtimlsnpe = gst_element_factory_make ("qtimlsnpe", "qtimlsnpe");
  qtimlvdetection = gst_element_factory_make ("qtimlvdetection", "qtimlvdetection");
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
      !qtimlvconverter || !qtimlsnpe || !qtimlvdetection || !waylandsink ||
      !queue1 || !queue2 || !queue3 || !queue4 || !queue5 || !queue6 || !queue7
      ) {
    g_printerr ("One element could not be created. Exiting.\n");
    return FALSE;
  }

  // Append all elements in a list
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, main_capsfilter);
  appctx->plugins = g_list_append (appctx->plugins, tee);
  appctx->plugins = g_list_append (appctx->plugins, qtivcomposer);
  appctx->plugins = g_list_append (appctx->plugins, qtimlvconverter);
  appctx->plugins = g_list_append (appctx->plugins, qtimlsnpe);
  appctx->plugins = g_list_append (appctx->plugins, qtimlvdetection);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);
  appctx->plugins = g_list_append (appctx->plugins, queue1);
  appctx->plugins = g_list_append (appctx->plugins, queue2);
  appctx->plugins = g_list_append (appctx->plugins, queue3);
  appctx->plugins = g_list_append (appctx->plugins, queue4);
  appctx->plugins = g_list_append (appctx->plugins, queue5);
  appctx->plugins = g_list_append (appctx->plugins, queue6);
  appctx->plugins = g_list_append (appctx->plugins, queue7);

  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);

  // Set qtimlsnpe properties
  g_object_set (G_OBJECT (qtimlsnpe), "delegate", 1, NULL); // dsp

  g_value_init (&layers, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_STRING);

  if (model == 0) {
    g_print ("Use SNPEv1_Yolo model\n");
    // Set Yolo specific settings
    g_object_set (G_OBJECT (qtimlsnpe), "model", SNPEv1_YOLO_MODEL, NULL);
    g_value_set_string (&value, "Conv_139");
    gst_value_array_append_value (&layers, &value);
    g_value_set_string (&value, "Conv_140");
    gst_value_array_append_value (&layers, &value);
    g_value_set_string (&value, "Conv_141");
    gst_value_array_append_value (&layers, &value);
    g_object_set_property (G_OBJECT (qtimlsnpe), "layers", &layers);

    g_object_set (G_OBJECT (qtimlvdetection), "module", 5, NULL); // yolov5
    g_object_set (G_OBJECT (qtimlvdetection), "labels", SNPE_YOLO_LABELS, NULL);
  } else if (model == 1) {
    g_print ("Use SNPEv1_SSD model\n");
    // Set SSD specific settings
    g_object_set (G_OBJECT (qtimlsnpe), "model", SNPEv1_SSD_MODEL, NULL);
    g_value_set_string (&value,
        "Postprocessor/BatchMultiClassNonMaxSuppression");
    gst_value_array_append_value (&layers, &value);
    g_object_set_property (G_OBJECT (qtimlsnpe), "layers", &layers);

    g_object_set (G_OBJECT (qtimlvdetection), "module", 3, NULL); // ssd
    g_object_set (G_OBJECT (qtimlvdetection), "labels", SNPE_SSD_LABELS, NULL);
  } else if (model == 2) {
    g_print ("Use SNPEv2_SSD model\n");
    // Set SSD specific settings
    g_object_set (G_OBJECT (qtimlsnpe), "model", SNPEv2_SSD_MODEL, NULL);
    g_value_set_string (&value,
        "Postprocessor/BatchMultiClassNonMaxSuppression");
    gst_value_array_append_value (&layers, &value);
    g_object_set_property (G_OBJECT (qtimlsnpe), "layers", &layers);

    g_object_set (G_OBJECT (qtimlvdetection), "module", 3, NULL); // ssd
    g_object_set (G_OBJECT (qtimlvdetection), "labels", SNPE_SSD_LABELS, NULL);
  }

  // Set qtimlvdetection properties
  g_object_set (G_OBJECT (qtimlvdetection), "threshold", 70.0, NULL);
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

  // Add elements to the pipeline
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      qtiqmmfsrc, main_capsfilter, tee, qtivcomposer,
      qtimlvconverter, qtimlsnpe, qtimlvdetection, waylandsink, NULL);
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      queue1, queue2, queue3, queue4, queue5, queue6, queue7, NULL);

  g_print ("Linking elements...\n");

  // Linking stream 1
  ret = gst_element_link_many (
      qtiqmmfsrc, main_capsfilter, queue1, tee, queue2, qtivcomposer,
      queue3, waylandsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  // Linking stream 2
  ret = gst_element_link_many (
      tee, queue4, qtimlvconverter, queue5, qtimlsnpe, queue6,
      qtimlvdetection, queue7, qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }
  g_print ("All elements are linked successfully\n");

  return TRUE;

error:
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      qtiqmmfsrc, main_capsfilter, tee, qtivcomposer,
      qtimlvconverter, qtimlsnpe, qtimlvdetection, waylandsink, NULL);
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
    queue1, queue2, queue3, queue4, queue5, queue6, queue7, NULL);
  return FALSE;
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
  gint width = DEFAULT_OUTPUT_WIDTH;
  gint height = DEFAULT_OUTPUT_HEIGHT;
  gint model = DEFAULT_MODEL;

  // Configure input parameters
  GOptionEntry entries[] = {
    { "width", 'w', DEFAULT_OUTPUT_WIDTH, G_OPTION_ARG_INT,
      &width,
      "width",
      "image width"
    },
    { "height", 'h', DEFAULT_OUTPUT_HEIGHT, G_OPTION_ARG_INT,
      &height,
      "height",
      "image height"
    },
    { "model", 'm', DEFAULT_MODEL, G_OPTION_ARG_INT,
      &model,
      "model",
      "0 - SNPEv1_yolov5, 1 - SNPEv1_SSD, 2 - SNPEv2_SSD"
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
  pipeline = gst_pipeline_new ("gst-test-app");
  if (!pipeline) {
    g_printerr ("failed to create pipeline.\n");
    return -1;
  }

  appctx.pipeline = pipeline;

  // Build the pipeline
  ret = create_pipe (&appctx, width, height, model);
  if (!ret) {
    g_printerr ("failed to create GST pipe.\n");
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
