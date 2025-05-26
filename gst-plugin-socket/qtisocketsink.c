/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qtisocketsink.h"

#include <gst/allocators/gstfdmemory.h>
#include <gst/utils/common-utils.h>

#include <gst/ml/gstmlmeta.h>
#include <gst/video/video-utils.h>

#include <errno.h>

#define gst_socket_sink_parent_class parent_class
G_DEFINE_TYPE (GstFdSocketSink, gst_socket_sink, GST_TYPE_BASE_SINK);

GST_DEBUG_CATEGORY_STATIC (gst_socket_sink_debug);
#define GST_CAT_DEFAULT gst_socket_sink_debug

#define GST_SOCKET_SINK_CAPS \
    "neural-network/tensors;" \
    "video/x-raw(ANY);" \
    "text/x-raw"

#define POLL_TIMEOUT_MS 100000

enum
{
  PROP_0,
  PROP_SOCKET
};

static GstStaticPadTemplate socket_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_SOCKET_SINK_CAPS));

static gint
gst_socket_sink_get_protection_meta_count (GstBuffer * buffer)
{
  GstMeta *meta = NULL;
  gpointer state = NULL;
  gint size = 0;
  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
      GST_PROTECTION_META_API_TYPE))) {
    size++;
  }
  return size;
}

static gboolean
gst_socket_sink_set_location (GstFdSocketSink * sink, const gchar * location)
{
  g_free (sink->sockfile);

  if (location != NULL) {
    sink->sockfile = g_strdup (location);
    GST_INFO_OBJECT (sink, "sockfile : %s", sink->sockfile);
  } else {
    sink->sockfile = NULL;
  }
  return TRUE;
}

static void
gst_socket_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (sink);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (sink);
  switch (prop_id) {
    case PROP_SOCKET:
      gst_socket_sink_set_location (sink, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sink);
}

static void
gst_socket_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (object);

  GST_OBJECT_LOCK (sink);
  switch (prop_id) {
    case PROP_SOCKET:
      g_value_set_string (value, sink->sockfile);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sink);
}

static gboolean
gst_socket_sink_try_connect (GstFdSocketSink * sink)
{
  struct sockaddr_un address = {0};

  g_mutex_lock (&sink->socklock);

  if (g_atomic_int_get (&sink->connected)) {
    g_mutex_unlock (&sink->socklock);
    return TRUE;
  }

  if ((sink->socket = socket (AF_UNIX, SOCK_SEQPACKET, 0)) < 0) {
    GST_ERROR_OBJECT (sink, "Socket creation error");
    g_mutex_unlock (&sink->socklock);
    return FALSE;
  }

  address.sun_family = AF_UNIX;
  g_strlcpy (address.sun_path, sink->sockfile, sizeof (address.sun_path));

  if (connect (sink->socket,
      (struct sockaddr *) &address, sizeof (address)) < 0) {
    close (sink->socket);
    sink->socket = 0;
    errno = 0;
    g_mutex_unlock (&sink->socklock);
    GST_DEBUG_OBJECT (sink, "connect unsuccessfull");
    return FALSE;
  }

  g_mutex_unlock (&sink->socklock);

  g_atomic_int_set (&sink->connected, TRUE);

  GST_DEBUG_OBJECT (sink, "Socket connected");

  return TRUE;
}

static void
gst_socket_deinitialize_for_buffers (GstFdSocketSink * sink)
{
  GList *keys_list = NULL;

  g_mutex_lock (&sink->bufmaplock);

  GST_DEBUG_OBJECT (sink, "Cleaning potential buffers %p", sink->bufmap);

  keys_list = g_hash_table_get_keys (sink->bufmap);
  for (GList *element = keys_list; element; element = element->next) {
    gint buf_id = GPOINTER_TO_INT (element->data);
    GstBuffer *buffer =
        g_hash_table_lookup (sink->bufmap, GINT_TO_POINTER (buf_id));

    g_hash_table_remove (sink->bufmap, GINT_TO_POINTER (buf_id));

    if (buffer) {
      gst_buffer_unref (buffer);
    }

    GST_DEBUG_OBJECT (sink, "Cleanup buffer: %p, buf_id: %d", buffer, buf_id);
  }

  g_list_free (keys_list);
  g_mutex_unlock (&sink->bufmaplock);
}

