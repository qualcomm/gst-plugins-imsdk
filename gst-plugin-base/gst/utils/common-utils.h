/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_COMMON_UTILS_H__
#define __GST_QTI_COMMON_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

// Macro for checking whether a property can be set in current plugin state.
#define GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
        : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
            : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
                : (state <= GST_STATE_NULL))))

#define GST_BUFFER_ITERATE_ROI_METAS(buffer, state) \
    (GstVideoRegionOfInterestMeta*) gst_buffer_iterate_meta_filtered (buffer, \
        &state, GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE)

#define GPOINTER_CAST(obj)             ((gpointer) obj)
#define GST_PROTECTION_META_CAST(obj)  ((GstProtectionMeta *) obj)
#define GST_VIDEO_ROI_META_CAST(obj)   ((GstVideoRegionOfInterestMeta *) obj)

#define EXTRACT_RED_COLOR(color)       ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color)     ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)      ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color)     ((color) & 0xFF)

// The bit offset where the stream ID can be embedded in a variable
#define GST_MUX_STREAM_ID_OFFSET       24
#define GST_MUX_STREAM_ID_MASK         (0xFF << GST_MUX_STREAM_ID_OFFSET)

// The bit offset for stage and sequence ID embedded in a single variable.
#define GST_META_STAGE_ID_OFFSET       16
#define GST_META_SEQ_ID_OFFSET         8

// Macro to create unique meta ID from stage, sequence and entry IDs.
#define GST_META_ID(stage_id, sequence_id, entry_id) \
    (((stage_id & 0xFF) << GST_META_STAGE_ID_OFFSET) | \
        ((sequence_id & 0xFF) << GST_META_SEQ_ID_OFFSET) | entry_id)

// Macro to extract stage from meta ID.
#define GST_META_ID_GET_STAGE(id) ((id >> GST_META_STAGE_ID_OFFSET) & 0xFF)
// Macro to extract stage from meta ID.
#define GST_META_ID_GET_ENTRY(id) (id & 0xFF)

/**
 * gst_mux_stream_name:
 * @index: The index of the muxed stream inside the buffer.
 *
 * Return the string representation of the stream index for use as name of the
 * #GstProtectionMeta attached when buffers are created from muxed streams.
 *
 * This is convinient in order to avoid the constant allocation of a string
 * when corresponding to the batch number when there is a need for it.
 *
 * return: Pointer to string in "mux-stream-%2d" format or NULL on failure
 */
GST_API const gchar *
gst_mux_stream_name (guint index);

/**
 * gst_mux_buffer_get_memory_stream_id:
 * @mem_idx: The index of the memory inside the muxed buffer.
 *
 * Extract the stream ID of the buffer memory inside the muxed buffer.
 *
 * return: Stream ID on success or -1 on failure.
 */
GST_API gint
gst_mux_buffer_get_memory_stream_id (GstBuffer * buffer, gint mem_idx);

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

#if GLIB_MAJOR_VERSION < 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 62)
/**
 * g_array_copy:
 * @array: A #GArray.
 *
 * Simple replacement for g_array_copy() in glib version < 2.62
 * TODO: Remove when only glib version >= 2.62 is used.
 *
 * return: A copy of of the array.
 **/
GST_API GArray *
g_array_copy (GArray * array);
#endif // GLIB_MAJOR_VERSION < 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 62)

G_END_DECLS

#endif /* __GST_QTI_COMMON_UTILS_H__ */
