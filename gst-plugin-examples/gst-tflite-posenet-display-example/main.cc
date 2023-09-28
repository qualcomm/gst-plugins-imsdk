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
* GStreamer Single stream with TfLite posenet overlay
*
* Description:
* This is an application of posenet with overlay for one stream from
* a file source decoded.
*
* The output is shown on the display
*
* Usage:
* gst-tflite-posenet-display-example
*
* Help:
* gst-tflite-posenet-display-example --help
*/

#include <errno.h>
#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

#define DEFAULT_DECODER       CODEC2_DECODER
#define TFLITE_POSENET_MODEL  "/data/posenet_mobilenet_v1_075_481_641_quant.tflite"
#define TFLITE_POSENET_LABELS "/data/posenet.labels"
#define FILESOURCE            "/data/Draw_1080p_180s_30FPS.mp4"

#define GST_POSENET_PIPELINE_CODEC2  "qtivcomposer name=mixer " \
  "sink_0::position=\"<0, 0>\" sink_0::dimensions=\"<1920, 1080>\" " \
  "sink_1::position=\"<0,  0>\" sink_1::dimensions=\"<1920, 1080>\" " \
  "mixer. ! queue ! waylandsink enable-last-sample=false async=false sync=true fullscreen=true " \
  "filesrc name=source location=/data/Draw_1080p_180s_30FPS.mp4 ! qtdemux ! queue ! " \
  "h264parse ! qtic2vdec ! queue ! tee name=split " \
  "split. ! queue ! mixer. " \
  "split. ! queue ! qtimlvconverter ! queue ! " \
  "qtimltflite name=infeng delegate=gpu model=/data/posenet_mobilenet_v1_075_481_641_quant.tflite ! queue ! " \
  "qtimlvpose name=postproc threshold=40.0 results=4 module=posenet labels=/data/posenet.labels " \ 
  "constants=\"Posenet,q-offsets=<128.0,128.0,117.0>,q-scales=<0.0784313753247261,0.0784313753247261,1.3875764608383179>;\" ! " \
  "capsfilter caps=video/x-raw,width=640,height=360 ! mixer."

#define GST_POSENET_PIPELINE_OMX  "qtivcomposer name=mixer " \
  "sink_0::position=\"<0, 0>\" sink_0::dimensions=\"<1920, 1080>\" " \
  "sink_1::position=\"<0,  0>\" sink_1::dimensions=\"<1920, 1080>\" " \
  "mixer. ! queue ! waylandsink enable-last-sample=false async=false sync=true fullscreen=true " \
  "filesrc name=source location=/data/Draw_1080p_180s_30FPS.mp4 ! qtdemux ! queue ! " \
  "h264parse ! omxh264dec ! queue ! tee name=split " \
  "split. ! queue ! mixer. " \
  "split. ! queue ! qtimlvconverter ! queue ! " \
  "qtimltflite name=infeng delegate=gpu model=/data/posenet_mobilenet_v1_075_481_641_quant.tflite ! queue ! " \
  "qtimlvpose name=postproc threshold=40.0 results=4 module=posenet labels=/data/posenet.labels " \
  "constants=\"Posenet,q-offsets=<128.0,128.0,117.0>,q-scales=<0.0784313753247261,0.0784313753247261,1.3875764608383179>;\" ! " \
  "capsfilter caps=video/x-raw,width=640,height=360 ! mixer."

typedef struct _GstAppContext GstAppContext;

enum
{
  CODEC2_DECODER = 0,
  OMX_DECODER    = 1,
};

struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // Pointer to the mainloop
  GMainLoop *mloop;
};

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = (GstAppContext *)g_new0 (GstAppContext, 1);

  if (NULL == ctx) {
    g_printerr ("Unable to create App Context");
    return NULL;
  }

  ctx->pipeline = NULL;
  ctx->mloop    = NULL;
  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx->mloop != NULL) {
    g_main_loop_unref (ctx->mloop);
    ctx->mloop = NULL;
  }

  if (ctx->pipeline != NULL) {
    gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
    gst_object_unref (ctx->pipeline);
    ctx->pipeline = NULL;
  }

  g_free (ctx);
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

// Create all elements and link
static gboolean
create_pipe (GstAppContext *appctx, gint decoder_opt, gchar *file)
{
  GstBus *bus = NULL;
  GError *error = NULL;
  GstElement *element = NULL;

  // Initiate an empty pipeline
  if (decoder_opt == CODEC2_DECODER) {
    appctx->pipeline = gst_parse_launch (GST_POSENET_PIPELINE_CODEC2, &error);
  } else if (decoder_opt == OMX_DECODER) {
    appctx->pipeline = gst_parse_launch (GST_POSENET_PIPELINE_OMX, &error);
  }

  if (appctx->pipeline == NULL) {
    if (NULL != error) {
      g_printerr ("Posenet Pipeline couldn't be created, error %s",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
    }
    return FALSE;
  }

  element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "source");

  if (element != NULL) {
    g_object_set (G_OBJECT (element), "location", file, NULL);
    gst_object_unref (element);
  } else {
    g_printerr ("Couldn't find filesrc\n");
    return FALSE;
  }

  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  gboolean ret = FALSE;
  gint decoder = DEFAULT_DECODER;
  gchar *file = ((gchar *)FILESOURCE);
  gchar *model = ((gchar *)TFLITE_POSENET_MODEL);
  gchar *labels = ( (gchar *)TFLITE_POSENET_LABELS);
  GstAppContext *appctx = NULL;
  GstElement * element = NULL;

  // Configure input parameters
  GOptionEntry entries[] = {
    { "decoder", 'd', 0, G_OPTION_ARG_INT,
      &decoder,
      "decoder",
      "decoder to use 0 - qtic2vdec, 1 - omxh264dec"
    },
    { "input_file", 'i', 0,
      G_OPTION_ARG_FILENAME, &file,
      "Input filename - by default takes /data/Draw_1080p_180s_30FPS.mp4"
    },
    { "model_file", 'm', 0,
      G_OPTION_ARG_FILENAME, &model,
     "Model file - by default takes"
     " /data/posenet_mobilenet_v1_075_481_641_quant.tflite"
    },
    { "label_file", 'l', 0,
      G_OPTION_ARG_FILENAME, &labels,
      "Labels file - by default takes /data/posenet.labels"
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

  appctx = gst_app_context_new ();

  if (NULL == appctx) {
    return -1;
  }

  // Build the pipeline
  ret = create_pipe (appctx, decoder, file);
  if (!ret) {
    g_printerr ("failed to create GST pipe.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "infeng");
  if (element != NULL) {
    g_object_set (G_OBJECT (element), "model", model, NULL);
    gst_object_unref (element);
  } else {
    g_printerr ("Failed to find qtimltflite\n");
    gst_app_context_free (appctx);
    return -1;
  }

  element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "postproc");
  if (element != NULL) {
    g_object_set (G_OBJECT (element), "labels", labels, NULL);
    gst_object_unref (element);
  } else {
    g_printerr ("Failed to find qtimlvpose plugin\n");
    gst_app_context_free (appctx);
    return  -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  g_print ("Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
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

  g_print ("Destory pipeline\n");
  gst_app_context_free (appctx);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
