/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_MODULE_UTILS_H__
#define __GST_QTI_ML_MODULE_UTILS_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>
#include <gst/ml/gstmlmeta.h>

G_BEGIN_DECLS

#define GINT8_PTR_CAST(data)        ((gint8*) data)
#define GUINT8_PTR_CAST(data)       ((guint8*) data)
#define GINT16_PTR_CAST(data)       ((gint16*) data)
#define GUINT16_PTR_CAST(data)      ((guint16*) data)
#define GINT32_PTR_CAST(data)       ((gint32*) data)
#define GUINT32_PTR_CAST(data)      ((guint32*) data)
#define GINT64_PTR_CAST(data)       ((gint64*) data)
#define GUINT64_PTR_CAST(data)      ((guint64*) data)
#if defined(__ARM_FP16_FORMAT_IEEE)
#define GFLOAT16_PTR_CAST(data)       ((__fp16*) data)
#endif // __ARM_FP16_FORMAT_IEEE
#define GFLOAT_PTR_CAST(data)       ((gfloat*) data)

/**
 * gst_ml_stage_get_unique_index:
 *
 * Get an available unique sequentially increasing number.
 *
 * return: Index on success or -1 on failure.
 */
GST_API gint8
gst_ml_stage_get_unique_index (void);

/**
 * gst_ml_stage_register_unique_index:
 * @index: The unique index which to register in the internal table.
 *
 * Add an index number to the internal mapping.
 * If the index is already registered this function will fail.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_stage_register_unique_index (gint8 index);

/**
 * gst_ml_stage_unregister_unique_index:
 * @index: The unique index which to unregister from the internal table.
 *
 * Remove an index number from the internal mapping.
 *
 * return: None
 */
GST_API void
gst_ml_stage_unregister_unique_index (gint8 index);

/**
 * gst_ml_tensor_assign_value:
 * @mltype: ML type of the tensor.
 * @data: Pointer to the data in the ML tensor.
 * @idx: Index of the data in the tensor to be extracted.
 * @value: The value which to assign to the tensor in gdouble format.
 *
 * Helper function for assigning a value to a tensor.
 *
 * return: None
 */
GST_API void
gst_ml_tensor_assign_value (GstMLType mltype, gpointer data, guint idx,
                            gdouble value);

/**
 * gst_ml_tensor_compare_values:
 * @mltype: ML type of the tensor.
 * @data: Pointer to the data in the ML tensor.
 * @l_idx: Left (or First) index of a value in the tensor.
 * @r_idx: Right (or Second) index of a value in the tensor.
 *
 * Helper function for comparing values at two indexes inside the same tensor.
 *
 * return: (1) If value at left index is greater.
 *         (-1) If value at right index is greater.
 *         (0) If both values are equal
 */
GST_API gint
gst_ml_tensor_compare_values (GstMLType mltype, gpointer data, guint l_idx,
                              guint r_idx);
/**
 * gst_ml_structure_has_source_dimensions:
 * @structure: #GstStructure for ML post-processing parameters.
 *
 * Helper function for retrieving if the the postion and dimensions of
 * the region exists
 *
 * return: (TRUE) The source region fields exists.
 *         (FALSE) The source region fields doesn't exists.
 */
gboolean
gst_ml_structure_has_source_dimensions (const GstStructure * structure);

/**
 * gst_ml_structure_set_source_dimensions:
 * @structure: #GstStructure for ML post-processing parameters.
 * @width: Width of the source tensor.
 * @height: Height of the source tensor.
 *
 * Helper function for retrieving if the width and height exist.
 *
 * return: (TRUE) The width and height fields exists.
 *         (FALSE) The width and height fields doesn't exists.
 */
GST_API void
gst_ml_structure_set_source_dimensions (GstStructure * structure,
                                        guint width, guint height);

/**
 * gst_ml_structure_get_source_dimensions:
 * @structure: #GstStructure for ML post-processing parameters.
 * @width: Width parameter which will be populated.
 * @height: Height parameter which will be populated.
 *
 * Helper function for retrieving the width and height of the model source
 * image tensor. Primary to be used in some post-processing modules.
 *
 * return: None
 */
GST_API void
gst_ml_structure_get_source_dimensions (const GstStructure * structure,
                                        guint * width, guint * height);

/**
 * gst_ml_structure_has_source_region:
 * @structure: #GstStructure for ML post-processing parameters.
 *
 * Helper function for retrieving if the the postion and dimensions of
 * the region exists
 *
 * return: (TRUE) The source region fields exists.
 *         (FALSE) The source region fields doesn't exists.
 */
gboolean
gst_ml_structure_has_source_region (const GstStructure * structure);

/**
 * gst_ml_structure_set_source_region:
 * @structure: #GstStructure for ML post-processing parameters.
 * @region: Video rectangle with the region in the tensor.
 *
 * Helper function for populating the postion and dimensions of the region
 * in the model source tensor actually ocupied with data.
 *
 * return: None
 */
GST_API void
gst_ml_structure_set_source_region (GstStructure * structure,
                                    GstVideoRectangle * region);

/**
 * gst_ml_structure_get_source_region:
 * @structure: #GstStructure for ML post-processing parameters.
 * @region: Video rectangle which will be populated.
 *
 * Helper function for retrieving the postion and dimensions of the region
 * in the model source tensor actually ocupied with data.
 *
 * return: None
 */
GST_API void
gst_ml_structure_get_source_region (const GstStructure * structure,
                                    GstVideoRectangle * region);

G_END_DECLS

#endif /* __GST_QTI_ML_MODULE_UTILS_H__ */