/*
* Copyright (c) 2021,2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
* GStreamer Activate/Deactivate streams runtime
*
* Description:
* This application demonstrate the ability of the qmmfsrc to
* activate/deactivate the streams runtime, without a reconfiguration and gap
* on already activated streams.
* It creates three streams and activate/deactivate them in different order.
*
* Usage:
* gst-activate-deactivate-streams-runtime-example
*
* Help:
* gst-activate-deactivate-streams-runtime-example --help
*
* Parameters:
* -u - Usecase (Accepted values: "Basic" or "Full", default is "Basic")
* -o - Output (Accepted values: "File" or "Display", default is "File")
*
*/

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <pthread.h>

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
  gboolean    dummy;
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
  // Stream count
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

// Wait fot end of streaming
static gboolean
wait_for_eos (GstAppContext * appctx) {
  g_mutex_lock (&appctx->lock);
  gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (2000000);
  gboolean timeout = g_cond_wait_until (&appctx->eos_signal,
      &appctx->lock, wait_time);
  if (!timeout) {
    g_print ("Timeout on wait for eos\n");
    g_mutex_unlock (&appctx->lock);
    return FALSE;
  }
  g_mutex_unlock (&appctx->lock);
  return TRUE;
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

static gboolean
create_encoder_stream (GstAppContext * appctx, GstStreamInf * stream,
  GstElement *qtiqmmfsrc)
{
  static guint output_cnt = 0;
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

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

  if (!stream->capsfilter || !stream->encoder || !stream->filesink ||
      !stream->h264parse || !stream->mp4mux) {
    gst_object_unref (stream->capsfilter);
    gst_object_unref (stream->encoder);
    gst_object_unref (stream->filesink);
    gst_object_unref (stream->h264parse);
    gst_object_unref (stream->mp4mux);
    g_printerr ("One element could not be created of found. Exiting.\n");
    return FALSE;
  }

  // Set caps the the caps filter
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (stream->encoder), "target-bitrate", 6000000, NULL);
#ifndef CODEC2_ENCODE
  g_object_set (G_OBJECT (stream->encoder), "periodicity-idr", 1, NULL);
  g_object_set (G_OBJECT (stream->encoder), "interval-intraframes", 29,
      NULL);
  g_object_set (G_OBJECT (stream->encoder), "control-rate", 2, NULL);
#endif

  // Set mp4mux in robust mode
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-moov-update-period",
      1000000, NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-bytes-per-sec", 10000,
      NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-max-duration", 1000000000,
      NULL);

  snprintf (temp_str, sizeof (temp_str), "/data/video_%d.mp4", output_cnt++);
  g_object_set (G_OBJECT (stream->filesink), "location", temp_str, NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->encoder);
  gst_element_sync_state_with_parent (stream->h264parse);
  gst_element_sync_state_with_parent (stream->mp4mux);
  gst_element_sync_state_with_parent (stream->filesink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (
    qtiqmmfsrc, gst_pad_get_name (stream->qmmf_pad),
    stream->capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->encoder,
          stream->h264parse, stream->mp4mux, stream->filesink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->encoder, GST_STATE_NULL);
  gst_element_set_state (stream->h264parse, GST_STATE_NULL);
  gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  return FALSE;
}

static void
release_encoder_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  GstState state = GST_STATE_VOID_PENDING;
  GstElement *qtiqmmfsrc = NULL;

  // Get qtiqmmfsrc instance
  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qmmf");

  // Unlink the elements of this stream
  g_print ("Unlinking elements...\n");
  gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter, NULL);

  gst_element_get_state (appctx->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
  if (state == GST_STATE_PLAYING)
    gst_element_send_event (stream->encoder, gst_event_new_eos ());

  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->encoder, GST_STATE_NULL);
  gst_element_set_state (stream->h264parse, GST_STATE_NULL);
  gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);

  // Unlink the elements of this stream
  gst_element_unlink_many (stream->capsfilter, stream->encoder,
      stream->h264parse, stream->mp4mux, stream->filesink, NULL);
  g_print ("Unlinked successfully \n");

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  stream->capsfilter = NULL;
  stream->encoder = NULL;
  stream->h264parse = NULL;
  stream->mp4mux = NULL;
  stream->filesink = NULL;

  gst_object_unref (qtiqmmfsrc);
}

