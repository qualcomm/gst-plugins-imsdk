/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * This file provides utility functions for gst applications.
 */

#include <stdio.h>
#include <stdarg.h>
#include <glob.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib-unix.h>
#include <gst/gst.h>

#include "gst_sample_apps_utils.h"

/*
 * Check if File Exists
 *
 * @param path file path to check for existence
 * @result TRUE if file exists and can be accessed, FALSE otherwise
 */
gboolean
file_exists (const gchar * path)
{
  FILE *fp = fopen (path, "r");
  if (fp) {
    fclose (fp);
    return TRUE;
  }
  return FALSE;
}

/*
 * Check if File Location is Valid
 *
 * @param path file path to check for valid path
 * @result TRUE if path exists and can be accessed, FALSE otherwise
 */
gboolean
file_location_exists (const gchar * path)
{
  FILE *fp = fopen (path, "ab");
  if (fp) {
    fclose (fp);
    return TRUE;
  }
  return FALSE;
}

/*
 * Get Active Display Mode
 *
 * @param width fill display current width
 * @param height fill display current height
 * @result TRUE if api is able to provide information, FALSE otherwise
 */
gboolean
get_active_display_mode (gint * width, gint * height)
{
  gchar line[128];
  FILE *fp = fopen ("/sys/class/drm/card0-HDMI-A-1/modes", "rb");
  if (!fp) {
    return FALSE;
  }

  if (fgets (line, sizeof (line), fp) == NULL) {
    fclose (fp);
    return FALSE;
  }

  fclose (fp);

  if (strlen (line) > 0) {
    if (2 == sscanf (line, "%dx%d", width, height)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * Handles interrupt by CTRL+C.
 *
 * @param userdata pointer to AppContext.
 * @return FALSE if the source should be removed, else TRUE.
 */
gboolean
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
  return TRUE;
}

/**
 * Handles error events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to Main Loop for Handling Error.
 */
void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop *) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (mloop);
}

/**
 * Handles warning events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to Main Loop for Handling Error.
 */
void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

/**
 * Handles End-Of-Stream(eos) Event.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to Main Loop for Handling eos.
 */
void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop *) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

/**
 * Handles state change events for the pipeline
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to Application Pipeline.
 */
void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);

  g_print ("\nPipeline state changed from %s to %s:\n",
      gst_element_state_get_name (old),
      gst_element_state_get_name (new_st));

  if ((new_st == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {

    if (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr (
          "\nPipeline doesn't want to transition to PLAYING state!\n");
      return;
    }
  }
}

/**
 * Sets an enum property on a GstElement
 *
 * @param element The GstElement on which to set the property.
 * @param propname The name of the property to set.
 * @param valname The value to set the property to.
 */
void
gst_element_set_enum_property (GstElement * element, const gchar * propname,
    const gchar * valname)
{
  GValue value = G_VALUE_INIT;
  GParamSpec *propspecs = NULL;

  propspecs =
      g_object_class_find_property (G_OBJECT_GET_CLASS (element), propname);
  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspecs));
  gst_value_deserialize (&value, valname);

  g_object_set_property (G_OBJECT (element), propname, &value);
  g_value_unset (&value);
}

/**
 * Get enum for property nick name
 *
 * @param element Plugin to query the property.
 * @param prop_name Property Name.
 * @param prop_value_nick Property Nick Name.
 */
gint
get_enum_value (GstElement * element, const gchar * prop_name,
    const gchar * prop_value_nick)
{
  GParamSpec **property_specs;
  GObject *obj = G_OBJECT (element);
  GObjectClass *obj_class = G_OBJECT_GET_CLASS (element);
  guint num_properties, i;
  gint ret = -1;

  property_specs = g_object_class_list_properties (obj_class, &num_properties);

  for (i = 0; i < num_properties; i++) {
    GParamSpec *param = property_specs[i];
    GEnumValue *values;
    GType owner_type = param->owner_type;
    guint j = 0;

    if (obj == NULL && (owner_type == G_TYPE_OBJECT
            || owner_type == GST_TYPE_OBJECT || owner_type == GST_TYPE_PAD))
      continue;

    if (strcmp (prop_name, param->name)) {
      continue;
    }

    if (!G_IS_PARAM_SPEC_ENUM (param)) {
      continue;
    }

    values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
    while (values[j].value_name) {
      if (!strcmp (prop_value_nick, values[j].value_nick)) {
        ret = values[j].value;
        break;
      }
      j++;
    }
  }

  g_free (property_specs);
  return ret;
}

/**
 * Unref Gstreamer plugin elements
 *
 * @param element Plugins.
 *
 * Unref all elements if any fails to create.
 */
void
unref_elements (void *first_elem, ...)
{
  va_list args;

  va_start (args, first_elem);

  while (1) {
    if (first_elem) {
      if (!strcmp ((char *) first_elem, "NULL"))
        break;
      gst_object_unref (first_elem);
    }

    first_elem = va_arg (args, void *);
  }

  va_end (args);
}

