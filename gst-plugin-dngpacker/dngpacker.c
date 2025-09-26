/*
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include "dngpacker.h"
#include "packer-utils.h"

#define GST_CAT_DEFAULT gst_dngpacker_debug
GST_DEBUG_CATEGORY (gst_dngpacker_debug);

#define gst_dngpacker_parent_class parent_class
G_DEFINE_TYPE (GstDngPacker, gst_dngpacker, GST_TYPE_ELEMENT);

#define GST_DNGPACKER_MISMATCH_CHECK(ele, buf, set) do {                    \
  if (buf != set)                                                           \
    GST_WARNING_OBJECT (ele,                                                \
        #buf "(%d) and " #set "(%d) mismatch", buf, set );                  \
} while (0)

#define GST_DNGPACKER_RAW_SINK_CAPS                   \
    "video/x-bayer,"                                  \
    "format = (string) { bggr, rggb, gbrg, grbg },"   \
    "width = (int) [ 16,  65536 ], "                  \
    "height = (int) [ 16, 65536 ], "                  \
    "stride = (int) [ 16, 65536 ], "                  \
    "bpp = (string) { 8, 10, 12, 16} ;"

#define GST_DNGPACKER_IMAGE_SINK_CAPS                 \
    "image/jpeg,  "                                   \
    "width = (int) [ 16,  65536 ], "                  \
    "height = (int) [ 16, 65536 ]; "

#define GST_DNGPACKER_SRC_CAPS                        \
    "image/dng,  "                                    \
    "width = (int) [ 16,  65536 ], "                  \
    "height = (int) [ 16, 65536 ]; "

static GstStaticPadTemplate gst_dngpacker_raw_sink_template =
    GST_STATIC_PAD_TEMPLATE("raw_sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_DNGPACKER_RAW_SINK_CAPS)
    );

static GstStaticPadTemplate gst_dngpacker_image_sink_template =
    GST_STATIC_PAD_TEMPLATE("image_sink",
        GST_PAD_SINK,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_DNGPACKER_IMAGE_SINK_CAPS)
    );

static GstStaticPadTemplate gst_dngpacker_src_template =
    GST_STATIC_PAD_TEMPLATE("dng_src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_DNGPACKER_SRC_CAPS)
    );

static void
gst_dngpacker_utils_log_callback (void *context, const gchar * file,
                                  const gchar * function, gint line,
                                  const char *fmt, va_list args)
{
  gst_debug_log_valist(GST_CAT_DEFAULT, GST_LEVEL_DEBUG, file,
      function, line, (GObject *) (context), fmt, args);
}

static void
gst_dngpacker_utils_error_callback (const char * fmt, va_list args)
{
  g_logv ("GstDngPacker", G_LOG_LEVEL_WARNING, fmt, args);
}

static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;

  if (item->object != NULL)
    gst_buffer_unref (GST_BUFFER (item->object));

  g_slice_free (GstDataQueueItem, item);
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  return FALSE;
}

static void
queue_empty_cb (GstDataQueue * queue, gpointer checkdata)
{
  return;
}

static DngPackerCFAPattern
gst_dngpacker_convert_gst_format_to_cfa (GstBayerFormat format)
{
  DngPackerCFAPattern cfa;

  switch (format) {
    case GST_BAYER_FORMAT_BGGR:
      cfa = DNGPACKER_CFA_BGGR;
      break;
    case GST_BAYER_FORMAT_GBRG:
      cfa = DNGPACKER_CFA_GBRG;
      break;
    case GST_BAYER_FORMAT_GRBG:
      cfa = DNGPACKER_CFA_GRBG;
      break;
    case GST_BAYER_FORMAT_RGGB:
      cfa = DNGPACKER_CFA_RGGB;
      break;
    default:
      cfa = DNGPACKER_CFA_UNKNOWN;
      break;
  }

  return cfa;
}

static void
gst_dngpacker_update_packer_request (GstDngPacker *packer, GstVideoMeta *meta,
    size_t raw_size, uint8_t *raw_buf, size_t jpg_size, uint8_t *jpg_buf,
    DngPackRequest *request)
{
  GstRawImageSettings * settings = &packer->raw_img_settings;

  request->raw_size = raw_size;
  request->raw_buf = raw_buf;
  request->jpg_size = jpg_size;
  request->jpg_buf = jpg_buf;
  request->raw_bpp = settings->bpp;

  // if buffer is from qtiqmmfsrc, meta info will be provided, need to check
  // meta info between GstVideoMeta and GstCaps, but will always update
  // request with GstVideoMeta
  // if buffer is from filesource, no GstVideoMeta provided, so we have to
  // trust information from GstCaps
  if (meta == NULL) {
    request->cfa = settings->cfa;
    request->raw_width = settings->width;
    request->raw_height = settings->height;
    request->raw_stride = settings->stride;
  } else {
    uint32_t buf_stride;
    DngPackerCFAPattern buf_cfa;

    // in libgbm
    // for RAW10, meta->stride[0] = actual stride * 10 / 8
    // for RAW12, meta->stride[0] = actual stride * 12 / 8
    // to get actual stride, meta->stride[0] need to be converted accordingly
    switch (settings->bpp) {
    case 10:
      buf_stride = (meta->stride[0] * 8) / 10;
      break;
    case 12:
      buf_stride = (meta->stride[0] * 8) / 12;
      break;
    case 16:
      buf_stride = (meta->stride[0]);
      break;
    default:
      buf_stride = (meta->stride[0]);
      break;
    }

    buf_cfa = gst_dngpacker_convert_gst_format_to_cfa ((GstBayerFormat)meta->format);
    GST_DNGPACKER_MISMATCH_CHECK (packer, buf_cfa, settings->cfa);
    request->cfa = buf_cfa;

    GST_DNGPACKER_MISMATCH_CHECK (packer, buf_stride, (uint32_t)settings->stride);
    request->raw_stride = buf_stride;

    GST_DNGPACKER_MISMATCH_CHECK (packer, meta->width, (uint32_t)settings->width);
    request->raw_width = meta->width;

    GST_DNGPACKER_MISMATCH_CHECK (packer, meta->height, (uint32_t)settings->height);
    request->raw_height = meta->height;
  }
}

static void
gst_dngpacker_task (gpointer userdata)
{
  GstDngPacker *packer = GST_DNGPACKER (userdata);
  GstDataQueueItem *raw_item, *image_item;
  GstBuffer *raw_buf, *img_buf, *out_buf;
  guint8 *image_data = NULL;
  gsize image_data_size = 0;
  GstMapInfo raw_map_info, image_map_info;
  GstVideoMeta *vmeta;
  DngPackRequest request;

  raw_item = image_item = NULL;
  raw_buf = img_buf = NULL;

  // if raw buffer queue is under flushing, return directly
  if (!gst_data_queue_peek (packer->raw_buf_queue, &raw_item))
      return;

  raw_buf = GST_BUFFER (raw_item->object);
  vmeta = gst_buffer_get_video_meta (raw_buf);

  if (vmeta)
    GST_DEBUG_OBJECT (packer, "format=%d flags=%x width=%d height=%d "
        "n_planes=%d stride[0]=%d", vmeta->format, vmeta->flags, vmeta->width,
        vmeta->height, vmeta->n_planes, vmeta->stride[0]);

  if (!gst_buffer_map (raw_buf, &raw_map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (packer, "gst raw buffer map failed");
    goto free_raw_item;
  }

  GST_DEBUG_OBJECT (packer, "mapped raw buffer:data(%zu) size=%ld",
      (size_t) raw_map_info.data, raw_map_info.size);

  if (packer->img_sink_pad) {
    if (gst_data_queue_peek (packer->image_buf_queue, &image_item)) {
      img_buf = GST_BUFFER (image_item->object);

      if (!gst_buffer_map (img_buf, &image_map_info, GST_MAP_READ)) {
        GST_ERROR_OBJECT (packer, "gst image buffer map failed");

        if (gst_data_queue_pop (packer->image_buf_queue, &image_item))
          image_item->destroy (image_item);

        img_buf = NULL;
        image_data = NULL;
        image_data_size = 0;
      } else {
        GST_DEBUG_OBJECT (packer, "mapped image buffer:data(%zu) size=%ld",
            (size_t) image_map_info.data, image_map_info.size);

        image_data = image_map_info.data;
        image_data_size = image_map_info.size;
      }
    } else {
      goto unmap_raw_buf;
    }
  }

  gst_dngpacker_update_packer_request (packer, vmeta, raw_map_info.size,
      raw_map_info.data, image_data_size, image_data, &request);

  if (dngpacker_utils_pack_dng (packer->packer_utils, &request) == 0) {
    out_buf = gst_buffer_new_wrapped (request.output, request.output_size);
    gst_pad_push (packer->dng_src_pad, out_buf);
  } else {
    g_log("GstDngPacker", G_LOG_LEVEL_WARNING,
        "Dng generation failed, please check log for details\n");
  }

  if (img_buf != NULL) {
    gst_buffer_unmap (img_buf, &image_map_info);

    GST_DEBUG_OBJECT (packer, "remove image data queue item");
    if (gst_data_queue_pop (packer->image_buf_queue, &image_item))
      image_item->destroy (image_item);
  }

unmap_raw_buf:
  gst_buffer_unmap (raw_buf, &raw_map_info);

free_raw_item:
  if (gst_data_queue_pop (packer->raw_buf_queue, &raw_item))
    raw_item->destroy (raw_item);

  GST_DNGPACKER_LOCK (packer);
  packer->process_buf_num--;
  if (packer->process_buf_num == 0)
    g_cond_signal (&packer->cond_buf_idle);
  GST_DNGPACKER_UNLOCK (packer);
}

static gboolean
gst_dngpacker_start_task (GstDngPacker * packer)
{
  GST_DNGPACKER_LOCK (packer);

  if (packer->task_active) {
    GST_DNGPACKER_UNLOCK (packer);
    return TRUE;
  }

  packer->task = gst_task_new (gst_dngpacker_task, packer, NULL);
  gst_task_set_lock (packer->task, &packer->task_lock);

  GST_INFO_OBJECT (packer, "Created task %p", packer->task);

  packer->task_active = TRUE;

  GST_DNGPACKER_UNLOCK (packer);

  if (!gst_task_start (packer->task)) {
    GST_ERROR_OBJECT (packer, "Failed to start packing task!");

    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_dngpacker_stop_task (GstDngPacker * packer)
{
  GST_DNGPACKER_LOCK (packer);

  if (!packer->task_active) {
    GST_DNGPACKER_UNLOCK (packer);

    return TRUE;
  }

  GST_INFO_OBJECT (packer, "Stopping task %p", packer->task);

  if (!gst_task_stop (packer->task))
    GST_WARNING_OBJECT (packer, "Failed to stop packing task!");

  packer->task_active = FALSE;

  GST_DNGPACKER_UNLOCK (packer);

  if (!gst_task_join (packer->task)) {
    GST_ERROR_OBJECT (packer, "Failed to join packing task!");

    return FALSE;
  }

  GST_INFO_OBJECT (packer, "Removing task %p", packer->task);

  gst_object_unref (packer->task);
  packer->task = NULL;

  return TRUE;
}

static gboolean
gst_dngpacker_fixate_raw_sink_caps (GstDngPacker *packer, GstCaps * caps)
{
  GstStructure *structure;
  GstRawImageSettings *settings;
  const gchar *format = NULL;
  const gchar *bpp = NULL;
  gboolean ret = TRUE;

  settings = &packer->raw_img_settings;
  structure = gst_caps_get_structure (caps, 0);

  format = gst_structure_get_string (structure, "format");
  if (format != NULL) {
    if (!strcmp(format, "rggb")) {
      settings->cfa = DNGPACKER_CFA_RGGB;
    } else if (!strcmp(format, "bggr")) {
      settings->cfa = DNGPACKER_CFA_BGGR;
    } else if (!strcmp(format, "gbrg")) {
      settings->cfa = DNGPACKER_CFA_GBRG;
    } else if (!strcmp(format, "grbg")) {
      settings->cfa = DNGPACKER_CFA_GRBG;
    } else {
      GST_ERROR_OBJECT (packer, "unsupported CFA pattern %s", format);
      ret = FALSE;
    }
  } else {
    GST_ERROR_OBJECT (packer, "format caps is not set");
    ret = FALSE;
  }

  if (!gst_structure_get_int (structure, "width", &settings->width)) {
    GST_ERROR_OBJECT (packer, "width caps is not set");
    ret = FALSE;
  }

  if (!gst_structure_get_int (structure, "height", &settings->height)) {
    GST_ERROR_OBJECT (packer, "height caps is not set");
    ret = FALSE;
  }

  bpp = gst_structure_get_string (structure, "bpp");
  if (bpp != NULL) {
    if (!strcmp (bpp, "8"))
      settings->bpp = 8;
    else if (!strcmp (bpp, "10"))
      settings->bpp = 10;
    else if (!strcmp (bpp, "12"))
      settings->bpp = 12;
    else if (!strcmp (bpp, "16"))
      settings->bpp = 16;
    else {
      GST_ERROR_OBJECT (packer, "invalid bpp (%s)", bpp);
      ret = FALSE;
    }
  } else {
    GST_ERROR_OBJECT (packer, "bpp caps is not set");
    ret = FALSE;
  }

  if (!gst_structure_get_int (structure, "stride", &settings->stride) ||
      settings->stride == 0) {
    GST_ERROR_OBJECT (packer, "stride caps is not set or invalid");
    ret = FALSE;
  }

  if (ret == TRUE)
    GST_DEBUG_OBJECT (packer, "caps update: CFA(%d), bpp(%d),"
        " width(%d), height(%d), stride(%d)",
        settings->cfa, settings->bpp, settings->width,
        settings->height, settings->stride);

  return ret;
}

static gboolean
gst_dngpacker_raw_sink_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstDngPacker *packer= GST_DNGPACKER (parent);

  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL, *tmplcaps = NULL, *intersect = NULL;

      gst_event_parse_caps (event, &caps);

      GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

      // Get the negotiated caps between the srcpad and its peer.
      tmplcaps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (pad, "Template caps %" GST_PTR_FORMAT, tmplcaps);

      intersect = gst_caps_intersect (tmplcaps, caps);
      GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

      gst_caps_unref (tmplcaps);

      if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
        GST_ERROR_OBJECT (pad, "Template and sink caps do not intersect!");

        if (intersect != NULL)
          gst_caps_unref (intersect);

        return FALSE;
      }

      if (gst_dngpacker_fixate_raw_sink_caps (packer, intersect) != TRUE)
        return FALSE;

      break;
    }
    case GST_EVENT_EOS:
    {
      GST_DNGPACKER_LOCK (packer);
      while (packer->process_buf_num != 0)
        g_cond_wait (&packer->cond_buf_idle, GST_DNGPACKER_GET_LOCK (packer));
      GST_DNGPACKER_UNLOCK (packer);
      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_dngpacker_raw_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstDngPacker *packer= GST_DNGPACKER (parent);
  GstDataQueueItem *item = NULL;

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, buffer);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (packer->raw_buf_queue, item)) {
    GST_ERROR_OBJECT (packer, "RAW Pad push failed");
    item->destroy (item);
  } else {
    GST_DNGPACKER_LOCK (packer);
    packer->process_buf_num++;
    GST_DNGPACKER_UNLOCK (packer);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_dngpacker_image_sink_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL, *tmplcaps = NULL, *intersect = NULL;

      gst_event_parse_caps (event, &caps);
      gst_event_unref (event);

      GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

      // Get the negotiated caps between the srcpad and its peer.
      tmplcaps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (pad, "Template caps %" GST_PTR_FORMAT, tmplcaps);

      intersect = gst_caps_intersect (tmplcaps, caps);
      GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

      gst_caps_unref (tmplcaps);

      if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
        GST_ERROR_OBJECT (pad, "Template and sink caps do not intersect!");

        if (intersect != NULL)
          gst_caps_unref (intersect);

        return FALSE;
      }

      return TRUE;
    }
    case GST_EVENT_FLUSH_START:
    {
      //TODO: set image queue to be flashed

      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_SEGMENT:
    case GST_EVENT_GAP:
    case GST_EVENT_STREAM_START:
      // Drop the event, those events are forwarded by the main sink pad.
      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_dngpacker_image_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstDngPacker *packer= GST_DNGPACKER (parent);
  GstDataQueueItem *item = NULL;

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, buffer);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (packer->image_buf_queue, item)) {
    GST_ERROR_OBJECT (packer, "RAW Pad push failed");
    item->destroy (item);
  }

  return GST_FLOW_OK;
}

static GstPad*
gst_dngpacker_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstDngPacker *packer = GST_DNGPACKER(element);
  GstPad *pad = NULL;

  GST_DEBUG_OBJECT (packer, "Request Pad: %s", reqname);

  GST_DNGPACKER_LOCK (packer);

  if (packer->img_sink_pad != NULL) {
    GST_ERROR_OBJECT (packer, "Image pad has already been created");
    GST_DNGPACKER_UNLOCK (packer);

    return NULL;
  }

  pad = g_object_new (GST_TYPE_PAD, "name", reqname, "direction",
      templ->direction, "template", templ, NULL);

  if (pad == NULL) {
    GST_ERROR_OBJECT (packer, "Failed to create sink pad!");
    GST_DNGPACKER_UNLOCK (packer);
    return NULL;
  }

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_dngpacker_image_sink_pad_event));
  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_dngpacker_image_sink_pad_chain));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (packer, "Failed to add sink pad!");
    gst_object_unref (pad);
    GST_DNGPACKER_UNLOCK (packer);
    return NULL;
  }

  packer->img_sink_pad = pad;

  GST_DNGPACKER_UNLOCK (packer);

  GST_DEBUG_OBJECT (packer, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_dngpacker_release_pad (GstElement * element, GstPad * pad)
{
  GstDngPacker *packer = GST_DNGPACKER(element);

  GST_DEBUG_OBJECT (packer, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_DNGPACKER_LOCK (packer);
  packer->img_sink_pad = NULL;
  GST_DNGPACKER_UNLOCK (packer);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_dngpacker_change_state (GstElement * element, GstStateChange transition)
{
  GstDngPacker *packer = GST_DNGPACKER(element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_data_queue_set_flushing (packer->raw_buf_queue, FALSE);
      gst_data_queue_set_flushing (packer->image_buf_queue, FALSE);
      gst_dngpacker_start_task (packer);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_data_queue_set_flushing (packer->raw_buf_queue, TRUE);
      gst_data_queue_set_flushing (packer->image_buf_queue, TRUE);
      gst_data_queue_flush (packer->raw_buf_queue);
      gst_data_queue_flush (packer->image_buf_queue);
      gst_dngpacker_stop_task (packer);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_dngpacker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstDngPacker *packer = GST_DNGPACKER (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dngpacker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstDngPacker *packer = GST_DNGPACKER (object); */

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dngpacker_finalize (GObject * object)
{
  GstDngPacker *packer = GST_DNGPACKER (object);

  g_rec_mutex_clear (&packer->task_lock);

  g_mutex_clear (&packer->lock);
  g_cond_clear (&packer->cond_buf_idle);

  gst_object_unref (packer->raw_buf_queue);
  gst_object_unref (packer->image_buf_queue);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (packer));
}

