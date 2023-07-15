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
* GStreamer Start 2 concurrent running cameras in Playing state
*
* Description:
* This application starts two concurrent cameras of the device.
* And handle pipeline errors
*
* Usage:
* gst-concurrent-cameras-example
*
*/

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

#define OUTPUT_WIDTH 1280
#define OUTPUT_HEIGHT 720
#define PIPELINES_COUNT 2
#define FILE_1 "/data/mux0.mp4"
#define FILE_2 "/data/mux1.mp4"

typedef struct _GstConcurrentCameraPipeCtx GstConcurrentCameraPipeCtx;

// Contains pipeline context information
struct _GstConcurrentCameraPipeCtx
{
  // Pointers to the pipelines
  GstElement *pipeline;
  // Pointer to the mainloop
  GMainLoop *mloop;
  // Camera Id
  gint  camera;
  const gchar *pipe_name;

  gint width;
  gint height;

  GstElement *qtiqmmfsrc;
  GstElement *capsfilter;

  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;

  // Guard against potential non atomic access to members
  GMutex *lock;
  // Total instantiated pipes
  guint *refcount;
};

// Sets pipeline to NULL and then destroys it
static void
deinit_camera_pipeline (GstConcurrentCameraPipeCtx * ctx) {
  // Set to NULL
  g_print ("Setting pipe %s to NULL State ...\n", ctx->pipe_name);
  gst_element_set_state (ctx->pipeline, GST_STATE_NULL);

  g_print ("Unlinking elem from %s ...\n", ctx->pipe_name);
  gst_bin_remove_many (GST_BIN (ctx->pipeline),
    ctx->qtiqmmfsrc, ctx->capsfilter,
    ctx->encoder, ctx->h264parse,
    ctx->mp4mux, ctx->filesink, NULL);
  // Unref pipe
  gst_object_unref (ctx->pipeline);
}

// Decrements total pipelines counter and if 0 tries to quit loop
static void
request_end_loop (GstConcurrentCameraPipeCtx * ctx) {
  // Lock mutex to prevent non atomic modification to refcount
  g_mutex_lock (ctx->lock);
  // Decrement reference counter
  // and if reached 0 request exit main loop
  if (--*ctx->refcount == 0) {
    g_main_loop_quit (ctx->mloop);
  }
  // Unlock mutex
  g_mutex_unlock (ctx->lock);
}

// Tries to change pipelines state
static gboolean
change_state_pipelines (GstConcurrentCameraPipeCtx *ctx, guint newstate)
{
  GstStateChangeReturn ret;
  guint i = 0;

  for (; i < PIPELINES_COUNT; ++i) {
    g_print ("Setting pipeline %s to %d\n", ctx[i].pipe_name, newstate);
    ret = gst_element_set_state (ctx[i].pipeline, newstate);

    switch (ret) {
      case GST_STATE_CHANGE_FAILURE:
        g_printerr ("ERROR: Failed to transition to PLAYING state!\n");
        break;
      case GST_STATE_CHANGE_NO_PREROLL:
        g_print ("Pipeline is live and does not need PREROLL.\n");
        break;
      case GST_STATE_CHANGE_ASYNC:
        g_print ("Pipeline is PREROLLING ...\n");

        ret = gst_element_get_state (ctx[i].pipeline,
          NULL, NULL, GST_CLOCK_TIME_NONE);

        if (ret == GST_STATE_CHANGE_FAILURE) {
          g_printerr ("Pipeline failed to PREROLL!\n");
          break;
        }
        break;
      case GST_STATE_CHANGE_SUCCESS:
        g_print ("Pipeline state change was successful\n");
        break;
    }
    // ret is checked for GST_STATE_CHANGE_SUCCESS here
    // so both initial set state and later (in case of GST_STATE_CHANGE_ASYNC)
    // values are taken into account
    if (ret == GST_STATE_CHANGE_SUCCESS)
      ++*ctx->refcount;
  }
  return *ctx->refcount > 0;
}

