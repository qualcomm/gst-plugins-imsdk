/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "offline-camera.h"

#include <gst/video/gstimagepool.h>
#include <gst/utils/common-utils.h>

#define GST_CAT_DEFAULT offline_camera_debug
GST_DEBUG_CATEGORY_STATIC (offline_camera_debug);

// Default property
#define DEFAULT_POOL_MIN_BUFFERS  2
#define DEFAULT_POOL_MAX_BUFFERS  24

#define DEFAULT_PROP_CAMERA_ID             0
#define DEFAULT_PROP_REQUEST_METADATA_PATH NULL
#define DEFAULT_PROP_REQUEST_METADATA_STEP 0
#define DEFAULT_PROP_EIS                   GST_OFFLINE_CAMERA_EIS_NONE

// Pad Template
#define GST_CAPS_FORMATS "{ NV12 }"
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"

// GType
#define GST_TYPE_OFFLINE_CAMERA_EIS (gst_offline_camera_eis_get_type())

G_DEFINE_TYPE (GstOfflineCamera, gst_offline_camera, GST_TYPE_BASE_TRANSFORM);

enum {
  PROP_0,
  PROP_CAMERA_ID,
  PROP_REQUEST_METADATA_PATH,
  PROP_REQUEST_METADATA_STEP,
  PROP_EIS,
  PROP_SESSION_METADATA,
};

static GstStaticPadTemplate gst_offline_camera_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES (
            GST_CAPS_FEATURE_MEMORY_GBM, GST_CAPS_FORMATS))
);

static GstStaticPadTemplate gst_offline_camera_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES (
            GST_CAPS_FEATURE_MEMORY_GBM, GST_CAPS_FORMATS))
);

static GType
gst_offline_camera_eis_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_OFFLINE_CAMERA_EIS_V2,
        "Electronic Image Stabilization with version 2",
        "eisv2"
    },
    { GST_OFFLINE_CAMERA_EIS_V3,
        "Electronic Image Stabilization with version 3",
        "eisv3"
    },
    { GST_OFFLINE_CAMERA_EIS_NONE,
        "None", "none"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstOfflineCameraEis", variants);

  return gtype;
}

static void
gst_offline_camera_event_callback (guint event, gpointer userdata)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (userdata);

  switch (event) {
    case EVENT_SERVICE_DIED:
      GST_ERROR_OBJECT (offcam, "Service has died!");
      break;
    case EVENT_CAMERA_ERROR:
      GST_ERROR_OBJECT (offcam, "Module encountered an un-recoverable error!");
      break;
    case EVENT_FRAME_ERROR:
      GST_WARNING_OBJECT (offcam, "Module has encountered frame drop!");
      break;
    case EVENT_METADATA_ERROR:
      GST_WARNING_OBJECT (offcam, "Module has encountered metadata drop error!");
      break;
    default:
      GST_WARNING_OBJECT (offcam, "Unknown module event.");
      break;
  }
}

