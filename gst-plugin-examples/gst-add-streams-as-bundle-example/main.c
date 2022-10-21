/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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
* GStreamer Add/Remove streams as bundle
*
* Description:
* This application demonstrate the ability of the qmmfsrc to
* add/remove the streams runtime with once camera reconfiguration.
*
* Usage:
* gst-add-streams-as-bundle-example
*
* Help:
* gst-add-streams-as-bundle-example --help
*
* Parameters:
* -o - Output (Accepted values: "File" or "Display", default is "File")
*
*/

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

typedef struct _GstAppContext GstAppContext;
typedef struct _GstStreamInf GstStreamInf;

// Contains information for used plugins in the stream
struct _GstStreamInf
{
  GstElement *capsfilter;
  GstElement *waylandsink;
  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;
  GstPad     *qmmf_pad;
  GstCaps    *qmmf_caps;
  gint        width;
  gint        height;
};

// Contains app context information
struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // Pointer to the mainloop
  GMainLoop *mloop;
  // List with all streams
  GList *streams_list;
  // Stream count
  gint stream_cnt;
  // Mutex lock
  GMutex lock;
  // Exit thread flag
  gboolean exit;
  // EOS signal
  GCond eos_signal;
  // Flag for display usage or filesink
  gboolean use_display;
  // Selected usecase
  void (*usecase_fn) (GstAppContext * appctx);
};

static gboolean
check_for_exit (GstAppContext * appctx) {
  g_mutex_lock (&appctx->lock);
  if (appctx->exit) {
    g_mutex_unlock (&appctx->lock);
    return TRUE;
  }
  g_mutex_unlock (&appctx->lock);
  return FALSE;
}

// Hangles interrupt signals like Ctrl+C etc.
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

  g_mutex_lock (&appctx->lock);
  appctx->exit = TRUE;
  g_mutex_unlock (&appctx->lock);

  return TRUE;
}

// Handles state change transisions
static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new_st),
      gst_element_state_get_name (pending));
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
  GMainLoop *mloop = (GMainLoop*) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (mloop);
}

// Error callback function
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  g_mutex_lock (&appctx->lock);
  g_cond_signal (&appctx->eos_signal);
  g_mutex_unlock (&appctx->lock);

  if (check_for_exit (appctx)) {
    g_main_loop_quit (appctx->mloop);
  }
}

