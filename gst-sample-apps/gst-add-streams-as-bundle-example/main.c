/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
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

#include <glib-unix.h>
#include <stdbool.h>
#include <stdio.h>

#include <gst/gst.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define GST_APP_SUMMARY "This application demonstrate the ability of the " \
  "add/remove the streams runtime with once camera reconfiguration \n " \
  "\nCommand:\n" \
  "To preview the stream:\n" \
  "  gst-add-streams-as-bundle-example -o Display \n" \
  "To encode the stream:\n" \
  "  gst-add-streams-as-bundle-example -o File \n" \
  "\nOutput:\n" \
  "  Upon executing the application, with Display option user will observe " \
  "content displayed on the screen, \n" \
  "with File option encoded stream will be stored at /opt/video_%d.mp4" \

#define STREAM_COUNT 3
typedef struct _GstStreamInf GstStreamInf;

// Contains information for used plugins in the stream
struct _GstStreamInf {
  GstElement *capsfilter;
  GstElement *waylandsink;
  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;
};

// Contains app context information
struct _GstCameraAppContext {
  GstElement *pipeline;
  GMainLoop *mloop;
  // List with all streams
  GList *streams_list;
  GstPad *qmmf_pad[STREAM_COUNT];
  gint stream_cnt;
  GMutex lock;
  gboolean exit;
  GCond eos_signal;
  gboolean use_display;
};

typedef struct _GstCameraAppContext GstCameraAppContext;

static gboolean
check_for_exit (GstCameraAppContext * appctx) {
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
wait_for_eos (GstCameraAppContext * appctx) {
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

/*
 * Add new stream to the pipeline and outputs to the display.
 * Requests a new pad from qmmfsrc and link it to the other elements
 *
 * x: Position X on the screen
 * y: Position Y on the screen
 * w: Camera width
 * h: Camera height
 */
static GstStreamInf *
create_stream_display (GstCameraAppContext *appctx, gint x, gint y, gint w, gint h)
{
  gchar *padname = NULL;
  GstCaps *qmmf_caps;
  gchar temp_str[100];
  gboolean ret = FALSE;
  GstStreamInf *stream = g_new0 (GstStreamInf, 1);

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "camerasrc");

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "waylandsink_%d", appctx->stream_cnt);
  stream->waylandsink = gst_element_factory_make ("waylandsink", temp_str);

  // Check if all elements are created successfully
  if (!appctx->pipeline || !qtiqmmfsrc || !stream->capsfilter ||
      !stream->waylandsink) {
    gst_object_unref (qtiqmmfsrc);
    gst_object_unref (stream->capsfilter);
    gst_object_unref (stream->waylandsink);
    g_free (stream);
    g_printerr ("One element could not be created of found. Exiting.\n");
    return NULL;
  }

  qmmf_caps = gst_caps_new_simple ("video/x-raw",
    "format", G_TYPE_STRING, "NV12",
    "width", G_TYPE_INT, w,
    "height", G_TYPE_INT, h,
    "framerate", GST_TYPE_FRACTION, 30, 1,
    "compression", G_TYPE_STRING, "ubwc",
    NULL);
  gst_caps_set_features (qmmf_caps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (stream->capsfilter), "caps", qmmf_caps, NULL);
  gst_caps_unref (qmmf_caps);

  // Set waylandsink properties
  g_object_set (G_OBJECT (stream->waylandsink), "x", x, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "y", y, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "width", 640, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "height", 480, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "async", TRUE, NULL);
  g_object_set (G_OBJECT (stream->waylandsink), "enable-last-sample", FALSE, NULL);

  // Add the elements to the pipeline
  gst_bin_add_many (GST_BIN (appctx->pipeline), stream->capsfilter,
      stream->waylandsink, NULL);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->waylandsink);

  // Get qmmfsrc Element class
  GstElementClass *qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);

  // Get qmmfsrc pad template
  GstPadTemplate
      *qtiqmmfsrc_template = gst_element_class_get_pad_template (qtiqmmfsrc_klass,
          "video_%u");

  // Request a pad from qmmfsrc
  appctx->qmmf_pad[appctx->stream_cnt] = gst_element_request_pad (qtiqmmfsrc,
      qtiqmmfsrc_template, "video_%u", NULL);
  if (!appctx->qmmf_pad[appctx->stream_cnt]) {
    g_printerr ("Error: pad cannot be retrieved from qmmfsrc!\n");
    goto cleanup;
  }
  padname = gst_pad_get_name (appctx->qmmf_pad[appctx->stream_cnt]);
  g_print ("Pad received - %s\n", padname);

  // Set first stream as preview
  if (appctx->stream_cnt == 0) {
    g_object_set (G_OBJECT (appctx->qmmf_pad[appctx->stream_cnt]), "type", 1,
        NULL);
    g_print ("Preview Pad - %s\n", padname);
  }

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads (qtiqmmfsrc, padname, stream->capsfilter, NULL);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }
  g_free (padname);

  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->waylandsink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  // Add the stream to the list
  appctx->streams_list = g_list_append (appctx->streams_list, stream);
  appctx->stream_cnt++;
  gst_object_unref (qtiqmmfsrc);

  return stream;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->waylandsink, GST_STATE_NULL);

  if (appctx->qmmf_pad[appctx->stream_cnt]) {
    // Release the unlinked pad
    gst_element_release_request_pad (qtiqmmfsrc,
        appctx->qmmf_pad[appctx->stream_cnt]);
  }

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline), stream->capsfilter,
      stream->waylandsink, NULL);

  g_free (padname);
  gst_object_unref (qtiqmmfsrc);
  g_free (stream);

  return NULL;
}