/* Receives a list of pointers to variable containing pointer to gst element
 * and unrefs the gst element if needed
 */
void
cleanup_gst (void *first_elem, ...)
{
  va_list args;
  void **p_gst_obj = (void **) first_elem;

  va_start (args, first_elem);
  while (p_gst_obj) {
    if (*p_gst_obj)
      gst_object_unref (*p_gst_obj);
    p_gst_obj = va_arg (args, void **);
  }
  va_end (args);
}

gboolean
is_camera_available ()
{
  gboolean is_qmmf_on = FALSE;
  GError *error = NULL;

  GOptionEntry entries[] = { { NULL } };
  GOptionContext *ctx = g_option_context_new ("Dummy context");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  g_option_context_parse (ctx, NULL, NULL, &error);
  g_option_context_free (ctx);

  GstRegistry *gst_reg = gst_registry_get ();
  GstPlugin *gst_plugin = NULL;

  if (gst_reg)
    gst_plugin = gst_registry_find_plugin (gst_reg, "qtiqmmfsrc");

  if (gst_plugin) {
    gst_object_unref (gst_plugin);
    is_qmmf_on = TRUE;
  }

  return is_qmmf_on;
}

gboolean
is_camx_present (void)
{
  glob_t video_nodes;
  gboolean camx_present = FALSE;
  size_t i;

  memset (&video_nodes, 0, sizeof (video_nodes));

  if (access ("/sys/class/video4linux", F_OK) != 0)
    return FALSE;

  if (glob ("/dev/video*", 0, NULL, &video_nodes) != 0)
    return FALSE;

  for (i = 0; i < video_nodes.gl_pathc; ++i) {
    const gchar *video_path = video_nodes.gl_pathv[i];
    const gchar *video_name = strrchr (video_path, '/');
    gchar driver_module_link[PATH_MAX];
    gchar resolved_path[PATH_MAX];

    if (video_name == NULL || *(video_name + 1) == '\0')
      continue;

    video_name++;

    g_snprintf (driver_module_link, sizeof (driver_module_link),
        "/sys/class/video4linux/%s/device/driver/module", video_name);

    if (realpath (driver_module_link, resolved_path) == NULL)
      continue;

    if (strstr (resolved_path, "camera") != NULL) {
      camx_present = TRUE;
      break;
    }
  }

  globfree (&video_nodes);
  return camx_present;
}

GstElement *
create_camera_source_bin (const gchar * bin_name)
{
  GstElement *src_bin = NULL;
  GstElement *src = NULL;
  GstElement *qtivtransform = NULL;
  GstPad *target_src_pad = NULL;
  GstPad *ghost_src_pad = NULL;
  gboolean camx_present = FALSE;

  camx_present = is_camx_present ();

  if (camx_present) {
    src = gst_element_factory_make ("qtiqmmfsrc", "camera_src");
    if (!src) {
      g_printerr ("Failed to create qtiqmmfsrc\n");
      return NULL;
    }
    g_print ("Camera Overlay enabled \n");
    return src;
  }

  src_bin = gst_bin_new (bin_name ? bin_name : "camera_source_bin");

  if (!src_bin) {
    g_printerr ("Failed to create camera source bin\n");
    return NULL;
  }

  src = gst_element_factory_make ("libcamerasrc", "camera_src");
  qtivtransform = gst_element_factory_make ("qtivtransform", "camera_transform");

  if (!src || !qtivtransform) {
    g_printerr ("Failed to create libcamerasrc fallback source chain\n");
    gst_object_unref (src_bin);
    return NULL;
  }

  gst_bin_add_many (GST_BIN (src_bin), src, qtivtransform, NULL);

  if (!gst_element_link_many (src, qtivtransform, NULL)) {
    g_printerr ("Failed to link libcamerasrc -> qtivtransform\n");
    gst_object_unref (src_bin);
    return NULL;
  }

  target_src_pad = gst_element_get_static_pad (qtivtransform, "src");
  if (!target_src_pad) {
    g_printerr ("Failed to get qtivtransform src pad\n");
    gst_object_unref (src_bin);
    return NULL;
  }

  g_print ("Camera Overlay not enabled\n");
  ghost_src_pad = gst_ghost_pad_new ("src", target_src_pad);
  gst_object_unref (target_src_pad);

  if (!ghost_src_pad) {
    g_printerr ("Failed to create ghost src pad for camera source bin\n");
    gst_object_unref (src_bin);
    return NULL;
  }

  if (!gst_element_add_pad (src_bin, ghost_src_pad)) {
    g_printerr ("Failed to add ghost src pad to camera source bin\n");
    gst_object_unref (ghost_src_pad);
    gst_object_unref (src_bin);
    return NULL;
  }

  return src_bin;
}
