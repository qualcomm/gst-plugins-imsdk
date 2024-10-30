/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mlmetaparser.h"

#include <gst/utils/common-utils.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

#define GST_TYPE_PARSER_MODULES (gst_ml_meta_parser_modules_get_type())

#define GST_CAT_DEFAULT gst_ml_meta_parser_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_meta_parser_debug);

#define gst_ml_meta_parser_parent_class parent_class
G_DEFINE_TYPE (GstMlMetaParser, gst_ml_meta_parser, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_MIN_BUFFERS      2
#define DEFAULT_MAX_BUFFERS      10
#define DEFAULT_TEXT_BUFFER_SIZE 24576

#define DEFAULT_PROP_MODULE 0

#define GST_ML_META_PARSER_SINK_CAPS \
    "video/x-raw(ANY); " \
    "text/x-raw, format = (string) utf8"

#define GST_ML_META_PARSER_SRC_CAPS \
    "text/x-raw, format = (string) utf8"

enum
{
  PROP_0,
  PROP_MODULE
};

static GstStaticPadTemplate gst_ml_meta_parser_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_ML_META_PARSER_SINK_CAPS)
    );
static GstStaticPadTemplate gst_ml_meta_parser_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_ML_META_PARSER_SRC_CAPS)
    );

static GType
gst_ml_meta_parser_modules_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue *variants = NULL;

  if (gtype)
    return gtype;

  variants = gst_parser_enumarate_modules ("ml-meta-parser-");
  gtype = g_enum_register_static ("GstMLParserModules", variants);

  return gtype;
}

