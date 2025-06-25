/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "camera-reprocess.h"

#include <gst/video/gstimagepool.h>
#include <gst/utils/common-utils.h>
#include <gst/video/video-utils.h>

#define GST_CAT_DEFAULT camera_reprocess_debug
GST_DEBUG_CATEGORY_STATIC (camera_reprocess_debug);

// Default property
#define DEFAULT_POOL_MIN_BUFFERS  2
#define DEFAULT_POOL_MAX_BUFFERS  24

#define DEFAULT_PROP_CAMERA_ID             0
#define DEFAULT_PROP_REQUEST_METADATA_PATH NULL
#define DEFAULT_PROP_REQUEST_METADATA_STEP 0
#define DEFAULT_PROP_EIS                   GST_CAMERA_REPROCESS_EIS_NONE

// Pad Template
#define GST_CAPS_FORMATS "{ NV12, NV12_Q08C, P010_10LE }"

// GType
#define GST_TYPE_CAMERA_REPROCESS_EIS (gst_camera_reprocess_eis_get_type())

G_DEFINE_TYPE (GstCameraReprocess, gst_camera_reprocess, GST_TYPE_BASE_TRANSFORM);

enum {
  PROP_0,
  PROP_CAMERA_ID,
  PROP_REQUEST_METADATA_PATH,
  PROP_REQUEST_METADATA_STEP,
  PROP_EIS,
  PROP_SESSION_METADATA,
};

static GstStaticPadTemplate gst_camera_reprocess_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES (
            GST_CAPS_FEATURE_MEMORY_GBM, GST_CAPS_FORMATS))
);

static GstStaticPadTemplate gst_camera_reprocess_src_pad_template =
    GST_STATIC_PAD_TEMPLATE ("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES (
            GST_CAPS_FEATURE_MEMORY_GBM, GST_CAPS_FORMATS))
);

static GType
gst_camera_reprocess_eis_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_CAMERA_REPROCESS_EIS_V3,
      "Eis with version 3 which will consume future frames",
      "v3"
    },
    { GST_CAMERA_REPROCESS_EIS_V2,
        "Eis with version 2 which will consume previous frames",
        "v2"
    },
    { GST_CAMERA_REPROCESS_EIS_NONE,
        "None", "none"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstCameraReprocessEis", variants);

  return gtype;
}

static void
gst_camera_reprocess_event_callback (guint event, gpointer userdata)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (userdata);

  switch (event) {
    case EVENT_SERVICE_DIED:
      GST_ERROR_OBJECT (camreproc, "Service has died!");
      break;
    case EVENT_CAMERA_ERROR:
      GST_ERROR_OBJECT (camreproc, "Encountered an un-recoverable error!");
      break;
    case EVENT_FRAME_ERROR:
      GST_WARNING_OBJECT (camreproc, "Encountered frame drop!");
      break;
    case EVENT_METADATA_ERROR:
      GST_WARNING_OBJECT (camreproc, "Encountered metadata drop error!");
      break;
    default:
      GST_WARNING_OBJECT (camreproc, "Unknown module event.");
      break;
  }
}

