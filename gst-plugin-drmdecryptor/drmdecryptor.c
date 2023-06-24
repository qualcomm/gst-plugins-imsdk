/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "drmdecryptor.h"

#include <gst/memory/gstmempool.h>

#define GST_CAT_DEFAULT decryptor_debug
GST_DEBUG_CATEGORY_STATIC (decryptor_debug);

#define gst_drm_decryptor_parent_class parent_class
G_DEFINE_TYPE (GstDrmDecryptor, gst_drm_decryptor, GST_TYPE_ELEMENT);

#define PLAYREADY_SYSTEM_ID       "9a04f079-9840-4286-ab92-e65be0885f95"

#define DEFAULT_PROP_SESSION_ID   NULL
// TODO: Check with SSG team to have a common macro or fetch the buffer size
// requirements from prdrmengine lib
#define DEFAULT_BUFFER_SIZE       (1024*1024*2)
#define DEFAULT_MIN_BUFFERS       2
#define DEFAULT_MAX_BUFFERS       10

static GstStaticPadTemplate gst_drm_decryptor_sink_pad_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("application/x-cenc, "
      "protection-system = (string) "PLAYREADY_SYSTEM_ID)
);

static GstStaticPadTemplate gst_drm_decryptor_src_pad_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("video/x-h264;"
      "video/x-h265;"
      "video/x-vp8;"
      "video/x-vp9")
);

enum {
  PROP_0,
  PROP_SESSION_ID
};

static gboolean
gst_drm_decryptor_update_srccaps (GstDrmDecryptor *decryptor, GstCaps *caps)
{
  GstStructure *structure;
  GstCaps *src_caps, *updated_caps;
  const gchar *media_type;

  GST_INFO_OBJECT (decryptor, "Sink caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  media_type = gst_structure_get_string (structure, "original-media-type");
  if (!media_type) {
    GST_ERROR_OBJECT (decryptor, "Original media type not found !");
    return FALSE;
  }

  structure = gst_structure_copy (structure);
  gst_structure_set_name (structure, media_type);
  gst_structure_remove_fields (structure, "original-media-type",
      "protection-system",
      NULL);

  src_caps = gst_pad_get_pad_template_caps (decryptor->srcpad);

  updated_caps = gst_caps_new_empty();
  gst_caps_append_structure (updated_caps, structure);

  if (gst_caps_can_intersect (updated_caps, src_caps)) {
    gst_pad_set_caps (decryptor->srcpad, updated_caps);
    GST_INFO_OBJECT (decryptor, "Src caps: %" GST_PTR_FORMAT, updated_caps);
  } else {
    GST_ERROR_OBJECT (decryptor, "No intersection between new caps and allowed caps");
    gst_caps_unref (updated_caps);
    gst_caps_unref (src_caps);
    return FALSE;
  }

  gst_caps_unref (updated_caps);
  gst_caps_unref (src_caps);

  return TRUE;
}

static GstBufferPool*
gst_drm_decryptor_create_pool (GstDrmDecryptor *decryptor)
{
  GstStructure *config = NULL;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;

  if (!(pool = gst_mem_buffer_pool_new (GST_MEMORY_BUFFER_POOL_TYPE_SECURE))) {
    GST_ERROR_OBJECT (decryptor, "Failed to create new buffer pool !");
    return NULL;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, NULL, DEFAULT_BUFFER_SIZE,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (!(allocator = gst_fd_allocator_new ())) {
    GST_ERROR_OBJECT (decryptor, "Failed to create fd allocator !");
    g_clear_object (&pool);
    return NULL;
  }
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (decryptor, "Failed to set pool configuration !");
    g_clear_object (&pool);
  }

  g_object_unref (allocator);
  return pool;
}


static GstFlowReturn
gst_drm_decryptor_sinkpad_chain (GstPad *pad, GstObject *parent, GstBuffer *in_buffer)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (parent);
  GstBuffer *out_buffer = NULL;

  GstProtectionMeta *pmeta = gst_buffer_get_protection_meta (in_buffer);
  if (pmeta == NULL) {
    GST_ERROR_OBJECT (decryptor, "No protection metadata in buffer !");
    return GST_FLOW_ERROR;
  }

  if (gst_buffer_pool_acquire_buffer (
      decryptor->pool, &out_buffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (decryptor, "Failed to acquire secure buffer from pool!");
    return GST_FLOW_ERROR;
  }

  if (!gst_drm_decryptor_engine_execute (decryptor->engine, in_buffer, out_buffer)) {
    GST_ERROR_OBJECT (decryptor, "Decryption failed !");
    gst_buffer_unref (out_buffer);
    gst_buffer_unref (in_buffer);
    return GST_FLOW_OK;
  }

  gst_buffer_copy_into (out_buffer, in_buffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return gst_pad_push (decryptor->srcpad, out_buffer);
}

static gboolean
gst_drm_decryptor_sinkpad_event (GstPad *pad, GstObject *parent, GstEvent *event)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (parent);
  gboolean success = FALSE;

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      success = gst_drm_decryptor_update_srccaps (decryptor, caps);
      gst_event_unref (event);

      if (success && !decryptor->pool &&
          !(decryptor->pool = gst_drm_decryptor_create_pool (decryptor))) {
        GST_ERROR_OBJECT (decryptor, "Failed to create buffer pool!");
        return FALSE;
      }

      if (success && !gst_buffer_pool_is_active (decryptor->pool) &&
          !gst_buffer_pool_set_active (decryptor->pool, TRUE)) {
        GST_ERROR_OBJECT (decryptor, "Failed to activate buffer pool!");
        return FALSE;
      }
      break;
    }
    default:
      success = gst_pad_event_default (pad, parent, event);
      break;
  }

  return success;
}