/*
 * Add new stream to the pipeline and outputs to the display
 * Requests a new pad from qmmfsrc and link it to the other elements
 *
 * x: Position X on the screen
 * y: Position Y on the screen
 * w: Camera width
 * h: Camera height
*/
static GstStreamInf *
create_stream (GstAppContext * appctx,
    gint x, gint y, gint w, gint h)
{
  gchar temp_str[100];
  gboolean ret = FALSE;
  GstStreamInf *stream = g_new0 (GstStreamInf, 1);
  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qmmf");

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d",
      appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);
  if (appctx->use_display) {
    snprintf (temp_str, sizeof (temp_str), "waylandsink_%d",
        appctx->stream_cnt);
    stream->waylandsink = gst_element_factory_make ("waylandsink", temp_str);
  } else {
    snprintf (temp_str, sizeof (temp_str), "encoder_%d", appctx->stream_cnt);
#ifdef CODEC2_ENCODE
    stream->encoder = gst_element_factory_make ("qtic2venc", temp_str);
#else
    stream->encoder = gst_element_factory_make ("omxh264enc", temp_str);
#endif
    snprintf (temp_str, sizeof (temp_str), "filesink_%d", appctx->stream_cnt);
    stream->filesink = gst_element_factory_make ("filesink", temp_str);
    snprintf (temp_str, sizeof (temp_str), "h264parse_%d", appctx->stream_cnt);
    stream->h264parse = gst_element_factory_make ("h264parse", temp_str);
    snprintf (temp_str, sizeof (temp_str), "mp4mux_%d", appctx->stream_cnt);
    stream->mp4mux = gst_element_factory_make ("mp4mux", temp_str);
  }

  stream->width = w;
  stream->height = h;

  // Check if all elements are created successfully
  if (appctx->use_display) {
    if (!appctx->pipeline || !qtiqmmfsrc || !stream->capsfilter ||
        !stream->waylandsink) {
      gst_object_unref (qtiqmmfsrc);
      gst_object_unref (stream->capsfilter);
      gst_object_unref (stream->waylandsink);
      g_free (stream);
      g_printerr ("One element could not be created of found. Exiting.\n");
      return NULL;
    }
  } else {
    if (!appctx->pipeline || !qtiqmmfsrc || !stream->capsfilter ||
        !stream->encoder || !stream->filesink || !stream->h264parse ||
        !stream->mp4mux) {
      gst_object_unref (qtiqmmfsrc);
      gst_object_unref (stream->capsfilter);
      gst_object_unref (stream->encoder);
      gst_object_unref (stream->filesink);
      gst_object_unref (stream->h264parse);
      gst_object_unref (stream->mp4mux);
      g_free (stream);
      g_printerr ("One element could not be created of found. Exiting.\n");
      return NULL;
    }
  }

  stream->qmmf_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, w,
      "height", G_TYPE_INT, h,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (stream->qmmf_caps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  if (appctx->use_display) {
    // Set waylandsink properties
    g_object_set (G_OBJECT (stream->waylandsink), "x", x, NULL);
    g_object_set (G_OBJECT (stream->waylandsink), "y", y, NULL);
    g_object_set (G_OBJECT (stream->waylandsink), "width", 640, NULL);
    g_object_set (G_OBJECT (stream->waylandsink), "height", 480, NULL);
    g_object_set (G_OBJECT (stream->waylandsink), "async", TRUE, NULL);
    g_object_set (G_OBJECT (stream->waylandsink), "enable-last-sample", FALSE,
        NULL);
  } else {
    // Set encoder properties
    g_object_set (G_OBJECT (stream->encoder), "target-bitrate", 6000000,
        NULL);
#ifndef CODEC2_ENCODE
    g_object_set (G_OBJECT (stream->encoder), "periodicity-idr", 1, NULL);
    g_object_set (G_OBJECT (stream->encoder), "interval-intraframes", 29,
        NULL);
    g_object_set (G_OBJECT (stream->encoder), "control-rate", 2, NULL);
#endif

    snprintf (temp_str, sizeof (temp_str), "/data/video_%d.mp4",
        appctx->stream_cnt);
    g_object_set (G_OBJECT (stream->filesink), "location", temp_str, NULL);
  }

  // Add the elements to the pipeline
  if (appctx->use_display) {
    gst_bin_add_many (GST_BIN (appctx->pipeline),
        stream->capsfilter, stream->waylandsink, NULL);
  } else {
    gst_bin_add_many (GST_BIN (appctx->pipeline),
        stream->capsfilter, stream->encoder, stream->h264parse,
        stream->mp4mux, stream->filesink, NULL);
  }

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  if (appctx->use_display) {
    gst_element_sync_state_with_parent (stream->waylandsink);
  } else {
    gst_element_sync_state_with_parent (stream->encoder);
    gst_element_sync_state_with_parent (stream->h264parse);
    gst_element_sync_state_with_parent (stream->mp4mux);
    gst_element_sync_state_with_parent (stream->filesink);
  }

  // Get qmmfsrc Element class
  GstElementClass *qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);

  // Get qmmfsrc pad template
  GstPadTemplate *qtiqmmfsrc_template =
      gst_element_class_get_pad_template (qtiqmmfsrc_klass, "video_%u");

  // Request a pad from qmmfsrc
  stream->qmmf_pad = gst_element_request_pad (qtiqmmfsrc, qtiqmmfsrc_template,
      "video_%u", NULL);
  if (!stream->qmmf_pad) {
    g_printerr ("Error: pad cannot be retrieved from qmmfsrc!\n");
    goto cleanup;
  }
  g_print ("Pad received - %s\n",  gst_pad_get_name (stream->qmmf_pad));

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (
    qtiqmmfsrc, gst_pad_get_name (stream->qmmf_pad),
    stream->capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  if (appctx->use_display) {
    // Link the elements
    if (!gst_element_link_many (stream->capsfilter, stream->waylandsink, NULL)) {
      g_printerr ("Error: Link cannot be done!\n");
      goto cleanup;
    }
  } else {
    // Link the elements
    if (!gst_element_link_many (stream->capsfilter, stream->encoder,
            stream->h264parse, stream->mp4mux, stream->filesink, NULL)) {
      g_printerr ("Error: Link cannot be done!\n");
      goto cleanup;
    }
  }

  // Add the stream to the list
  appctx->streams_list =
      g_list_append (appctx->streams_list, stream);
  appctx->stream_cnt++;
  gst_object_unref (qtiqmmfsrc);

  return stream;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  if (appctx->use_display) {
    gst_element_set_state (stream->waylandsink, GST_STATE_NULL);
  } else {
    gst_element_set_state (stream->encoder, GST_STATE_NULL);
    gst_element_set_state (stream->h264parse, GST_STATE_NULL);
    gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
    gst_element_set_state (stream->filesink, GST_STATE_NULL);
  }

  if (stream->qmmf_pad) {
    // Release the unlinked pad
    gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);
  }

  // Remove the elements from the pipeline
  if (appctx->use_display) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline),
        stream->capsfilter, stream->waylandsink, NULL);
  } else {
    gst_bin_remove_many (GST_BIN (appctx->pipeline),
        stream->capsfilter, stream->encoder, stream->h264parse,
        stream->mp4mux, stream->filesink, NULL);
  }

  gst_object_unref (qtiqmmfsrc);
  gst_caps_unref (stream->qmmf_caps);
  g_free (stream);

  return NULL;
}

