/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/allocators/gstfdmemory.h>
#include <ml-meta/ml_meta.h>

#include "gstoverlay.h"

#define GST_CAT_DEFAULT overlay_debug
GST_DEBUG_CATEGORY_STATIC (overlay_debug);

#define gst_overlay_parent_class parent_class
G_DEFINE_TYPE (GstOverlay, gst_overlay, GST_TYPE_VIDEO_FILTER);

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767]"

#define GST_VIDEO_FORMATS "{ NV12, NV21 }"

#define DEFAULT_PROP_OVERLAY_BBOX_COLOR  kColorBlue
#define DEFAULT_PROP_OVERLAY_DATE_COLOR  kColorRed
#define DEFAULT_PROP_OVERLAY_TEXT_COLOR  kColorYellow

#define GST_OVERLAY_UNUSED(var) ((void)var)

enum {
  PROP_0,
  PROP_OVERLAY_BBOX_COLOR,
  PROP_OVERLAY_DATE_COLOR,
  PROP_OVERLAY_TEXT_COLOR
};

static GstStaticCaps gst_overlay_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS));

/**
 * GstOverlayMetaApplyFunc:
 * @gst_overlay: context
 * @meta: metadata payload
 * @item_id: overlay item unique id
 *
 * Function called when overlay is configured by metadata.
 */
typedef gboolean (*GstOverlayMetaApplyFunc)
    (GstOverlay *gst_overlay, gpointer meta, uint32_t * item_id);


static void
gst_overlay_destroy_overlay_item (gpointer data, gpointer user_data)
{
  uint32_t *item_id = (uint32_t *) data;
  GstOverlay *gst_overlay = (GstOverlay *) user_data;

  int32_t ret = gst_overlay->overlay->DisableOverlayItem (*item_id);
  if (ret != 0) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay disable failed!");
  }

  ret = gst_overlay->overlay->DeleteOverlayItem (*item_id);
  if (ret != 0) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay delete failed!");
  }

  *item_id = 0;
}

static gboolean gst_overlay_apply_item_list (GstOverlay *gst_overlay,
  GSList * meta_list, GstOverlayMetaApplyFunc apply_func, GSequence * ov_id)
{
  gboolean res = TRUE;

  guint meta_num = g_slist_length (meta_list);
  if (meta_num) {
    for (uint32_t i = g_sequence_get_length (ov_id);
        i < meta_num; i++) {
      g_sequence_append(ov_id, calloc(1, sizeof(uint32_t)));
    }

    for (uint32_t i = 0; i < meta_num; i++) {
      res = apply_func (gst_overlay, g_slist_nth_data (meta_list, 0),
          (uint32_t *) g_sequence_get (g_sequence_get_iter_at_pos (ov_id, i)));
      if (!res) {
        GST_ERROR_OBJECT (gst_overlay, "Overlay create failed!");
        return res;
      }
      meta_list = meta_list->next;
    }
  }
  if ((guint) g_sequence_get_length (ov_id) > meta_num) {
    g_sequence_foreach_range (
        g_sequence_get_iter_at_pos (ov_id, meta_num),
        g_sequence_get_end_iter (ov_id),
        gst_overlay_destroy_overlay_item, gst_overlay);
    g_sequence_remove_range (
        g_sequence_get_iter_at_pos (ov_id, meta_num),
        g_sequence_get_end_iter (ov_id));
  }

  return TRUE;
}