/*
 * Add new stream to the pipeline and outputs to the encode file.
 * Requests a new pad from qmmfsrc and link it to the other elements
 *
 * x: Position X on the screen
 * y: Position Y on the screen
 * w: Camera width
 * h: Camera height
 */
static GstStreamInf *
create_stream_encode (GstCameraAppContext *appctx, gint x, gint y, gint w, gint h)
{
  gchar *padname = NULL;
  GstCaps *qmmf_caps;
  gchar temp_str[100];
  gboolean ret = FALSE;
  GstStreamInf *stream = g_new0 (GstStreamInf, 1);

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "camerasrc");

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);
  snprintf (temp_str, sizeof (temp_str), "encoder_%d", appctx->stream_cnt);
  stream->encoder = gst_element_factory_make ("v4l2h264enc", temp_str);
  snprintf (temp_str, sizeof (temp_str), "filesink_%d", appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("filesink", temp_str);
  snprintf (temp_str, sizeof (temp_str), "h264parse_%d", appctx->stream_cnt);
  stream->h264parse = gst_element_factory_make ("h264parse", temp_str);
  snprintf (temp_str, sizeof (temp_str), "mp4mux_%d", appctx->stream_cnt);
  stream->mp4mux = gst_element_factory_make ("mp4mux", temp_str);

  // Check if all elements are created successfully
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

  qmmf_caps = gst_caps_new_simple ("video/x-raw",
    "format", G_TYPE_STRING, "NV12",
    "width", G_TYPE_INT, w,
    "height", G_TYPE_INT, h,
    "framerate", GST_TYPE_FRACTION, 30, 1,
    "compression", G_TYPE_STRING, "ubwc",
    "interlace-mode", G_TYPE_STRING, "progressive",
    "colorimetry", G_TYPE_STRING, "bt601",
    NULL);
  gst_caps_set_features (qmmf_caps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (stream->capsfilter), "caps", qmmf_caps, NULL);
  gst_caps_unref (qmmf_caps);

  // Set encoder properties
  g_object_set (G_OBJECT (stream->encoder), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (stream->encoder), "output-io-mode", 5, NULL);

  // Set mp4mux in robust mode
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-moov-update-period", 1000000,
      NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-bytes-per-sec", 10000, NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-max-duration", 1000000000,
      NULL);

  snprintf (temp_str, sizeof (temp_str), "/opt/video_%d.mp4", appctx->stream_cnt);
  g_object_set (G_OBJECT (stream->filesink), "location", temp_str, NULL);

  // Add the elements to the pipeline
  gst_bin_add_many (GST_BIN (appctx->pipeline), stream->capsfilter,
      stream->encoder, stream->h264parse, stream->mp4mux, stream->filesink, NULL);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->encoder);
  gst_element_sync_state_with_parent (stream->h264parse);
  gst_element_sync_state_with_parent (stream->mp4mux);
  gst_element_sync_state_with_parent (stream->filesink);

  // Get qmmfsrc Element class
  GstElementClass *qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);

  // Get qmmfsrc pad template
  GstPadTemplate
      *qtiqmmfsrc_template = gst_element_class_get_pad_template (qtiqmmfsrc_klass,
          "video_%u");

  // Request a pad from qmmfsrc
  appctx->qmmf_pad[appctx->stream_cnt] = gst_element_request_pad (qtiqmmfsrc,
      qtiqmmfsrc_template, "video_%u", NULL);
  if (!appctx->qmmf_pad[appctx->stream_cnt]) {
    g_printerr ("Error: pad cannot be retrieved from qmmfsrc!\n");
    goto cleanup;
  }
  padname = gst_pad_get_name (appctx->qmmf_pad[appctx->stream_cnt]);
  g_print ("Pad received - %s\n", padname);

  // Set first stream as preview
  if (appctx->stream_cnt == 0) {
    g_object_set (G_OBJECT (appctx->qmmf_pad[appctx->stream_cnt]), "type", 1,
        NULL);
    g_print ("Preview Pad - %s\n", padname);
  }

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads (qtiqmmfsrc, padname, stream->capsfilter, NULL);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }
  g_free (padname);

  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->encoder,
          stream->h264parse, stream->mp4mux, stream->filesink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  // Add the stream to the list
  appctx->streams_list = g_list_append (appctx->streams_list, stream);
  appctx->stream_cnt++;
  gst_object_unref (qtiqmmfsrc);

  return stream;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->encoder, GST_STATE_NULL);
  gst_element_set_state (stream->h264parse, GST_STATE_NULL);
  gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);

  if (appctx->qmmf_pad[appctx->stream_cnt]) {
    // Release the unlinked pad
    gst_element_release_request_pad (qtiqmmfsrc,
        appctx->qmmf_pad[appctx->stream_cnt]);
  }

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline), stream->capsfilter,
      stream->encoder, stream->h264parse, stream->mp4mux, stream->filesink, NULL);
  g_free (padname);

  gst_object_unref (qtiqmmfsrc);
  g_free (stream);

  return NULL;
}