static void
gst_camera_reprocess_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (object);
  GstState state = GST_STATE (camreproc);
  const gchar *propname = g_param_spec_get_name (pspec);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  switch (prop_id) {
    case PROP_CAMERA_ID:
      gst_camera_reprocess_context_set_property (camreproc->context,
          PARAM_CAMERA_ID, value);
      break;
    case PROP_REQUEST_METADATA_PATH:
      gst_camera_reprocess_context_set_property (camreproc->context,
          PARAM_REQ_META_PATH, value);
      break;
    case PROP_REQUEST_METADATA_STEP:
      gst_camera_reprocess_context_set_property (camreproc->context,
          PARAM_REQ_META_STEP, value);
      break;
    case PROP_EIS:
      gst_camera_reprocess_context_set_property (camreproc->context,
          PARAM_EIS, value);
      break;
    case PROP_SESSION_METADATA:
      gst_camera_reprocess_context_set_property (camreproc->context,
          PARAM_SESSION_METADATA, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_camera_reprocess_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (object);

  switch (prop_id) {
    case PROP_CAMERA_ID:
      gst_camera_reprocess_context_get_property (camreproc->context,
          PARAM_CAMERA_ID, value);
      break;
    case PROP_REQUEST_METADATA_PATH:
      gst_camera_reprocess_context_get_property (camreproc->context,
          PARAM_REQ_META_PATH, value);
      break;
    case PROP_REQUEST_METADATA_STEP:
      gst_camera_reprocess_context_get_property (camreproc->context,
          PARAM_REQ_META_STEP, value);
      break;
    case PROP_EIS:
      gst_camera_reprocess_context_get_property (camreproc->context,
          PARAM_EIS, value);
      break;
    case PROP_SESSION_METADATA:
      gst_camera_reprocess_context_get_property (camreproc->context,
          PARAM_SESSION_METADATA, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_camera_reprocess_finalize (GObject * object)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (object);

  if (!gst_camera_reprocess_context_disconnect (camreproc->context)) {
    GST_ERROR_OBJECT (camreproc, "Failed to disconnect.");
  }

  // Check if bufferpool has been destroied
  if (camreproc->pool) {
    gst_buffer_pool_set_active (camreproc->pool, FALSE);
    gst_object_unref (camreproc->pool);
    camreproc->pool = NULL;
    GST_WARNING_OBJECT (camreproc, "Destroied buffer pool.");
  }

  if (camreproc->context) {
    gst_camera_reprocess_context_free (camreproc->context);
    camreproc->context = NULL;
  }

  G_OBJECT_CLASS (gst_camera_reprocess_parent_class)->finalize (object);
}

static void
gst_camera_reprocess_data_callback (gpointer *array, gpointer userdata)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (userdata);
  GPtrArray *ptr_array = (GPtrArray *)array;
  GstBuffer *outbuf = NULL, *inbuf = NULL;

  inbuf = (GstBuffer *) g_ptr_array_index (ptr_array, 0);
  gst_buffer_unref (inbuf);

  outbuf = (GstBuffer *) g_ptr_array_index (ptr_array, 1);
  gst_pad_push (GST_BASE_TRANSFORM (camreproc)->srcpad, outbuf);

  GST_LOG_OBJECT (camreproc, "Callback called. GstBuffer(%p) pushed.", outbuf);
}

static gboolean
gst_camera_reprocess_stop (GstBaseTransform *base)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (base);

  GST_DEBUG_OBJECT (camreproc, "Destroying camera reprocess module session.");

  if (!gst_camera_reprocess_context_destroy (camreproc->context)) {
    GST_DEBUG ("Failed to destroy camera reprocess module session.");
    return FALSE;
  }

  if (camreproc->pool) {
    gst_buffer_pool_set_active (camreproc->pool, FALSE);
    gst_object_unref (camreproc->pool);
    camreproc->pool = NULL;
    GST_DEBUG_OBJECT (camreproc, "Destroied buffer pool.");
  }

  GST_DEBUG_OBJECT (camreproc, "Destroyed camera reprocess module session.");

  return TRUE;
}

static GstFlowReturn
gst_camera_reprocess_transform (GstBaseTransform *base, GstBuffer *inbuf,
    GstBuffer *outbuf)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (base);
  GstClockTime position = GST_CLOCK_TIME_NONE;
  GstClockTime timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  GstClockTime duration = GST_BUFFER_DURATION (inbuf);

  // Update segment for postion query
  if (timestamp != GST_CLOCK_TIME_NONE) {
    if (duration != GST_CLOCK_TIME_NONE)
      position = timestamp + duration;
    else
      position = timestamp;
  }
  if (position != GST_CLOCK_TIME_NONE && base->segment.format == GST_FORMAT_TIME)
    base->segment.position = position;

  // Handle gap buffer
  if (gst_buffer_get_size (outbuf) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuf, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  GST_LOG_OBJECT (camreproc, "Sending request(inbuf: %p, outbuf: %p) to process.",
      inbuf, outbuf);

  // Avoid unref buffers in chain function of basetransform
  gst_buffer_ref (inbuf);
  gst_buffer_ref (outbuf);

  if (!gst_camera_reprocess_context_process (camreproc->context, inbuf, outbuf)) {
    GST_ERROR_OBJECT (camreproc, "Failed to send request to process.");
    gst_buffer_unref (inbuf);
    gst_buffer_unref (outbuf);
    return GST_FLOW_EOS;
  }

  GST_LOG_OBJECT (camreproc, "Sent request(inbuf: %p, outbuf: %p) to process.",
      inbuf, outbuf);

  // GST_FLOW_CUSTOM_SUCCESS_1 will not invoke gst_pad_push
  return GST_FLOW_CUSTOM_SUCCESS_1;
}

static gboolean
gst_camera_reprocess_set_caps (GstBaseTransform *base, GstCaps *incaps,
    GstCaps *outcaps)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (base);
  GstStructure *in = NULL, *out = NULL;
  GstCameraReprocessBufferParams params[2] = {0};

  g_return_val_if_fail (gst_caps_is_fixed (incaps), FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (outcaps), FALSE);

  GST_INFO_OBJECT (camreproc, "InputCaps: %" GST_PTR_FORMAT, incaps);
  GST_INFO_OBJECT (camreproc, "OutputCaps: %" GST_PTR_FORMAT, outcaps);

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

  GST_DEBUG_OBJECT (camreproc, "Creating camera reprocess module.");

  if (!gst_camera_reprocess_context_create (camreproc->context, params,
      gst_camera_reprocess_data_callback)) {
    GST_ERROR_OBJECT (camreproc, "Failed to configure camera reprocess module.");
    return FALSE;
  }

  GST_DEBUG_OBJECT (camreproc, "Created camera reprocess module.");

  return TRUE;
}