static void
gst_offline_camera_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (object);
  GstState state = GST_STATE (offcam);
  const gchar *propname = g_param_spec_get_name (pspec);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  switch (prop_id) {
    case PROP_CAMERA_ID:
      gst_offline_camera_context_set_property (offcam->context,
          PARAM_CAMERA_ID, value);
      break;
    case PROP_REQUEST_METADATA_PATH:
      gst_offline_camera_context_set_property (offcam->context,
          PARAM_REQ_META_PATH, value);
      break;
    case PROP_REQUEST_METADATA_STEP:
      gst_offline_camera_context_set_property (offcam->context,
          PARAM_REQ_META_STEP, value);
      break;
    case PROP_EIS:
      gst_offline_camera_context_set_property (offcam->context,
          PARAM_EIS, value);
      break;
    case PROP_SESSION_METADATA:
      gst_offline_camera_context_set_property (offcam->context,
          PARAM_SESSION_METADATA, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_offline_camera_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (object);

  switch (prop_id) {
    case PROP_CAMERA_ID:
      gst_offline_camera_context_get_property (offcam->context,
          PARAM_CAMERA_ID, value);
      break;
    case PROP_REQUEST_METADATA_PATH:
      gst_offline_camera_context_get_property (offcam->context,
          PARAM_REQ_META_PATH, value);
      break;
    case PROP_REQUEST_METADATA_STEP:
      gst_offline_camera_context_get_property (offcam->context,
          PARAM_REQ_META_STEP, value);
      break;
    case PROP_EIS:
      gst_offline_camera_context_get_property (offcam->context,
          PARAM_EIS, value);
      break;
    case PROP_SESSION_METADATA:
      gst_offline_camera_context_get_property (offcam->context,
          PARAM_SESSION_METADATA, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_offline_camera_finalize (GObject * object)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (object);

  if (!gst_offline_camera_context_disconnect (offcam->context)) {
    GST_ERROR_OBJECT (offcam, "Failed to disconnect.");
  }

  if (offcam->pool) {
    gst_buffer_pool_set_active (offcam->pool, FALSE);
    gst_object_unref (offcam->pool);
    offcam->pool = NULL;
    GST_DEBUG_OBJECT (offcam, "Destoried buffer pool.");
  }

  if (offcam->context) {
    gst_offline_camera_context_free (offcam->context);
    offcam->context = NULL;
  }

  G_OBJECT_CLASS (gst_offline_camera_parent_class)->finalize (object);
}

static void
gst_offline_camera_data_callback (GstBuffer *buffer, gpointer userdata)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (userdata);

  gst_pad_push (GST_BASE_TRANSFORM(offcam)->srcpad, buffer);

  GST_LOG_OBJECT (offcam, "Callback called. GstBuffer(%p) pushed.", buffer);
}

static gboolean
gst_offline_camera_stop (GstBaseTransform *base)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (base);

  GST_DEBUG_OBJECT (offcam, "Destroying offline camera module session.");

  if (!gst_offline_camera_context_destroy (offcam->context)) {
    GST_DEBUG ("Failed to destroy offline camera module session.");
    return FALSE;
  }

  GST_DEBUG_OBJECT (offcam, "Destroyed offline camera module session.");

  return TRUE;
}

static GstFlowReturn
gst_offline_camera_transform (GstBaseTransform *base, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (base);

  // Handle gap buffer
  if (gst_buffer_get_size (outbuf) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuf, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  GST_LOG_OBJECT (offcam, "Sending request(inbuf: %p, outbuf: %p) to process.",
      inbuf, outbuf);

  // Avoid unref buffers in chain function of basetransform
  gst_buffer_ref (inbuf);
  gst_buffer_ref (outbuf);

  if (!gst_offline_camera_context_process (offcam->context, inbuf,
      outbuf)) {
    GST_ERROR_OBJECT (offcam, "Failed to send request to process.");
    gst_buffer_unref (inbuf);
    gst_buffer_unref (outbuf);
    return GST_FLOW_EOS;
  }

  GST_LOG_OBJECT (offcam, "Sent request(inbuf: %p, outbuf: %p) to process.",
      inbuf, outbuf);

  // GST_FLOW_CUSTOM_SUCCESS_1 will not invoke gst_pad_push
  return GST_FLOW_CUSTOM_SUCCESS_1;
}

static gboolean
gst_offline_camera_set_caps (GstBaseTransform *base, GstCaps *incaps,
    GstCaps *outcaps)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (base);
  GstStructure *in = NULL, *out = NULL;
  GstOfflineCameraBufferParams params[2] = {0};

  g_return_val_if_fail (gst_caps_is_fixed (incaps), FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (outcaps), FALSE);

  GST_INFO_OBJECT (offcam, "InputCaps: %" GST_PTR_FORMAT, incaps);
  GST_INFO_OBJECT (offcam, "OutputCaps: %" GST_PTR_FORMAT, outcaps);

  // Input BufferParams
  in = gst_caps_get_structure (incaps, 0);
  gst_structure_get_int (in, "width", &params[0].width);
  gst_structure_get_int (in, "height", &params[0].height);
  params[0].format = gst_video_format_from_string (
      gst_structure_get_string (in, "format"));

  // Output BufferParams
  out = gst_caps_get_structure (outcaps, 0);
  gst_structure_get_int (out, "width", &params[1].width);
  gst_structure_get_int (out, "height", &params[1].height);
  params[1].format = gst_video_format_from_string (
      gst_structure_get_string (out, "format"));

  GST_DEBUG_OBJECT (offcam, "Creating offline camera module.");

  if (!gst_offline_camera_context_create (offcam->context, params,
      gst_offline_camera_data_callback)) {
    GST_ERROR_OBJECT (offcam, "Failed to configure offline camera module.");
    return FALSE;
  }

  GST_DEBUG_OBJECT (offcam, "Created offline camera module.");

  return TRUE;
}

static GstBufferPool*
gst_offline_camera_create_buffer_pool (GstOfflineCamera *offcam, GstCaps *caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (offcam, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  // If downstream allocation query supports GBM, allocate gbm memory.
  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
    GST_INFO_OBJECT (offcam, "Buffer pool uses GBM memory");
  } else {
    pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
    GST_INFO_OBJECT (offcam, "Buffer pool uses ION memory");
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_POOL_MIN_BUFFERS, DEFAULT_POOL_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (offcam, "Failed to set pool configuration!");
    gst_object_unref (pool);
    pool = NULL;
  }

  gst_object_unref (allocator);

  return pool;
}

static gboolean
gst_offline_camera_decide_allocation (GstBaseTransform *base, GstQuery *query)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (base);
  GstCaps *caps = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  guint size, minbuffers, maxbuffers;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (offcam, "Failed to parse caps in allocation query.");
    return FALSE;
  }

  // Old buffer pool will remain in case there is other caps negotiation
  if (offcam->pool) {
    gst_buffer_pool_set_active (offcam->pool, FALSE);
    gst_object_unref (offcam->pool);
    offcam->pool = NULL;
    GST_DEBUG_OBJECT (offcam, "Destoried old buffer pool.");
  }

  // Allocate a new buffer pool and config
  offcam->pool = gst_offline_camera_create_buffer_pool (offcam, caps);
  if (!offcam->pool) {
    GST_ERROR_OBJECT (offcam, "Failed to create buffer pool.");
    return FALSE;
  }

  // Set parameters of buffer pool in query
  config = gst_buffer_pool_get_config (offcam->pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (config);

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, offcam->pool, size,
        minbuffers, maxbuffers);
  else
    gst_query_add_allocation_pool (query, offcam->pool, size, minbuffers,
        maxbuffers);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_offline_camera_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer *inbuf, GstBuffer **outbuf)
{
  GstOfflineCamera *offcam = GST_OFFLINE_CAMERA (trans);

  g_return_val_if_fail (offcam->pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (offcam->pool) &&
      !gst_buffer_pool_set_active (offcam->pool, TRUE)) {
    GST_ERROR_OBJECT (offcam, "Failed to activate output video buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Handle gap buffer
  if (gst_buffer_get_size (inbuf) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
    GST_DEBUG_OBJECT (offcam, "Get gap buffer.");
    *outbuf = gst_buffer_new ();
  }

  if ((*outbuf == NULL) && gst_buffer_pool_acquire_buffer (offcam->pool,
      outbuf, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (offcam, "Failed to create output video buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuf, inbuf,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static void
gst_offline_camera_class_init (GstOfflineCameraClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_offline_camera_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_offline_camera_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_offline_camera_finalize);

  gst_element_class_set_static_metadata (
      element, "Offline Camera", "Filter/Converter",
      "Process images via IPE of camera module", "QTI");

  g_object_class_install_property (gobject, PROP_CAMERA_ID,
      g_param_spec_uint ("camera-id", "Camera ID",
          "Camera ID", 0, G_MAXINT8, DEFAULT_PROP_CAMERA_ID,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));
  g_object_class_install_property (gobject, PROP_REQUEST_METADATA_PATH,
      g_param_spec_string ("request-meta-path", "Request Metadata Path",
          "Absolute path of request metadata to read by camera hal.",
          DEFAULT_PROP_REQUEST_METADATA_PATH,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject, PROP_REQUEST_METADATA_STEP,
      g_param_spec_uint ("request-meta-step", "Request Metadata Step",
          "Step to read request metadata by camera hal.",
          0, G_MAXUINT16, DEFAULT_PROP_REQUEST_METADATA_STEP,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));
  g_object_class_install_property (gobject, PROP_EIS,
      g_param_spec_enum ("eis", "EIS",
          "Electronic Image Stabilization to reduce the effects of camera shake",
          GST_TYPE_OFFLINE_CAMERA_EIS, DEFAULT_PROP_EIS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));
  g_object_class_install_property (gobject, PROP_SESSION_METADATA,
      g_param_spec_pointer ("session-metadata", "Session Metadata",
          "Settings metadata used for creating offline camera session",
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));

  gst_element_class_add_static_pad_template (element,
      &gst_offline_camera_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_offline_camera_src_pad_template);

  // GstBufferPool
  base->decide_allocation = GST_DEBUG_FUNCPTR (gst_offline_camera_decide_allocation);

  // main API
  base->stop = GST_DEBUG_FUNCPTR (gst_offline_camera_stop);
  base->transform = GST_DEBUG_FUNCPTR (gst_offline_camera_transform);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_offline_camera_prepare_output_buffer);

  // Caps negotiation
  base->set_caps = GST_DEBUG_FUNCPTR (gst_offline_camera_set_caps);

  // FIXME: Event query, may need to overwrite for GST_QUERY_POSITION

  GST_DEBUG_CATEGORY_INIT (offline_camera_debug, "qtiofflinecamera", 0,
      "QTI Offline Camera");
}

static void
gst_offline_camera_init (GstOfflineCamera *offcam)
{
  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (offcam), FALSE);

  offcam->context = gst_offline_camera_context_new ();
  g_return_if_fail (offcam->context != NULL);

  if (!gst_offline_camera_context_connect (offcam->context,
      gst_offline_camera_event_callback, offcam)) {
    GST_ERROR_OBJECT (offcam, "Failed to connect.");
    gst_offline_camera_context_free (offcam->context);
    return;
  }

  GST_INFO_OBJECT (offcam, "Offline camera plugin instance init.");
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "qtiofflinecamera", GST_RANK_PRIMARY,
      GST_TYPE_OFFLINE_CAMERA);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtiofflinecamera,
    "Process images via offline camera",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
