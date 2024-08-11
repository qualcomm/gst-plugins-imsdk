/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "metatransform.h"

#include <gst/utils/common-utils.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

#define GST_CAT_DEFAULT gst_meta_transform_debug
GST_DEBUG_CATEGORY_STATIC (gst_meta_transform_debug);

#define gst_meta_transform_parent_class parent_class
G_DEFINE_TYPE (GstMetaTransform, gst_meta_transform, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_MODULE_BACKEND 0
#define DEFAULT_PROP_MODULE_PARAMS  NULL

#define GST_META_TRANSFORM_SINK_CAPS \
    "video/x-raw(ANY)"

#define GST_META_TRANSFORM_SRC_CAPS \
    "video/x-raw(ANY)"

enum
{
  PROP_0,
  PROP_MODULE_BACKEND,
  PROP_MODULE_PARAMS,
};

static GstStaticPadTemplate gst_meta_transform_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_META_TRANSFORM_SINK_CAPS)
    );
static GstStaticPadTemplate gst_meta_transform_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_META_TRANSFORM_SRC_CAPS)
    );

static gboolean
gst_meta_transform_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMetaTransform *metatrans = GST_META_TRANSFORM (base);
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;

  if (DEFAULT_PROP_MODULE_BACKEND == metatrans->backend) {
    GST_ELEMENT_ERROR (metatrans, RESOURCE, NOT_FOUND, (NULL),
        ("Module name not set, automatic module pick up not supported!"));
    return FALSE;
  }

  eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_META_TRANSFORM_BACKEND));
  evalue = g_enum_get_value (eclass, metatrans->backend);

  gst_meta_transform_module_free (metatrans->module);
  metatrans->module = gst_meta_transform_module_new (evalue->value_name,
      metatrans->params);

  if (NULL == metatrans->module) {
    GST_ELEMENT_ERROR (metatrans, RESOURCE, FAILED, (NULL),
        ("Module creation failed!"));
    return FALSE;
  }

  GST_DEBUG_OBJECT (metatrans, "Output caps: %" GST_PTR_FORMAT, outcaps);
  return TRUE;
}

static GstFlowReturn
gst_meta_transform_transform_ip (GstBaseTransform * base, GstBuffer * buffer)
{
  GstMetaTransform *metatrans = GST_META_TRANSFORM (base);
  GstClockTime time = GST_CLOCK_TIME_NONE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  time = gst_util_get_timestamp ();

  if (!gst_meta_transform_module_process (metatrans->module, buffer)) {
    GST_ERROR_OBJECT (metatrans, "Failed to process buffer metas!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (metatrans, "Process took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_meta_transform_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMetaTransform *metatrans = GST_META_TRANSFORM (object);

  switch (prop_id) {
    case PROP_MODULE_BACKEND:
      metatrans->backend = g_value_get_enum (value);
      break;
    case PROP_MODULE_PARAMS:
    {
      GValue structure = G_VALUE_INIT;

      g_value_init (&structure, GST_TYPE_STRUCTURE);

      if (!gst_parse_string_property_value (value, &structure)) {
        GST_ERROR_OBJECT (metatrans, "Failed to parse constants!");
        break;
      }

      if (metatrans->params != NULL)
        gst_structure_free (metatrans->params);

      metatrans->params = GST_STRUCTURE (g_value_dup_boxed (&structure));
      g_value_unset (&structure);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_meta_transform_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMetaTransform *metatrans = GST_META_TRANSFORM (object);

  switch (prop_id) {
    case PROP_MODULE_BACKEND:
      g_value_set_enum (value, metatrans->backend);
      break;
    case PROP_MODULE_PARAMS:
    {
      gchar *string = NULL;

      if (metatrans->params != NULL)
        string = gst_structure_to_string (metatrans->params);

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
gst_meta_transform_finalize (GObject * object)
{
  GstMetaTransform *metatrans = GST_META_TRANSFORM (object);

  gst_meta_transform_module_free (metatrans->module);

  if (metatrans->params != NULL)
    gst_structure_free (metatrans->params);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (metatrans));
}

static void
gst_meta_transform_class_init (GstMetaTransformClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_meta_transform_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_meta_transform_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_meta_transform_finalize);

  g_object_class_install_property (gobject, PROP_MODULE_BACKEND,
      g_param_spec_enum ("module", "Module",
          "Module name that is going to be used for processing the buffer metas",
          GST_TYPE_META_TRANSFORM_BACKEND, DEFAULT_PROP_MODULE_BACKEND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_MODULE_PARAMS,
      g_param_spec_string ("module-params", "Module Parameters",
          "Parameters specific to the chosen module for processing/filtering/"
          "conversion of buffer metas. The format is in GstStructure string.",
          DEFAULT_PROP_MODULE_PARAMS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element, "Meta Transform",
      "Filter/Effect/Converter",
      "Performs filtering/processing on meta attached to buffers", "QTI");

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_meta_transform_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_meta_transform_src_template));

  base->set_caps = GST_DEBUG_FUNCPTR (gst_meta_transform_set_caps);
  base->transform_ip = GST_DEBUG_FUNCPTR (gst_meta_transform_transform_ip);
}

static void
gst_meta_transform_init (GstMetaTransform * metatrans)
{
  metatrans->backend = DEFAULT_PROP_MODULE_BACKEND;
  metatrans->params = DEFAULT_PROP_MODULE_PARAMS;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (metatrans), TRUE);
  // Set plugin to be always in-place.
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (metatrans), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_meta_transform_debug, "qtimetatransform", 0,
      "QTI meta transform plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimetatransform", GST_RANK_NONE,
      GST_TYPE_META_TRANSFORM);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimetatransform,
    "QTI meta transform plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
