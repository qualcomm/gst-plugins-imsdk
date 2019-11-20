/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#include <stdio.h>
#include <gst/gst.h>

#define OPTION_ARRAY_SIZE 20
#define QUIT_OPTION       "q"

#define GST_APP_CONTEXT_CAST(obj)           ((GstAppContext*)(obj))

typedef struct _GstAppContext GstAppContext;
typedef struct _GstPipeStatus GstPipeStatus;

enum
{
  // End-of-Stream event has been received.
  GST_APP_PIPE_STATUS_EOS         = 1 << 0,
  // Asynchronous operations are in progress.
  GST_APP_PIPE_STATUS_IN_PROGRESS = 1 << 1,
  // Buffering is in progress.
  GST_APP_PIPE_STATUS_BUFFERING   = 1 << 2,
};

struct _GstPipeStatus
{
  // Status mutex.
  GMutex   mutex;
  // Status changed condition.
  GCond    changed;
  // Current status of the pipeline
  GstState state;
  // Flags describing other states of the pipeline.
  gint     flags;
};

struct _GstAppContext
{
  // Main application event loop.
  GMainLoop    *mloop;

  // GStreamer pipeline instance.
  GstElement   *pipeline;

  // Structure for keeping track of pipeline state changes.
  GstPipeStatus pstatus;

  // Command line option variables.
  gboolean      eos_on_exit;
};

static void
request_state_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (data);
  gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
  GstState state;

  gst_message_parse_request_state (message, &state);
  g_print ("\nSetting pipeline state to %s as requested by %s...\n",
      gst_element_state_get_name (state), name);

  gst_element_set_state (appctx->pipeline, state);

  g_free (name);
}

static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (data);
  GstState old, new, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (appctx->pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new),
      gst_element_state_get_name (pending));

  g_mutex_lock (&appctx->pstatus.mutex);

  appctx->pstatus.state = new;
  g_cond_signal (&appctx->pstatus.changed);

  g_mutex_unlock (&appctx->pstatus.mutex);
}

static void
buffering_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (data);
  static GstState target_state = GST_STATE_VOID_PENDING;
  gboolean buffering = FALSE;
  gint percent;

  gst_message_parse_buffering (message, &percent);
  g_print ("\nBuffering... %d%%  \r", percent);

  if (percent == 100) {
    // Clear the BUFFERING status flag.
    g_mutex_lock (&appctx->pstatus.mutex);

    appctx->pstatus.flags &= ~GST_APP_PIPE_STATUS_BUFFERING;
    g_cond_signal (&appctx->pstatus.changed);

    g_mutex_unlock (&appctx->pstatus.mutex);

    // Done buffering, if the pending state is playing, go back.
    if (target_state == GST_STATE_PLAYING) {
      g_print ("\nFinished buffering, setting state to PLAYING.\n");
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);
    }
  } else {
    // Busy buffering...
    target_state = GST_STATE_TARGET (appctx->pipeline);

    g_mutex_lock (&appctx->pstatus.mutex);

    buffering = (appctx->pstatus.flags & GST_APP_PIPE_STATUS_BUFFERING) ?
        TRUE : FALSE;

    g_mutex_unlock (&appctx->pstatus.mutex);

    if (!buffering && target_state == GST_STATE_PLAYING) {
      // Buffering has been initiated, PAUSE the pipeline.
      g_print ("\nBuffering, setting pipeline to PAUSED state.\n");
      gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED);
    }

    g_mutex_lock (&appctx->pstatus.mutex);

    appctx->pstatus.flags |= GST_APP_PIPE_STATUS_BUFFERING;
    g_cond_signal (&appctx->pstatus.changed);

    g_mutex_unlock (&appctx->pstatus.mutex);
  }
}

static void
progress_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (data);
  GstProgressType type;
  gchar *code, *text;

  gst_message_parse_progress (message, &type, &code, &text);
  g_print ("\nProgress: (%s) %s\n", code, text);

  g_mutex_lock (&appctx->pstatus.mutex);

  switch (type) {
    case GST_PROGRESS_TYPE_START:
    case GST_PROGRESS_TYPE_CONTINUE:
      appctx->pstatus.flags |= GST_APP_PIPE_STATUS_IN_PROGRESS;
      break;
    case GST_PROGRESS_TYPE_COMPLETE:
    case GST_PROGRESS_TYPE_CANCELED:
    case GST_PROGRESS_TYPE_ERROR:
      appctx->pstatus.flags &= ~GST_APP_PIPE_STATUS_IN_PROGRESS;
      break;
  }
  g_cond_signal (&appctx->pstatus.changed);

  g_mutex_unlock (&appctx->pstatus.mutex);

  g_free (code);
  g_free (text);
}

