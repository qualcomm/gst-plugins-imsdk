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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "videocomposersinkpad.h"

#include <gst/video/video-utils.h>
#include <gst/utils/common-utils.h>

GST_DEBUG_CATEGORY_STATIC (gst_video_composer_sinkpad_debug);
#define GST_CAT_DEFAULT gst_video_composer_sinkpad_debug

#define DEFAULT_PROP_Z_ORDER            (-1)
#define DEFAULT_PROP_CROP_X             0
#define DEFAULT_PROP_CROP_Y             0
#define DEFAULT_PROP_CROP_WIDTH         0
#define DEFAULT_PROP_CROP_HEIGHT        0
#define DEFAULT_PROP_DESTINATION_X      0
#define DEFAULT_PROP_DESTINATION_Y      0
#define DEFAULT_PROP_DESTINATION_WIDTH  0
#define DEFAULT_PROP_DESTINATION_HEIGHT 0
#define DEFAULT_PROP_ALPHA              1.0
#define DEFAULT_PROP_FLIP_HORIZONTAL    FALSE
#define DEFAULT_PROP_FLIP_VERTICAL      FALSE
#define DEFAULT_PROP_ROTATE             GST_VIDEO_COMPOSER_ROTATE_NONE

G_DEFINE_TYPE (GstVideoComposerSinkPad, gst_video_composer_sinkpad,
               GST_TYPE_VIDEO_AGGREGATOR_PAD);

enum
{
  PROP_0,
  PROP_Z_ORDER,
  PROP_CROP,
  PROP_POSITION,
  PROP_DIMENSIONS,
  PROP_ALPHA,
  PROP_FLIP_HORIZONTAL,
  PROP_FLIP_VERTICAL,
  PROP_ROTATE,
};