// Hangles interrupt signals like Ctrl+C etc.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstConcurrentCameraPipeCtx *ctx = userdata;
  guint i = 0;
  GstState state, pending;
  GstStateChangeReturn ret;

  g_print ("\n\nReceived an interrupt signal ...\n");

  for (; i < PIPELINES_COUNT; ++ i) {
    // Try to query current pipeline state immediately
    ret = gst_element_get_state (
      ctx[i].pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
    // If not able to succeed print error and ignore
    if (ret == GST_STATE_CHANGE_FAILURE) {
      g_print ("ERROR: %s get current state! %d\n", ctx[i].pipe_name, ret);
      continue;
    }
    // Check if in PLAYING state and send eos
    if (state == GST_STATE_PLAYING) {
      gst_element_send_event (ctx[i].pipeline, gst_event_new_eos ());
    }
  }

  return TRUE;
}

// Handle warnings
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

// Handle errors
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstConcurrentCameraPipeCtx *ctx = userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);

  // Error message should be printed below
  // via the default error handler
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  // Since there is error, set pipeline to NULL state.
  gst_element_set_state (ctx->pipeline, GST_STATE_NULL);

  // Decrease the refcount of the pipeline which got error.
  --*ctx->refcount;

  g_free (debug);
  g_error_free (error);
}

// End of stream callback function
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstConcurrentCameraPipeCtx *ctx = userdata;

  g_print ("\n%s Received End-of-Stream from '%s' ...\n",
      ctx->pipe_name,
      GST_MESSAGE_SRC_NAME (message));

  request_end_loop (ctx);
}