static void
warning_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  gchar *debug = NULL;
  GError *error = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

static void
error_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (data);
  gchar *debug = NULL;
  GError *error = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
  g_main_loop_quit (appctx->mloop);
}

static void
eos_cb (GstBus * bus, GstMessage * message, gpointer data)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (data);
  g_print ("\nReceived End-of-stream\n");

  g_mutex_lock (&appctx->pstatus.mutex);

  appctx->pstatus.flags |= GST_APP_PIPE_STATUS_EOS;
  g_cond_signal (&appctx->pstatus.changed);

  g_mutex_unlock (&appctx->pstatus.mutex);

  g_main_loop_quit (appctx->mloop);
}

static void
wait_state (GstAppContext * appctx, GstState state)
{
  g_mutex_lock (&appctx->pstatus.mutex);

  g_print ("Waiting for %s state...\n", gst_element_state_get_name (state));

  while (appctx->pstatus.state != state)
    g_cond_wait (&appctx->pstatus.changed, &appctx->pstatus.mutex);

  if (state == GST_STATE_PAUSED) {
    while (appctx->pstatus.flags & GST_APP_PIPE_STATUS_BUFFERING) {
      g_print ("\nPrerolled, waiting for buffering to finish...\n");
      g_cond_wait (&appctx->pstatus.changed, &appctx->pstatus.mutex);
    }
    while (appctx->pstatus.flags & GST_APP_PIPE_STATUS_IN_PROGRESS) {
      g_print ("\nPrerolled, waiting for progress to finish...\n");
      g_cond_wait (&appctx->pstatus.changed, &appctx->pstatus.mutex);
    }
  }

  g_mutex_unlock (&appctx->pstatus.mutex);
}

static gboolean
start_pipeline (GstAppContext * appctx)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

  g_print ("Setting pipeline to PAUSED\n");
  ret = gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");

      ret = gst_element_get_state (appctx->pipeline, NULL, NULL,
                GST_CLOCK_TIME_NONE);

      if (ret != GST_STATE_CHANGE_SUCCESS) {
        g_printerr ("Pipeline failed to PREROLL!\n");
        return FALSE;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline is PREROLLED\n");
      break;
  }

  wait_state (appctx, GST_STATE_PAUSED);

  g_print ("Setting pipeline to PLAYING\n");
  ret = gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("ERROR: Failed to start up pipeline!\n");
    return FALSE;
  }

  wait_state (appctx, GST_STATE_PLAYING);

  return TRUE;
}