static gboolean
gst_overlay_apply_bbox_item (GstOverlay * gst_overlay, gpointer metadata,
    uint32_t * item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (item_id != NULL, FALSE);
  g_return_val_if_fail (metadata != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  GstMLDetectionMeta * meta = (GstMLDetectionMeta *) metadata;
  GstMLClassificationResult * result =
      (GstMLClassificationResult *) g_slist_nth_data (meta->box_info, 0);

  if (!(*item_id)) {
    ov_param = {};
    ov_param.type = OverlayType::kBoundingBox;
    ov_param.location = OverlayLocationType::kTopLeft;
    ov_param.color = gst_overlay->bbox_color;
    ov_param.dst_rect.start_x = meta->bounding_box.x;
    ov_param.dst_rect.start_y = meta->bounding_box.y;
    ov_param.dst_rect.width = meta->bounding_box.width;
    ov_param.dst_rect.height = meta->bounding_box.height;

    if (sizeof (ov_param.bounding_box.box_name) < strlen (result->name)) {
      GST_ERROR_OBJECT (gst_overlay, "Text size exceeded %d < %d",
        sizeof (ov_param.bounding_box.box_name), strlen (result->name));
      return FALSE;
    }
    g_strlcpy (ov_param.bounding_box.box_name, result->name,
        sizeof (ov_param.bounding_box.box_name));

    ret = gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay create failed! ret: %d", ret);
      return FALSE;
    }

    ret = gst_overlay->overlay->EnableOverlayItem (*item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay enable failed! ret: %d", ret);
      return FALSE;
    }
  } else {
    ret = gst_overlay->overlay->GetOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay get param failed! ret: %d", ret);
      return FALSE;
    }

    ov_param.dst_rect.start_x = meta->bounding_box.x;
    ov_param.dst_rect.start_y = meta->bounding_box.y;
    ov_param.dst_rect.width = meta->bounding_box.width;
    ov_param.dst_rect.height = meta->bounding_box.height;

    if (sizeof (ov_param.bounding_box.box_name) < strlen (result->name)) {
      GST_ERROR_OBJECT (gst_overlay, "Text size exceeded %d < %d",
        sizeof (ov_param.bounding_box.box_name), strlen (result->name));
      return FALSE;
    }
    g_strlcpy (ov_param.bounding_box.box_name, result->name,
        sizeof (ov_param.bounding_box.box_name));

    ret = gst_overlay->overlay->UpdateOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay set param failed! ret: %d", ret);
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean
gst_overlay_apply_simg_item (GstOverlay *gst_overlay, gpointer metadata,
    uint32_t *item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (item_id != NULL, FALSE);
  g_return_val_if_fail (metadata != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  GstMLSegmentationMeta *meta = (GstMLSegmentationMeta *) metadata;

  if (!(*item_id)) {
    ov_param = {};
    ov_param.type = OverlayType::kStaticImage;
    ov_param.location = OverlayLocationType::kRandom;
    ov_param.dst_rect.start_x = 0;
    ov_param.dst_rect.start_y = 0;
    ov_param.dst_rect.width = gst_overlay->width;
    ov_param.dst_rect.height = gst_overlay->height;
    ov_param.image_info.image_type = OverlayImageType::kBlobType;
    ov_param.image_info.source_rect.start_x = 0;
    ov_param.image_info.source_rect.start_y = 0;
    ov_param.image_info.source_rect.width = meta->img_width;
    ov_param.image_info.source_rect.height = meta->img_height;
    ov_param.image_info.image_buffer = (char *)meta->img_buffer;
    ov_param.image_info.image_size = meta->img_size;
    ov_param.image_info.buffer_updated = true;

    ret = gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay create failed! ret: %d", ret);
      return FALSE;
    }

    ret = gst_overlay->overlay->EnableOverlayItem (*item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay enable failed! ret: %d", ret);
      return FALSE;
    }
  } else {
    ret = gst_overlay->overlay->GetOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay get param failed! ret: %d", ret);
      return FALSE;
    }

    ov_param.dst_rect.start_x = 0;
    ov_param.dst_rect.start_y = 0;
    ov_param.dst_rect.width = gst_overlay->width;
    ov_param.dst_rect.height = gst_overlay->height;
    ov_param.image_info.source_rect.start_x = 0;
    ov_param.image_info.source_rect.start_y = 0;
    ov_param.image_info.source_rect.width = meta->img_width;
    ov_param.image_info.source_rect.height = meta->img_height;
    ov_param.image_info.image_buffer = (char *)meta->img_buffer;
    ov_param.image_info.image_size = meta->img_size;
    ov_param.image_info.buffer_updated = true;

    ret = gst_overlay->overlay->UpdateOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay set param failed! ret: %d", ret);
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean
gst_overlay_apply_text_item (GstOverlay *gst_overlay, gpointer metadata,
  uint32_t * item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (item_id != NULL, FALSE);
  g_return_val_if_fail (metadata != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  GstMLClassificationMeta * meta = (GstMLClassificationMeta *) metadata;

  if (!(*item_id)) {
    ov_param = {};
    ov_param.type = OverlayType::kUserText;
    ov_param.color = gst_overlay->text_color;

    switch (meta->location) {
      case GstOverlayLocationType::TOP_LEFT:
        ov_param.location = OverlayLocationType::kTopLeft;
        break;
      case GstOverlayLocationType::TOP_RIGHT:
        ov_param.location = OverlayLocationType::kTopRight;
        break;
      case GstOverlayLocationType::CENTER:
        ov_param.location = OverlayLocationType::kCenter;
        break;
      case GstOverlayLocationType::BOTTOM_LEFT:
        ov_param.location = OverlayLocationType::kBottomLeft;
        break;
      case GstOverlayLocationType::BOTTOM_RIGHT:
        ov_param.location = OverlayLocationType::kBottomRight;
        break;
      case GstOverlayLocationType::CUSTOM:
      default:
        GST_ERROR_OBJECT (gst_overlay,
            "location: %d not supported. Set to Top Left", meta->location);
        ov_param.location = OverlayLocationType::kTopLeft;
        break;
    }

    if (sizeof (ov_param.bounding_box.box_name) < strlen (meta->result.name)) {
      GST_ERROR_OBJECT (gst_overlay, "Text size exceeded %d < %d",
        sizeof (ov_param.bounding_box.box_name), strlen (meta->result.name));
      return FALSE;
    }
    g_strlcpy (ov_param.user_text, meta->result.name,
        sizeof (ov_param.user_text));

    ret = gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay create failed! ret: %d", ret);
      return FALSE;
    }

    ret = gst_overlay->overlay->EnableOverlayItem (*item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay enable failed! ret: %d", ret);
      return FALSE;
    }
  } else {
    ret = gst_overlay->overlay->GetOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay get param failed! ret: %d", ret);
      return FALSE;
    }

    if (sizeof (ov_param.bounding_box.box_name) < strlen (meta->result.name)) {
      GST_ERROR_OBJECT (gst_overlay, "Text size exceeded %d < %d",
        sizeof (ov_param.bounding_box.box_name), strlen (meta->result.name));
      return FALSE;
    }
    g_strlcpy (ov_param.user_text, meta->result.name,
        sizeof (ov_param.user_text));

    ret = gst_overlay->overlay->UpdateOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay set param failed! ret: %d", ret);
      return FALSE;
    }
  }
  return TRUE;
}

