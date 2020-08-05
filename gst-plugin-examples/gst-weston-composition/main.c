/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <glib-unix.h>
#include <gst/gst.h>

/// Command line option variables.
static gchar** sources = NULL;

static const GOptionEntry entries[] = {
    { "source", 's', 0, G_OPTION_ARG_STRING_ARRAY, &sources,
        "Pipeline that is going to be used and must contain a waylandsink."
        " Quotes around the pipeline are mandatory. Can be entered maximum of 4"
        " times for different sources.", NULL
    },
    {NULL}
};


static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\n\nReceived an interrupt signal, quit main loop ...\n");
  g_main_loop_quit (mloop);

  return TRUE;
}

static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new),
      gst_element_state_get_name (pending));

  if ((new == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {
    g_print ("\nSetting pipeline to PLAYING state ...\n");

    if (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr ("\nPipeline doesn't want to transition to PLAYING state!\n");
      return;
    }
  }
}

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

static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  gst_element_set_state (pipeline, GST_STATE_NULL);
}

static void
zoomin (GstElement * pipeline, guint idx, guint stepcnt)
{
  GstElement * element = NULL;
  GValue value = G_VALUE_INIT;
  guint x = 0, y = 0, width = 0, height = 0, step = 0;

  if ((element = gst_bin_get_by_name (GST_BIN (pipeline), "wayland")) == NULL) {
    g_printerr ("Invalid plugin name!\n");
    return;
  }

  g_value_init (&value, G_TYPE_UINT);

  g_object_get_property (G_OBJECT (element), "x", &value);
  x = g_value_get_uint (&value);

  g_object_get_property (G_OBJECT (element), "y", &value);
  y = g_value_get_uint (&value);

  g_object_get_property (G_OBJECT (element), "width", &value);
  width = g_value_get_uint (&value);

  g_object_get_property (G_OBJECT (element), "height", &value);
  height = g_value_get_uint (&value);

  g_print ("\n waylandsink %u old rectangle: x(%d) y(%d) width(%d) height(%d)\n",
      idx, x, y, width, height);

  // Calculate the X axis step increment.
  step = x / (11 - stepcnt);
  x -= step;

  // Calculate the X axis step increment.
  step = y / (11 - stepcnt);
  y -= step;

  // Calculate the width step increment.
  step = (1920 - width) / (11 - stepcnt);
  width += step;

  // Calculate the height step increment.
  step = (810 - height) / (11 - stepcnt);
  height += step;

  g_print ("\n waylandsink %u new rectangle: x(%d) y(%d) width(%d) height(%d)\n",
      idx, x, y, width, height);

  g_object_set (G_OBJECT (element),
      "x", x, "y", y, "width", width, "height", height, NULL);

  gst_object_unref (element);
}

static void
zoomout (GstElement * pipeline, guint idx, guint stepcnt)
{
  GstElement * element = NULL;
  GValue value = G_VALUE_INIT;
  guint x = 0, y = 0, width = 0, height = 0;
  guint step = 0, target = 0;

  if ((element = gst_bin_get_by_name (GST_BIN (pipeline), "wayland")) == NULL) {
    g_printerr ("Invalid plugin name!\n");
    return;
  }

  g_value_init (&value, G_TYPE_UINT);

  g_object_get_property (G_OBJECT (element), "x", &value);
  x = g_value_get_uint (&value);

  g_object_get_property (G_OBJECT (element), "y", &value);
  y = g_value_get_uint (&value);

  g_object_get_property (G_OBJECT (element), "width", &value);
  width = g_value_get_uint (&value);

  g_object_get_property (G_OBJECT (element), "height", &value);
  height = g_value_get_uint (&value);

  g_print ("\n waylandsink %u old rectangle: x(%d) y(%d) width(%d) height(%d)\n",
      idx, x, y, width, height);

  // Calculate the X axis step increment.
  target = (idx * 480);
  step = ((x >= target) ? (x - target) : (target - x)) / (11 - stepcnt);
  x = (x >= target) ? (x - step) : (x + step);

  // Calculate the Y axis step increment.
  step = ((y >= 810) ? (y - 810) : (810 - y)) / (11 - stepcnt);
  y = (y >= 810) ? (y - step) : (y + step);

  // Calculate the width step increment.
  step = (width - 480) / (11 - stepcnt);
  width -= step;

  // Calculate the height step increment.
  step = (height - 270) / (11 - stepcnt);
  height -= step;

  g_print ("\n waylandsink %u new rectangle: x(%d) y(%d) width(%d) height(%d)\n",
      idx, x, y, width, height);

  g_object_set (G_OBJECT (element),
      "x", x, "y", y, "width", width, "height", height, NULL);

  gst_object_unref (element);
}

static gboolean
handle_timeout_event (gpointer userdata)
{
  GList *pipelines = userdata;
  GstElement *element = NULL;
  static guint count = 0, active = 0, idx = 0;

  // Increment the counter variable, reset it when 3 seconds have passed.
  count = (count != 120) ? (count + 1) : 1;

  // Interchange main screens after 2.75 seconds (count to 110).
  if (count <= 110)
    return TRUE;

  for (idx = 0; idx < g_list_length (pipelines); idx++) {
    GstElement *pipeline = GST_ELEMENT (g_list_nth_data (pipelines, idx));
    GstState state = GST_STATE_VOID_PENDING;

    // Get the current state of the pipeline.
    gst_element_get_state (pipeline, &state, NULL, 0);

    if (state != GST_STATE_PLAYING)
      continue;

    if (idx == active)
      zoomin (pipeline, idx, (count - 110));
    else
      zoomout (pipeline, idx, (count - 110));
  }

  // Update the active sink index.
  if (count >= 120 && active >=3)
    active = 0;
  else if (count >= 120 && active < 3)
    active++;

  return TRUE;
}

