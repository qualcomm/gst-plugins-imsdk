/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
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

#define GST_INT8_PTR_CAST(data)        ((gint8*) data)
#define GST_UINT8_PTR_CAST(data)       ((guint8*) data)
#define GST_INT16_PTR_CAST(data)       ((gint16*) data)
#define GST_UINT16_PTR_CAST(data)      ((guint16*) data)
#define GST_INT32_PTR_CAST(data)       ((gint32*) data)
#define GST_UINT32_PTR_CAST(data)      ((guint32*) data)
#define GST_INT64_PTR_CAST(data)       ((gint64*) data)
#define GST_UINT64_PTR_CAST(data)      ((guint64*) data)
#if defined(__ARM_FP16_FORMAT_IEEE)
#define GST_FLOAT16_PTR_CAST(data)     ((__fp16*) data)
#endif // __ARM_FP16_FORMAT_IEEE
#define GST_FLOAT_PTR_CAST(data)       ((gfloat*) data)

#define GST_PTR_CAST(obj)              ((gpointer) obj)
#define GST_PROTECTION_META_CAST(obj)  ((GstProtectionMeta *) obj)

#define GST_COLOR_RED(color)           ((color >> 24) & 0xFF)
#define GST_COLOR_GREEN(color)         ((color >> 16) & 0xFF)
#define GST_COLOR_BLUE(color)          ((color >> 8) & 0xFF)
#define GST_COLOR_ALPHA(color)         ((color) & 0xFF)

#define GST_FLOAT_COLOR_RED(color)     (((color >> 24) & 0xFF) / 255.0)
#define GST_FLOAT_COLOR_GREEN(color)   (((color >> 16) & 0xFF) / 255.0)
#define GST_FLOAT_COLOR_BLUE(color)    (((color >> 8) & 0xFF) / 255.0)
#define GST_FLOAT_COLOR_ALPHA(color)   (((color) & 0xFF) / 255.0)

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

typedef struct _GstClassLabel GstClassLabel;

/**
 * GstClassLabel:
 * @name: Label name.
 * @confidence: Confidence score.
 * @color: Optional color value.
 * @xtraparams: (optional): A #GstStructure containing additional parameters.
 *
 * Generic helper structure representing a class label.
 */
struct _GstClassLabel {
  GQuark       name;
  gdouble      confidence;
  guint32      color;
  GstStructure *xtraparams;
};

/**
 * gst_class_label_reset:
 * @label: A #GstClassLabel
 *
 * Helper function for freeing any allocated resources owned by the label.
 */
GST_API void
gst_class_label_reset (GstClassLabel * label);

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
 * Returns: Pointer to string in "mux-stream-%2d" format or NULL on failure
 */
GST_API const gchar *
gst_mux_stream_name (guint index);

/**
 * gst_mux_buffer_get_memory_stream_id:
 * @mem_idx: The index of the memory inside the muxed buffer.
 *
 * Extract the stream ID of the buffer memory inside the muxed buffer.
 *
 * Returns: Stream ID on success or -1 on failure.
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
 * Returns: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_caps_has_feature (const GstCaps * caps, const gchar * feature);

/**
 * gst_value_deserialize_file:
 * @value: The output value which will contain the result. Usually of type
 *          GST_TYPE_STRUCTURE or G_TYPE_LIST.
 * @filename: String containing file name and path.
 *
 * Extract the contents of a GStreamer string file and deserialize it to
 * GValue of type GST_TYPE_STRUCTURE or G_TYPE_LIST.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_value_deserialize_file (GValue * value, const gchar * filename);

/**
 * gst_structure_from_json_file:
 * @filename: String containing file name and path.
 *
 * Extract the contents of a JSON file and convert it to GstStructure.
 *
 * return: Pointer to GstStructure or NULL on failure.
 */
GST_API GstStructure *
gst_structure_from_json_file (const gchar * filename);

/**
 * gst_structure_from_json_string:
 * @string: String in JSON containing JSON object
 *
 * Converts JSON string containing an object into GstStructure.
 *
 * return: Pointer to GstStructure or NULL on failure.
 */
GST_API GstStructure *
gst_structure_from_json_string (const gchar * string);

/**
 * gst_structure_to_json_string:
 * @structure: The #GStStructure which will be converted.
 *
 * Converts structure to a human-readable JSON string representation.
 *
 * return: Pointer to string or NULL on failure.
 */
GST_API gchar *
gst_structure_to_json_string (GstStructure * structure);

/**
 * gst_buffer_get_protection_meta_id:
 * @buffer: A #GstBuffer
 * @name: The name of the #GstProtectionMeta.
 *
 * Find the #GstProtectionMeta on @buffer with the given @name.
 *
 * Buffers can contain multiple #GstProtectionMeta metadata items.
 *
 * Returns: (transfer none) (nullable): The #GstProtectionMeta with @id or
 *          %NULL when there is no such metadata on @buffer.
 */
GST_API GstProtectionMeta *
gst_buffer_get_protection_meta_id (GstBuffer * buffer, const gchar * name);

/**
 * gst_buffer_copy_protection_meta:
 * @destination: The #GstBuffer to which to copy #GstProtectionMeta.
 * @source: The #GstBuffer from which to copy #GstProtectionMeta.
 *
 * Copy all #GstProtectionMeta from the source to the destination #GstBuffer.
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
 * Returns: A copy of of the array.
 **/
GST_API GArray *
g_array_copy (GArray * array);
#endif // GLIB_MAJOR_VERSION < 2 || (GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION < 62)

/**
 * g_param_spec_copy:
 * @param: An existing #GParamSpec from which the values are extracted.
 * @prefix: String which will be prepended to the name of the new #GParamSpec.
 *          If @prefix is NULL, the original name of #GParamSpec will be used.
 *
 * Copy the data from @param, based on its value type.
 *
 * Returns: (transfer full): A new #GParamSpec
 **/
GST_API GParamSpec *
g_param_spec_copy (GParamSpec * param, const gchar * prefix);

G_END_DECLS

#endif /* __GST_QTI_COMMON_UTILS_H__ */