/*
 * Unlink and release an exiting stream
 * Unlink all elements for that stream and release it's pad and resources
 */
static void
release_stream (GstCameraAppContext *appctx, GstStreamInf *stream, gint stream_cnt)
{
  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline),
      "camerasrc");

  g_print ("Unlinking elements...\n");

  // Deactivation the pad
  gst_pad_set_active (appctx->qmmf_pad[stream_cnt], FALSE);

  // Set NULL to capsfilter element
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);

  // Set NULL to other elements and unlink the elements of this stream
  if (appctx->use_display) {
    gst_element_set_state (stream->waylandsink, GST_STATE_NULL);
    gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter, stream->waylandsink,
        NULL);
  } else {
    GstState state = GST_STATE_VOID_PENDING;
    gst_element_get_state (appctx->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
    if (state == GST_STATE_PLAYING){
      gst_element_send_event (stream->encoder, gst_event_new_eos ());
      wait_for_eos (appctx);
    }
    gst_element_set_state (stream->encoder, GST_STATE_NULL);
    gst_element_set_state (stream->h264parse, GST_STATE_NULL);
    gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
    gst_element_set_state (stream->filesink, GST_STATE_NULL);

    gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter, stream->encoder,
        stream->h264parse, stream->mp4mux, stream->filesink, NULL);
  }
  g_print ("Unlinked successfully \n");

  // Release the unlinked pad
  gst_element_release_request_pad (qtiqmmfsrc, appctx->qmmf_pad[stream_cnt]);

  // Remove the elements from the pipeline
  if (appctx->use_display) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), stream->capsfilter,
        stream->waylandsink, NULL);
  } else {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), stream->capsfilter,
        stream->encoder, stream->h264parse, stream->mp4mux, stream->filesink,
        NULL);
  }

  gst_object_unref (qtiqmmfsrc);

  // Remove the stream from the list
  appctx->streams_list = g_list_remove (appctx->streams_list, stream);

  g_free (stream);

  g_print ("\n\n");
}