static GstBufferPool*
gst_camera_reprocess_create_buffer_pool (GstCameraReprocess *camreproc,
    GstCaps *caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (camreproc, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if ((pool = gst_image_buffer_pool_new ()) == NULL) {
    GST_ERROR_OBJECT (camreproc, "Failed to create image pool!");
    return NULL;
  }

  if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    allocator = gst_fd_allocator_new ();
    GST_INFO_OBJECT (camreproc, "Buffer pool uses GBM memory");
  } else {
    allocator = gst_qti_allocator_new (GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    GST_INFO_OBJECT (camreproc, "Buffer pool uses DMA memory");
  }

  if (allocator == NULL) {
    GST_ERROR_OBJECT (camreproc, "Failed to create allocator");
    gst_clear_object (&pool);
    return NULL;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_POOL_MIN_BUFFERS, DEFAULT_POOL_MAX_BUFFERS);

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (GST_VIDEO_INFO_FORMAT (&info) == GST_VIDEO_FORMAT_NV12_Q08C ||
      GST_VIDEO_INFO_FORMAT (&info) == GST_VIDEO_FORMAT_NV12_Q10LE32C)
    GST_DEBUG_OBJECT (camreproc, "Buffer pool uses UBWC mode.");

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (camreproc, "Failed to set pool configuration!");
    gst_clear_object (&pool);
  }

  gst_object_unref (allocator);

  return pool;
}