static gboolean
gst_overlay_apply_overlay (GstOverlay *gst_overlay, GstVideoFrame *frame)
{
  int32_t ret;
  GstMemory *memory = gst_buffer_peek_memory (frame->buffer, 0);
  guint fd = gst_fd_memory_get_fd (memory);

  OverlayTargetBuffer overlay_buf;
  overlay_buf.width     = GST_VIDEO_FRAME_WIDTH (frame);
  overlay_buf.height    = GST_VIDEO_FRAME_HEIGHT (frame);
  overlay_buf.ion_fd    = fd;
  overlay_buf.frame_len = GST_VIDEO_FRAME_SIZE (frame);
  overlay_buf.format    = gst_overlay->format;

  ret = gst_overlay->overlay->ApplyOverlay (overlay_buf);
  if (ret != 0) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay apply failed!");
    return FALSE;
  }
  return TRUE;
}

static GstCaps *
gst_overlay_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;
  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_overlay_format_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_overlay_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_overlay_caps ());
}

static GstPadTemplate *
gst_overlay_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_overlay_caps ());
}

static void
gst_overlay_finalize (GObject * object)
{
  GstOverlay *gst_overlay = GST_OVERLAY (object);

  if (gst_overlay->overlay) {
    g_sequence_foreach (gst_overlay->bbox_id, gst_overlay_destroy_overlay_item,
        gst_overlay);
    g_sequence_free (gst_overlay->bbox_id);

    g_sequence_foreach (gst_overlay->simg_id, gst_overlay_destroy_overlay_item,
        gst_overlay);
    g_sequence_free (gst_overlay->simg_id);

    g_sequence_foreach (gst_overlay->text_id, gst_overlay_destroy_overlay_item,
        gst_overlay);
    g_sequence_free (gst_overlay->text_id);

    delete (gst_overlay->overlay);
    gst_overlay->overlay = nullptr;
  }

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (gst_overlay));
}

