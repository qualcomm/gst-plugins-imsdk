/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <string.h>
#include <stdlib.h>

#include "plugin-suite.h"

typedef struct _GstAppContext GstAppContext;

struct _GstAppContext
{
  gboolean          allsuites;
  gint              iteration;
  gint              duration;
  gboolean          help;

  // Enabled suite names.
  GList*            enabledsuites;

  // Main application event loop.
  GMainLoop         *mloop;

  // Asynchronous queue thread communication.
  GAsyncQueue       *messages;
};

static const GEnumValue suite_values[] =
{
  { GST_TEST_SUITE_ALL, "all suites", "all" },
  { GST_TEST_SUITE_CAMERA, "camera suite", "camera" },
  { GST_TEST_SUITE_AI, "AI suite", "ai" },
  { GST_TEST_SUITE_ML, "machine learning suite", "ml" },
  // Add new suites.
  { 0, NULL, NULL }
};

static GType
gst_test_suite_values_get_type (void)
{
  static GType gtype = 0;

  if (!gtype)
    gtype = g_enum_register_static ("GstTestSuiteValues", suite_values);

  return gtype;
}

static gboolean parse_option_snames (GstAppContext* appctx, gchar **snames)
{
  GType type;
  GEnumClass *enumclass;
  GEnumValue *val;
  gchar **sname = NULL, **temps = NULL;
  gchar *s = NULL;
  gboolean ret = FALSE;

  if (snames == NULL || *snames == NULL) {
    appctx->allsuites = TRUE;
    GST_INFO ("All suites are enabled\n");
    return TRUE;
  }

  type = gst_test_suite_values_get_type();
  enumclass = g_type_class_ref (type);
  sname = g_strsplit (*snames, " ", -1);
  temps = sname;

  while (temps != NULL && *temps != NULL) {
    s = *temps;
    val = g_enum_get_value_by_nick (enumclass, s);

    if (val == NULL) {
      GST_WARNING ("unsupported suite %s\n", s);
      temps++;
      continue;
    }
    GST_LOG ("%s suite(%d) is enabled\n", val->value_nick, val->value);
    appctx->enabledsuites = g_list_append (appctx->enabledsuites,
        GINT_TO_POINTER (val->value));
    ret = TRUE;
    temps++;
  }

  g_strfreev (sname);
  return ret;
}

static void gst_plugin_suite_help (GstPluginSuite *psuite)
{
  gint i = 0, length = 0;

  if (psuite == NULL) {
    g_printerr (
        "Usage: %s -s [snames] -i [iteration] -d [duration] -h  ...",
        "gst-test-framework");
    gst_printerr ("\n");
    gst_printerr (
        "  -s: Suite names, could be camera/ml\n"
        "  -i: Iteration times for each test, default is 1 time\n"
        "  -d: Running time for each test in seconds, default is 10 seconds\n"
        "  -h: Print available test case names when -s is configured");
    gst_printerr ("\n\n");
    return;
  }

  length = g_list_length (psuite->tcnames);
  g_print ("%s suite contains %d cases:\n", psuite->name, length);

  if (length == 0)
    return;

  for (i = 0; i < length; i++)
    g_print ("Case%d: %s\n", i, (g_list_nth (psuite->tcnames, i)->data));
}

static gboolean gst_plugin_get_suite (GstPluginSuite *psuite)
{
  gboolean ret = TRUE;

  if(psuite == NULL)
    return FALSE;

  // Get suite via suite index.
  switch (psuite->idx) {
    case GST_TEST_SUITE_CAMERA:
      GST_PLUGIN_GET_SUITE (camera, psuite);
      break;
    case GST_TEST_SUITE_ML:
      GST_PLUGIN_GET_SUITE (ml, psuite);
      break;
    default:
      ret = FALSE;
      GST_WARNING ("Unknown suite index %d.", psuite->idx);
      break;
  }

  return ret;
}

static int gst_plugin_run_suites (GstAppContext* appctx)
{
  gint ret = 0;
  GList *list = NULL;
  GstPluginSuite psuite;

  gst_check_init (NULL, NULL);

  for (list = appctx->enabledsuites; list != NULL; list = list->next) {
    psuite.idx = GPOINTER_TO_INT (list->data);
    psuite.iteration = appctx->iteration;
    psuite.duration = appctx->duration;
    psuite.tcnames = NULL;

    // Get the suite cases and run it.
    if (gst_plugin_get_suite (&psuite))
      if (appctx->help)
        gst_plugin_suite_help (&psuite);
      else
        ret = gst_check_run_suite (psuite.suite, psuite.name, __FILE__);
  }

  return ret;
}

int main (int argc, char **argv)
{
  GstAppContext appctx;
  GOptionContext *optctx;
  GError *error = NULL;
  gchar **snames = NULL;
  gint iteration = 1;
  gint duration = 10;
  gboolean help = FALSE;

  g_set_prgname ("gst-test-framework");

  GOptionEntry options[] = {
    {"snames", 's', 0, G_OPTION_ARG_STRING_ARRAY, &snames,
        "Sepcify suite names to be run", NULL},
    {"iteration", 'i', 0, G_OPTION_ARG_INT, &iteration,
        "Iteration times for each test, default is 1 time", NULL},
    {"duration", 'd', 0, G_OPTION_ARG_INT, &duration,
        "Running time for each test in seconds, default is 10 seconds", NULL},
    {"help", 'h', 0, G_OPTION_ARG_NONE, &help,
        "Print available test case names and exit", NULL},
    {NULL}
  };

  optctx = g_option_context_new ("");
  g_option_context_add_main_entries (optctx, options, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());

  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("ERROR: Couldn't initialize: %s\n",
        GST_STR_NULL (error->message));

    g_option_context_free (optctx);
    g_clear_error (&error);

    return -1;
  }
  g_option_context_free (optctx);

  if (snames == NULL) {
    gst_plugin_suite_help (NULL);
    return 0;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Process options.
  appctx.enabledsuites = NULL;
  if (!(parse_option_snames (&appctx, snames))) {
    g_printerr ("ERROR: invalid suite names\n");
    goto exit;
  }

  appctx.iteration = iteration;
  appctx.duration = duration;
  appctx.help = help;

  // Run suites
  gst_plugin_run_suites (&appctx);

exit:
  if (snames != NULL)
    g_strfreev (snames);

  g_list_free (appctx.enabledsuites);
  appctx.enabledsuites = NULL;

  return 0;
}