static void
gst_dngpacker_class_init (GstDngPackerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GstElementClass *eclass = GST_ELEMENT_CLASS (klass);

  oclass->set_property = GST_DEBUG_FUNCPTR (gst_dngpacker_set_property);
  oclass->get_property = GST_DEBUG_FUNCPTR (gst_dngpacker_get_property);
  oclass->finalize     = GST_DEBUG_FUNCPTR (gst_dngpacker_finalize);

  gst_element_class_add_static_pad_template (eclass,
      &gst_dngpacker_raw_sink_template);
  gst_element_class_add_static_pad_template (eclass,
      &gst_dngpacker_image_sink_template);
  gst_element_class_add_static_pad_template (eclass,
      &gst_dngpacker_src_template);

  gst_element_class_set_static_metadata (eclass,
      "DNG Packer", "RAW to DNG Packer",
      "Pack MIPI CSI2 RAW Image into DNG with JPEG as thumbnail", "QTI"
  );

  eclass->request_new_pad = GST_DEBUG_FUNCPTR (gst_dngpacker_request_pad);
  eclass->release_pad = GST_DEBUG_FUNCPTR (gst_dngpacker_release_pad);
  eclass->change_state = GST_DEBUG_FUNCPTR (gst_dngpacker_change_state);

  // Initializes a new packer GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_dngpacker_debug, "qtidngpacker", 0, "QTI Dng Packer");
}