static GstVideoRoiMetaPayload*
gst_socket_serialize_roi_meta (GstVideoRegionOfInterestMeta * roi_meta)
{
  GstVideoRoiMetaPayload *roi_meta_pl =
      g_malloc (sizeof (GstVideoRoiMetaPayload));
  const GstStructure *object_detection = NULL, *xtraparams = NULL;
  gchar *label = NULL;
  gsize label_size = 0, label_maxsize = 0;

  roi_meta_pl->identity = MESSAGE_VIDEO_ROI_META;
  roi_meta_pl->id = roi_meta->id;
  roi_meta_pl->parent_id = roi_meta->parent_id;

  label = g_quark_to_string(roi_meta->roi_type);
  label_size = strlen (label) + 1;
  label_maxsize = sizeof (roi_meta_pl->label);

  if (label_size > label_maxsize) {
    GST_WARNING ("Detection label too long, cut at %lu symbols", label_maxsize);
    label_size = label_maxsize;
  }

  memmove (roi_meta_pl->label, label, label_size);
  roi_meta_pl->label[label_size - 1] = '\0';

  roi_meta_pl->x = roi_meta->x;
  roi_meta_pl->y = roi_meta->y;
  roi_meta_pl->w = roi_meta->w;
  roi_meta_pl->h = roi_meta->h;

  roi_meta_pl->det_size = 0;
  object_detection =
      gst_video_region_of_interest_meta_get_param (roi_meta, "ObjectDetection");
  if (object_detection) {
    gchar *meta = gst_structure_to_string (object_detection);
    gsize meta_size = strlen (meta) + 1;
    gsize meta_maxsize = sizeof (roi_meta_pl->det_meta);

    if (meta_size > meta_maxsize) {
      GST_WARNING ("Detection meta too long, cut at %lu symbols", meta_maxsize);
      meta_size = meta_maxsize;
    }

    memmove (roi_meta_pl->det_meta, meta, meta_size);
    roi_meta_pl->det_meta[meta_size - 1] = '\0';
    g_free (meta);
    roi_meta_pl->det_size = meta_size;
  }

  roi_meta_pl->xtraparams_size = 0;
  xtraparams =
      gst_video_region_of_interest_meta_get_param (roi_meta, "xtraparams");
  if (xtraparams) {
    gchar *xp = gst_structure_to_string (xtraparams);
    gsize xp_size = strlen (xp) + 1;
    gsize xp_maxsize = sizeof (roi_meta_pl->xtraparams);

    if (xp_size > xp_maxsize) {
      GST_WARNING ("Detection xtraparams too long, cut at %lu symbols",
          xp_maxsize);
      xp_size = xp_maxsize;
    }

    memmove (roi_meta_pl->xtraparams, xp, xp_size);
    roi_meta_pl->det_meta[xp_size - 1] = '\0';
    g_free (xp);
    roi_meta_pl->xtraparams_size = xp_size;
  }

  return roi_meta_pl;
}

static GstVideoClassMetaPayload*
gst_socket_serialize_class_meta (GstVideoClassificationMeta * class_meta)
{
  GstVideoClassMetaPayload *class_meta_pl =
      g_malloc (sizeof (GstVideoClassMetaPayload));
  GArray *labels = NULL;
  gsize labels_maxsize = 0;

  class_meta_pl->identity = MESSAGE_VIDEO_CLASS_META;
  class_meta_pl->id = class_meta->id;
  class_meta_pl->parent_id = class_meta->parent_id;

  labels = class_meta->labels;
  class_meta_pl->size = labels->len;
  labels_maxsize = sizeof (class_meta_pl->labels) / sizeof (GstClassLabelSer);

  if (class_meta_pl->size > labels_maxsize) {
    GST_WARNING ("Too many labels, cut at %lu labels", labels_maxsize);
    class_meta_pl->size = labels_maxsize;
  }

  for (guint i = 0; i < labels->len && i < labels_maxsize; i++) {
    GstClassLabel *l = &(g_array_index (labels, GstClassLabel, i));

    gchar *label = g_quark_to_string(l->name);
    gsize label_size = strlen (label) + 1;
    gsize label_maxsize = sizeof (class_meta_pl->labels[i].name);

    if (label_size > label_maxsize) {
      GST_WARNING ("Classification label too long, cut at %lu symbols",
          label_maxsize);
      label_size = label_maxsize;
    }

    memmove (class_meta_pl->labels[i].name, label, label_size);
    class_meta_pl->labels[i].name[label_size - 1] = '\0';

    class_meta_pl->labels[i].confidence = l->confidence;
    class_meta_pl->labels[i].color = l->color;

    class_meta_pl->labels[i].xtraparams_size = 0;
    if (l->xtraparams) {
      gchar *xtraparams = gst_structure_to_string (l->xtraparams);
      gsize xtraparams_size = strlen (xtraparams) + 1;
      gsize xtraparams_maxsize = sizeof (class_meta_pl->labels[i].xtraparams);

      if (xtraparams_size > xtraparams_maxsize) {
        GST_WARNING ("Label xtraparams too long, cut at %lu symbols",
            xtraparams_maxsize);
        xtraparams_size = xtraparams_maxsize;
      }

      memmove (class_meta_pl->labels[i].xtraparams, xtraparams,
          xtraparams_size);
      class_meta_pl->labels[i].xtraparams[xtraparams_size - 1] = '\0';
      g_free (xtraparams);
      class_meta_pl->labels[i].xtraparams_size = xtraparams_size;
    }
  }

  return class_meta_pl;
}

