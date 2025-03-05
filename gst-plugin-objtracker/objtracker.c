/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "objtracker.h"

#include <stdio.h>
#include <stdlib.h>


#define GST_CAT_DEFAULT gst_objtracker_debug
GST_DEBUG_CATEGORY_STATIC (gst_objtracker_debug);

#define gst_objtracker_parent_class parent_class
G_DEFINE_TYPE (GstObjTracker, gst_objtracker, GST_TYPE_BASE_TRANSFORM);

#define GST_OBJ_TRACKER_SINK_CAPS \
    "video/x-raw(ANY);" \
    "text/x-raw, format = utf8"

#define GST_OBJ_TRACKER_SRC_CAPS \
    "video/x-raw(ANY);" \
    "text/x-raw, format = utf8"

#define DEFAULT_PROP_ALGO_BACKEND           NULL
#define DEFAULT_PROP_PARAMETERS             NULL

enum
{
  PROP_0,
  PROP_ALGO_BACKEND,
  PROP_PARAMETERS,
};

static GstStaticPadTemplate gst_objtracker_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_OBJ_TRACKER_SINK_CAPS)
    );
static GstStaticPadTemplate gst_objtracker_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_OBJ_TRACKER_SRC_CAPS)
    );

static GstFlowReturn
gst_objtracker_prepare_output_buffer (GstBaseTransform *base,
    GstBuffer *inbuffer, GstBuffer **outbuffer)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (base);

  if (gst_base_transform_is_in_place (base)) {
    GST_DEBUG_OBJECT (objtracker, "In place modification");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  *outbuffer = gst_buffer_new ();

  // Copy the timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_TIMESTAMPS, 0,
      -1);

  return GST_FLOW_OK;
}

