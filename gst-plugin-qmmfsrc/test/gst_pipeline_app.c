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

// TODO remove it from global.
static GMainLoop *loop = NULL;

static gboolean
bus_message (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;

  switch (message->type) {
    case GST_MESSAGE_EOS:
      g_print ("End-of-stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_WARNING:
    {
      gchar *debug = NULL;
      GError *error = NULL;

      gst_message_parse_warning (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      gchar *debug = NULL;
      GError *error = NULL;

      gst_message_parse_error (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static void
print_pipeline_elements (GstElement * pipeline)
{
  GstIterator *it = gst_bin_iterate_elements (GST_BIN (pipeline));
  GValue item = G_VALUE_INIT;
  gboolean done = FALSE;

  g_print ("\nPipeline plugin names:");

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

  g_value_unset (&item);
  gst_iterator_free (it);
}

static void
print_property_options (GstElement * element, const gchar * name)
{
  GParamSpec *propspecs;
  GValue gvalue = { 0, };

  propspecs = g_object_class_find_property (G_OBJECT_GET_CLASS (element), name);
  g_value_init (&gvalue, propspecs->value_type);

  g_print (" ---------------------------------\n");

  switch (G_VALUE_TYPE (&gvalue)) {
    case G_TYPE_STRING:
    {
      gchar *value;
      g_object_get (G_OBJECT (element), name, &value, NULL);
      g_print (" Current value: %s\n", value);
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
          range->minimum, range->maximum);;
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

  g_value_reset (&gvalue);
}

static void
print_options_menu (GstElement * element, GstStructure *params)
{
  GParamSpec **propspecs;
  guint i = 0, idx = 0, nprops = 0;
  const gchar *name;

  g_print ("\n\n=========== Properties ===========\n\n");
  g_print (" ---------------------------------\n");

  propspecs = g_object_class_list_properties (
      G_OBJECT_GET_CLASS (element), &nprops);

  for (i = 0; i < nprops; i++) {
    GParamSpec *param = propspecs[i];
    gchar *field = NULL;

    // List only the properties that are mutable in any state.
    if (!(param->flags & GST_PARAM_MUTABLE_PLAYING))
      continue;

    name = g_param_spec_get_name (param);
    g_print ("   (%u) %-20s: %s\n", idx, name, g_param_spec_get_blurb (param));

    field = g_strdup_printf ("%u", idx);
    gst_structure_set (params, field, G_TYPE_STRING, name, NULL);

    idx++;
    g_free (field);
  }

  g_print ("   (%s) Quit\n", QUIT_OPTION);
}

static void
parse_cmdline (gchar * cmdline)
{
  fflush (stdout);

  if (!fgets (cmdline, OPTION_ARRAY_SIZE, stdin) ) {
    g_print ("Failed to parse input!\n");
    return;
  }

  // Clear trailing whitespace and newline.
  g_strchomp (cmdline);
}

static gpointer
options_menu (gpointer data)
{
  GstElement *pipeline = GST_ELEMENT_CAST (data);
  GstElement *element = NULL;
  GstStructure *params = gst_structure_new_empty ("properties");
  gchar *property = g_new0 (gchar, OPTION_ARRAY_SIZE);
  gchar *value = g_new0 (gchar, OPTION_ARRAY_SIZE);
  gchar *name = g_new0 (gchar, OPTION_ARRAY_SIZE);
  gboolean active = TRUE;

  print_pipeline_elements (pipeline);

  while (NULL == element) {
    g_print ("\nEnter name of the plugin which will be controlled: ");
    parse_cmdline (name);

    element = gst_bin_get_by_name (GST_BIN (pipeline), name);
    if (NULL == element)
      g_printerr ("WARNING: Invalid plugin name!\n");
  }
  g_free (name);

  while (active) {
    print_options_menu (element, params);
    g_print ("\n\nChoose parameter to modify: ");
    parse_cmdline (property);

    if (gst_structure_has_field (params, property)) {
      const gchar *propname = gst_structure_get_string (params, property);

      print_property_options (element, propname);
      g_print ("\nEnter value (or press Enter to keep current one): ");
      parse_cmdline (value);

      if (!g_str_equal (value, "")) {
        gint propval = g_ascii_strtoll (value, NULL, 10);
        g_object_set (G_OBJECT (element), propname, propval, NULL);
      }
    } else if (g_str_equal (property, QUIT_OPTION)) {
      g_main_loop_quit (loop);
      active = FALSE;
    } else {
      g_print ("Invalid option: '%s'\n", property);
    }

    memset (value, 0x0, OPTION_ARRAY_SIZE * sizeof (*value));
    memset (property, 0x0, OPTION_ARRAY_SIZE * sizeof (*property));
  }

  gst_object_unref (element);
  g_free (value);
  g_free (property);
  gst_structure_free (params);
  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  GstStateChangeReturn state;
  GThread *thread;
  guint watch_id;

  g_set_prgname ("gst-pipeline-app");

  // Initialize GST library.
  gst_init (&argc, &argv);

  // GST pipeline creation sequence.
  {
    gchar **argvn = g_new0 (gchar *, argc);
    GError *error = NULL;

    // Create a null-terminated copy of argv[].
    memcpy (argvn, argv + 1, sizeof (gchar *) * (argc - 1));

    // Create a GST pipeline element.
    pipeline = gst_parse_launchv ((const gchar **) argvn, &error);
    g_free (argvn);

    // Check for errors on pipe creation.
    if ((NULL == pipeline) && (error != NULL)) {
      g_printerr ("ERROR: Failed to create pipeline: %s.\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -1;
    } else if ((NULL == pipeline) && (NULL == error)) {
      g_printerr ("ERROR: Failed to create pipeline.\n");
      return -1;
    } else if ((pipeline != NULL) && (error != NULL)) {
      g_printerr ("ERROR: erroneous pipeline: %s\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -1;
    }
  }

  // Initialize main loop.
  loop = g_main_loop_new (NULL, FALSE);

  // Watch for messages on the pipeline's bus.
  {
    GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    watch_id = gst_bus_add_watch (bus, bus_message, loop);
    gst_object_unref (bus);
  }

  g_print ("Setting pipeline to PAUSED\n");
  state = gst_element_set_state (pipeline, GST_STATE_PAUSED);

  switch (state) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to paused state!\n");
      goto cleanup;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline is PREROLLED\n");
      break;
  }

  g_print ("Setting pipeline to PLAYING\n");
  state = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (state == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("ERROR: Failed to start up pipeline!\n");
    goto cleanup;
  }

  // Initiate the menu thread and main loop.
  thread = g_thread_new ("OptionsMenuThread", options_menu, pipeline);
  g_main_loop_run (loop);

  g_thread_join (thread);

cleanup:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  g_source_remove (watch_id);
  g_main_loop_unref (loop);

  return 0;
}