static void
gst_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOverlay *gst_overlay = GST_OVERLAY (object);

  GST_OBJECT_LOCK (gst_overlay);
  switch (prop_id) {
    case PROP_OVERLAY_BBOX_COLOR:
      gst_overlay->bbox_color = g_value_get_uint (value);
      break;
    case PROP_OVERLAY_DATE_COLOR:
      gst_overlay->date_color = g_value_get_uint (value);
      break;
    case PROP_OVERLAY_TEXT_COLOR:
      gst_overlay->text_color = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (gst_overlay);
}

static void
gst_overlay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOverlay *gst_overlay = GST_OVERLAY (object);

  GST_OBJECT_LOCK (gst_overlay);
  switch (prop_id) {
    case PROP_OVERLAY_BBOX_COLOR:
      g_value_set_uint (value, gst_overlay->bbox_color);
      break;
    case PROP_OVERLAY_DATE_COLOR:
      g_value_set_uint (value, gst_overlay->date_color);
      break;
    case PROP_OVERLAY_TEXT_COLOR:
      g_value_set_uint (value, gst_overlay->text_color);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (gst_overlay);
}

static gboolean
gst_overlay_set_info (GstVideoFilter * filter, GstCaps * in,
    GstVideoInfo * ininfo, GstCaps * out, GstVideoInfo * outinfo)
{
  GstOverlay *gst_overlay = GST_OVERLAY (filter);
  TargetBufferFormat  new_format;

  GST_OVERLAY_UNUSED(in);
  GST_OVERLAY_UNUSED(out);
  GST_OVERLAY_UNUSED(outinfo);

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);

  gst_overlay->width = GST_VIDEO_INFO_WIDTH (ininfo);
  gst_overlay->height = GST_VIDEO_INFO_HEIGHT (ininfo);

  switch (GST_VIDEO_INFO_FORMAT(ininfo)) { // GstVideoFormat
    case GST_VIDEO_FORMAT_NV12:
      new_format = TargetBufferFormat::kYUVNV12;
      break;
    case GST_VIDEO_FORMAT_NV21:
      new_format = TargetBufferFormat::kYUVNV21;
      break;
    default:
      GST_ERROR_OBJECT (gst_overlay, "Unhandled gst format: %d",
        GST_VIDEO_INFO_FORMAT (ininfo));
      return FALSE;
  }

  if (gst_overlay->overlay && gst_overlay->format == new_format) {
    GST_DEBUG_OBJECT (gst_overlay, "Overlay already initialized");
    return TRUE;
  }

  if (gst_overlay->overlay) {
    delete (gst_overlay->overlay);
  }

  gst_overlay->format = new_format;
  gst_overlay->overlay = new Overlay();

  int32_t ret = gst_overlay->overlay->Init (gst_overlay->format);
  if (ret != 0) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay init failed! Format: %u",
        (guint)gst_overlay->format);
    delete (gst_overlay->overlay);
    gst_overlay->overlay = nullptr;
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_overlay_transform_frame_ip (GstVideoFilter *filter, GstVideoFrame *frame)
{
  GstOverlay *gst_overlay = GST_OVERLAY_CAST (filter);
  gboolean res = TRUE;

  GST_DEBUG_OBJECT (gst_overlay,
    "process id %d dim %dx%d, stride %d size %d flags 0x%x", frame->id,
    frame->info.width, frame->info.height, frame->info.stride[0],
    frame->info.size, frame->flags);

  if (!gst_overlay->overlay) {
    GST_ERROR_OBJECT (gst_overlay, "failed: overlay not initialized");
    return GST_FLOW_ERROR;
  }

  res = gst_overlay_apply_item_list (gst_overlay,
                            gst_buffer_get_detection_meta (frame->buffer),
                            gst_overlay_apply_bbox_item,
                            gst_overlay->bbox_id);
  if (!res) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay apply bbox item list failed!");
    return GST_FLOW_ERROR;
  }

  res = gst_overlay_apply_item_list (gst_overlay,
                            gst_buffer_get_segmentation_meta (frame->buffer),
                            gst_overlay_apply_simg_item,
                            gst_overlay->simg_id);
  if (!res) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay apply image item list failed!");
    return GST_FLOW_ERROR;
  }

  res = gst_overlay_apply_item_list (gst_overlay,
                            gst_buffer_get_classification_meta (frame->buffer),
                            gst_overlay_apply_text_item,
                            gst_overlay->text_id);
  if (!res) {
    GST_ERROR_OBJECT (gst_overlay,
        "Overlay apply classification item list failed!");
    return GST_FLOW_ERROR;
  }

  if (!g_sequence_is_empty (gst_overlay->bbox_id) ||
      !g_sequence_is_empty (gst_overlay->simg_id) ||
      !g_sequence_is_empty (gst_overlay->text_id)) {
    res = gst_overlay_apply_overlay (gst_overlay, frame);
    if (!res) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay apply failed!");
      return GST_FLOW_ERROR;
    }
  }
  return GST_FLOW_OK;
}