static GstCaps *
gst_video_composer_sinkpad_transform_caps (GstAggregatorPad * pad,
    GstCaps * caps, GstCaps * filter)
{
  GstCaps *result = NULL;
  GstStructure *structure = NULL;
  GstCapsFeatures *features = NULL;
  gint idx = 0, length = 0;

  GST_DEBUG_OBJECT (pad, "Transforming caps %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (pad, "Filter caps %" GST_PTR_FORMAT, filter);

  result = gst_caps_new_empty ();

  // In case there is no featureless or memory:GBM caps structure add one.
  if (gst_gbm_qcom_backend_is_supported () && !gst_caps_is_empty (caps) &&
      !gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
    structure = gst_caps_get_structure (caps, 0);
    features = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL);

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Set width and height to a range instead of fixed value.
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    // If pixel aspect ratio, make a range of it.
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    // Remove the format/color/compression related fields.
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", "compression", NULL);

    gst_caps_append_structure_full (result, structure, features);
  } else if (!gst_caps_is_empty (caps) && !gst_caps_has_feature (caps, NULL)) {
    structure = gst_caps_get_structure (caps, 0);

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Set width and height to a range instead of fixed value.
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    // If pixel aspect ratio, make a range of it.
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    // Remove the format/color/compression related fields.
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", "compression", NULL);

    gst_caps_append_structure (result, structure);
  }

  length = gst_caps_get_size (caps);

  for (idx = 0; idx < length; idx++) {
    structure = gst_caps_get_structure (caps, idx);
    features = gst_caps_get_features (caps, idx);

    // If this is already expressed by the existing caps skip this structure.
    if (idx > 0 && gst_caps_is_subset_structure_full (result, structure, features))
      continue;

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // Set width, height and framerate to a range instead of fixed value.
    gst_structure_set (structure, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT16,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT16, "framerate",
        GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXUINT8, 1, NULL);

    // If pixel aspect ratio field exists, make a range of it.
    if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
      gst_structure_set (structure, "pixel-aspect-ratio",
          GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    // Remove the format/color related fields.
    gst_structure_remove_fields (structure, "format", "colorimetry",
        "chroma-site", NULL);

    gst_caps_append_structure_full (result, structure,
        gst_caps_features_copy (features));
  }

  if (filter) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

gboolean
gst_video_composer_sinkpad_acceptcaps (GstAggregatorPad * pad,
    GstAggregator * aggregator, GstCaps * caps)
{
  GstPad *srcpad = GST_AGGREGATOR_SRC_PAD (aggregator);
  GstCaps *tmplcaps = NULL, *sinkcaps = NULL;
  gboolean success = FALSE;

  GST_DEBUG_OBJECT (pad, "Caps %" GST_PTR_FORMAT, caps);

  tmplcaps = gst_pad_get_pad_template_caps (GST_PAD (pad));
  GST_DEBUG_OBJECT (pad, "Template: %" GST_PTR_FORMAT, tmplcaps);

  success = gst_caps_can_intersect (caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Caps can't intersect with pad template!");
    return FALSE;
  }

  // Use currently set caps if they are set otherwise use template caps.
  tmplcaps = gst_pad_get_pad_template_caps (srcpad);

  GST_DEBUG_OBJECT (pad, "Trying to transform with source template as filter:"
      " %" GST_PTR_FORMAT, tmplcaps);

  sinkcaps = gst_video_composer_sinkpad_transform_caps (pad, caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  success = (sinkcaps != NULL) && !gst_caps_is_empty (sinkcaps);

  if (sinkcaps != NULL)
    gst_caps_unref (sinkcaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Failed to transform %" GST_PTR_FORMAT
        " in anything supported by the pad!", caps);
    return FALSE;
  }

  return TRUE;
}

GstCaps *
gst_video_composer_sinkpad_getcaps (GstAggregatorPad * pad,
    GstAggregator * aggregator, GstCaps * filter)
{
  GstPad *srcpad = GST_AGGREGATOR_SRC_PAD (aggregator);
  GstCaps *srccaps = NULL, *sinkcaps = NULL, *peercaps = NULL;
  GstCaps *tmpcaps = NULL, *tmplcaps = NULL, *intersect = NULL;

  // Use currently set caps if they are set otherwise use template caps.
  srccaps = gst_pad_get_pad_template_caps (srcpad);

  if (filter != NULL) {
    GST_DEBUG_OBJECT (pad, "Filter caps  %" GST_PTR_FORMAT, filter);

    tmplcaps = gst_pad_get_pad_template_caps (GST_PAD (pad));
    GST_DEBUG_OBJECT (pad, "Sink template caps %" GST_PTR_FORMAT, tmplcaps);

    // Intersect filter caps with the sink pad template.
    intersect =
        gst_caps_intersect_full (filter, tmplcaps, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

    gst_caps_unref (tmplcaps);

    // Check whether the intersected caps can be transformed.
    tmpcaps =
        gst_video_composer_sinkpad_transform_caps (pad, intersect, NULL);
    GST_DEBUG_OBJECT (pad, "Transformed caps  %" GST_PTR_FORMAT, tmpcaps);

    gst_caps_unref (intersect);

    // If transformed caps are not empty intersect them with the source caps.
    if (tmpcaps && !gst_caps_is_empty (tmpcaps)) {
      GST_DEBUG_OBJECT (pad, "Source caps  %" GST_PTR_FORMAT, srccaps);

      // Intersect with source pad caps.
      intersect = gst_caps_intersect_full (tmpcaps, srccaps,
          GST_CAPS_INTERSECT_FIRST);

      gst_caps_unref (tmpcaps);
      tmpcaps = intersect;
    }
  }

  GST_DEBUG_OBJECT (pad, "Peer filter caps %" GST_PTR_FORMAT, tmpcaps);

  if ((tmpcaps != NULL) && gst_caps_is_empty (tmpcaps)) {
    GST_WARNING_OBJECT (pad, "Peer filter caps are empty!");
    gst_caps_unref (srccaps);
    return tmpcaps;
  }

  // Query the source pad peer with the transformed filter.
  peercaps = gst_pad_peer_query_caps (srcpad, tmpcaps);

  if (tmpcaps != NULL)
    gst_caps_unref (tmpcaps);

  if (peercaps != NULL) {
    GST_DEBUG_OBJECT (pad, "Peer caps  %" GST_PTR_FORMAT, peercaps);

    // Filter the peer caps against the source pad caps.
    tmpcaps =
        gst_caps_intersect_full (peercaps, srccaps, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, tmpcaps);

    gst_caps_unref (srccaps);
    gst_caps_unref (peercaps);
  } else {
    tmpcaps = srccaps;
  }

  // Check whether the intersected sink caps can be transformed.
  sinkcaps = gst_video_composer_sinkpad_transform_caps (pad, tmpcaps, filter);
  gst_caps_unref (tmpcaps);

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, sinkcaps);
  return sinkcaps;
}
#if (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR < 16)
static gboolean
gst_video_composer_sinkpad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vaggregator)
{
  GstVideoFrame *frame = NULL;

  if (pad->buffer == NULL)
    return TRUE;

  // GAP event, nothing to do.
  if (gst_buffer_get_size (pad->buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (pad->buffer, GST_BUFFER_FLAG_GAP))
    return TRUE;

  frame = g_slice_new0 (GstVideoFrame);

  if (!gst_video_frame_map (frame, &pad->info, pad->buffer, GST_MAP_READ)) {
    GST_WARNING_OBJECT (vaggregator, "Could not map input buffer");
    g_slice_free (GstVideoFrame, frame);
    return FALSE;
  }

  pad->aggregated_frame = frame;
  return TRUE;
}
#endif // (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR < 16)

static void
gst_video_composer_sinkpad_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec *pspec)
{
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (object);
  GstElement *parent = gst_pad_get_parent_element (GST_PAD (sinkpad));
  const gchar *propname = g_param_spec_get_name (pspec);

  // Extract the state from the pad parent or in case there is no parent
  // use default value as parameters are being set upon object construction.
  GstState state = parent ? GST_STATE (parent) : GST_STATE_VOID_PENDING;

  // Decrease the pad parent reference count as it is not needed any more.
  if (parent != NULL)
    gst_object_unref (parent);

  if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (pspec, state)) {
    GST_WARNING_OBJECT (sinkpad, "Property '%s' change not supported in %s "
        "state!", propname, gst_element_state_get_name (state));
    return;
  }

  GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

  switch (property_id) {
    case PROP_Z_ORDER:
      sinkpad->zorder = g_value_get_int (value);
      break;
    case PROP_CROP:
    {
      guint x = 0, y = 0, width = 0, height = 0;

      g_return_if_fail (gst_value_array_get_size (value) == 4);

      x = g_value_get_int (gst_value_array_get_value (value, 0));
      y = g_value_get_int (gst_value_array_get_value (value, 1));
      width = g_value_get_int (gst_value_array_get_value (value, 2));
      height = g_value_get_int (gst_value_array_get_value (value, 3));

      if ((width == 0 && height != 0) || (width != 0 && height == 0)) {
        GST_WARNING_OBJECT (sinkpad, "Invalid crop parameters!");
        break;
      }

      sinkpad->crop.x = x;
      sinkpad->crop.y = y;
      sinkpad->crop.w = width;
      sinkpad->crop.h = height;
      break;
    }
    case PROP_POSITION:
      g_return_if_fail (gst_value_array_get_size (value) == 2);

      sinkpad->destination.x =
          g_value_get_int (gst_value_array_get_value (value, 0));
      sinkpad->destination.y =
          g_value_get_int (gst_value_array_get_value (value, 1));
      break;
    case PROP_DIMENSIONS:
    {
      guint width = 0, height = 0;

      g_return_if_fail (gst_value_array_get_size (value) == 2);

      width = g_value_get_int (gst_value_array_get_value (value, 0));
      height = g_value_get_int (gst_value_array_get_value (value, 1));

      if ((width == 0 && height != 0) || (width != 0 && height == 0)) {
        GST_WARNING_OBJECT (sinkpad, "Invalid dimensions!");
        break;
      }

      sinkpad->destination.w = width;
      sinkpad->destination.h = height;
      break;
    }
    case PROP_ALPHA:
      sinkpad->alpha = g_value_get_double (value);
      break;
    case PROP_FLIP_HORIZONTAL:
      sinkpad->flip_h = g_value_get_boolean (value);
      break;
    case PROP_FLIP_VERTICAL:
      sinkpad->flip_v = g_value_get_boolean (value);
      break;
    case PROP_ROTATE:
      sinkpad->rotation = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (sinkpad, property_id, pspec);
      break;
  }

  GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);

  // Emit a 'notify' signal for the changed property.
  g_object_notify_by_pspec (G_OBJECT (sinkpad), pspec);
}