static void
stop_pipeline (GstAppContext * appctx)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

  if (appctx->eos_on_exit) {
    g_print ("EOS on exit enabled -- Sending EOS event on the pipeline\n");
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());

    g_mutex_lock (&appctx->pstatus.mutex);

    g_print ("Waiting for EOS event...\n");
    while (!appctx->pstatus.flags & GST_APP_PIPE_STATUS_EOS)
      g_cond_wait (&appctx->pstatus.changed, &appctx->pstatus.mutex);

    appctx->pstatus.flags &= ~GST_APP_PIPE_STATUS_EOS;
    g_mutex_unlock (&appctx->pstatus.mutex);
  }

  g_print ("Setting pipeline to PAUSED ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED);

  // Iterate over the main loop to process any pending events.
  while (g_main_context_iteration (NULL, FALSE));

  g_print ("Setting pipeline to NULL ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
}

static void
print_pipeline_elements (GstElement * pipeline)
{
  GstIterator *it = NULL;
  GValue item = G_VALUE_INIT;
  gboolean done = FALSE;

  if (NULL == pipeline)
    return;

  g_print (" ---------------------------------\n");
  g_print (" Pipeline plugin names:");

  it = gst_bin_iterate_sorted (GST_BIN (pipeline));

  while (!done) {
    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_OK:
      {
        GstElement *element = g_value_get_object (&item);
        g_print (" %s", gst_element_get_name (element));
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
    g_print ("%s", done ? "\n" : " ");
  }

  g_print (" ---------------------------------\n");

  g_value_unset (&item);
  gst_iterator_free (it);
}

static void
print_property_options (GstElement * element, const gchar * name)
{
  GParamSpec *propspecs =
      g_object_class_find_property (G_OBJECT_GET_CLASS (element), name);

  g_print (" ---------------------------------\n");

  switch (G_PARAM_SPEC_VALUE_TYPE (propspecs)) {
    case G_TYPE_UINT:
    {
      guint value;
      GParamSpecUInt *range = G_PARAM_SPEC_UINT (propspecs);
      g_object_get (G_OBJECT (element), name, &value, NULL);

      g_print (" Current value: %u, Range: %u - %u\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_INT:
    {
      gint value;
      GParamSpecInt *range = G_PARAM_SPEC_INT (propspecs);
      g_object_get (G_OBJECT (element), name, &value, NULL);

      g_print (" Current value: %d, Range: %d - %d\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_ULONG:
    {
      gulong value;
      GParamSpecULong *range = G_PARAM_SPEC_ULONG (propspecs);
      g_object_get (G_OBJECT (element), name, &value, NULL);

      g_print (" Current value: %lu, Range: %lu - %lu\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_LONG:
    {
      glong value;
      GParamSpecLong *range = G_PARAM_SPEC_LONG (propspecs);
      g_object_get (G_OBJECT (element), name, &value, NULL);

      g_print (" Current value: %ld, Range: %ld - %ld\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_UINT64:
    {
      guint64 value;
      GParamSpecUInt64 *range = G_PARAM_SPEC_UINT64 (propspecs);
      g_object_get (G_OBJECT (element), name, &value, NULL);

      g_print (" Current value: %" G_GUINT64_FORMAT ", "
          "Range: %" G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT "\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_INT64:
    {
      gint64 value;
      GParamSpecInt64 *range = G_PARAM_SPEC_INT64 (propspecs);
      g_object_get (G_OBJECT (element), name, &value, NULL);

      g_print (" Current value: %" G_GINT64_FORMAT ", "
          "Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT "\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_FLOAT:
    {
      gfloat value;
      GParamSpecFloat *range = G_PARAM_SPEC_FLOAT (propspecs);
      g_object_get (G_OBJECT (element), name, &value, NULL);

      g_print (" Current value: %15.7g, Range: %15.7g - %15.7g\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_DOUBLE:
    {
      gdouble value;
      GParamSpecDouble *range = G_PARAM_SPEC_DOUBLE (propspecs);
      g_object_get (G_OBJECT (element), name, &value, NULL);

      g_print (" Current value: %15.7g, Range: %15.7g - %15.7g\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_BOOLEAN:
    {
      gboolean value;
      g_object_get (G_OBJECT (element), name, &value, NULL);
      g_print (" Current value: %s, Possible values: 0(false), 1(true)\n",
          value ? "true" : "false");
      break;
    }
    case G_TYPE_STRING:
    {
      gchar *value;
      g_object_get (G_OBJECT (element), name, &value, NULL);
      g_print (" Current value: %s\n", value);
      break;
    }
    default:
      if (G_IS_PARAM_SPEC_ENUM (propspecs)) {
        GEnumValue *enumvalues;
        gint value;
        const gchar *nick = "";
        guint idx = 0;

        g_object_get (G_OBJECT (element), name, &value, NULL);
        enumvalues = G_ENUM_CLASS (
            g_type_class_ref (propspecs->value_type))->values;

        while (enumvalues[idx].value_name) {
          g_print ("   (%d): %-16s - %s\n", enumvalues[idx].value,
              enumvalues[idx].value_nick, enumvalues[idx].value_name);

          if (enumvalues[idx].value == value)
            nick = enumvalues[idx].value_nick;

          idx++;
        }

        g_print ("\n Current value: %d, \"%s\"\n", value, nick);
      } else {
        g_print ("Unknown type %ld \"%s\"\n",
            (glong) propspecs->value_type, g_type_name (propspecs->value_type));
      }
      break;
  }

  g_print (" ---------------------------------\n");
}

static void
print_menu_options (GstElement * element, GstStructure * props,
    GstStructure * signals)
{
  GParamSpec **propspecs;
  GSignalQuery *query = NULL;
  guint *signal_ids = NULL;
  guint i = 0, idx = 0, nprops = 0, nsignals = 0;
  gchar *field = NULL;
  GType type;

  g_print ("\n\n================= Menu Options =================\n\n");
  g_print (" --------------- Properties ---------------\n");

  propspecs = g_object_class_list_properties (
      G_OBJECT_GET_CLASS (element), &nprops);

  for (i = 0; i < nprops; i++) {
    GParamSpec *param = propspecs[i];
    const gchar *name;

    // List only the properties that are mutable in any state.
    if (!(param->flags & GST_PARAM_MUTABLE_PLAYING))
      continue;

    name = g_param_spec_get_name (param);
    g_print ("   (%u) %-20s: %s\n", idx, name, g_param_spec_get_blurb (param));

    field = g_strdup_printf ("%u", idx);
    gst_structure_set (props, field, G_TYPE_STRING, name, NULL);

    g_free (field);
    field = NULL;

    // Increment the index for the next option.
    idx++;
  }

  g_print (" ---------------- Signals -----------------\n");

  for (type = G_OBJECT_TYPE (element); type; type = g_type_parent (type)) {
    if (type == GST_TYPE_ELEMENT || type == GST_TYPE_OBJECT)
      break;

    // Ignore GstBin elements.
    if (type == GST_TYPE_BIN && G_OBJECT_TYPE (element) != GST_TYPE_BIN)
      continue;

    // Lists the signals that this element type has.
    signal_ids = g_signal_list_ids (type, &nsignals);

    // Go over each signal and query additional information.
    for (i = 0; i < nsignals; i++) {
      query = g_new0 (GSignalQuery, 1);
      g_signal_query (signal_ids[i], query);

      if (query->signal_flags & G_SIGNAL_ACTION) {
        g_print ("   (%u) %-20s\n", idx, query->signal_name);

        field = g_strdup_printf ("%u", idx);
        gst_structure_set (signals, field, G_TYPE_STRING,
            query->signal_name, NULL);

        g_free (field);
        field = NULL;

        // Increment the index for the next option.
        idx++;
      }

      g_free (query);
      query = NULL;
    }

    // Free the allocated resources for the next iteration.
    g_free (signal_ids);
    signal_ids = NULL;
  }

  g_print (" ----------------- Other ------------------\n");
  g_print ("   (%s) Quit\n", QUIT_OPTION);
}

static void
parse_input (gchar * input)
{
  fflush (stdout);

  if (!fgets (input, OPTION_ARRAY_SIZE, stdin) ) {
    g_print ("Failed to parse input!\n");
    return;
  }

  // Clear trailing whitespace and newline.
  g_strchomp (input);
}

static gpointer
main_menu (gpointer data)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (data);
  GstElement *element = NULL;
  GstStructure *props = gst_structure_new_empty ("properties");
  GstStructure *signals = gst_structure_new_empty ("signals");
  gchar *option = g_new0 (gchar, OPTION_ARRAY_SIZE);
  gchar *value = g_new0 (gchar, OPTION_ARRAY_SIZE);
  gchar *name = g_new0 (gchar, OPTION_ARRAY_SIZE);

  // Start pipeline.
  gboolean active = start_pipeline (appctx);

  // Print a graph with all plugins in the pipeline.
  print_pipeline_elements (appctx->pipeline);

  // Choose a plugin to control.
  while (active && (NULL == element)) {
    g_print ("\nEnter name of the plugin which will be controlled: ");
    parse_input (name);

    element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), name);
    if (NULL == element)
      g_printerr ("WARNING: Invalid plugin name!\n");
  }

  g_free (name);

  // Options menu loop.
  while (active) {
    print_menu_options (element, props, signals);

    g_print ("\n\nChoose an option: ");
    parse_input (option);

    if (gst_structure_has_field (props, option)) {
      const gchar *propname = gst_structure_get_string (props, option);

      print_property_options (element, propname);
      g_print ("\nEnter value (or press Enter to keep current one): ");
      parse_input (value);

      if (!g_str_equal (value, "")) {
        gint propval = g_ascii_strtoll (value, NULL, 10);
        g_object_set (G_OBJECT (element), propname, propval, NULL);
      }
    } else if (gst_structure_has_field (signals, option)) {
      const gchar *signalname = gst_structure_get_string (signals, option);
      g_signal_emit_by_name (element, signalname);
    } else if (g_str_equal (option, QUIT_OPTION)) {
      active = FALSE;
    } else {
      g_print ("Invalid option: '%s'\n", option);
    }

    memset (value, 0x0, OPTION_ARRAY_SIZE * sizeof (*value));
    memset (option, 0x0, OPTION_ARRAY_SIZE * sizeof (*option));
  }

  // Free allocated resources.
  g_free (value);
  g_free (option);

  gst_structure_free (props);
  gst_structure_free (signals);

  gst_object_unref (element);

  // Stop pipeline.
  stop_pipeline (appctx);

  // Signal Main loop to shutdown.
  g_main_loop_quit (appctx->mloop);

  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GstAppContext *appctx = g_new0 (GstAppContext, 1);
  GOptionEntry entries[] = {
      {"eos-on-exit", 'e', 0, G_OPTION_ARG_NONE, &appctx->eos_on_exit,
          "Send EOS event before shutting the pipeline down", NULL},
      {NULL}
  };

  g_set_prgname ("gst-pipeline-app");

  {
    // Parse command line entries.
    GOptionContext *ctx = g_option_context_new ("DESCRIPTION");
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
      g_free (appctx);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      g_free (appctx);
      return -1;
    }
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  {
    // GST pipeline creation sequence.
    gchar **argvn = g_new0 (gchar *, argc);
    GError *error = NULL;

    // Create a null-terminated copy of argv[].
    memcpy (argvn, argv + 1, sizeof (gchar *) * (argc - 1));

    // Create a GST pipeline element.
    appctx->pipeline = gst_parse_launchv ((const gchar **) argvn, &error);
    g_free (argvn);

    // Check for errors on pipe creation.
    if ((NULL == appctx->pipeline) && (error != NULL)) {
      g_printerr ("ERROR: Failed to create pipeline: %s.\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      g_free (appctx);
      return -1;
    } else if ((NULL == appctx->pipeline) && (NULL == error)) {
      g_printerr ("ERROR: Failed to create pipeline.\n");
      g_free (appctx);
      return -1;
    } else if ((appctx->pipeline != NULL) && (error != NULL)) {
      g_printerr ("ERROR: erroneous pipeline: %s\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_object_unref (appctx->pipeline);
      g_free (appctx);
      return -1;
    }
  }

  // Initialize main loop.
  appctx->mloop = g_main_loop_new (NULL, FALSE);

  // Initialize application status variables.
  g_mutex_init (&appctx->pstatus.mutex);
  g_cond_init (&appctx->pstatus.changed);
  appctx->pstatus.state = GST_STATE_NULL;
  appctx->pstatus.flags = 0;

  {
    // Watch for messages on the pipeline's bus.
    GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline));
    gst_bus_add_signal_watch (bus);

    g_signal_connect (bus, "message::request-state",
        G_CALLBACK (request_state_cb), appctx);
    g_signal_connect (bus, "message::state-changed",
        G_CALLBACK (state_changed_cb), appctx);
    g_signal_connect (bus, "message::buffering", G_CALLBACK (buffering_cb),
        appctx);
    g_signal_connect (bus, "message::progress", G_CALLBACK (progress_cb),
        appctx);
    g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
    g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), appctx);
    g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), appctx);

    gst_object_unref (bus);
  }

  {
    // Initiate the main menu thread.
    GThread *thread = g_thread_new ("MainMenuThread", main_menu, appctx);

    // Run main loop.
    g_main_loop_run (appctx->mloop);

    // Waits until main menu thread finishes.
    g_thread_join (thread);
  }

  g_main_loop_unref (appctx->mloop);

  gst_object_unref (appctx->pipeline);

  gst_deinit ();

  g_mutex_clear (&appctx->pstatus.mutex);
  g_cond_clear (&appctx->pstatus.changed);

  g_free (appctx);

  return 0;
}
