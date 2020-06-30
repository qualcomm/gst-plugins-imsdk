/*
* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
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
#include <glib/gstdio.h>
#include <gst/gst.h>

#define HASH_LINE  "##################################################"
#define EQUAL_LINE "=================================================="
#define DASH_LINE  "--------------------------------------------------"

#define APPEND_SECTION_SEPARATOR(string) \
  g_string_append_printf (string, " %.*s%.*s\n", 39, DASH_LINE, 40, DASH_LINE);

#define APPEND_MENU_HEADER(string) \
  g_string_append_printf (string, "\n\n%.*s MENU %.*s\n\n", \
      37, HASH_LINE, 37, HASH_LINE);

#define APPEND_PIPELINE_CONTROLS_SECTION(string) \
  g_string_append_printf (string, " %.*s Pipeline Controls %.*s\n", \
      30, EQUAL_LINE, 30, EQUAL_LINE);

#define APPEND_ELEMENT_PROPERTIES_SECTION(string) \
  g_string_append_printf (string, " %.*s Plugin Properties %.*s\n", \
      30, EQUAL_LINE, 30, EQUAL_LINE);

#define APPEND_PAD_PROPERTIES_SECTION(name, string) \
  g_string_append_printf (string, " %.*s %s Pad %.*s\n", \
      36 - strlen(name) / 2, DASH_LINE, name, \
      37 - (strlen(name) / 2) - (strlen(name) % 2), DASH_LINE);

#define APPEND_ELEMENT_SIGNALS_SECTION(string) \
  g_string_append_printf (string, " %.*s Plugin Signals %.*s\n", \
      31, EQUAL_LINE, 32, EQUAL_LINE);

#define APPEND_OTHER_OPTS_SECTION(string) \
  g_string_append_printf (string, " %.*s Other %.*s\n", \
      36, EQUAL_LINE, 36, EQUAL_LINE);

#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
        : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
            : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
                : (state <= GST_STATE_NULL))))


#define STDIN_MESSAGE          "APP_STDIN_MSG"
#define TERMINATE_MESSAGE      "APP_TERMINATE_MSG"
#define PIPELINE_STATE_MESSAGE "APP_PIPELINE_STATE_MSG"
#define PIPELINE_EOS_MESSAGE   "APP_PIPELINE_EOS_MSG"

#define NULL_STATE_OPTION      "0"
#define READY_STATE_OPTION     "1"
#define PAUSED_STATE_OPTION    "2"
#define PLAYING_STATE_OPTION   "3"

#define PLUGIN_MODE_OPTION     "p"
#define MENU_BACK_OPTION       "b"

#define GST_APP_CONTEXT_CAST(obj)           ((GstAppContext*)(obj))

typedef struct _GstAppContext GstAppContext;

struct _GstAppContext
{
  // Main application event loop.
  GMainLoop   *mloop;

  // GStreamer pipeline instance.
  GstElement  *pipeline;

  // Asynchronous queue thread communication.
  GAsyncQueue *messages;
};

/// Command line option variables.
static gboolean eos_on_shutdown = FALSE;

static const GOptionEntry entries[] = {
    { "eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
        "Send EOS event before transition from PLAYING to NULL state", NULL
    },
    {NULL}
};

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = g_new0 (GstAppContext, 1);

  ctx->messages = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);
  ctx->pipeline = NULL;
  ctx->mloop = NULL;

  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  if (ctx->pipeline != NULL)
    gst_object_unref (ctx->pipeline);

  g_async_queue_unref (ctx->messages);
  g_free (ctx);

  return;
}

static gboolean
wait_stdin_message (GAsyncQueue * messages, gchar ** input)
{
  GstStructure *message = NULL;

  // Cleanup input variable from previous uses.
  g_free (*input);
  *input = NULL;

  // Wait for either a STDIN or TERMINATE message.
  while ((message = g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, STDIN_MESSAGE)) {
      *input = g_strdup (gst_structure_get_string (message, "input"));
      break;
    }

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
wait_pipeline_state_message (GAsyncQueue * messages, GstState state)
{
  GstStructure *message = NULL;

  // Pipeline does not notify us when changing to NULL state, skip wait.
  if (state == GST_STATE_NULL)
    return TRUE;

  // Wait for either a PIPELINE_STATE or TERMINATE message.
  while ((message = g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_STATE_MESSAGE)) {
      GstState new = GST_STATE_VOID_PENDING;
      gst_structure_get_uint (message, "new", (guint*) &new);

      if (new == state)
        break;
    }

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
wait_pipeline_eos_message (GAsyncQueue * messages)
{
  GstStructure *message = NULL;

  // Wait for either a PIPELINE_EOS or TERMINATE message.
  while ((message = g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_EOS_MESSAGE))
      break;

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstState state = GST_STATE_VOID_PENDING;
  static gboolean waiting_eos = FALSE;

  // Signal menu thread to quit.
  g_async_queue_push (appctx->messages,
      gst_structure_new_empty (TERMINATE_MESSAGE));

  // Get the current state of the pipeline.
  gst_element_get_state (appctx->pipeline, &state, NULL, 0);

  if (eos_on_shutdown && !waiting_eos && (state == GST_STATE_PLAYING)) {
    g_print ("\nEOS enabled -- Sending EOS on the pipeline\n");

    gst_element_post_message (GST_ELEMENT (appctx->pipeline),
        gst_message_new_custom (GST_MESSAGE_EOS, GST_OBJECT (appctx->pipeline),
            gst_structure_new_empty ("GST_PIPELINE_INTERRUPT")));

    g_print ("\nWaiting for EOS ...\n");
    waiting_eos = TRUE;
  } else if (eos_on_shutdown && waiting_eos) {
    g_print ("\nInterrupt while waiting for EOS - quit main loop...\n");

    gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
    g_main_loop_quit (appctx->mloop);

    waiting_eos = FALSE;
  } else {
    g_print ("\n\nReceived an interrupt signal, stopping pipeline ...\n");
    gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
    g_main_loop_quit (appctx->mloop);
  }

  return TRUE;
}

static gboolean
handle_stdin_source (GIOChannel * source, GIOCondition condition,
    gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GIOStatus status = G_IO_STATUS_NORMAL;
  gchar *input = NULL;

  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((G_IO_STATUS_ERROR == status) && (error != NULL)) {
      g_printerr ("Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    } else if ((G_IO_STATUS_ERROR == status) && (NULL == error)) {
      g_printerr ("Unknown error!\n");
      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  // Clear trailing whitespace and newline.
  input = g_strchomp (input);

  // Push stdin string into the inputs queue.
  g_async_queue_push (appctx->messages, gst_structure_new (STDIN_MESSAGE,
      "input", G_TYPE_STRING, input, NULL));
  g_free (input);

  return TRUE;
}

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  static GstState target_state = GST_STATE_VOID_PENDING;
  static gboolean in_progress = FALSE, buffering = FALSE;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_error (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);

      g_print ("\nSetting pipeline to NULL ...\n");
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (TERMINATE_MESSAGE));
      g_main_loop_quit (appctx->mloop);
    }
      break;
    case GST_MESSAGE_WARNING:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_warning (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);
    }
      break;
    case GST_MESSAGE_EOS:
      g_print ("\nReceived End-of-Stream from '%s' ...\n",
          GST_MESSAGE_SRC_NAME (message));

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (PIPELINE_EOS_MESSAGE));

      // Stop pipeline and quit main loop in case user interrupt has been sent.
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
      g_main_loop_quit (appctx->mloop);
      break;
    case GST_MESSAGE_REQUEST_STATE:
    {
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      GstState state;

      gst_message_parse_request_state (message, &state);
      g_print ("\nSetting pipeline state to %s as requested by %s...\n",
          gst_element_state_get_name (state), name);

      gst_element_set_state (appctx->pipeline, state);
      target_state = state;

      g_free (name);
    }
      break;
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old, new, pending;

      // Handle state changes only for the pipeline.
      if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (appctx->pipeline))
        break;

      gst_message_parse_state_changed (message, &old, &new, &pending);
      g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
          gst_element_state_get_name (old), gst_element_state_get_name (new),
          gst_element_state_get_name (pending));

      g_async_queue_push (appctx->messages, gst_structure_new (
          PIPELINE_STATE_MESSAGE, "new", G_TYPE_UINT, new,
          "pending", G_TYPE_UINT, pending, NULL));
    }
      break;
    case GST_MESSAGE_BUFFERING:
    {
      gint percent = 0;

      gst_message_parse_buffering (message, &percent);
      g_print ("\nBuffering... %d%%  \r", percent);

      if (percent == 100) {
        // Clear the BUFFERING status.
        buffering = FALSE;

        // Done buffering, if the pending state is playing, go back.
        if (target_state == GST_STATE_PLAYING) {
          g_print ("\nFinished buffering, setting state to PLAYING.\n");
          gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);
        }
      } else {
        // Busy buffering...
        gst_element_get_state (appctx->pipeline, NULL, &target_state, 0);

        if (!buffering && target_state == GST_STATE_PLAYING) {
          g_print ("\nBuffering, setting pipeline to PAUSED state.\n");
          gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED);
          target_state = GST_STATE_PAUSED;
        }

        buffering = TRUE;
      }
    }
      break;
    case GST_MESSAGE_PROGRESS:
    {
      GstProgressType type;
      gchar *code = NULL, *text = NULL;

      gst_message_parse_progress (message, &type, &code, &text);
      g_print ("\nProgress: (%s) %s\n", code, text);

      switch (type) {
        case GST_PROGRESS_TYPE_START:
        case GST_PROGRESS_TYPE_CONTINUE:
          in_progress = TRUE;
          break;
        case GST_PROGRESS_TYPE_COMPLETE:
        case GST_PROGRESS_TYPE_CANCELED:
        case GST_PROGRESS_TYPE_ERROR:
          in_progress = FALSE;
          break;
      }

      g_free (code);
      g_free (text);
    }
      break;
    default:
      break;
  }

  return TRUE;
}

static gboolean
update_pipeline_state (GstElement * pipeline, GAsyncQueue * messages,
    GstState state)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstState current, pending;

  // First check current and pending states of the pipeline.
  ret = gst_element_get_state (pipeline, &current, &pending, 0);

  if (ret != GST_STATE_CHANGE_SUCCESS) {
    g_printerr ("Failed to retrieve pipeline state!\n");
    return TRUE;
  }

  if (state == current) {
    g_print ("Already in %s state\n", gst_element_state_get_name (state));
    return TRUE;
  } else if (state == pending) {
    g_print ("Pending %s state\n", gst_element_state_get_name (state));
    return TRUE;
  }

  // Check whether to send an EOS event on the pipeline.
  if (eos_on_shutdown &&
      (current == GST_STATE_PLAYING) && (state == GST_STATE_NULL)) {
    g_print ("EOS enabled -- Sending EOS on the pipeline\n");

    if (!gst_element_send_event (pipeline, gst_event_new_eos ())) {
      g_printerr ("Failed to send EOS event!");
      return TRUE;
    }

    if (!wait_pipeline_eos_message (messages))
      return FALSE;
  }

  g_print ("Setting pipeline to %s\n", gst_element_state_get_name (state));
  ret = gst_element_set_state (pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to %s state!\n",
          gst_element_state_get_name (state));
      return TRUE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");

      ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret != GST_STATE_CHANGE_SUCCESS) {
        g_printerr ("Pipeline failed to PREROLL!\n");
        return TRUE;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  if (!wait_pipeline_state_message (messages, state))
    return FALSE;

  return TRUE;
}

static void
get_object_properties (GObject * object, GstState state, guint * index,
    GstStructure * props, GString * options)
{
  GParamSpec **propspecs;
  guint i = 0, nprops = 0;

  propspecs = g_object_class_list_properties (
      G_OBJECT_GET_CLASS (object), &nprops);

  for (i = 0; i < nprops; i++) {
    GParamSpec *param = propspecs[i];
    gchar *field = NULL, *property = NULL;
    const gchar *name = NULL;

    // List only the properties that are mutable in current state.
    if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (param, state))
      continue;

    name = g_param_spec_get_name (param);

    field = g_strdup_printf ("%u", (*index));
    property = !GST_IS_PAD (object) ? g_strdup (name) :
        g_strdup_printf ("%s::%s", GST_PAD_NAME (object), name);

    gst_structure_set (props, field, G_TYPE_STRING, property, NULL);

    g_string_append_printf (options, "   (%2u) %-25s: %s\n", (*index),
        name, g_param_spec_get_blurb (param));

    g_free (property);
    g_free (field);

    // Increment the index for the next option.
    (*index)++;
  }

  return;
}

static void
get_object_signals (GObject * object, guint * index, GstStructure * signals,
    GString * options)
{
  GSignalQuery *query = NULL;
  GType type;

  for (type = G_OBJECT_TYPE (object); type; type = g_type_parent (type)) {
    guint i = 0, *signal_ids = NULL, nsignals = 0;
    gchar *field = NULL;

    if (type == GST_TYPE_ELEMENT || type == GST_TYPE_OBJECT)
      break;

    // Ignore GstBin elements.
    if (type == GST_TYPE_BIN && G_OBJECT_TYPE (object) != GST_TYPE_BIN)
      continue;

    // Lists the signals that this element type has.
    signal_ids = g_signal_list_ids (type, &nsignals);

    // Go over each signal and query additional information.
    for (i = 0; i < nsignals; i++) {
      query = g_new0 (GSignalQuery, 1);
      g_signal_query (signal_ids[i], query);

      if (query->signal_flags & G_SIGNAL_ACTION) {
        field = g_strdup_printf ("%u", (*index));
        gst_structure_set (signals, field, G_TYPE_STRING,
            query->signal_name, NULL);

        g_string_append_printf (options, "   (%2u) %-25s\n", (*index),
            query->signal_name);

        g_free (field);
        field = NULL;

        // Increment the index for the next option.
        (*index)++;
      }

      g_free (query);
      query = NULL;
    }

    // Free the allocated resources for the next iteration.
    g_free (signal_ids);
    signal_ids = NULL;
  }

  return;
}

static void
print_pipeline_options (GstElement * pipeline)
{
  GString *options = g_string_new (NULL);

  APPEND_MENU_HEADER (options);

  APPEND_PIPELINE_CONTROLS_SECTION (options);
  g_string_append_printf (options, "   (%s) %-25s: %s\n", NULL_STATE_OPTION,
      "NULL", "Set the pipeline into NULL state");
  g_string_append_printf (options, "   (%s) %-25s: %s\n", READY_STATE_OPTION,
      "READY", "Set the pipeline into READY state");
  g_string_append_printf (options, "   (%s) %-25s: %s\n", PAUSED_STATE_OPTION,
      "PAUSED", "Set the pipeline into PAUSED state");
  g_string_append_printf (options, "   (%s) %-25s: %s\n", PLAYING_STATE_OPTION,
      "PLAYING", "Set the pipeline into PLAYING state");

  APPEND_OTHER_OPTS_SECTION (options);
  g_string_append_printf (options, "   (%s) %-25s: %s\n", PLUGIN_MODE_OPTION,
      "Plugin Mode", "Choose a plugin which to control");

  g_print ("%s", options->str);
  g_string_free (options, TRUE);
}

static void
print_pipeline_elements (GstElement * pipeline, GstStructure * plugins)
{
  GString *graph = g_string_new (NULL);
  guint index = 1;

  GstIterator *it = NULL;
  GValue item = G_VALUE_INIT;
  gboolean done = FALSE;

  APPEND_SECTION_SEPARATOR (graph);

  it = gst_bin_iterate_sorted (GST_BIN (pipeline));

  while (!done) {
    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_OK:
      {
        GstElement *element = g_value_get_object (&item);
        gchar *name = gst_element_get_name (element);
        gchar *field = g_strdup_printf ("%u", index);

        gst_structure_set (plugins, field, G_TYPE_STRING, name, NULL);
        g_string_append_printf (graph, "   (%2u) %-25s\n", index, name);

        g_free (field);
        g_free (name);

        g_value_reset (&item);
        index++;
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
  }

  APPEND_SECTION_SEPARATOR (graph);

  g_value_unset (&item);
  gst_iterator_free (it);

  g_print ("%s", graph->str);
  g_string_free (graph, TRUE);
}

static void
print_element_options (GstElement * element, GstStructure * props,
    GstStructure * signals)
{
  GString *options = g_string_new (NULL);
  GstState state = GST_STATE_VOID_PENDING;
  guint index = 0;

  APPEND_MENU_HEADER (options);

  // Get the current state of the element.
  gst_element_get_state (element, &state, NULL, 0);

  // Get the plugin element properties.
  APPEND_ELEMENT_PROPERTIES_SECTION (options);
  get_object_properties (G_OBJECT (element), state, &index, props, options);

  {
    GstIterator *it = NULL;
    gboolean done = FALSE;

    // Iterate over the element pads and check their properties.
    it = gst_element_iterate_pads (element);

    while (!done) {
      GValue item = G_VALUE_INIT;
      GObject *object = NULL;

      switch (gst_iterator_next (it, &item)) {
        case GST_ITERATOR_OK:
          object = g_value_get_object (&item);

          APPEND_PAD_PROPERTIES_SECTION (GST_PAD_NAME (object), options);
          get_object_properties (object, state, &index, props, options);

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
  }

  // Get the plugin element signals.
  APPEND_ELEMENT_SIGNALS_SECTION (options);
  get_object_signals (G_OBJECT (element), &index, signals, options);

  APPEND_OTHER_OPTS_SECTION (options);
  g_string_append_printf (options, "   (%s) %-25s: %s\n", MENU_BACK_OPTION,
      "Back", "Return to the previous menu");

  g_print ("%s", options->str);
  g_string_free (options, TRUE);
}

static void
print_property_info (GObject * object, GParamSpec *propspecs)
{
  GString *info = g_string_new (NULL);

  APPEND_SECTION_SEPARATOR (info);

  switch (G_PARAM_SPEC_VALUE_TYPE (propspecs)) {
    case G_TYPE_UINT:
    {
      guint value;
      GParamSpecUInt *range = G_PARAM_SPEC_UINT (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %u, Range: %u - %u\n",
          value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_INT:
    {
      gint value;
      GParamSpecInt *range = G_PARAM_SPEC_INT (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %d, Range: %d - %d\n",
          value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_ULONG:
    {
      gulong value;
      GParamSpecULong *range = G_PARAM_SPEC_ULONG (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %lu, Range: %lu - %lu\n",
          value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_LONG:
    {
      glong value;
      GParamSpecLong *range = G_PARAM_SPEC_LONG (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %ld, Range: %ld - %ld\n",
          value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_UINT64:
    {
      guint64 value;
      GParamSpecUInt64 *range = G_PARAM_SPEC_UINT64 (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %" G_GUINT64_FORMAT ", "
          "Range: %" G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT "\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_INT64:
    {
      gint64 value;
      GParamSpecInt64 *range = G_PARAM_SPEC_INT64 (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %" G_GINT64_FORMAT ", "
          "Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT "\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_FLOAT:
    {
      gfloat value;
      GParamSpecFloat *range = G_PARAM_SPEC_FLOAT (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %15.7g, "
          "Range: %15.7g - %15.7g\n", value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_DOUBLE:
    {
      gdouble value;
      GParamSpecDouble *range = G_PARAM_SPEC_DOUBLE (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %15.7g, "
          "Range: %15.7g - %15.7g\n", value, range->minimum, range->maximum);
      break;
    }
    case G_TYPE_BOOLEAN:
    {
      gboolean value;
      g_object_get (object, propspecs->name, &value, NULL);

      g_string_append_printf (info, " Current value: %s, Possible values: "
          "0(false), 1(true)\n", value ? "true" : "false");
      break;
    }
    case G_TYPE_STRING:
    {
      gchar *value;
      g_object_get (object, propspecs->name, &value, NULL);
      g_string_append_printf (info, " Current value: %s\n", value);
      break;
    }
    default:
      if (G_IS_PARAM_SPEC_ENUM (propspecs)) {
        GEnumValue *enumvalues;
        gint value;
        const gchar *nick = "";
        guint idx = 0;

        g_object_get (object, propspecs->name, &value, NULL);
        enumvalues = G_ENUM_CLASS (
            g_type_class_ref (propspecs->value_type))->values;

        while (enumvalues[idx].value_name) {
          g_string_append_printf (info, "   (%d): %-16s - %s\n",
              enumvalues[idx].value, enumvalues[idx].value_nick,
              enumvalues[idx].value_name);

          if (enumvalues[idx].value == value)
            nick = enumvalues[idx].value_nick;

          idx++;
        }

        g_string_append_printf (info, "\n Current value: %d, \"%s\"\n",
            value, nick);
      } else if (propspecs->value_type == GST_TYPE_ARRAY) {
        GValue value = G_VALUE_INIT;
        gchar *string = NULL;

        g_value_init (&value, GST_TYPE_ARRAY);
        g_object_get_property (object, propspecs->name, &value);

        string = gst_value_serialize (&value);
        g_string_append_printf (info, "\n Current value: %s\n", string);

        g_value_unset (&value);
        g_free (string);
      } else if (propspecs->value_type == GST_TYPE_STRUCTURE) {
        GValue value = G_VALUE_INIT;
        GstStructure *structure = NULL;
        gchar *string = NULL;

        g_value_init (&value, GST_TYPE_STRUCTURE);
        g_object_get_property (object, propspecs->name, &value);

        structure = g_value_dup_boxed (&value);
        g_value_unset (&value);

        string = gst_structure_to_string (structure);
        gst_structure_free (structure);

        g_string_append_printf (info, "\n Current value: %s\n", string);
        g_free (string);
      } else {
        g_string_append_printf (info, "Unknown type %ld \"%s\"\n",
            (glong) propspecs->value_type, g_type_name (propspecs->value_type));
      }
      break;
  }

  APPEND_SECTION_SEPARATOR (info);

  g_print ("%s", info->str);
  g_string_free (info, TRUE);
}

static gboolean
gst_pipeline_menu (GstElement * pipeline, GAsyncQueue * messages,
    GstElement ** element)
{
  gchar *input = NULL;

  print_pipeline_options (pipeline);

  g_print ("\n\nChoose an option: ");

  // If FALSE is returned termination signal has been issued.
  if (!wait_stdin_message (messages, &input))
    return FALSE;

  if (g_str_equal (input, NULL_STATE_OPTION)) {
    if (!update_pipeline_state (pipeline, messages, GST_STATE_NULL))
      return FALSE;

  } else if (g_str_equal (input, READY_STATE_OPTION)) {
    if (!update_pipeline_state (pipeline, messages, GST_STATE_READY))
      return FALSE;

  } else if (g_str_equal (input, PAUSED_STATE_OPTION)) {
    if (!update_pipeline_state (pipeline, messages, GST_STATE_PAUSED))
      return FALSE;

  } else if (g_str_equal (input, PLAYING_STATE_OPTION)) {
    if (!update_pipeline_state (pipeline, messages, GST_STATE_PLAYING))
      return FALSE;

  } else if (g_str_equal (input, PLUGIN_MODE_OPTION)) {
    GstStructure *plugins = gst_structure_new_empty ("plugins");

    // Print a graph with all plugins in the pipeline.
    print_pipeline_elements (pipeline, plugins);

    // Choose a plugin to control.
    g_print ("\nEnter plugin name or its index (or press Enter to return): ");

    // If FALSE is returned termination signal has been issued.
    if (!wait_stdin_message (messages, &input)) {
      gst_structure_free (plugins);
      return FALSE;
    }

    if (gst_structure_has_field (plugins, input)) {
      const gchar *name = gst_structure_get_string (plugins, input);

      if ((*element = gst_bin_get_by_name (GST_BIN (pipeline), name)) == NULL)
        g_printerr ("Invalid plugin index!\n");

    } else if (!g_str_equal (input, "")) {
      if ((*element = gst_bin_get_by_name (GST_BIN (pipeline), input)) == NULL)
        g_printerr ("Invalid plugin name!\n");
    }

    gst_structure_free (plugins);
  }

  g_free (input);
  return TRUE;
}

static gboolean
gst_element_menu (GstElement ** element, GAsyncQueue * messages)
{
  GstStructure *props = NULL, *signals = NULL;
  gchar *input = NULL;

  props = gst_structure_new_empty ("properties");
  signals = gst_structure_new_empty ("signals");

  print_element_options (*element, props, signals);

  g_print ("\n\nChoose an option: ");

  // If FALSE is returned termination signal has been issued.
  if (!wait_stdin_message (messages, &input)) {
    gst_structure_free (props);
    gst_structure_free (signals);

    return FALSE;
  }

  if (gst_structure_has_field (props, input)) {
    GObject *object = NULL;
    GParamSpec *propspecs = NULL;
    gchar **strings = NULL;

    // Get the property string from the structure.
    const gchar *propname = gst_structure_get_string (props, input);

    // Split the string in order to check whether it is pad property.
    strings = g_strsplit (propname, "::", 2);

    // In case property belongs to a pad get reference to that pad by name.
    object = (g_strv_length (strings) != 2) ? G_OBJECT (*element) :
        G_OBJECT (gst_element_get_static_pad (*element, strings[0]));

    // In case property belongs to a pad get pad property name.
    propname = (g_strv_length (strings) != 2) ? propname : strings[1];

    // Get the property specs structure.
    propspecs =
        g_object_class_find_property (G_OBJECT_GET_CLASS (object), propname);

    print_property_info (object, propspecs);

    if (propspecs->flags & G_PARAM_WRITABLE) {
      g_print ("\nEnter value (or press Enter to keep current one): ");

      // If FALSE is returned termination signal has been issued.
      if (!wait_stdin_message (messages, &input)) {
        gst_structure_free (props);
        gst_structure_free (signals);

        return FALSE;
      }

      // If it's not an empty string deserialize the string to a GValue.
      if (!g_str_equal (input, "")) {
        GValue value = G_VALUE_INIT;
        g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));

        if (gst_value_deserialize (&value, input))
          g_object_set_property (object, propname, &value);
      }
    } else if (propspecs->flags & G_PARAM_READABLE) {
      g_print ("\nRead-Only property. Press Enter to continue...");
    }

    // Unreference in case the object was a pad.
    if (GST_IS_PAD (object))
      gst_object_unref (object);

    g_strfreev (strings);
  } else if (gst_structure_has_field (signals, input)) {
    const gchar *signalname = gst_structure_get_string (signals, input);
    g_signal_emit_by_name (*element, signalname);
  } else if (g_str_equal (input, MENU_BACK_OPTION)) {
    gst_object_unref (*element);
    *element = NULL;
  } else {
    g_print ("Invalid option: '%s'\n", input);
  }

  g_free (input);

  gst_structure_free (props);
  gst_structure_free (signals);

  return TRUE;
}

static gpointer
main_menu (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstElement *element = NULL;
  gboolean active = TRUE;

  while (active) {
    // In case no element has been chosen enter in the pipeline menu.
    if (NULL == element)
      active = gst_pipeline_menu (appctx->pipeline, appctx->messages, &element);
    else
      active = gst_element_menu (&element, appctx->messages);
  }

  if (element != NULL)
    gst_object_unref (element);

  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GstAppContext *appctx = gst_app_context_new ();
  GOptionContext *optsctx = NULL;
  GstBus *bus = NULL;
  GIOChannel *iostdin = NULL;
  GThread *mthread = NULL;
  guint bus_watch_id = 0, intrpt_watch_id = 0, stdin_watch_id = 0;
  gchar **argvn = NULL;

  g_set_prgname ("gst-pipeline-app");

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

      gst_app_context_free (appctx);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // GST pipeline creation sequence.
  if ((argvn = g_new0 (gchar *, argc)) != NULL) {
    GError *error = NULL;

    // Create a null-terminated copy of argv[].
    memcpy (argvn, argv + 1, sizeof (gchar *) * (argc - 1));

    // Create a GST pipeline element.
    appctx->pipeline = gst_parse_launchv ((const gchar **) argvn, &error);
    g_free (argvn);

    // Check for errors on pipe creation.
    if ((NULL == appctx->pipeline) && (error != NULL)) {
      g_printerr ("Failed to create pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);

      gst_app_context_free (appctx);
      return -1;
    } else if ((NULL == appctx->pipeline) && (NULL == error)) {
      g_printerr ("Failed to create pipeline, unknown error!\n");
      gst_app_context_free (appctx);
      return -1;
    } else if ((appctx->pipeline != NULL) && (error != NULL)) {
      g_printerr ("Erroneous pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);

      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("ERROR: Failed to allocate memory for input arguments!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (
      SIGINT, handle_interrupt_signal, appctx);

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  bus_watch_id = gst_bus_add_watch (bus, handle_bus_message, appctx);
  gst_object_unref (bus);

  // Create IO channel from the stdin stream.
  if ((iostdin = g_io_channel_unix_new (fileno (stdin))) == NULL) {
    g_printerr ("ERROR: Failed to initialize Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Register handing function with the main loop for stdin channel data.
  stdin_watch_id = g_io_add_watch (
      iostdin, G_IO_IN | G_IO_PRI, handle_stdin_source, appctx);
  g_io_channel_unref (iostdin);

  // Initiate the main menu thread.
  if ((mthread = g_thread_new ("MainMenu", main_menu, appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create event loop thread!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Run main loop.
  g_main_loop_run (appctx->mloop);

  // Waits until main menu thread finishes.
  g_thread_join (mthread);

  g_source_remove (stdin_watch_id);
  g_source_remove (intrpt_watch_id);
  g_source_remove (bus_watch_id);

  gst_app_context_free (appctx);
  gst_deinit ();

  return 0;
}
