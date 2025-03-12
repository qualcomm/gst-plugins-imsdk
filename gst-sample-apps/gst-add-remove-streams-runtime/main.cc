/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* GStreamer Add/Remove streams runtime
*
* Description:
* This application demonstrate the ability of the qmmfsrc to
* add/remove the streams runtime with camera reconfiguration.
* It creates three streams and add/remove them in different order.
*
* Usage:
* gst-add-remove-streams-runtime-example
*
* Help:
* gst-add-remove-streams-runtime-example --help
*
* Parameters:
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
  // Flag for display usage or filesink
  gboolean use_display;
  // Exit thread flag
  gboolean exit;
};

static gboolean
check_for_exit (GstAppContext * appctx) {
  if (appctx->exit) {
    return TRUE;
  }
  return FALSE;
}

/**
 * Sets an enum property on a GstElement
 *
 * @param element The GstElement on which to set the property.
 * @param propname The name of the property to set.
 * @param valname The value to set the property to.
 *
 */
void
gst_element_set_enum_property (GstElement * element, const gchar * propname,
    const gchar * valname)
{
  GValue value = G_VALUE_INIT;
  GParamSpec *propspecs = NULL;

  propspecs = g_object_class_find_property (G_OBJECT_GET_CLASS (element), propname);
  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));
  gst_value_deserialize (&value, valname);

  g_object_set_property (G_OBJECT (element), propname, &value);
  g_value_unset (&value);
}

// Hangles interrupt signals like Ctrl+C etc.
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

  appctx->exit = TRUE;

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

  if (check_for_exit (appctx)) {
    g_main_loop_quit (appctx->mloop);
  }
}

static gboolean
create_encoder_stream (GstAppContext * appctx, GstStreamInf * stream,
  GstElement *qtiqmmfsrc)
{
  gchar *padname = NULL;
  static guint output_cnt = 0;
  gchar temp_str[100];
  gboolean ret = FALSE;

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
  gst_caps_unref (stream->qmmf_caps);

  // Set encoder properties
  gst_element_set_enum_property (stream->encoder, "capture-io-mode", "dmabuf");
  gst_element_set_enum_property (stream->encoder, "output-io-mode", "dmabuf-import");

  // Set mp4mux in robust mode
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-moov-update-period",
      1000000, NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-bytes-per-sec", 10000,
      NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-max-duration", 1000000000,
      NULL);

  snprintf (temp_str, sizeof (temp_str), "/opt/video_%d.mp4", output_cnt++);
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

  padname = gst_pad_get_name (stream->qmmf_pad);
  g_print ("Pad received - %s\n", padname);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads (qtiqmmfsrc, padname, stream->capsfilter, NULL);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  if (padname) {
    g_free (padname);
    padname = NULL;
  }

  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->encoder,
          stream->h264parse, stream->mp4mux, stream->filesink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  if (padname) {
    g_free (padname);
    padname = NULL;
  }

  return FALSE;
}

static void
release_encoder_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  GstState state = GST_STATE_VOID_PENDING;
  GstElement *qtiqmmfsrc;

  // Get qtiqmmfsrc instance
  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "camerasrc");

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

  gst_object_unref (qtiqmmfsrc);
}

static gboolean
create_display_stream (GstAppContext * appctx, GstStreamInf * stream,
  GstElement *qtiqmmfsrc, gint x, gint y, gint w, gint h)
{
  gchar *padname = NULL;
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
  gst_caps_unref (stream->qmmf_caps);

  // Add the elements to the pipeline
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->waylandsink, NULL);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->waylandsink);

  padname = gst_pad_get_name (stream->qmmf_pad);
  g_print ("Pad received - %s\n", padname);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads (qtiqmmfsrc, padname, stream->capsfilter, NULL);
  if (!ret) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  if (padname) {
    g_free (padname);
    padname = NULL;
  }

  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->waylandsink, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      stream->capsfilter, stream->waylandsink, NULL);

  if (padname) {
    g_free (padname);
    padname = NULL;
  }

  return FALSE;
}