// Release all streams in the list
static void
release_all_streams (GstCameraAppContext *appctx)
{
  GList *list = NULL;
  gint count = 0;
  for (list = appctx->streams_list; list != NULL; list = list->next) {
    GstStreamInf *stream = (GstStreamInf *) list->data;
    release_stream (appctx, stream, count);
    count++;
  }
}

/*
 * Handles interrupt by CTRL+C.
 *
 * @param userdata pointer to AppContext.
 * @return FALSE if the source should be removed, else TRUE.
 */
static gboolean
handle_interrupt (gpointer userdata)
{
  GstCameraAppContext *appctx = (GstCameraAppContext *) userdata;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (appctx->pipeline, &state, &pending,
          GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    g_print ("\n\n EOS sent ...\n");
  } else {
    g_main_loop_quit (appctx->mloop);
    g_print ("\n\n End the main loop ...\n");
  }

  g_mutex_lock (&appctx->lock);
  appctx->exit = TRUE;
  g_mutex_unlock (&appctx->lock);

  return TRUE;
}

// Error callback function
static void
eos_signal_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstCameraAppContext *appctx = (GstCameraAppContext *) userdata;
  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  g_mutex_lock (&appctx->lock);
  g_cond_signal (&appctx->eos_signal);
  g_mutex_unlock (&appctx->lock);

  if (check_for_exit (appctx)) {
    g_main_loop_quit (appctx->mloop);
  }
}

// In case of ASYNC state change it will properly wait for state change
static gboolean
wait_for_state_change (GstCameraAppContext *appctx)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  g_print ("Pipeline is PREROLLING ...\n");

  ret = gst_element_get_state (appctx->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

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
streams_usecase (GstCameraAppContext * appctx)
{
  GstStreamInf *stream_inf_1, *stream_inf_2, *stream_inf_3;
  // Create a 1080p stream and link it to the qtiqmmfsrc pad
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will syncronize the state of the new elements to the pipeline state.
  // After that will link all elements to a new created pad from the qmmfsrc.
  g_print ("Create 1080p stream\n\n");

  if (appctx->use_display) {
    stream_inf_1 = create_stream_display (appctx, 0, 0, 1920, 1080);
  } else {
    stream_inf_1 = create_stream_encode (appctx, 0, 0, 1920, 1080);
  }

  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    wait_for_state_change (appctx);
  }

  // first stream preview on display wait for 5 second and then stop
  sleep (5);

  // State transition for PLAYING state to READY
  // After that we can add a number of streams using one configure streams
  gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  wait_for_eos (appctx);
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

  if (appctx->use_display) {
    stream_inf_2 = create_stream_display (appctx, 650, 0, 1280, 720);
  } else {
    stream_inf_2 = create_stream_encode (appctx, 650, 0, 1280, 720);
  }

  // Create a 480p stream and link it to the qtiqmmfsrc pad
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will syncronize the state of the new elements to the pipeline state.
  // After that will link all elements to a new created pad from the qmmfsrc.
  g_print ("Create 480p stream\n\n");

  if (appctx->use_display) {
    stream_inf_3 = create_stream_display (appctx, 0, 610, 640, 480);
  } else {
    stream_inf_3 = create_stream_encode (appctx, 0, 610, 640, 480);
  }

  // State transition for READY state to PLAYING
  // The new streams will be configured in a bundle
  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING)) {
    wait_for_state_change (appctx);
  }

  // All stream previews on display wait for 5 second and then stop one by one
  sleep (5);

  // Release stream 1080p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 1080p stream\n\n");
  release_stream (appctx, stream_inf_1, 0);

  sleep (5);

  // Release stream 720p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 720p stream\n\n");
  release_stream (appctx, stream_inf_2, 1);

  sleep (5);

  // Release stream 480p in PLAYING state
  // This function will unlink all elemets of the stream.
  // It will set all elements to NULL state and will remove them from the bin.
  // Qmmfsrc pad will be deactivated and released, it cannot be used anymore.
  g_print ("Release 480p stream\n\n");
  release_stream (appctx, stream_inf_3, 2);
}

