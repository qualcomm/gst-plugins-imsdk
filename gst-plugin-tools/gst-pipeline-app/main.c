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

#define PRINT_LINE(c, l) \
  { guint i = 0; while (i < l) { g_print ("%c", c); i++; } }

#define PRINT_SECTION_SEPARATOR(c) \
  { g_print(" "); PRINT_LINE(c, 79); g_print("\n"); }

#define PRINT_MENU_HEADER \
  {\
    g_print("\n\n"); \
    PRINT_LINE('#', 37); g_print (" MENU "); PRINT_LINE('#', 37); \
    g_print("\n\n"); \
  }

#define PRINT_ELEMENT_PROPERTIES_SECTION(props) \
  if (g_list_length (props) > 0) { \
    GList *list = NULL; \
    \
    g_print(" "); \
    PRINT_LINE('=', 34); g_print (" Properties "); PRINT_LINE('=', 34); \
    g_print("\n"); \
    \
    for (list = props; list != NULL; list = list->next) \
      g_print ("%s\n", (gchar *) list->data); \
    \
    g_list_free_full (props, g_free); \
    props = NULL; \
  }

#define PRINT_PAD_PROPERTIES_SECTION(name, props) \
  if (g_list_length (props) > 0) { \
    GList *list = NULL; \
    \
    g_print(" "); \
    PRINT_LINE('-', (72 - strlen(name)) / 2); \
    g_print (" %*s Pad ", strlen(name), name); \
    PRINT_LINE('-', (74 - strlen(name)) / 2); \
    g_print("\n"); \
    \
    for (list = props; list != NULL; list = list->next) \
      g_print ("%s\n", (gchar *) list->data); \
    \
    g_list_free_full (props, g_free); \
    props = NULL; \
  }

#define PRINT_ELEMENT_SIGNALS_SECTION(signals) \
  if (g_list_length (signals) > 0) { \
    GList *list = NULL; \
    \
    g_print(" "); \
    PRINT_LINE('=', 36); g_print (" Signals "); PRINT_LINE('=', 35); \
    g_print("\n"); \
    \
    for (list = signals; list != NULL; list = list->next) \
      g_print ("%s\n", (gchar *) list->data); \
    \
    g_list_free_full (signals, g_free); \
    signals = NULL; \
  }

#define PRINT_OTHER_OPTS_HEADER \
  {\
    g_print(" "); \
    PRINT_LINE('=', 36); g_print (" Other "); PRINT_LINE('=', 36); \
    g_print("\n"); \
  }

#define MAX_INPUT_SIZE  50
#define QUIT_OPTION     "q"

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

    while (!appctx->pstatus.flags & GST_APP_PIPE_STATUS_EOS) {
      g_print ("Waiting for EOS event...\n");
      g_cond_wait (&appctx->pstatus.changed, &appctx->pstatus.mutex);
    }

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

  PRINT_SECTION_SEPARATOR ('-');
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

  PRINT_SECTION_SEPARATOR ('-');

  g_value_unset (&item);
  gst_iterator_free (it);
}