static void
gst_video_composer_sinkpad_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (object);

  GST_VIDEO_COMPOSER_SINKPAD_LOCK (sinkpad);

  switch (property_id) {
    case PROP_Z_ORDER:
      g_value_set_int (value, sinkpad->zorder);
      break;
    case PROP_CROP:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, sinkpad->crop.x);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, sinkpad->crop.y);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, sinkpad->crop.w);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, sinkpad->crop.h);
      gst_value_array_append_value (value, &val);
      break;
    }
    case PROP_POSITION:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, sinkpad->destination.x);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, sinkpad->destination.y);
      gst_value_array_append_value (value, &val);
      break;
    }
    case PROP_DIMENSIONS:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, sinkpad->destination.w);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, sinkpad->destination.h);
      gst_value_array_append_value (value, &val);
      break;
    }
    case PROP_ALPHA:
      g_value_set_double (value, sinkpad->alpha);
      break;
    case PROP_FLIP_HORIZONTAL:
      g_value_set_boolean (value, sinkpad->flip_h);
      break;
    case PROP_FLIP_VERTICAL:
      g_value_set_boolean (value, sinkpad->flip_v);
      break;
    case PROP_ROTATE:
      g_value_set_enum (value, sinkpad->rotation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (sinkpad, property_id, pspec);
      break;
  }

  GST_VIDEO_COMPOSER_SINKPAD_UNLOCK (sinkpad);
}

static void
gst_video_composer_sinkpad_finalize (GObject * object)
{
  GstVideoComposerSinkPad *sinkpad = GST_VIDEO_COMPOSER_SINKPAD (object);

  g_mutex_clear (&sinkpad->lock);

  G_OBJECT_CLASS (gst_video_composer_sinkpad_parent_class)->finalize(object);
}