static void
gst_overlay_class_init (GstOverlayClass * klass)
{
  GObjectClass *gobject            = G_OBJECT_CLASS (klass);
  GstElementClass *element         = GST_ELEMENT_CLASS (klass);
  GstVideoFilterClass *filter      = GST_VIDEO_FILTER_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_overlay_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_overlay_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_overlay_finalize);

  g_object_class_install_property (gobject, PROP_OVERLAY_BBOX_COLOR,
    g_param_spec_uint ("bbox-color", "BBox color", "Bounding box overlay color",
      0, G_MAXUINT, DEFAULT_PROP_OVERLAY_BBOX_COLOR, static_cast<GParamFlags>(
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_OVERLAY_DATE_COLOR,
    g_param_spec_uint ("date-color", "Date color", "Date overlay color",
      0, G_MAXUINT, DEFAULT_PROP_OVERLAY_DATE_COLOR, static_cast<GParamFlags>(
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_OVERLAY_TEXT_COLOR,
    g_param_spec_uint ("text-color", "Text color", "Text overlay color",
      0, G_MAXUINT, DEFAULT_PROP_OVERLAY_TEXT_COLOR, static_cast<GParamFlags>(
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element, "QTI Overlay", "Overlay",
      "Apply image, bounding boxes and text overlay.", "QTI");

  gst_element_class_add_pad_template (element, gst_overlay_sink_template ());
  gst_element_class_add_pad_template (element, gst_overlay_src_template ());

  filter->set_info = GST_DEBUG_FUNCPTR (gst_overlay_set_info);
  filter->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_overlay_transform_frame_ip);
}

static void
gst_overlay_init (GstOverlay * gst_overlay)
{
  gst_overlay->overlay = nullptr;

  gst_overlay->bbox_id = g_sequence_new (free);
  gst_overlay->simg_id = g_sequence_new (free);
  gst_overlay->text_id = g_sequence_new (free);

  gst_overlay->bbox_color = DEFAULT_PROP_OVERLAY_BBOX_COLOR;
  gst_overlay->date_color = DEFAULT_PROP_OVERLAY_DATE_COLOR;
  gst_overlay->text_color = DEFAULT_PROP_OVERLAY_TEXT_COLOR;

  GST_DEBUG_CATEGORY_INIT (overlay_debug, "qtioverlay", 0, "QTI overlay");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtioverlay", GST_RANK_PRIMARY,
          GST_TYPE_OVERLAY);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtioverlay,
    "QTI Overlay. Supports image, bounding boxes and text overlay.",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