static GstVideoLmMetaPayload*
gst_socket_serialize_lm_meta (GstVideoLandmarksMeta *lm_meta)
{
  GstVideoLmMetaPayload *lm_meta_pl =
      g_malloc (sizeof (GstVideoLmMetaPayload));
  GArray *kps = NULL, *links = NULL;
  gsize kps_maxsize = 0, links_maxsize = 0;

  lm_meta_pl->identity = MESSAGE_VIDEO_LM_META;
  lm_meta_pl->id = lm_meta->id;
  lm_meta_pl->parent_id = lm_meta->parent_id;

  lm_meta_pl->confidence = lm_meta->confidence;

  kps = lm_meta->keypoints;
  lm_meta_pl->kps_size = kps->len;
  kps_maxsize = sizeof (lm_meta_pl->kps) / sizeof (GstVideoKeypointSer);

  if (lm_meta_pl->kps_size > kps_maxsize) {
    GST_WARNING ("Too many keypoints, cut at %lu keypoints", kps_maxsize);
    lm_meta_pl->kps_size = kps_maxsize;
  }

  for (guint i = 0; i < kps->len && i < kps_maxsize; i++) {
    GstVideoKeypoint *kp = &(g_array_index (kps, GstVideoKeypoint, i));

    gchar *keypoint = g_quark_to_string(kp->name);
    gsize keypoint_size = strlen (keypoint) + 1;
    gsize keypoint_maxsize = sizeof (lm_meta_pl->kps[i].name);

    if (keypoint_size > keypoint_maxsize) {
      GST_WARNING ("Keypoint label too long, cut at %lu symbols",
          keypoint_maxsize);
      keypoint_size = keypoint_maxsize;
    }

    memmove (lm_meta_pl->kps[i].name, keypoint, keypoint_size);
    lm_meta_pl->kps[i].name[keypoint_size - 1] = '\0';

    lm_meta_pl->kps[i].confidence = kp->confidence;
    lm_meta_pl->kps[i].color = kp->color;
    lm_meta_pl->kps[i].x = kp->x;
    lm_meta_pl->kps[i].y = kp->y;
  }

  links = lm_meta->links;
  lm_meta_pl->links_size = links->len;
  links_maxsize = sizeof (lm_meta_pl->links) / sizeof (GstVideoKeypointLinkSer);

  if (lm_meta_pl->links_size > links_maxsize) {
    GST_WARNING ("Too many links, cut at %lu links", links_maxsize);
    lm_meta_pl->links_size = links_maxsize;
  }

  for (guint i = 0; i < links->len && i < links_maxsize; i++) {
    GstVideoKeypointLinkSer *l =
        &(g_array_index (links, GstVideoKeypointLinkSer, i));

    lm_meta_pl->links[i].s_kp_idx = l->s_kp_idx;
    lm_meta_pl->links[i].d_kp_idx = l->d_kp_idx;
  }

  lm_meta_pl->xtraparams_size = 0;
  if (lm_meta->xtraparams) {
    gchar *xtraparams = gst_structure_to_string (lm_meta->xtraparams);
    gsize xtraparams_size = strlen (xtraparams) + 1;
    gsize xtraparams_maxsize = sizeof (lm_meta_pl->xtraparams);

    if (xtraparams_size > xtraparams_maxsize) {
      GST_WARNING ("Landmarks xtraparams too long, cut at %lu symbols",
          xtraparams_maxsize);
      xtraparams_size = xtraparams_maxsize;
    }

    memmove (lm_meta_pl->xtraparams, xtraparams, xtraparams_size);
    lm_meta_pl->xtraparams[xtraparams_size - 1] = '\0';
    g_free (xtraparams);
    lm_meta_pl->xtraparams_size = xtraparams_size;
  }

  return lm_meta_pl;
}