static void
gst_dngpacker_init (GstDngPacker * packer)
{
  GstRawImageSettings *settings = &packer->raw_img_settings;

  g_mutex_init (&packer->lock);

  packer->raw_sink_pad = NULL;
  packer->img_sink_pad = NULL;
  packer->dng_src_pad = NULL;

  g_cond_init (&packer->cond_buf_idle);
  packer->process_buf_num = 0;

  settings->cfa = DNGPACKER_CFA_UNKNOWN;
  settings->bpp = 0;
  settings->width = 0;
  settings->height = 0;
  settings->stride = 0;

  packer->task = NULL;
  g_rec_mutex_init (&packer->task_lock);
  packer->task_active = FALSE;

  // create raw sink pad
  packer->raw_sink_pad = gst_pad_new_from_static_template (
      &gst_dngpacker_raw_sink_template, "raw_sink");
  if (packer->raw_sink_pad != NULL) {
    gst_pad_set_event_function (GST_PAD (packer->raw_sink_pad),
        GST_DEBUG_FUNCPTR (gst_dngpacker_raw_sink_pad_event));
    gst_pad_set_chain_function (GST_PAD (packer->raw_sink_pad),
        GST_DEBUG_FUNCPTR (gst_dngpacker_raw_sink_pad_chain));
    gst_element_add_pad (GST_ELEMENT (packer), packer->raw_sink_pad);

    GST_DEBUG_OBJECT (packer, "create raw sink pad OK");
  } else {
    GST_ERROR_OBJECT (packer, "create raw sink pad failed");
  }

  // create source pad
  packer->dng_src_pad = gst_pad_new_from_static_template (
      &gst_dngpacker_src_template, "dng_src");
  if (packer->dng_src_pad != NULL) {
    gst_element_add_pad (GST_ELEMENT (packer), GST_PAD (packer->dng_src_pad));

    GST_DEBUG_OBJECT (packer, "create dng source pad OK");
  } else {
    GST_DEBUG_OBJECT (packer, "create dng source pad failed");
  }

  packer->raw_buf_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, packer);

  packer->image_buf_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, queue_empty_cb, packer);

  dngpacker_utils_register_error_cb (gst_dngpacker_utils_error_callback);
  packer->packer_utils =
      dngpacker_utils_init (gst_dngpacker_utils_log_callback, (void *) (packer));
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtidngpacker", GST_RANK_NONE,
      GST_TYPE_DNGPACKER);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtidngpacker,
    "QTI Dng Packer",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
