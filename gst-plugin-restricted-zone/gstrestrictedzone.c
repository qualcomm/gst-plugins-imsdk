/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrestrictedzone.h"
#include "restricted-zone-engine.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT gst_restricted_zone_debug
GST_DEBUG_CATEGORY_STATIC (gst_restricted_zone_debug);

#define gst_restricted_zone_parent_class parent_class
G_DEFINE_TYPE (GstRestrictedZone, gst_restricted_zone, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_PROP_ZONE_CONFIG  NULL
#define DEFAULT_ENGINE              NULL

#define GST_RESTRICTED_ZONE_SINK_CAPS \
    "video/x-raw(ANY)"

#define GST_RESTRICTED_ZONE_SRC_CAPS \
    "video/x-raw(ANY)"

enum
{
  PROP_0,
  PROP_ZONE_CONFIG,
};

static GstStaticPadTemplate gst_restricted_zone_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_RESTRICTED_ZONE_SINK_CAPS)
    );
static GstStaticPadTemplate gst_restricted_zone_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_RESTRICTED_ZONE_SRC_CAPS)
    );

static gboolean
gst_restricted_zone_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstRestrictedZone *rz = GST_RESTRICTED_ZONE (base);

  gst_structure_set (rz->config, "caps", GST_TYPE_CAPS, incaps, NULL);

  if (rz->engine != NULL) {
    gst_restricted_zone_engine_free (rz->engine);
  }

  rz->engine = gst_restricted_zone_engine_new (rz->config);

  GST_DEBUG_OBJECT (rz, "Output caps: %" GST_PTR_FORMAT, outcaps);
  return TRUE;
}

static GstFlowReturn
gst_restricted_zone_transform_ip (GstBaseTransform * base, GstBuffer * buffer)
{
  GstRestrictedZone *rz = GST_RESTRICTED_ZONE (base);
  GstClockTime time = GST_CLOCK_TIME_NONE;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  time = gst_util_get_timestamp ();

  if (!gst_restricted_zone_engine_process (rz->engine, buffer)) {
    GST_ERROR_OBJECT (rz, "Failed to process buffer metas!");
    return GST_FLOW_ERROR;
  }

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (rz, "Process took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_restricted_zone_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRestrictedZone *rz = GST_RESTRICTED_ZONE (object);

  switch (prop_id) {
    case PROP_ZONE_CONFIG:
    {
      GValue structure = G_VALUE_INIT;

      g_value_init (&structure, GST_TYPE_STRUCTURE);

      if (!gst_parse_string_property_value (value, &structure)) {
        GST_ERROR_OBJECT (rz, "Failed to parse zone configuration!");
        break;
      }

      if (rz->config != NULL)
        gst_structure_free (rz->config);

      rz->config = GST_STRUCTURE (g_value_dup_boxed (&structure));
      g_value_unset (&structure);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_restricted_zone_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRestrictedZone *rz = GST_RESTRICTED_ZONE (object);

  switch (prop_id) {
    case PROP_ZONE_CONFIG:
    {
      gchar *string = NULL;

      if (rz->config != NULL)
        string = gst_structure_to_string (rz->config);

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
gst_restricted_zone_finalize (GObject * object)
{
  GstRestrictedZone *rz = GST_RESTRICTED_ZONE (object);

  gst_restricted_zone_engine_free (rz->engine);

  if (rz->config != NULL)
    gst_structure_free (rz->config);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (rz));
}

static void
gst_restricted_zone_class_init (GstRestrictedZoneClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_restricted_zone_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_restricted_zone_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_restricted_zone_finalize);

  g_object_class_install_property (gobject, PROP_ZONE_CONFIG,
      g_param_spec_string ("zone-config", "Restricted Zone config",
          "Restricted zone configuration"
          "The format is in GstStructure string. Example multiple Zones can be passed as"
          "zone-config=\"Zones,zone1=<<100,700>,<750,700>,<750,1000>,<550,1050>,<100,900>>,"
          "zone2=<<1200,700>,<1850,700>,<1850,1000>,<1350,1050>,<1200,900>>;\"",
          DEFAULT_PROP_ZONE_CONFIG, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element, "Restricted Zone Filter",
      "Filter/Effect/Converter",
      "Performs filtering/processing based on Restricted Zone config", "QTI");

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_restricted_zone_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_restricted_zone_src_template));

  base->set_caps = GST_DEBUG_FUNCPTR (gst_restricted_zone_set_caps);
  base->transform_ip = GST_DEBUG_FUNCPTR (gst_restricted_zone_transform_ip);
}

static void
gst_restricted_zone_init (GstRestrictedZone * rz)
{
  rz->engine = DEFAULT_ENGINE;
  rz->config = gst_structure_new_empty ("config");

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (rz), TRUE);
  // Set plugin to be always in-place.
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (rz), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_restricted_zone_debug, "qtirestrictedzonedbg", 0,
      "QTI Restricted Zone filter plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtirestrictedzonedbg", GST_RANK_NONE,
      GST_TYPE_RESTRICTED_ZONE);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtirestrictedzonedbg,
    "QTI Restricted Zone Filter plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