static GstFlowReturn
gst_socket_sink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (bsink);
  GstBufferPayload * buffer_pl = NULL;
  GstMemory *memory = NULL;
  GstMeta *meta = NULL;
  gpointer state = NULL;
  GstPayloadInfo pl_info = {0};
  gint memory_fds[GST_MAX_MEM_BLOCKS]; // todo expand
  gint memory_fds_send[GST_MAX_MEM_BLOCKS]; // todo expand
  guint n_memory = 0;
  gint n_memory_send = 0;

  GST_DEBUG_OBJECT (sink, "gst_socket_sink_render %d", sink->mode);

  g_mutex_lock (&sink->socklock);
  if (!g_atomic_int_get (&sink->connected) ||
       g_atomic_int_get (&sink->should_disconnect) ||
       g_atomic_int_get (&sink->should_stop)) {
    g_mutex_unlock (&sink->socklock);
    return GST_FLOW_OK;
  }
  g_mutex_unlock (&sink->socklock);

  pl_info.protection_metadata_info = g_ptr_array_new_full (
      gst_socket_sink_get_protection_meta_count (buffer), g_free);

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
      GST_PROTECTION_META_API_TYPE))) {
    GstProtectionMetadataPayload * pmeta_pl = NULL;
    const gchar * pmeta = gst_structure_to_string (
        GST_PROTECTION_META_CAST (meta)->info);

    gsize size = strlen (pmeta) + 1;

    pmeta_pl = g_malloc (sizeof (GstProtectionMetadataPayload));

    GST_DEBUG_OBJECT (sink, "Add protetion metadata: %s", pmeta);

    pmeta_pl->identity = MESSAGE_PROTECTION_META;

    if (sizeof (pmeta_pl->contents) < size) {
        GST_ERROR_OBJECT (sink, "Got too much data");
        free_pl_struct (&pl_info);
        return GST_FLOW_ERROR;
    }

    memmove (pmeta_pl->contents, pmeta, size);

    pmeta_pl->size = size;
    pmeta_pl->maxsize = sizeof (pmeta_pl->contents);
    g_ptr_array_add (pl_info.protection_metadata_info, pmeta_pl);
  }

  n_memory = gst_buffer_n_memory (buffer);
  if (n_memory != GST_EXPECTED_MEM_BLOCKS(sink)) {
    GST_ERROR_OBJECT (sink, "Invalid number of memory buffers!");
    return GST_FLOW_ERROR;
  }

  buffer_pl = g_malloc (sizeof (GstBufferPayload));

  pl_info.mem_block_info =
      g_ptr_array_new_full (GST_EXPECTED_MEM_BLOCKS(sink), g_free);
  buffer_pl->identity = MESSAGE_BUFFER_INFO;
  buffer_pl->pts = GST_BUFFER_PTS (buffer);
  buffer_pl->dts = GST_BUFFER_DTS (buffer);
  buffer_pl->duration = GST_BUFFER_DURATION (buffer);
  buffer_pl->use_buffer_pool = buffer->pool != NULL;
  pl_info.buffer_info = buffer_pl;

  //From here needs batching logic
  for (guint i = 0; i < n_memory; i++) {
    gsize size = 0;
    gsize maxsize = 0;

    memory = gst_buffer_peek_memory (buffer, i);

    size = memory->size;
    maxsize = memory->maxsize;

    if (sink->mode == DATA_MODE_TEXT) {
      GstTextPayload * text_pl = NULL;
      GstMapInfo map_info;

      *(buffer_pl->buf_id) = -1;
      buffer_pl->use_buffer_pool = 0;

      if (sizeof (text_pl->contents) < size) {
        GST_ERROR ("Got too much text from memory block");
        free_pl_struct (&pl_info);
        return GST_FLOW_ERROR;
      }

      text_pl = g_malloc (sizeof (GstTextPayload));

      text_pl->identity = MESSAGE_TEXT;

      gst_memory_map (memory, &map_info, GST_MAP_READ);
      memmove (text_pl->contents, map_info.data, map_info.size);
      gst_memory_unmap (memory, &map_info);

      text_pl->size = size;
      text_pl->maxsize = maxsize;
      g_ptr_array_add (pl_info.mem_block_info, text_pl);
    } else if (!gst_is_fd_memory (memory)) {
      free_pl_struct (&pl_info);
      GST_ERROR_OBJECT (sink, "Memory allocator is not fd");
      return GST_FLOW_ERROR;
    }

    if (sink->mode == DATA_MODE_TENSOR) {
      GstTensorPayload * tensor_pl = NULL;

      memory_fds[i] = gst_fd_memory_get_fd (memory);
      buffer_pl->buf_id[i] = memory_fds[i];

      GstMLTensorMeta *mlmeta =
        gst_buffer_get_ml_tensor_meta_id (buffer, i);
      if (mlmeta == NULL) {
        GST_ERROR_OBJECT (sink, "Invalid mlmeta");
        return GST_FLOW_ERROR;
      }

      tensor_pl = g_malloc (sizeof (GstTensorPayload));

      tensor_pl->identity = MESSAGE_TENSOR;
      tensor_pl->type = mlmeta->type;
      tensor_pl->n_dimensions = mlmeta->n_dimensions;

      for (guint j = 0; j < mlmeta->n_dimensions; j++) {
        tensor_pl->dimensions[j] = mlmeta->dimensions[j];
      }

      GST_DEBUG_OBJECT (sink, "Buffer ML meta buf_id: %d", buffer_pl->buf_id[i]);

      tensor_pl->size = size;
      tensor_pl->maxsize = maxsize;
      g_ptr_array_add (pl_info.mem_block_info, tensor_pl);
    }

    if (sink->mode == DATA_MODE_VIDEO) {
      GstFramePayload * frame_pl = NULL;
      GstMeta *meta_i = NULL;

      memory_fds[i] = gst_fd_memory_get_fd (memory);
      buffer_pl->buf_id[i] = memory_fds[i];

      GstVideoMeta *meta = gst_buffer_get_video_meta (buffer);
      if (meta == NULL) {
        GST_ERROR_OBJECT (sink, "Invalid video meta");
        return GST_FLOW_ERROR;
      }

      frame_pl = g_malloc (sizeof (GstFramePayload));

      frame_pl->identity = MESSAGE_FRAME;
      frame_pl->width = meta->width;
      frame_pl->height = meta->height;
      frame_pl->format = meta->format;
      frame_pl->n_planes = meta->n_planes;
      frame_pl->flags = meta->flags;

      for (guint j = 0; j < meta->n_planes; j++) {
        frame_pl->offset[j] = meta->offset[j];
        frame_pl->stride[j] = meta->stride[j];
      }

      frame_pl->size = size;
      frame_pl->maxsize = maxsize;
      g_ptr_array_add (pl_info.mem_block_info, frame_pl);

      pl_info.roi_meta_info = g_ptr_array_new ();
      pl_info.class_meta_info = g_ptr_array_new ();
      pl_info.lm_meta_info = g_ptr_array_new ();

      while ((meta_i = gst_buffer_iterate_meta (buffer, &state))) {
        if (meta_i->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) {
          GstVideoRegionOfInterestMeta *roi_meta =
              GST_VIDEO_ROI_META_CAST (meta_i);
          GstVideoRoiMetaPayload *roi_meta_pl =
              gst_socket_serialize_roi_meta (roi_meta);
          g_ptr_array_add (pl_info.roi_meta_info, roi_meta_pl);
        }
        else if (meta_i->info->api == GST_VIDEO_CLASSIFICATION_META_API_TYPE) {
          GstVideoClassificationMeta *class_meta =
              GST_VIDEO_CLASSIFICATION_META_CAST (meta_i);
          GstVideoClassMetaPayload *class_meta_pl =
              gst_socket_serialize_class_meta (class_meta);
          g_ptr_array_add (pl_info.class_meta_info, class_meta_pl);
        }
        else if (meta_i->info->api == GST_VIDEO_LANDMARKS_META_API_TYPE) {
          GstVideoLandmarksMeta *lm_meta =
              GST_VIDEO_LANDMARKS_META_CAST (meta_i);
          GstVideoLmMetaPayload *lm_meta_pl =
              gst_socket_serialize_lm_meta (lm_meta);
          g_ptr_array_add (pl_info.lm_meta_info, lm_meta_pl);
        }
      }
    }

    if (sink->mode != DATA_MODE_TEXT &&
        sink->mode != DATA_MODE_TENSOR &&
        sink->mode != DATA_MODE_VIDEO) {
      GST_ERROR_OBJECT (sink, "Unsupported mode: %d", sink->mode);
      return GST_FLOW_ERROR;
    }

    if (sink->mode != DATA_MODE_TEXT) {
      g_mutex_lock (&sink->bufmaplock);
      if (!buffer->pool ||
          !g_hash_table_contains (sink->bufmap, GINT_TO_POINTER (memory_fds[i]))) {
        memory_fds_send[n_memory_send++] = memory_fds[i];
        g_hash_table_insert (sink->bufmap, GINT_TO_POINTER (memory_fds[i]), buffer);
      } else {
        g_hash_table_insert (sink->bufmap, GINT_TO_POINTER (memory_fds[i]), buffer);
      }
      sink->bufcount++;
      g_mutex_unlock (&sink->bufmaplock);

      gst_buffer_ref (buffer);
    }
  }

  if (n_memory_send > 0) {
    pl_info.fd_count = g_malloc (sizeof (GstFdCountPayload));
    pl_info.fd_count->identity = MESSAGE_FD_COUNT;
    pl_info.fd_count->n_fds = n_memory_send;
  }

  if (sink->mode == DATA_MODE_TEXT) {
    pl_info.fds = NULL;
  } else {
    pl_info.fds = memory_fds_send;
  }

  if (send_socket_message (sink->socket, &pl_info) < 0) {
    if (sink->mode == DATA_MODE_TEXT) {
      GST_ERROR_OBJECT (sink, "Send text message failed! %d", errno);
    } else {
      GST_ERROR_OBJECT (sink, "Send Fd message failed! %d", errno);
    }

    free_pl_struct (&pl_info);

    return GST_FLOW_ERROR;
  }

  free_pl_struct (&pl_info);

  GST_DEBUG_OBJECT (sink, "Sent data of type %d; Number of mem blocks sent: %d",
      sink->mode, n_memory_send);

  return GST_FLOW_OK;
}