static gboolean
create_display_stream (GstAppContext * appctx, GstStreamInf * stream,
  GstElement *qtiqmmfsrc, gint x, gint y, gint w, gint h)
{
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "waylandsink_%d", appctx->stream_cnt);
  stream->waylandsink = gst_element_factory_make ("waylandsink", temp_str);

  // Check if all elements are created successfully
  if (!stream->capsfilter || !stream->waylandsink) {
    gst_object_unref (stream->capsfilter);
    gst_object_unref (stream->waylandsink);
    g_printerr ("One element could not be created of found. Exiting.\n");
    return FALSE;
  }

  // Set caps the the caps filter
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  // Set waylandsink properties
  g_object_set (G_OBJECT (stream->waylandsink), "x", x, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "y", y, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "width", 640, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "height", 480, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "async", TRUE, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "enable-last-sample", FALSE,
      NULL);

  // Add the elements to the pipeline
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->waylandsink, NULL);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->waylandsink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (
    qtiqmmfsrc, gst_pad_get_name (stream->qmmf_pad),
    stream->capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->waylandsink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->waylandsink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->waylandsink, NULL);

  return FALSE;
}

static void
release_display_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qmmf");

  // Unlink the elements of this stream
  g_print ("Unlinking elements...\n");
  gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter,
      stream->waylandsink, NULL);
  g_print ("Unlinked successfully \n");

  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->waylandsink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->waylandsink, NULL);

  stream->capsfilter = NULL;
  stream->waylandsink = NULL;

  gst_object_unref (qtiqmmfsrc);
}

static gboolean
create_dummy_stream (GstAppContext * appctx, GstStreamInf * stream,
  GstElement *qtiqmmfsrc)
{
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "filesink_%d", appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("fakesink", temp_str);

  // Check if all elements are created successfully
  if (!stream->capsfilter || !stream->filesink) {
    gst_object_unref (stream->capsfilter);
    gst_object_unref (stream->filesink);
    g_printerr ("One element could not be created of found. Exiting.\n");
    return FALSE;
  }

  // Set caps the the caps filter
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  // Add the elements to the pipeline
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->filesink, NULL);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->filesink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (
    qtiqmmfsrc, gst_pad_get_name (stream->qmmf_pad),
    stream->capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->filesink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->filesink, NULL);

  return FALSE;
}

static void
release_dummy_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qmmf");

  // Unlink the elements of this stream
  g_print ("Unlinking elements...\n");
  gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter,
      stream->filesink, NULL);
  g_print ("Unlinked successfully \n");

  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->filesink, NULL);

  stream->capsfilter = NULL;
  stream->filesink = NULL;

  gst_object_unref (qtiqmmfsrc);
}

/*
 * Link already created stream to the pipeline
 *
 * x: Possition X on the screen
 * y: Possition Y on the screen
*/
static void
link_stream (GstAppContext * appctx, gint x, gint y,
    GstStreamInf * stream)
{
  gboolean ret = FALSE;

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qmmf");

  // Activation the pad
  gst_pad_set_active (stream->qmmf_pad, TRUE);
  g_print ("Pad name - %s\n",  gst_pad_get_name (stream->qmmf_pad));

  if (appctx->use_display) {
    ret = create_display_stream (appctx, stream, qtiqmmfsrc, x, y,
        stream->width, stream->height);
  } else {
    ret = create_encoder_stream (appctx, stream, qtiqmmfsrc);
  }
  if (!ret) {
    g_printerr ("Error: failed to create steam\n");
    gst_object_unref (qtiqmmfsrc);
    return;
  }

  appctx->stream_cnt++;
  gst_object_unref (qtiqmmfsrc);

  return;
}

/*
 * Unlink an exiting stream
 * Unlink all elements for that stream without a release
*/
static void
unlink_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Unlink all elements for that stream
  if (stream->dummy) {
    release_dummy_stream (appctx, stream);
    stream->dummy = FALSE;
  } else if (appctx->use_display) {
    release_display_stream (appctx, stream);
  } else {
    release_encoder_stream (appctx, stream);
  }

  // Deactivation the pad
  gst_pad_set_active (stream->qmmf_pad, FALSE);

  g_print ("\n\n");
}

/*
 * Add new stream to the pipeline and outputs to the display
 * Requests a new pad from qmmfsrc and link it to the other elements
 *
 * x: Possition X on the screen
 * y: Possition Y on the screen
 * w: Camera width
 * h: Camera height
*/
static GstStreamInf *
create_stream (GstAppContext * appctx, gboolean dummy,
    gint x, gint y, gint w, gint h)
{
  gchar temp_str[100];
  gboolean ret = FALSE;
  GstStreamInf *stream = g_new0 (GstStreamInf, 1);

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qmmf");

  stream->dummy = dummy;
  stream->width = w;
  stream->height = h;
  stream->qmmf_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, w,
      "height", G_TYPE_INT, h,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (stream->qmmf_caps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

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

  if (dummy) {
    ret = create_dummy_stream (appctx, stream, qtiqmmfsrc);
  } else if (appctx->use_display) {
    ret = create_display_stream (appctx, stream, qtiqmmfsrc, x, y, w, h);
  } else {
    ret = create_encoder_stream (appctx, stream, qtiqmmfsrc);
  }
  if (!ret) {
    g_printerr ("Error: failed to create steam\n");
    goto cleanup;
  }

  // Add the stream to the list
  appctx->streams_list = g_list_append (appctx->streams_list, stream);

  appctx->stream_cnt++;
  gst_object_unref (qtiqmmfsrc);

  return stream;

cleanup:
  if (stream->qmmf_pad) {
    // Release the unlinked pad
    gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);
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
  // Unlink all elements for that stream
  unlink_stream (appctx, stream);

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "qmmf");

  // Release the unlinked pad
  gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);

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
 * See @link_unlink_streams_usecase_full for detailed description
 * This is a more straightforward version to test
 *