static gboolean
gst_camera_reprocess_decide_allocation (GstBaseTransform *base, GstQuery *query)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (base);
  GstCaps *caps = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;
  guint size, minbuffers, maxbuffers;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (camreproc, "Failed to parse caps in allocation query.");
    return FALSE;
  }

  // Old buffer pool will remain in case there is other caps negotiation
  if (camreproc->pool) {
    gst_buffer_pool_set_active (camreproc->pool, FALSE);
    gst_object_unref (camreproc->pool);
    camreproc->pool = NULL;
    GST_DEBUG_OBJECT (camreproc, "Destroied old buffer pool.");
  }

  // Allocate a new buffer pool and config
  camreproc->pool = gst_camera_reprocess_create_buffer_pool (camreproc, caps);
  if (!camreproc->pool) {
    GST_ERROR_OBJECT (camreproc, "Failed to create buffer pool.");
    return FALSE;
  }

  // Set parameters of buffer pool in query
  config = gst_buffer_pool_get_config (camreproc->pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (config);

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, camreproc->pool, size,
        minbuffers, maxbuffers);
  else
    gst_query_add_allocation_pool (query, camreproc->pool, size, minbuffers,
        maxbuffers);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_camera_reprocess_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer *inbuf, GstBuffer **outbuf)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (base);

  g_return_val_if_fail (camreproc->pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (camreproc->pool) &&
      !gst_buffer_pool_set_active (camreproc->pool, TRUE)) {
    GST_ERROR_OBJECT (camreproc, "Failed to activate output video buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Handle gap buffer
  if (gst_buffer_get_size (inbuf) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP)) {
    GST_DEBUG_OBJECT (camreproc, "Get gap buffer.");
    *outbuf = gst_buffer_new ();
  }

  if ((*outbuf == NULL) && gst_buffer_pool_acquire_buffer (camreproc->pool,
      outbuf, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (camreproc, "Failed to create output video buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuf, inbuf,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return GST_FLOW_OK;
}

static GstCaps*
gst_camera_reprocess_transform_caps (GstBaseTransform *base,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (base);
  GstCaps *result = gst_caps_new_empty ();
  GstStructure *structure = NULL;

  for (gint index = 0; index < (gint)gst_caps_get_size (caps); ++index) {
    structure = gst_caps_get_structure (caps, index);
    structure = gst_structure_copy (structure);

    // Make width and height to range for othercaps which is allowed to transform
    if (gst_structure_has_field (structure, "width"))
      gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        NULL);

    if (gst_structure_has_field (structure, "height"))
      gst_structure_set (structure, "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        NULL);

    gst_structure_remove_fields (structure, "format", NULL);

    gst_caps_append_structure_full (result, structure,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL));
  }

  if (filter) {
    GstCaps *intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (camreproc, "Transformed caps: %" GST_PTR_FORMAT, result);

  return result;
}

static GstCaps*
gst_camera_reprocess_fixate_caps (GstBaseTransform *base,
    GstPadDirection direction, GstCaps *caps, GstCaps *othercaps)
{
  GstCameraReprocess *camreproc = GST_CAMERA_REPROCESS (base);
  GstStructure *structure = NULL;
  GstStructure *other_structure = NULL;
  GType type = 0;
  gint width = 0, height = 0;

  structure = gst_caps_get_structure (caps, 0);
  other_structure = gst_caps_get_structure (othercaps, 0);

  type = gst_structure_get_field_type (other_structure, "width");
  if (type == GST_TYPE_INT_RANGE) {
    gst_structure_get_int (structure, "width", &width);
    gst_structure_set (other_structure, "width", G_TYPE_INT, width, NULL);
  }

  type = gst_structure_get_field_type (other_structure, "height");
  if (type == GST_TYPE_INT_RANGE) {
    gst_structure_get_int (structure, "height", &height);
    gst_structure_set (other_structure, "height", G_TYPE_INT, height, NULL);
  }

  othercaps = gst_caps_fixate (othercaps);
  GST_DEBUG_OBJECT (camreproc, "Fixated to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}

static gboolean
gst_camera_reprocess_query (GstBaseTransform *base, GstPadDirection direction,
    GstQuery *query)
{
  GstPad *otherpad;
  gboolean ret = FALSE;

  otherpad = (direction == GST_PAD_SRC) ? base->sinkpad : base->srcpad;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);
      if (format == GST_FORMAT_TIME && base->segment.format == GST_FORMAT_TIME) {
        gint64 pos;
        ret = TRUE;

        pos = gst_segment_to_stream_time (&base->segment, GST_FORMAT_TIME,
            base->segment.position);
        gst_query_set_position (query, format, pos);
      } else {
        ret = gst_pad_peer_query (otherpad, query);
      }
      break;
    }
    default:
    {
      ret = GST_BASE_TRANSFORM_CLASS (gst_camera_reprocess_parent_class) ->
          query (base, direction, query);
      break;
    }
  }

  return ret;
}

static void
gst_camera_reprocess_class_init (GstCameraReprocessClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_camera_reprocess_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_camera_reprocess_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_camera_reprocess_finalize);

  gst_element_class_set_static_metadata (
      element, "Camera Reprocess", "Filter/Converter",
      "Reprocess images via camera module", "QTI");

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
          GST_TYPE_CAMERA_REPROCESS_EIS, DEFAULT_PROP_EIS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));
  g_object_class_install_property (gobject, PROP_SESSION_METADATA,
      g_param_spec_pointer ("session-metadata", "Session Metadata",
          "Settings metadata used for creating offline camera session",
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PAUSED));

  gst_element_class_add_static_pad_template (element,
      &gst_camera_reprocess_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_camera_reprocess_src_pad_template);

  // GstBufferPool
  base->decide_allocation = GST_DEBUG_FUNCPTR (gst_camera_reprocess_decide_allocation);

  // Main API
  base->stop = GST_DEBUG_FUNCPTR (gst_camera_reprocess_stop);
  base->transform = GST_DEBUG_FUNCPTR (gst_camera_reprocess_transform);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_camera_reprocess_prepare_output_buffer);

  // Caps negotiation
  base->transform_caps = GST_DEBUG_FUNCPTR (gst_camera_reprocess_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_camera_reprocess_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_camera_reprocess_set_caps);

  // Query
  base->query = GST_DEBUG_FUNCPTR (gst_camera_reprocess_query);

  GST_DEBUG_CATEGORY_INIT (camera_reprocess_debug, "qticamreproc", 0,
      "QTI Camera Reprocess");
}

static void
gst_camera_reprocess_init (GstCameraReprocess *camreproc)
{
  gboolean success = TRUE;

  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (camreproc), FALSE);
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (camreproc), FALSE);

  camreproc->context = gst_camera_reprocess_context_new ();
  g_return_if_fail (camreproc->context != NULL);

  success = gst_camera_reprocess_context_connect (camreproc->context,
      gst_camera_reprocess_event_callback, camreproc);
  if (!success) {
    GST_ERROR_OBJECT (camreproc, "Failed to connect.");
    gst_camera_reprocess_context_free (camreproc->context);
    return;
  }

  GST_INFO_OBJECT (camreproc, "Camera reprocess plugin instance inited.");
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "qticamreproc", GST_RANK_PRIMARY,
      GST_TYPE_CAMERA_REPROCESS);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qticamreproc,
    "Reprocess images via camera module",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