static gpointer
gst_socket_sink_wait_message_loop (gpointer user_data)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (user_data);
  GstBuffer *buffer = NULL;
  GstPayloadInfo pl_info = {0};
  struct pollfd poll_fd;
  gint ret = 0;
  gboolean running = TRUE;

  while (running) {
    switch (sink->state) {

      case GST_SOCKET_TRY_CONNECT: {
        if (g_atomic_int_get (&sink->should_stop)) {
          running = FALSE;
          break;
        }

        if (gst_socket_sink_try_connect (sink)) {
          sink->state = GST_SOCKET_RUNNING;
          break;
        }

        usleep (10000);
        break;
      }

      case GST_SOCKET_RUNNING: {
        if (g_atomic_int_get (&sink->should_disconnect)) {
          if (sink->mode == DATA_MODE_VIDEO ||
              sink->mode == DATA_MODE_TENSOR) {

            g_mutex_lock (&sink->bufmaplock);
            if (sink->bufcount == 0) {
              g_mutex_unlock (&sink->bufmaplock);
              sink->state = GST_SOCKET_DISCONNECT;
              break;
            }
            g_mutex_unlock (&sink->bufmaplock);

          } else {
            sink->state = GST_SOCKET_DISCONNECT;
            break;
          }
        }

        poll_fd.fd = sink->socket;
        poll_fd.events = POLLIN;

        ret = poll (&poll_fd, 1, POLL_TIMEOUT_MS);
        if (ret < 0) {
          GST_DEBUG_OBJECT (sink, "Socket poll error");

          free_pl_struct (&pl_info);
          sink->state = GST_SOCKET_DISCONNECT;
          break;
        } else if (ret == 0) {
          GST_DEBUG_OBJECT (sink, "Socket poll timeout");

          free_pl_struct (&pl_info);
          sink->state = GST_SOCKET_DISCONNECT;

          break;
        } else {
          if (poll_fd.revents & POLLHUP ||
              poll_fd.revents & POLLERR) {
            GST_DEBUG_OBJECT (sink, "Socket closed");

            free_pl_struct (&pl_info);
            sink->state = GST_SOCKET_DISCONNECT;

            break;
          }
        }

        if (receive_socket_message (sink->socket, &pl_info, 0) == -1) {
          GST_INFO_OBJECT (sink, "Socket mesage receive failed");
          free_pl_struct (&pl_info);
          break;
        }

        if (GST_PL_INFO_IS_MESSAGE (&pl_info, MESSAGE_DISCONNECT)) {
          GST_DEBUG_OBJECT (sink, "MESSAGE_DISCONNECT");
          g_atomic_int_set (&sink->should_disconnect, TRUE);
          free_pl_struct (&pl_info);
          break;
        }

        if (pl_info.return_buffer != NULL) {
          if (sink->mode == DATA_MODE_TEXT) {
            GST_WARNING_OBJECT (sink,
                "Received return buffer, but text mode was chosen!");
            free_pl_struct (&pl_info);
            break;
          }

          if (pl_info.fd_count == NULL) {
            GST_ERROR_OBJECT (sink, "Received return buffer, but no fd_count");
            free_pl_struct (&pl_info);
            break;
          }

          GST_DEBUG_OBJECT (sink, "Number of returned fds: %d", pl_info.fd_count->n_fds);

          for (gint i = 0; i < pl_info.fd_count->n_fds; i++) {
            gint buf_id = pl_info.return_buffer->buf_id[i];

            g_mutex_lock (&sink->bufmaplock);
            buffer = g_hash_table_lookup (sink->bufmap, GINT_TO_POINTER (buf_id));
            g_hash_table_insert (sink->bufmap, GINT_TO_POINTER (buf_id), NULL);
            sink->bufcount--;
            g_mutex_unlock (&sink->bufmaplock);
            gst_buffer_unref (buffer);

            GST_DEBUG_OBJECT (sink, "Buffer returned %p, buf_id: %d, count: %d",
                buffer, buf_id, sink->bufcount);
          }
        }

        free_pl_struct (&pl_info);
        break;
      }

      case GST_SOCKET_DISCONNECT: {
        g_mutex_lock (&sink->socklock);

        g_atomic_int_set (&sink->connected, FALSE);

        if (sink->socket > 0) {
          shutdown (sink->socket, SHUT_RDWR);
          close (sink->socket);
          unlink (sink->sockfile);
          sink->socket = 0;
        }
        g_mutex_unlock (&sink->socklock);

        gst_socket_deinitialize_for_buffers (sink);

        g_atomic_int_set (&sink->should_disconnect, FALSE);

        if (sink->should_stop) {
          running = FALSE;
        }

        sink->state = GST_SOCKET_TRY_CONNECT;

        break;
      }
    }
  }

  return NULL;
}