static void
print_property_info (GObject * object, GParamSpec *propspecs)
{
  PRINT_SECTION_SEPARATOR ('-');

  switch (G_PARAM_SPEC_VALUE_TYPE (propspecs)) {
    case G_TYPE_UINT:
    {
      guint value;
      GParamSpecUInt *range = G_PARAM_SPEC_UINT (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_print (" Current value: %u, Range: %u - %u\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_INT:
    {
      gint value;
      GParamSpecInt *range = G_PARAM_SPEC_INT (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_print (" Current value: %d, Range: %d - %d\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_ULONG:
    {
      gulong value;
      GParamSpecULong *range = G_PARAM_SPEC_ULONG (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_print (" Current value: %lu, Range: %lu - %lu\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_LONG:
    {
      glong value;
      GParamSpecLong *range = G_PARAM_SPEC_LONG (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_print (" Current value: %ld, Range: %ld - %ld\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_UINT64:
    {
      guint64 value;
      GParamSpecUInt64 *range = G_PARAM_SPEC_UINT64 (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_print (" Current value: %" G_GUINT64_FORMAT ", "
          "Range: %" G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT "\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_INT64:
    {
      gint64 value;
      GParamSpecInt64 *range = G_PARAM_SPEC_INT64 (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_print (" Current value: %" G_GINT64_FORMAT ", "
          "Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT "\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_FLOAT:
    {
      gfloat value;
      GParamSpecFloat *range = G_PARAM_SPEC_FLOAT (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_print (" Current value: %15.7g, Range: %15.7g - %15.7g\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_DOUBLE:
    {
      gdouble value;
      GParamSpecDouble *range = G_PARAM_SPEC_DOUBLE (propspecs);
      g_object_get (object, propspecs->name, &value, NULL);

      g_print (" Current value: %15.7g, Range: %15.7g - %15.7g\n", value,
          range->minimum, range->maximum);
      break;
    }
    case G_TYPE_BOOLEAN:
    {
      gboolean value;
      g_object_get (object, propspecs->name, &value, NULL);
      g_print (" Current value: %s, Possible values: 0(false), 1(true)\n",
          value ? "true" : "false");
      break;
    }
    case G_TYPE_STRING:
    {
      gchar *value;
      g_object_get (object, propspecs->name, &value, NULL);
      g_print (" Current value: %s\n", value);
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
          g_print ("   (%d): %-16s - %s\n", enumvalues[idx].value,
              enumvalues[idx].value_nick, enumvalues[idx].value_name);

          if (enumvalues[idx].value == value)
            nick = enumvalues[idx].value_nick;

          idx++;
        }

        g_print ("\n Current value: %d, \"%s\"\n", value, nick);
      } else if (propspecs->value_type == GST_TYPE_ARRAY) {
        GValue value = G_VALUE_INIT;
        gchar *string = NULL;

        g_value_init (&value, GST_TYPE_ARRAY);
        g_object_get_property (object, propspecs->name, &value);

        string = gst_value_serialize (&value);
        g_print ("\n Current value: %s\n", string);

        g_value_unset (&value);
        g_free (string);
      } else {
        g_print ("Unknown type %ld \"%s\"\n",
            (glong) propspecs->value_type, g_type_name (propspecs->value_type));
      }
      break;
  }

  PRINT_SECTION_SEPARATOR ('-');
}

static GList *
get_object_properties (GObject * object, guint * index, GstStructure * props)
{
  GParamSpec **propspecs;
  GList *list = NULL;
  guint i = 0, nprops = 0;

  propspecs = g_object_class_list_properties (
      G_OBJECT_GET_CLASS (object), &nprops);

  for (i = 0; i < nprops; i++) {
    GParamSpec *param = propspecs[i];
    gchar *field = NULL, *property = NULL;
    const gchar *name = NULL;

    // List only the properties that are mutable in any state.
    if (!(param->flags & GST_PARAM_MUTABLE_PLAYING))
      continue;

    name = g_param_spec_get_name (param);

    field = g_strdup_printf ("%u", (*index));
    property = !GST_IS_PAD (object) ? g_strdup (name) :
        g_strdup_printf ("%s::%s", GST_PAD_NAME (object), name);

    gst_structure_set (props, field, G_TYPE_STRING, property, NULL);

    list = g_list_append (list, g_strdup_printf ("   (%2u) %-20s: %s",
        (*index), name, g_param_spec_get_blurb (param)));

    g_free (property);
    g_free (field);

    // Increment the index for the next option.
    (*index)++;
  }

  return list;
}

static GList *
get_object_signals (GObject * object, guint * index, GstStructure * signals)
{
  GType type;
  GList *list = NULL;
  GSignalQuery *query = NULL;
  guint i = 0, *signal_ids = NULL, nsignals = 0;
  gchar *field = NULL;

  for (type = G_OBJECT_TYPE (object); type; type = g_type_parent (type)) {

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

        list = g_list_append (list, g_strdup_printf ("   (%2u) %-20s",
            (*index), query->signal_name));

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

  return list;
}

static void
print_menu_options (GstElement * element, GstStructure * props,
    GstStructure * signals)
{
  guint index = 0;
  GList *options = NULL;

  PRINT_MENU_HEADER;

  // Get the plugin element properties.
  options = get_object_properties (G_OBJECT (element), &index, props);
  PRINT_ELEMENT_PROPERTIES_SECTION (options);

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
          options = get_object_properties (object, &index, props);

          PRINT_PAD_PROPERTIES_SECTION (GST_PAD_NAME (object), options)
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
  options = get_object_signals (G_OBJECT (element), &index, signals);
  PRINT_ELEMENT_SIGNALS_SECTION (options);

  PRINT_OTHER_OPTS_HEADER;
  g_print ("   (%s) Quit\n", QUIT_OPTION);
}

static void
parse_input (gchar * input)
{
  fflush (stdout);
  memset (input, '\0', MAX_INPUT_SIZE * sizeof (*input));

  if (!fgets (input, MAX_INPUT_SIZE, stdin) ) {
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
  GstStructure *props = gst_structure_new_empty ("properties");
  GstStructure *signals = gst_structure_new_empty ("signals");
  GstElement *element = NULL;
  gchar *input = g_new0 (gchar, MAX_INPUT_SIZE);

  // Start pipeline.
  gboolean active = start_pipeline (appctx);

  // Print a graph with all plugins in the pipeline.
  print_pipeline_elements (appctx->pipeline);

  // Choose a plugin to control.
  while (active && (NULL == element)) {
    g_print ("\nEnter name of the plugin which will be controlled: ");
    parse_input (input);

    element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), input);
    if (NULL == element)
      g_printerr ("WARNING: Invalid plugin name!\n");
  }

  // Options menu loop.
  while (active) {
    print_menu_options (element, props, signals);

    g_print ("\n\nChoose an option: ");
    parse_input (input);

    if (gst_structure_has_field (props, input)) {
      GObject *object = NULL;
      GParamSpec *propspecs = NULL;
      gchar **strings = NULL;

      // Get the property string from the structure.
      const gchar *propname = gst_structure_get_string (props, input);

      // Split the string in order to check whether it is pad property.
      strings = g_strsplit (propname, "::", 2);

      // In case property belongs to a pad get reference to that pad by name.
      object = (g_strv_length (strings) != 2) ? G_OBJECT (element) :
          G_OBJECT (gst_element_get_static_pad (element, strings[0]));

      // In case property belongs to a pad get pad property name.
      propname = (g_strv_length (strings) != 2) ? propname : strings[1];

      // Get the property specs structure.
      propspecs =
          g_object_class_find_property (G_OBJECT_GET_CLASS (object), propname);

      print_property_info (object, propspecs);
      g_print ("\nEnter value (or press Enter to keep current one): ");
      parse_input (input);

      // If it's not an empty string deserialize the string to a GValue.
      if (!g_str_equal (input, "")) {
        GValue value = G_VALUE_INIT;
        g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));

        if (gst_value_deserialize (&value, input))
          g_object_set_property (object, propname, &value);
      }

      // Unreference in case the object was a pad.
      if (GST_IS_PAD (object))
        gst_object_unref (object);

      g_strfreev (strings);
    } else if (gst_structure_has_field (signals, input)) {
      const gchar *signalname = gst_structure_get_string (signals, input);
      g_signal_emit_by_name (element, signalname);
    } else if (g_str_equal (input, QUIT_OPTION)) {
      active = FALSE;
    } else {
      g_print ("Invalid option: '%s'\n", input);
    }
  }

  // Free allocated resources.
  g_free (input);

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