/*
 * Unlink and release an exiting stream
 * Unlink all elements for that stream and release it's pad and resources
*/
static void
release_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qmmf");

  // Unlink the elements of this stream
  g_print ("Unlinking elements...\n");
  if (appctx->use_display) {
    gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter,
        stream->waylandsink, NULL);
  } else {
    GstState state = GST_STATE_VOID_PENDING;
    gst_element_get_state (appctx->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
    if (state == GST_STATE_PLAYING)
      gst_element_send_event (stream->encoder, gst_event_new_eos ());

    gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter, stream->encoder,
        stream->h264parse, stream->mp4mux, stream->filesink, NULL);
  }
  g_print ("Unlinked successfully \n");

  // Deactivation the pad
  gst_pad_set_active (stream->qmmf_pad, FALSE);

  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  if (appctx->use_display) {
    gst_element_set_state (stream->waylandsink, GST_STATE_NULL);
  } else {
    gst_element_set_state (stream->encoder, GST_STATE_NULL);
    gst_element_set_state (stream->h264parse, GST_STATE_NULL);
    gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
    gst_element_set_state (stream->filesink, GST_STATE_NULL);
  }

  // Release the unlinked pad
  gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);

  // Remove the elements from the pipeline
  if (appctx->use_display) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline),
        stream->capsfilter, stream->waylandsink, NULL);
  } else {
    gst_bin_remove_many (GST_BIN (appctx->pipeline),
        stream->capsfilter, stream->encoder, stream->h264parse,
        stream->mp4mux, stream->filesink, NULL);
  }

  gst_object_unref (qtiqmmfsrc);
  gst_caps_unref (stream->qmmf_caps);

  // Remove the stream from the list
  appctx->streams_list =
      g_list_remove (appctx->streams_list, stream);

  g_free (stream);

  g_print ("\n\n");
}

// Release all streams in the list
static void
release_all_streams (GstAppContext *appctx)
{
  GList *list = NULL;
  for (list = appctx->streams_list; list != NULL; list = list->next) {
    GstStreamInf *stream = (GstStreamInf *) list->data;
    release_stream (appctx, stream);
  }
}

// In case of ASYNC state change it will properly wait for state change
static gboolean
wait_for_state_change (GstAppContext * appctx) {
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  g_print ("Pipeline is PREROLLING ...\n");

  ret = gst_element_get_state (appctx->pipeline,
      NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Pipeline failed to PREROLL!\n");
    return FALSE;
  }
  return TRUE;
}