static gboolean
gst_objtracker_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (base);
  GstStructure *structure = gst_caps_get_structure (incaps, 0);

  if (NULL == objtracker->backend) {
    GST_ELEMENT_ERROR (objtracker, RESOURCE, NOT_FOUND, (NULL),
        ("Algo name not set!"));
    return FALSE;
  }

  gst_base_transform_set_passthrough (base, FALSE);
  gst_base_transform_set_in_place (base,
      gst_structure_has_name (structure, "video/x-raw"));

  gst_objtracker_algo_free (objtracker->algo);
  objtracker->algo = gst_objtracker_algo_new (objtracker->backend);

  if (NULL == objtracker->algo) {
    GST_ELEMENT_ERROR (objtracker, RESOURCE, FAILED, (NULL),
        ("Algo creation failed!"));
    return FALSE;
  }

  if (!gst_objtracker_algo_init (objtracker->algo)) {
    GST_ELEMENT_ERROR (objtracker, RESOURCE, FAILED, (NULL),
        ("Algo initialization failed!"));
    return FALSE;
  }

  if (objtracker->algoparameters != NULL)
    structure = gst_structure_new ("options",
      GST_OBJTRACKER_ALGO_OPT_PARAMETERS, GST_TYPE_STRUCTURE,
          objtracker->algoparameters, NULL);
  else
    structure = NULL;

  if (!gst_objtracker_algo_set_opts (objtracker->algo, structure)) {
    GST_ELEMENT_ERROR (objtracker, RESOURCE, FAILED, (NULL),
        ("Failed to set algo options!"));
    return FALSE;
  }

  GST_DEBUG_OBJECT (objtracker, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstFlowReturn
gst_objtracker_transform (GstBaseTransform *base, GstBuffer *inbuffer,
    GstBuffer *outbuffer)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (base);
  GstMapInfo memmap = {};
  gchar *input_text = NULL, *output_text = NULL;
  guint length = 0;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  GstMemory *mem = NULL;
  gboolean success = FALSE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  time = gst_util_get_timestamp ();

  // Map the input buffer
  if (!gst_buffer_map (inbuffer, &memmap, GST_MAP_READ)) {
    GST_ERROR_OBJECT (objtracker, "Failed to map input buffer memory block!");
    return GST_FLOW_ERROR;
  }

  // Copy of the buffer's data is needed because gst_value_deserialize()
  // modifies the given data by placing null character at the end of the
  // string. This causes data loss when two plugins are modifying the
  // same buffer data.
  input_text = g_strndup ((gchar *) memmap.data, memmap.size);

  gst_buffer_unmap (inbuffer, &memmap);

  success = gst_objtracker_algo_execute_text(objtracker->algo, input_text,
      &output_text);
  if (!success) {
    GST_ERROR_OBJECT (objtracker, "Failed to serialize output data!");
    goto cleanup;
  }

  // Increase the length by 1 byte for the '\0' character.
  length = strlen (output_text) + 1;

  // Append the output string to the output buffer
  mem = gst_memory_new_wrapped ((GstMemoryFlags) 0, output_text, length, 0,
      length, output_text, g_free);
  gst_buffer_append_memory (outbuffer, mem);

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (objtracker, "Execute took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

cleanup:
  g_free (input_text);

  return success ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static GstFlowReturn
gst_objtracker_transform_ip (GstBaseTransform * base, GstBuffer * buffer)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (base);
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  time = gst_util_get_timestamp ();

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  success = gst_objtracker_algo_execute_buffer(objtracker->algo, buffer);
  if (!success) {
    GST_ERROR_OBJECT (objtracker, "Failed to process object tracker algo!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (objtracker, "Process took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_objtracker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (object);

  switch (prop_id) {
    case PROP_ALGO_BACKEND:
      g_free (objtracker->backend);
      objtracker->backend = g_strdup (g_value_get_string (value));
      break;
    case PROP_PARAMETERS:
    {
      GValue structure = G_VALUE_INIT;

      g_value_init (&structure, GST_TYPE_STRUCTURE);

      if (!gst_parse_string_property_value (value, &structure)) {
        GST_ERROR_OBJECT (objtracker, "Failed to parse parameters!");
        break;
      }

      if (objtracker->algoparameters != NULL)
        gst_structure_free (objtracker->algoparameters);

      objtracker->algoparameters =
          GST_STRUCTURE (g_value_dup_boxed (&structure));
      g_value_unset (&structure);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_objtracker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (object);

  switch (prop_id) {
    case PROP_ALGO_BACKEND:
      g_value_set_string (value, objtracker->backend);
      break;
    case PROP_PARAMETERS:
    {
      gchar *string = NULL;

      if (objtracker->algoparameters != NULL)
        string = gst_structure_to_string (objtracker->algoparameters);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_objtracker_finalize (GObject * object)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (object);

  if (objtracker->algo != NULL)
    gst_objtracker_algo_free (objtracker->algo);

  if (objtracker->algoparameters != NULL)
    gst_structure_free (objtracker->algoparameters);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (objtracker));
}

static void
gst_objtracker_class_init (GstObjTrackerClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_objtracker_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_objtracker_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_objtracker_finalize);

  g_object_class_install_property (gobject, PROP_ALGO_BACKEND,
      g_param_spec_string ("algo", "Algorithm",
          "Algorithm name that used for the video tracker",
          DEFAULT_PROP_ALGO_BACKEND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_PARAMETERS,
      g_param_spec_string ("parameters", "Parameters",
          "Parameters, parameters used by chosen object tracker algorithm "
          "in GstStructure string format. "
          "Applicable only for some algorithms.",
          DEFAULT_PROP_PARAMETERS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element, "Object Tracker",
      "Filter/Effect/Converter",
      "Tracks objects throughout consecutive frames", "QTI");

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_objtracker_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_objtracker_src_template));

  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_objtracker_prepare_output_buffer);

  base->set_caps = GST_DEBUG_FUNCPTR (gst_objtracker_set_caps);
  base->transform_ip = GST_DEBUG_FUNCPTR (gst_objtracker_transform_ip);
  base->transform = GST_DEBUG_FUNCPTR (gst_objtracker_transform);
}

static void
gst_objtracker_init (GstObjTracker * objtracker)
{
  objtracker->backend = DEFAULT_PROP_ALGO_BACKEND;

  objtracker->algoparameters = DEFAULT_PROP_PARAMETERS;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (objtracker), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_objtracker_debug, "qtiobjtracker", 0,
      "QTI object tracker plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtiobjtracker", GST_RANK_NONE,
      GST_TYPE_OBJ_TRACKER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtiobjtracker,
    "QTI object tracker plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