*/
static void
link_unlink_streams_usecase_basic (GstAppContext * appctx)
{
  // Create a 1080p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 1080p stream\n\n");
  GstStreamInf *stream_inf_1 = create_stream (appctx, FALSE, 0, 0, 1920, 1080);

  // Create a 720p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 720p stream\n\n");
  GstStreamInf *stream_inf_2 = create_stream (appctx, TRUE, 650, 0, 1280, 720);

  // Create a 480p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 480p stream\n\n");
  GstStreamInf *stream_inf_3 = create_stream (appctx, TRUE, 0, 610, 640, 480);

  // Go from NULL state to PAUSED state
  // In this state the negotiation of the capabilities will be done.
  g_print ("Set pipeline to GST_STATE_PAUSED state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
    wait_for_state_change (appctx);
  }

  // Remove unnecessary stream 480p before going in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // The qmmfsrc pad will be deactivated and it will be ready for further usage.
  g_print ("Unlink 480p stream\n\n");
  unlink_stream (appctx, stream_inf_3);

  // Remove unnecessary stream 720p before going in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // The qmmfsrc pad will be deactivated and it will be ready for further usage.
  g_print ("Unlink 720p stream\n\n");
  unlink_stream (appctx, stream_inf_2);

  // Set the pipeline in PLAYING state
  // After that, all enabled stream will start streaming.
  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    wait_for_state_change (appctx);
  }
  g_print ("Set pipeline to GST_STATE_PLAYING state done\n");

  sleep (10);

  // Link both streams together 480p and 720p which are already created earlier
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to already created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state. And will activate the qmmfsrc pad.
  g_print ("Link 480p and 720p streams\n\n");
  link_stream (appctx, 650, 0, stream_inf_2);
  link_stream (appctx, 0, 610, stream_inf_3);

  sleep (10);

  // Unlink both streams together 480p and 720p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // The qmmfsrc pad will be deactivated and it will be ready for further usage.
  // The other streams will not be interrupted.
  g_print ("Unlink 480p stream\n\n");
  unlink_stream (appctx, stream_inf_3);
  g_print ("Unlink 480p stream done\n\n");
  g_print ("Unlink 720p stream\n\n");
  unlink_stream (appctx, stream_inf_2);
  g_print ("Unlink 720p stream done\n\n");

  sleep (10);

  // State transition for PLAYING state to NULL and againg to PLAYING
  // This will stop the pipeline
  gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  wait_for_eos (appctx);
  g_print ("Set pipeline to GST_STATE_NULL state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL)) {
    wait_for_state_change (appctx);
  }

  // Link both streams together 480p and 720p which are already created earlier
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to already created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state. And will activate the qmmfsrc pad.
  g_print ("Link 480p and 720p streams\n\n");
  link_stream (appctx, 0, 0, stream_inf_2);
  link_stream (appctx, 0, 0, stream_inf_3);

  // Release stream 1080p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 1080p stream\n\n");
  release_stream (appctx, stream_inf_1);

  // Release stream 720p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 720p stream\n\n");
  release_stream (appctx, stream_inf_2);

  // Release stream 480p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 480p stream\n\n");
  release_stream (appctx, stream_inf_3);
}

