/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_COMMON_UTILS_H__
#define __GST_QTI_COMMON_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GPOINTER_CAST(obj)            ((gpointer) obj)
#define GST_PROTECTION_META_CAST(obj) ((GstProtectionMeta *) obj)

// Macro for checking whether a property can be set in current plugin state.
#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
        : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
            : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
                : (state <= GST_STATE_NULL))))

#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)

/**
 * gst_caps_has_feature:
 * @caps: The #GstCaps for verification.
 * @feature: The feature for witch to check.
 *
 * Check if given caps have a feature.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_caps_has_feature (const GstCaps * caps, const gchar * feature);

/**
 * gst_caps_has_compression:
 * @caps: The #GstCaps for verification.
 * @compression: The compression value for which to check.
 *
 * Check if given caps have a "compression" field with the given value.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_caps_has_compression (const GstCaps * caps, const gchar * compression);

/**
 * gst_parse_string_property_value:
 * @value: GValue of type G_TYPE_STRING which will be parsed.
 * @output: The output value which will contain the result. Usually of type
 *          GST_TYPE_STRUCTURE or G_TYPE_LIST.
 *
 * Parse a G_TYPE_STRING value containing either a list (or single) GValue in
 * string format or a file location which conatins that string data.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_parse_string_property_value (const GValue * value, GValue * output);

/**
 * gst_buffer_get_protection_meta_id:
 * @buffer: The #GstBuffer from in which to search.
 * @name: The name of the #GstProtectionMeta.
 *
 * Search and retieve a #GstProtectionMeta with the given name.
 *
 * return: Pointer to #GstProtectionMeta on success or NULL on failure
 */
GST_API GstProtectionMeta *
gst_buffer_get_protection_meta_id (GstBuffer * buffer, const gchar * name);

/**
 * gst_buffer_copy_protection_meta:
 * @destination: The #GstBuffer to which to copy #GstProtectionMeta.
 * @source: The #GstBuffer from which to copy #GstProtectionMeta.
 *
 * Copy all #GstProtectionMeta from the source to the destination #GstBuffer.
 *
 * return: NONE
 */
GST_API void
gst_buffer_copy_protection_meta (GstBuffer * destination, GstBuffer * source);

G_END_DECLS

#endif /* __GST_QTI_COMMON_UTILS_H__ */