/*
 * Description
 *
 * Create/release streams with single configure streams
 *
 * This use case will demonstrate the ability of the qmmf to
 * create cached streams and call configure streams once for all streams.
 *
 * First will create one stream and will sset the pipeline to PLAYING state.
 * After that will go to READY state and will create two streams.
 * The actual configure streams will happen when pipeline go to PLAYING state.
 * And it will be executed once fot both new streams.
 *
*/
static void
streams_usecase (GstAppContext * appctx)
{
  // Create a 1080p stream and link it to the qtiqmmfsrc pad
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will syncronize the state of the new elements to the pipeline state.
  // After that will link all elements to a new created pad from the qmmfsrc.
  g_print ("Create 1080p stream\n\n");
  GstStreamInf *stream_inf_1 = create_stream (appctx, 0, 0, 1920, 1080);

  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    wait_for_state_change (appctx);
  }

  sleep (5);

  // State transition for PLAYING state to READY
  // After that we can add a number of streams using one configure streams
  g_print ("Set pipeline to GST_STATE_READY state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_READY)) {
    wait_for_state_change (appctx);
  }

  // Create a 720p stream and link it to the qtiqmmfsrc pad
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will syncronize the state of the new elements to the pipeline state.
  // After that will link all elements to a new created pad from the qmmfsrc.
  g_print ("Create 720p stream\n\n");
  GstStreamInf *stream_inf_2 = create_stream (appctx, 650, 0, 1280, 720);

  // Create a 480p stream and link it to the qtiqmmfsrc pad
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will syncronize the state of the new elements to the pipeline state.
  // After that will link all elements to a new created pad from the qmmfsrc.
  g_print ("Create 480p stream\n\n");
  GstStreamInf *stream_inf_3 = create_stream (appctx, 0, 610, 640, 480);

  // State transition for READY state to PLAYING
  // The new streams will be configured in a bundle
  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    wait_for_state_change (appctx);
  }

  sleep (5);

  // Release stream 1080p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 1080p stream\n\n");
  release_stream (appctx, stream_inf_1);

  sleep (5);

  // Release stream 720p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 720p stream\n\n");
  release_stream (appctx, stream_inf_2);

  sleep (5);

  // Release stream 480p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 480p stream\n\n");
  release_stream (appctx, stream_inf_3);
}

static void *
thread_fn (gpointer user_data)
{
  GstAppContext *appctx = (GstAppContext *) user_data;
  appctx->usecase_fn (appctx);

  if (!check_for_exit (appctx)) {
    // Quit main loop
    g_main_loop_quit (appctx->mloop);
  }

  return NULL;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstCaps *filtercaps;
  GstElement *pipeline = NULL;
  GstElement *qtiqmmfsrc = NULL;
  gboolean ret = FALSE;
  gchar *usecase = NULL, *output = NULL;
  GstAppContext appctx = {};
  g_mutex_init (&appctx.lock);
  g_cond_init (&appctx.eos_signal);
  appctx.stream_cnt = 0;
  appctx.use_display = FALSE;
  appctx.usecase_fn = streams_usecase;

  GOptionEntry entries[] = {
    { "output", 'o', 0, G_OPTION_ARG_STRING,
      &output,
      "What output to use",
      "Accepted values: \"File\" or \"Display\""
    },
    { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new (
      "Verifies that multiple streams can run simultaneously "
      "without interfering with each other")) != NULL) {
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

  // By default output is file
  if (!g_strcmp0 (output, "Display")) {
    appctx.use_display = TRUE;
    g_print ("Output to display\n");
  } else {
    g_print ("Output to file\n");
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("gst-add-streams-as-bundle-example");
  appctx.pipeline = pipeline;

  // Create qmmfsrc element
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");

  // Set qmmfsrc properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "qmmf", NULL);

  // Add qmmfsrc to the pipeline
  gst_bin_add (GST_BIN (appctx.pipeline), qtiqmmfsrc);

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    gst_bin_remove (GST_BIN (appctx.pipeline), qtiqmmfsrc);
    gst_object_unref (pipeline);
    g_printerr ("ERROR: Failed to create Main loop!\n");
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    gst_bin_remove (GST_BIN (appctx.pipeline), qtiqmmfsrc);
    gst_object_unref (pipeline);
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), &appctx);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &appctx);

  // Run thread which perform link and unlink of streams
  GThread *thread = g_thread_new ("UsecaseThread", thread_fn, &appctx);

  // Run main loop.
  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  g_thread_join (thread);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  // Unlink all stream if any
  release_all_streams (&appctx);

  // Remove qmmfsrc from the pipeline
  gst_bin_remove (GST_BIN (appctx.pipeline), qtiqmmfsrc);

  // Free the streams list
  if (appctx.streams_list != NULL) {
    g_list_free (appctx.streams_list);
    appctx.streams_list = NULL;
  }

  g_mutex_clear (&appctx.lock);
  g_cond_clear (&appctx.eos_signal);

  gst_deinit ();

  g_print ("main: Exit\n");

  return 0;
}