static void
gst_video_composer_sinkpad_class_init (GstVideoComposerSinkPadClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
#if (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR < 16)
  GstVideoAggregatorPadClass *vaggpad = (GstVideoAggregatorPadClass *) klass;
#endif // (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR < 16)

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_video_composer_sinkpad_finalize);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_video_composer_sinkpad_get_property);
  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_video_composer_sinkpad_set_property);

  g_object_class_install_property (gobject, PROP_Z_ORDER,
      g_param_spec_int ("zorder", "Z order",
          "Z axis order, default will be order of creation",
          (-1), G_MAXINT, DEFAULT_PROP_Z_ORDER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_EXPLICIT_NOTIFY));
  g_object_class_install_property (gobject, PROP_CROP,
      gst_param_spec_array ("crop", "Crop rectangle",
          "The crop rectangle ('<X, Y, WIDTH, HEIGHT >')",
          g_param_spec_int ("value", "Crop Value",
              "One of X, Y, WIDTH or HEIGHT value.", 0, G_MAXINT, 0,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_EXPLICIT_NOTIFY));
  g_object_class_install_property (gobject, PROP_POSITION,
      gst_param_spec_array ("position", "Destination rectangle position",
          "The X and Y coordinates of the destination rectangle top left "
          "corner ('<X, Y>')",
          g_param_spec_int ("coord", "Coordinate",
              "One of X, Y value.", 0, G_MAXINT, 0,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_EXPLICIT_NOTIFY));
  g_object_class_install_property (gobject, PROP_DIMENSIONS,
      gst_param_spec_array ("dimensions", "Destination rectangle dimensions",
          "The destination rectangle width and height, if left as '0' they "
          "will be the same as input dimensions ('<WIDTH, HEIGHT>')",
          g_param_spec_int ("dim", "Dimension",
              "One of WIDTH or HEIGHT value.", 0, G_MAXINT, 0,
              G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_EXPLICIT_NOTIFY));
  g_object_class_install_property (gobject, PROP_ALPHA,
      g_param_spec_double ("alpha", "Alpha",
          "Alpha channel value", 0, 1.0, DEFAULT_PROP_ALPHA,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_EXPLICIT_NOTIFY));
  g_object_class_install_property (gobject, PROP_FLIP_HORIZONTAL,
      g_param_spec_boolean ("flip-horizontal", "Flip horizontally",
          "Flip video horizontally", DEFAULT_PROP_FLIP_HORIZONTAL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_EXPLICIT_NOTIFY));
  g_object_class_install_property (gobject, PROP_FLIP_VERTICAL,
      g_param_spec_boolean ("flip-vertical", "Flip vertically",
          "Flip video vertically", DEFAULT_PROP_FLIP_VERTICAL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_EXPLICIT_NOTIFY));
  g_object_class_install_property (gobject, PROP_ROTATE,
      g_param_spec_enum ("rotate", "Rotate", "Rotate video",
          GST_TYPE_VIDEO_COMPOSER_ROTATE, DEFAULT_PROP_ROTATE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING | G_PARAM_EXPLICIT_NOTIFY));

#if (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR < 16)
  vaggpad->prepare_frame =
      GST_DEBUG_FUNCPTR (gst_video_composer_sinkpad_prepare_frame);
#endif // (GST_VERSION_MAJOR == 1 && GST_VERSION_MINOR < 16)

  GST_DEBUG_CATEGORY_INIT (gst_video_composer_sinkpad_debug,
      "qtivcomposer", 0, "QTI Video Composer sink pad");
}

static void
gst_video_composer_sinkpad_init (GstVideoComposerSinkPad * sinkpad)
{
  g_mutex_init (&sinkpad->lock);

  sinkpad->index         = 0;

  sinkpad->zorder        = DEFAULT_PROP_Z_ORDER;
  sinkpad->crop.x        = DEFAULT_PROP_CROP_X;
  sinkpad->crop.y        = DEFAULT_PROP_CROP_Y;
  sinkpad->crop.w        = DEFAULT_PROP_CROP_WIDTH;
  sinkpad->crop.h        = DEFAULT_PROP_CROP_HEIGHT;
  sinkpad->destination.x = DEFAULT_PROP_DESTINATION_X;
  sinkpad->destination.y = DEFAULT_PROP_DESTINATION_Y;
  sinkpad->destination.w = DEFAULT_PROP_DESTINATION_WIDTH;
  sinkpad->destination.h = DEFAULT_PROP_DESTINATION_HEIGHT;
  sinkpad->alpha         = DEFAULT_PROP_ALPHA;
  sinkpad->flip_h        = DEFAULT_PROP_FLIP_HORIZONTAL;
  sinkpad->flip_v        = DEFAULT_PROP_FLIP_VERTICAL;
  sinkpad->rotation      = DEFAULT_PROP_ROTATE;
}