static void
release_display_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "camerasrc");

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

  gst_object_unref (qtiqmmfsrc);
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
create_stream (GstAppContext * appctx, gint x, gint y, gint w, gint h)
{
  gboolean ret = FALSE;
  GstStreamInf *stream = g_new0 (GstStreamInf, 1);

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "camerasrc");

  stream->qmmf_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12_Q08C",
      "width", G_TYPE_INT, w,
      "height", G_TYPE_INT, h,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      NULL);

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

  if (appctx->stream_cnt == 0 || appctx->stream_cnt == 3) {
    g_object_set (G_OBJECT (stream->qmmf_pad), "type", 1, NULL);
  }

  if (appctx->use_display) {
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
  if (appctx->use_display) {
    release_display_stream (appctx, stream);
  } else {
    release_encoder_stream (appctx, stream);
  }

  // Deactivation the pad
  gst_pad_set_active (stream->qmmf_pad, FALSE);

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->pipeline), "camerasrc");

  // Release the unlinked pad
  gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);

  gst_object_unref (qtiqmmfsrc);

  // Remove the stream from the list
  appctx->streams_list =
      g_list_remove (appctx->streams_list, stream);

  g_free (stream);

  g_print ("\n\n");
}

// Unlink all streams in the list
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
 * Create/release streams in different order.
 * It tests state transitions and create of streams in playing and paused state.
 *
*/
static void *
thread_fn (gpointer user_data)
{
  GstAppContext *appctx = (GstAppContext *) user_data;

  // Create a 1080p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 1080p stream\n\n");
  GstStreamInf *stream_inf_1 = create_stream (appctx, 0, 0, 1920, 1080);

  g_print ("Set pipeline to GST_STATE_PLAYING state\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);

  sleep (5);

  // Create a 720p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 720p stream\n\n");
  GstStreamInf *stream_inf_2 = create_stream (appctx, 650, 0, 1280, 720);

  sleep (5);

  // Create a 480p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 480p stream\n\n");
  create_stream (appctx, 0, 610, 640, 480);

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

  // State transition for PLAYING state to PAUSED
  // This state transition is for testing purposes only.
  // It demonstrate the correct state transition method.
  g_print ("Set pipeline to GST_STATE_PAUSED state\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
    wait_for_state_change (appctx);
  }

  sleep (5);

  appctx->stream_cnt = 0;
  // Create a 1080p stream and link it to the pipeline
  // This function will create new elements (waylanksink or encoder) and
  // will add them to the bin.
  // It will link all elements to a new created pad from the qmmfsrc.
  // After the successful link, will syncronize the state of the new elements
  // to the pipeline state.
  g_print ("Create 1080p stream\n\n");
  stream_inf_1 = create_stream (appctx, 0, 0, 1920, 1080);

  sleep (5);

  // State transition for PAUSED state to PLAYING
  // This state transition is for testing purposes only.
  // It demonstrate the correct state transition method.
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
  GstElement *pipeline = NULL;
  GstElement *qtiqmmfsrc = NULL;
  gchar *output = NULL;
  GstAppContext appctx = {};
  appctx.stream_cnt = 0;
  appctx.use_display = TRUE;

    // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  GOptionEntry entries[] = {
    { "output", 'o', 0, G_OPTION_ARG_STRING,
      &output,
      "What output to use",
      "Accepted values: \"File\" or \"Display\""
    },
    { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
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
  if (!g_strcmp0 (output, "File")) {
    g_print ("Output to file\n");
    appctx.use_display = FALSE;
  } else {
    g_print ("Output to display\n");
    appctx.use_display = TRUE;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("gst-add-remove-streams-runtime");
  appctx.pipeline = pipeline;

  // Create qmmfsrc element
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "camerasrc");

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

  gst_deinit ();

  g_print ("main: Exit\n");

  return 0;
}