static gboolean
gst_socket_sink_start (GstBaseSink * basesink)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (basesink);

  GST_DEBUG_OBJECT (sink, "Socket file : %s", sink->sockfile);

  g_object_set (G_OBJECT (basesink), "sync", FALSE, NULL);

  sink->bufmap = g_hash_table_new (NULL, NULL);
  sink->bufcount = 0;

  g_mutex_init (&sink->bufmaplock);
  g_mutex_init (&sink->socklock);
  g_mutex_init (&sink->msglock);

  g_atomic_int_set (&sink->should_stop, FALSE);
  g_atomic_int_set (&sink->should_disconnect, FALSE);
  g_atomic_int_set (&sink->connected, FALSE);

  sink->msg_thread = NULL;

  sink->state = GST_SOCKET_TRY_CONNECT;

  g_mutex_lock (&sink->msglock);
  if (sink->msg_thread == NULL) {
    if ((sink->msg_thread = g_thread_new ("Msg thread",
        gst_socket_sink_wait_message_loop, sink)) == NULL) {
      g_mutex_unlock (&sink->msglock);
      return FALSE;
    }
  }
  g_mutex_unlock (&sink->msglock);

  return TRUE;
}

static gboolean
gst_socket_sink_stop (GstBaseSink * basesink)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (basesink);

  GST_DEBUG_OBJECT (sink, "Stop");

  g_atomic_int_set (&sink->should_disconnect, TRUE);
  g_atomic_int_set (&sink->should_stop, TRUE);

  if (sink->msg_thread) {
    g_thread_join (sink->msg_thread);
    sink->msg_thread = NULL;
  }

  if (sink->bufmap) {
    g_hash_table_destroy (sink->bufmap);
  }

  g_mutex_clear (&sink->bufmaplock);
  g_mutex_clear (&sink->socklock);

  if (sink->mlinfo != NULL) {
    gst_ml_info_free (sink->mlinfo);
  }

  return TRUE;
}