static gboolean
is_pipeline_valid (GstElement * pipeline)
{
  GstIterator *it = NULL;
  gboolean done = FALSE, found = FALSE;

  it = gst_bin_iterate_sorted (GST_BIN (pipeline));

  while (!done) {
    GValue item = G_VALUE_INIT;
    GObject *object = NULL;
    GstElementFactory *factory = NULL;
    GType gtype = G_TYPE_INVALID;

    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_OK:
        object = g_value_get_object (&item);
        factory = gst_element_get_factory (GST_ELEMENT (object));
        gtype = gst_element_factory_get_element_type (factory);

        found = (g_strcmp0 ("GstWaylandSink", g_type_name (gtype)) == 0) ?
            TRUE : FALSE;
        done = found ? TRUE : FALSE;

        g_value_reset (&item);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }

  gst_iterator_free (it);
  return found;
}

gint
main (gint argc, gchar *argv[])
{
  GList *pipelines = NULL;
  GMainLoop *mloop = NULL;
  guint idx = 0, intrpt_watch_id = 0, event_watch_id = 0;

  g_set_prgname ("gst-weston-composition-example");

  {
    GOptionContext *optsctx = NULL;

    // Parse command line entries.
    if ((optsctx = g_option_context_new ("DESCRIPTION")) != NULL) {
      gboolean success = FALSE;
      GError *error = NULL;

      g_option_context_add_main_entries (optsctx, entries, NULL);
      g_option_context_add_group (optsctx, gst_init_get_option_group ());

      success = g_option_context_parse (optsctx, &argc, &argv, &error);
      g_option_context_free (optsctx);

      if (!success && (error != NULL)) {
        g_printerr ("ERROR: Failed to parse command line options: %s!\n",
            GST_STR_NULL (error->message));
        g_clear_error (&error);
        return -1;
      } else if (!success && (NULL == error)) {
        g_printerr ("ERROR: Initializing: Unknown error!\n");
        return -1;
      }
    } else {
      g_printerr ("ERROR: Failed to create options context!\n");
      return -1;
    }
  }

  if (sources == NULL) {
    g_printerr ("ERROR: option \"--sources/-s\" not specified!\n");
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create a GST pipeline elements.
  while ((sources[idx] != NULL) && (idx < 4)) {
    GstElement *pipeline = NULL;
    GError *error = NULL;

    pipeline = gst_parse_launch (sources[idx], &error);

    // Check for errors on pipe creation.
    if ((NULL == pipeline) && (error != NULL)) {
      g_printerr ("Failed to create pipeline, error: %s!\n",
          GST_STR_NULL (error->message));

      g_clear_error (&error);
      g_list_free_full (pipelines, gst_object_unref);
      g_strfreev (sources);

      return -1;
    } else if ((NULL == pipeline) && (NULL == error)) {
      g_printerr ("Failed to create pipeline, unknown error!\n");

      g_list_free_full (pipelines, gst_object_unref);
      g_strfreev (sources);

      return -1;
    } else if ((pipeline != NULL) && (error != NULL)) {
      g_printerr ("Erroneous pipeline, error: %s!\n",
          GST_STR_NULL (error->message));

      g_clear_error (&error);
      g_list_free_full (pipelines, gst_object_unref);
      g_strfreev (sources);

      return -1;
    }

    if (!is_pipeline_valid (pipeline)) {
      g_printerr ("Pipeline %u does not contain waylandsink plugin!\n", idx);

      g_list_free_full (pipelines, gst_object_unref);
      g_strfreev (sources);

      return -1;
    }

    pipelines = g_list_append (pipelines, pipeline);
    idx++;
  }

  g_strfreev (sources);

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    g_list_free_full (pipelines, gst_object_unref);
    return -1;
  }

  for (idx = 0; idx < g_list_length (pipelines); ++idx) {
    GstElement *pipeline = GST_ELEMENT (g_list_nth_data (pipelines, idx));
    GstBus *bus = NULL;

    // Retrieve reference to the pipeline's bus.
    if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
      g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");

      g_main_loop_unref (mloop);
      g_list_free_full (pipelines, gst_object_unref);

      return -1;
    }

    // Watch for messages on the pipeline's bus.
    gst_bus_add_signal_watch (bus);

    g_signal_connect (bus, "message::state-changed",
        G_CALLBACK (state_changed_cb), pipeline);
    g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
    g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
    g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), pipeline);

    gst_object_unref (bus);
  }


  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, mloop);

  // Register a timed source which will do the composition at 25 ms intervals.
  event_watch_id = g_timeout_add (25, handle_timeout_event, pipelines);

  for (idx = 0; idx < g_list_length (pipelines); ++idx) {
    GstElement *pipeline = GST_ELEMENT (g_list_nth_data (pipelines, idx));
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
  }

  // Run main loop.
  g_main_loop_run (mloop);

  g_print ("Setting pipelines to NULL state ...\n");
  for (idx = 0; idx < g_list_length (pipelines); ++idx) {
    GstElement *pipeline = GST_ELEMENT (g_list_nth_data (pipelines, idx));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  g_source_remove (intrpt_watch_id);
  g_source_remove (event_watch_id);

  g_main_loop_unref (mloop);
  g_list_free_full (pipelines, gst_object_unref);

  gst_deinit ();

  return 0;
}