static void *
thread_fn (gpointer user_data)
{
  GstCameraAppContext *appctx = (GstCameraAppContext *) user_data;
  streams_usecase (appctx);

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
  GstElement *pipeline = NULL;
  GstElement *qtiqmmfsrc = NULL;
  gchar *output = NULL;
  GstCameraAppContext appctx = {};
  guint intrpt_watch_id = 0;

  appctx.stream_cnt = 0;
  appctx.use_display = FALSE;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  GOptionEntry entries[] = {
    { "output", 'o', 0, G_OPTION_ARG_STRING,
      &output,
      "What output to use",
      "Accepted values: \"File\" or \"Display\""
    },
    { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new (GST_APP_SUMMARY)) != NULL) {
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
      g_free (output);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      g_free (output);
      return -1;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    g_free (output);
    return -1;
  }

  // By default output is file
  if (!g_strcmp0 (output, "Display")) {
    appctx.use_display = TRUE;
    g_print ("Output to display\n");
  } else {
    g_print ("Output to file");
  }
  g_free (output);

  // Initialize GST library.
  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("gst-add-streams-as-bundle-example");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    return -1;
  }
  appctx.pipeline = pipeline;

  // Create qmmfsrc element
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "camerasrc");

  // Add qmmfsrc to the pipeline
  gst_bin_add (GST_BIN (appctx.pipeline), qtiqmmfsrc);

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    gst_bin_remove (GST_BIN (pipeline), qtiqmmfsrc);
    gst_object_unref (pipeline);
    g_printerr ("ERROR: Failed to create Main loop!\n");
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    gst_bin_remove (GST_BIN (pipeline), qtiqmmfsrc);
    gst_object_unref (pipeline);
    g_main_loop_unref (mloop);
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (state_changed_cb),
      pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_signal_cb), &appctx);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt, &appctx);

  // Run thread which perform link and unlink of streams
  GThread *thread = g_thread_new ("UsecaseThread", thread_fn, &appctx);

  // Run main loop.
  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  g_thread_join (thread);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  if (!appctx.use_display)
    g_print ("Output to file: /opt/video_*.mp4\n");

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  // Unlink all stream if any
  release_all_streams (&appctx);

  // Free the streams list
  if (appctx.streams_list != NULL) {
    g_list_free (appctx.streams_list);
    appctx.streams_list = NULL;
  }

  // Remove the pipeline
  gst_bin_remove (GST_BIN (pipeline), qtiqmmfsrc);
  gst_object_unref (pipeline);

  gst_deinit ();

  g_print ("Application: Exit\n");

  return 0;
}