static gboolean
gst_socket_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (bsink);
  gboolean retval = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    {
      GstCaps *caps = NULL;
      GstStructure *structure = NULL;
      gboolean needpool = FALSE;

      gst_query_parse_allocation (query, &caps, &needpool);
      if (NULL == caps) {
        GST_ERROR_OBJECT (sink, "Failed to extract caps from query!");
        retval = FALSE;
        break;
      }

      structure = gst_caps_get_structure (caps, 0);

      if (gst_structure_has_name (structure, "video/x-raw") ||
          gst_structure_has_name (structure, "neural-network/tensors"))
        gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

      retval = TRUE;
      break;
    }
    default:
      retval = FALSE;
      break;
  }

  if (!retval)
    retval = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);

  return retval;
}

static gboolean
gst_socket_sink_event (GstBaseSink *bsink, GstEvent *event)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (bsink);
  GstPayloadInfo pl_info = {0};
  GstMessagePayload msg;

  GST_DEBUG_OBJECT (sink, "GST EVENT: %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_INFO_OBJECT (sink, "EOS event");

      g_atomic_int_set (&sink->should_stop, TRUE);

      msg.identity = MESSAGE_EOS;
      pl_info.message = &msg;

      if (g_atomic_int_get (&sink->connected) &&
            (send_socket_message (sink->socket, &pl_info) < 0)) {
        GST_WARNING_OBJECT (sink, "Unable to send EOS message.");
      }
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
}