static GstFlowReturn
gst_ml_meta_parser_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMlMetaParser *mlmetaparser = GST_ML_META_PARSER (base);

  if (gst_base_transform_is_passthrough (base)) {
    GST_DEBUG_OBJECT (mlmetaparser, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  *outbuffer = gst_buffer_new ();

  // Copy the timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstCaps *
gst_ml_meta_parser_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMlMetaParser *mlmetaparser = GST_ML_META_PARSER (base);
  GstCaps *result = NULL;

  GST_DEBUG_OBJECT (mlmetaparser, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (mlmetaparser, "Filter caps: %" GST_PTR_FORMAT, filter);

  // The source and sink pads caps do not depend on each other so directly
  // take the template caps for the corresponding pad and apply filter.
  if (direction == GST_PAD_SINK)
    result = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SRC_PAD (base));
  else if (direction == GST_PAD_SRC)
    result = gst_pad_get_pad_template_caps (GST_BASE_TRANSFORM_SINK_PAD (base));

  GST_DEBUG_OBJECT (mlmetaparser, "Template caps: %" GST_PTR_FORMAT, result);

  if (filter) {
    GstCaps *intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (mlmetaparser, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static gboolean
gst_ml_meta_parser_accept_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  GstMlMetaParser *mlmetaparser = GST_ML_META_PARSER (base);
  GstCaps *tmplcaps = NULL;
  gboolean success = FALSE;

  GST_DEBUG_OBJECT (mlmetaparser, "Accept caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK)
    tmplcaps = gst_pad_get_pad_template_caps (
        GST_BASE_TRANSFORM_SINK_PAD (base));
  else if (direction == GST_PAD_SRC)
    tmplcaps = gst_pad_get_pad_template_caps (
        GST_BASE_TRANSFORM_SRC_PAD (base));

  GST_DEBUG_OBJECT (mlmetaparser, "Template caps: %" GST_PTR_FORMAT, tmplcaps);

  success = gst_caps_can_intersect (caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  if (!success)
    GST_WARNING_OBJECT (base, "Caps can't intersect!");

  return success;
}

static gboolean
gst_ml_meta_parser_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMlMetaParser *mlmetaparser = GST_ML_META_PARSER (base);
  GEnumClass *eclass = NULL;
  GEnumValue *evalue = NULL;
  GstStructure *structure = NULL;
  gboolean success = FALSE;
  GstDataType data_type = GST_DATA_TYPE_NONE;

  // TODO Could be used for some initialization of a core component.
  // If not used should be removed.
  GST_DEBUG_OBJECT (mlmetaparser, "Output caps: %" GST_PTR_FORMAT, outcaps);

  eclass = G_ENUM_CLASS (g_type_class_peek (GST_TYPE_PARSER_MODULES));
  evalue = g_enum_get_value (eclass, mlmetaparser->mdlenum);

  gst_parser_module_free (mlmetaparser->module);
  mlmetaparser->module = gst_parser_module_new (evalue->value_name);

  if (!gst_parser_module_init (mlmetaparser->module)) {
    GST_ELEMENT_ERROR (mlmetaparser, RESOURCE, FAILED, (NULL),
        ("Module initialization failed!"));

    return FALSE;
  }

  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_has_name (structure, "text/x-raw")) {
    data_type = GST_DATA_TYPE_TEXT;
  } else if (gst_structure_has_name (structure, "video/x-raw")) {
    data_type = GST_DATA_TYPE_VIDEO;
  } else {
    GST_ELEMENT_ERROR (mlmetaparser, RESOURCE, FAILED, (NULL),
        ("Unsupported data type!"));
    return FALSE;
  }

  structure = gst_structure_new ("options",
      GST_PARSER_MODULE_OPT_DATA_TYPE, G_TYPE_ENUM, data_type,
      NULL);

  success = gst_parser_module_set_opts (mlmetaparser->module, structure);
  gst_structure_free (structure);

  if (!success) {
    GST_ELEMENT_ERROR (mlmetaparser, RESOURCE, FAILED, (NULL),
        ("Failed to set module options!"));
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_ml_meta_parser_transform (GstBaseTransform * base, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstMlMetaParser *mlmetaparser = GST_ML_META_PARSER (base);
  GstMemory *mem = NULL;
  gchar *output_string = NULL;
  gsize output_size = 0;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  time = gst_util_get_timestamp ();

  success = gst_parser_module_execute (mlmetaparser->module, inbuffer,
      &output_string);
  if (!success) {
    GST_ERROR_OBJECT(mlmetaparser, "Failed to parse metadata, sending empty buffer!");
    return GST_FLOW_ERROR;
  }

  output_size = strlen (output_string) + 1;
  mem = gst_memory_new_wrapped (
      GST_MEMORY_FLAG_ZERO_PADDED & GST_MEMORY_FLAG_ZERO_PREFIXED,
      output_string, output_size, 0, output_size, output_string, g_free);
  gst_buffer_append_memory (outbuffer, mem);

  GST_TRACE_OBJECT (mlmetaparser, "module output: %s", output_string);
  GST_LOG_OBJECT (mlmetaparser, "output buffer size %zu", output_size);

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (mlmetaparser, "Execute took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_meta_parser_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMlMetaParser *mlmetaparser = GST_ML_META_PARSER (object);

  switch (prop_id) {
    case PROP_MODULE:
      mlmetaparser->mdlenum = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_meta_parser_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMlMetaParser *mlmetaparser = GST_ML_META_PARSER (object);

  switch (prop_id) {
    case PROP_MODULE:
      g_value_set_enum (value, mlmetaparser->mdlenum);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_meta_parser_finalize (GObject * object)
{
  GstMlMetaParser *mlmetaparser = GST_ML_META_PARSER (object);
  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (mlmetaparser));
}

static void
gst_ml_meta_parser_class_init (GstMlMetaParserClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_ml_meta_parser_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_ml_meta_parser_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_ml_meta_parser_finalize);

  g_object_class_install_property (gobject, PROP_MODULE,
      g_param_spec_enum ("module", "Module",
          "Module name that is going to be used for parsing metadata",
          GST_TYPE_PARSER_MODULES, DEFAULT_PROP_MODULE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Meta parser", "Filter/Effect/Converter", "Mate parsing plugin", "QTI");

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_ml_meta_parser_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_ml_meta_parser_src_template));

  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_meta_parser_prepare_output_buffer);

  base->transform_caps = GST_DEBUG_FUNCPTR (gst_ml_meta_parser_transform_caps);
  base->accept_caps = GST_DEBUG_FUNCPTR (gst_ml_meta_parser_accept_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_meta_parser_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_meta_parser_transform);
}

static void
gst_ml_meta_parser_init (GstMlMetaParser * mlmetaparser)
{
  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (mlmetaparser), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_ml_meta_parser_debug, "qtimlmetaparser", 0,
      "QTI ML meta parser plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlmetaparser", GST_RANK_NONE,
      GST_TYPE_ML_META_PARSER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlmetaparser,
    "QTI ML meta parsing plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