/*
 * Description
 *
 * Link all streams at beginning and remove unnecessary streams in pause state.
 * It tests state transitions, link/unlink capability and pad
 * activate/deactivate without camera reconfiguration.
 *
*/
static void
link_unlink_streams_usecase_full (GstAppContext * appctx)
{
  // Create a 1080p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 1080p stream\n\n");
  GstStreamInf *stream_inf_1 = create_stream (appctx, TRUE, 0, 0, 1920, 1080);

  // Create a 720p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 720p stream\n\n");
  GstStreamInf *stream_inf_2 = create_stream (appctx, TRUE, 650, 0, 1280, 720);

  // Create a 480p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 480p stream\n\n");
  GstStreamInf *stream_inf_3 = create_stream (appctx, FALSE, 0, 610, 640, 480);

  // Go from NULL state to PAUSED state
  // In this state the negotiation of the capabilities will be done.
  g_print ("Set pipeline to GST_STATE_PAUSED state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
    wait_for_state_change (appctx);
  }

  // Remove unnecessary stream 1080p before going in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // The qmmfsrc pad will be deactivated and it will be ready for further usage.
  g_print ("Unlink 1080p stream\n\n");
  unlink_stream (appctx, stream_inf_1);

  // Remove unnecessary stream 720p before going in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // The qmmfsrc pad will be deactivated and it will be ready for further usage.
  g_print ("Unlink 720p stream\n\n");
  unlink_stream (appctx, stream_inf_2);

  // Set the pipeline in PLAYING state
  // After that, all enabled stream will start streaming.
  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    wait_for_state_change (appctx);
  }
  g_print ("Set pipeline to GST_STATE_PLAYING state done\n");

  sleep (10);

  // Link a 1080p stream which is already created earlier
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to already created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state. And will activate the qmmfsrc pad.
  g_print ("Link 1080p stream\n\n");
  link_stream (appctx, 0, 0, stream_inf_1);

  sleep (10);

  // Link a 720p stream which is already created earlier
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to already created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state. And will activate the qmmfsrc pad.
  g_print ("Link 720p stream\n\n");
  link_stream (appctx, 650, 0, stream_inf_2);

  sleep (10);

  // State transition for PLAYING state to NULL and againg to PLAYING
  // This state transition is for testing purposes only.
  // It demonstrate the correct state transition method.
  gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  wait_for_eos (appctx);
  g_print ("Set pipeline to GST_STATE_NULL state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL)) {
    wait_for_state_change (appctx);
  }
  sleep (10);
  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    wait_for_state_change (appctx);
  }

  sleep (10);

  // Unlink stream 1080p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // The qmmfsrc pad will be deactivated and it will be ready for further usage.
  // The other streams will not be interrupted.
  g_print ("Unlink 1080p stream\n\n");
  unlink_stream (appctx, stream_inf_1);

  sleep (10);

  // Unlink stream 720p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // The qmmfsrc pad will be deactivated and it will be ready for further usage.
  // The other streams will not be interrupted.
  g_print ("Unlink 720p stream\n\n");
  unlink_stream (appctx, stream_inf_2);

  sleep (10);

  // Link a 1080p stream which is already created earlier
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to already created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state. And will activate the qmmfsrc pad.
  g_print ("Link 1080p stream\n\n");
  link_stream (appctx, 0, 0, stream_inf_1);

  sleep (10);

  // Link a 720p stream which is already created earlier
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to already created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state. And will activate the qmmfsrc pad.
  g_print ("Link 720p stream\n\n");
  link_stream (appctx, 650, 0, stream_inf_2);

  sleep (10);

  // Unlink both streams together 720p and 480p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // The qmmfsrc pad will be deactivated and it will be ready for further usage.
  // The other streams will not be interrupted.
  g_print ("Unlink 720p stream\n\n");
  unlink_stream (appctx, stream_inf_2);
  g_print ("Unlink 480p stream\n\n");
  unlink_stream (appctx, stream_inf_3);

  sleep (10);

  // Link a 720p stream which is already created earlier
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to already created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state. And will activate the qmmfsrc pad.
  g_print ("Link 720p stream\n\n");
  link_stream (appctx, 650, 0, stream_inf_2);

  sleep (10);

  // Link a 480p stream which is already created earlier
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to already created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state. And will activate the qmmfsrc pad.
  g_print ("Link 480p stream\n\n");
  link_stream (appctx, 0, 610, stream_inf_3);

  sleep (10);

  // Set the pipeline state to NULL
  // This will stop the streaming of all streams and will go to NULL state
  gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  wait_for_eos (appctx);
  g_print ("Set pipeline to GST_STATE_NULL state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL)) {
    wait_for_state_change (appctx);
  }

  // Release stream 1080p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 1080p stream\n\n");
  release_stream (appctx, stream_inf_1);

  // Release stream 720p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 720p stream\n\n");
  release_stream (appctx, stream_inf_2);

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
  appctx.usecase_fn = link_unlink_streams_usecase_basic;

  GOptionEntry entries[] = {
    { "usecase", 'u', 0, G_OPTION_ARG_STRING,
      &usecase,
      "What degree of testing to perform",
      "Accepted values: \"Basic\" or \"Full\""
    },
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

  // By default the testcase is basic
  if (!g_strcmp0 (usecase, "Full")) {
    appctx.usecase_fn = link_unlink_streams_usecase_full;
    g_print ("Usecase Full\n");
  } else {
    g_print ("Usecase Basic\n");
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

  pipeline = gst_pipeline_new ("gst-activate-deactivate-streams-runtime");
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
  pthread_t thread;
  pthread_create (&thread, NULL, &thread_fn, &appctx);
  pthread_detach (thread);

  // Run main loop.
  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

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