static void
gst_socket_sink_dispose (GObject * obj)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (obj);

  g_free (sink->sockfile);
  sink->sockfile = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_socket_sink_init (GstFdSocketSink * sink)
{
  sink->msg_thread = NULL;
  sink->socket = -1;
  sink->mode = DATA_MODE_NONE;

  g_atomic_int_set (&sink->should_stop, FALSE);

  GST_DEBUG_CATEGORY_INIT (gst_socket_sink_debug, "qtisocketsink", 0,
    "qtisocketsink object");
}

static gboolean
gst_socket_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstFdSocketSink *sink = GST_SOCKET_SINK (bsink);

  GstStructure *structure = NULL;
  GstMLInfo mlinfo;

  GST_INFO_OBJECT (sink, "Input caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (structure, "video/x-raw")) {
    sink->mode = DATA_MODE_VIDEO;
  } else if (gst_structure_has_name (structure, "text/x-raw")) {
    sink->mode = DATA_MODE_TEXT;
  } else if (gst_structure_has_name (structure, "neural-network/tensors")) {
    sink->mode = DATA_MODE_TENSOR;

    if (!gst_ml_info_from_caps (&mlinfo, caps)) {
      GST_ERROR_OBJECT (sink, "Failed to get input ML info from caps %"
          GST_PTR_FORMAT "!", caps);
      return FALSE;
    }

    if (sink->mlinfo != NULL)
      gst_ml_info_free (sink->mlinfo);

    sink->mlinfo = gst_ml_info_copy (&mlinfo);
  }

  return TRUE;
}

static void
gst_socket_sink_class_init (GstFdSocketSinkClass * klass)
{
  GObjectClass *gobject;
  GstElementClass *gstelement;
  GstBaseSinkClass *gstbasesink;

  gobject = G_OBJECT_CLASS (klass);
  gstelement = GST_ELEMENT_CLASS (klass);
  gstbasesink = GST_BASE_SINK_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_socket_sink_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_socket_sink_get_property);
  gobject->dispose      = GST_DEBUG_FUNCPTR (gst_socket_sink_dispose);

  g_object_class_install_property (gobject, PROP_SOCKET,
    g_param_spec_string ("socket", "Socket Location",
        "Location of the Unix Domain Socket", NULL,
        G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS |
        GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (gstelement,
      "QTI Socket Sink Element", "Socket Sink Element",
      "This plugin sends a GST buffer over Unix Domain Socket", "QTI");

  gst_element_class_add_static_pad_template (gstelement, &socket_sink_template);

  gstbasesink->render = GST_DEBUG_FUNCPTR (gst_socket_sink_render);
  gstbasesink->start = GST_DEBUG_FUNCPTR (gst_socket_sink_start);
  gstbasesink->stop = GST_DEBUG_FUNCPTR (gst_socket_sink_stop);
  gstbasesink->query = GST_DEBUG_FUNCPTR (gst_socket_sink_query);
  gstbasesink->event = GST_DEBUG_FUNCPTR (gst_socket_sink_event);
  gstbasesink->set_caps = GST_DEBUG_FUNCPTR (gst_socket_sink_set_caps);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtisocketsink", GST_RANK_PRIMARY,
          GST_TYPE_SOCKET_SINK);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtisocketsink,
    "Transfer GST buffer over Unix Domain Socket",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