static gint
init_camera_pipeline (GstConcurrentCameraPipeCtx * ctx, const char * path_name) {
  GstElement *pipeline = gst_pipeline_new (ctx->pipe_name);
  GstCaps *filtercaps;
  GstElement *qtiqmmfsrc = NULL;
  GstElement *capsfilter = NULL;
  GstElement *encoder = NULL;
  GstElement *filesink = NULL;
  GstElement *h264parse = NULL;
  GstElement *mp4mux = NULL;

  ctx->pipeline = pipeline;

  // Create qmmfsrc element
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  // Check if all elements are created successfully
  if (!pipeline || !qtiqmmfsrc || !capsfilter) {
    g_printerr ("One element could not be created of found. Exiting.\n");
    return -1;
  }

  encoder         = gst_element_factory_make ("qtic2venc", "qtic2venc");
  filesink        = gst_element_factory_make ("filesink", "filesink");
  h264parse       = gst_element_factory_make ("h264parse", "h264parse");
  mp4mux          = gst_element_factory_make ("mp4mux", "mp4mux");

  // Check if all elements are created successfully
  if (!encoder || !filesink || !h264parse || !mp4mux) {
    g_printerr ("Encoder's elements could not be created of found. Exiting.\n");
    return -1;
  }

  g_object_set (G_OBJECT (h264parse), "name", "h264parse", NULL);
  g_object_set (G_OBJECT (mp4mux), "name", "mp4mux", NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (encoder), "name", "encoder", NULL);
  g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);

  g_object_set (G_OBJECT (filesink), "name", "filesink", NULL);
  g_object_set (G_OBJECT (filesink), "location", path_name, NULL);
  g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);

  // Set qmmfsrc properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "qmmf", NULL);
  g_object_set (G_OBJECT (qtiqmmfsrc), "camera", ctx->camera, NULL);

  // Set capsfilter properties
  g_object_set (G_OBJECT (capsfilter), "name", "capsfilter", NULL);

  // Set caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, ctx->width,
      "height", G_TYPE_INT, ctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  ctx->qtiqmmfsrc = qtiqmmfsrc;
  ctx->capsfilter = capsfilter;

  ctx->h264parse = h264parse;
  ctx->mp4mux = mp4mux;
  ctx->encoder = encoder;
  ctx->filesink = filesink;

  // Add qmmfsrc to the pipeline
  gst_bin_add_many (GST_BIN (ctx->pipeline), qtiqmmfsrc,
      capsfilter, encoder, h264parse, mp4mux, filesink, NULL);

  // Link the elements
  if (!gst_element_link_many (qtiqmmfsrc, capsfilter, encoder,
        h264parse, mp4mux, filesink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    return -1;
  }

  g_print ("\nPipeline %s fully linked.\n", ctx->pipe_name);

  return 0;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *optctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  gboolean ret = FALSE;
  guint i = 0;
  GstStateChangeReturn state_ret = GST_STATE_CHANGE_FAILURE;
  GstConcurrentCameraPipeCtx ctx[PIPELINES_COUNT] = {{0}};
  GMutex lock;
  guint refcounter = 0;
  GThread *mthread = NULL;

  g_mutex_init (&lock);

  // Initialize GST library.
  gst_init (&argc, &argv);

  ctx[0].camera = 0;
  ctx[1].camera = 1;

  ctx[0].width = OUTPUT_WIDTH;
  ctx[0].height = OUTPUT_HEIGHT;

  GOptionEntry entries[] = {
      { "camera1", 'm', 0, G_OPTION_ARG_INT,
        &ctx[0].camera,
        "ID of 1st camera",
        NULL,
      },
      { "camera2", 's', 0, G_OPTION_ARG_INT,
        &ctx[1].camera,
        "ID of 2nd camera",
        NULL,
      },
      { "width ", 'w', 0, G_OPTION_ARG_INT,
        &ctx[0].width,
        "Stream width",
        NULL,
      },
      { "height", 'h', 0, G_OPTION_ARG_INT,
        &ctx[0].height,
        "Stream height",
        NULL,
      },
      { NULL }
  };

  // Parse command line entries.
  if ((optctx = g_option_context_new ("DESCRIPTION")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (optctx, entries, NULL);
    g_option_context_add_group (optctx, gst_init_get_option_group ());

    success = g_option_context_parse (optctx, &argc, &argv, &error);
    g_option_context_free (optctx);

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

  ctx[1].width = ctx[0].width;
  ctx[1].height = ctx[0].height;

  ctx[0].refcount = &refcounter;
  ctx[1].refcount = &refcounter;

  // Create pipes

  ctx[0].pipe_name = "gst-concurrent-cam-0";

  if (init_camera_pipeline (&ctx[0], FILE_1) < 0) {
    g_printerr ("ERROR: Failed to create first camera pipe!\n");
    return -1;
  }

  ctx[1].pipe_name = "gst-concurrent-cam-1";

  if (init_camera_pipeline (&ctx[1], FILE_2) < 0) {
    g_printerr ("ERROR: Failed to create second camera pipe!\n");
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    return -1;
  }

  ctx[0].mloop = mloop;
  ctx[1].mloop = mloop;

  ctx[0].lock = &lock;
  ctx[1].lock = &lock;

  for (i = 0; i < PIPELINES_COUNT; i++) {
    // Retrieve reference to the pipeline's bus.
    if ((bus = gst_pipeline_get_bus (GST_PIPELINE (ctx[i].pipeline))) == NULL) {
      g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
      g_main_loop_unref (mloop);
      return -1;
    }

    // Watch for messages on the pipeline's bus.
    gst_bus_add_signal_watch (bus);
    g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
    g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), &ctx[i]);
    g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), &ctx[i]);
    gst_object_unref (bus);
  }

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, ctx);

  if (change_state_pipelines (ctx, GST_STATE_PLAYING)) {
    // Run main loop.
    g_print ("g_main_loop_run\n");
    g_main_loop_run (mloop);
    g_print ("g_main_loop_run ends\n");

    deinit_camera_pipeline (&ctx[0]);
    deinit_camera_pipeline (&ctx[1]);
  }

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  g_mutex_clear (&lock);

  gst_deinit ();

  g_print ("main: Exit\n");

  return 0;
}