static void
gst_drm_decryptor_set_property (GObject *gobject, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (gobject);

  switch (prop_id) {
    case PROP_SESSION_ID:
      g_free (decryptor->session_id);
      decryptor->session_id = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
  }
}

static void
gst_drm_decryptor_get_property (GObject *gobject, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (gobject);

  switch (prop_id) {
    case PROP_SESSION_ID:
      g_value_set_string (value, decryptor->session_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_drm_decryptor_change_state (GstElement *element, GstStateChange transition)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      decryptor->engine = gst_drm_decryptor_engine_new (decryptor->session_id);
      if (decryptor->engine == NULL) {
        GST_ERROR_OBJECT (decryptor, "Decryptor engine initialization failed!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (decryptor->engine != NULL) {
        gst_drm_decryptor_engine_free (decryptor->engine);
        decryptor->engine = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_drm_decryptor_finalize (GObject *object)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (object);

  g_free (decryptor->session_id);

  if (decryptor->engine != NULL)
    gst_drm_decryptor_engine_free (decryptor->engine);

  if (decryptor->pool)
    gst_object_unref (decryptor->pool);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (decryptor));
}

static void
gst_drm_decryptor_init (GstDrmDecryptor *decryptor)
{
  decryptor->engine = NULL;
  decryptor->session_id = DEFAULT_PROP_SESSION_ID;
  decryptor->pool = NULL;

  decryptor->sinkpad = gst_pad_new_from_static_template (
      &gst_drm_decryptor_sink_pad_template, "sink");

  decryptor->srcpad = gst_pad_new_from_static_template (
      &gst_drm_decryptor_src_pad_template, "src");

  gst_pad_set_chain_function (decryptor->sinkpad,
      GST_DEBUG_FUNCPTR (gst_drm_decryptor_sinkpad_chain));
  gst_pad_set_event_function (decryptor->sinkpad,
      GST_DEBUG_FUNCPTR (gst_drm_decryptor_sinkpad_event));

  gst_element_add_pad (GST_ELEMENT (decryptor), decryptor->sinkpad);
  gst_element_add_pad (GST_ELEMENT (decryptor), decryptor->srcpad);
}

static void
gst_drm_decryptor_class_init (GstDrmDecryptorClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_drm_decryptor_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_drm_decryptor_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_drm_decryptor_finalize);

  gst_element_class_add_static_pad_template (element,
      &gst_drm_decryptor_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_drm_decryptor_src_pad_template);

  g_object_class_install_property (gobject, PROP_SESSION_ID,
      g_param_spec_string ("session-id", "Session ID",
          "Session id that is generated upon PR DRM plugin open session",
          DEFAULT_PROP_SESSION_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element->change_state = GST_DEBUG_FUNCPTR (gst_drm_decryptor_change_state);

  gst_element_class_set_static_metadata (element,
      "QTI DRM Decryptor Plugin", GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Uses Playready DRM APIs to decrypt CENC scheme protected content",
      "QTI");

  GST_DEBUG_CATEGORY_INIT (decryptor_debug, "qtidrmdecryptor", 0,
      "QTI DRM Decryptor Plugin");
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "qtidrmdecryptor", GST_RANK_PRIMARY,
      GST_TYPE_DRM_DECRYPTOR);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtidrmdecryptor,
    "QTI DRM Decryptor Plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
