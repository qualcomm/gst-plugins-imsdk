/*
* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-3-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_QTI_ML_MODULE_AUDIO_CLASSIFICATION_H__
#define __GST_QTI_ML_MODULE_AUDIO_CLASSIFICATION_H__

#include <gst/gst.h>
#include <gst/ml/gstmlmodule.h>

G_BEGIN_DECLS

typedef struct _GstMLClassEntry GstMLClassEntry;
typedef struct _GstMLClassPrediction GstMLClassPrediction;

/**
 * GstMLClassEntry:
 * @name: Name of the prediction.
 * @confidence: Percentage certainty that the prediction is accurate.
 * @color: Possible color that is associated with this prediction.
 *
 * Information describing prediction result from image classification models.
 * All fields are mandatory and need to be filled by the submodule.
 */
struct _GstMLClassEntry {
  GQuark  name;
  gfloat  confidence;
  guint32 color;
};

/**
 * GstMLClassPrediction:
 * @entries: GArray of #GstMLClassEntry.
 * @info: Additonal info structure, beloging to the batch #GstProtectionMeta
 *        in the ML tensor buffer from which the prediction result was produced.
 *        Ownership is still with that tensor buffer.
 *
 * Information describing a group of prediction results beloging to the same batch.
 * All fields are mandatory and need to be filled by the submodule.
 */
struct _GstMLClassPrediction {
  GArray             *entries;
  const GstStructure *info;
};

/**
 * gst_ml_class_audio_prediction_cleanup:
 * @prediction: Pointer to the ML class prediction.
 *
 * Helper function for freeing any resources allocated owned by the prediction.
 *
 * return: None
 */
GST_API void
gst_ml_class_audio_prediction_cleanup (GstMLClassPrediction * prediction);

/**
 * gst_ml_class_audio_compare_entries:
 * @l_entry: Left (or First) ML class post-processing entry.
 * @r_entry: Right (or Second) ML class post-processing entry.
 *
 * Helper function for comparing two ML class entries.
 *
 * return: -1 (l_entry > r_entry), 1 (l_entry < r_entry) and 0 (l_entry == r_entry)
 */
GST_API gint
gst_ml_class_audio_compare_entries (const GstMLClassEntry * l_entry,
                                    const GstMLClassEntry * r_entry);

/**
 * gst_ml_module_audio_classification_execute:
 * @module: Pointer to ML post-processing module.
 * @mlframe: Frame containing mapped tensor memory blocks that need processing.
 * @predictions: GArray of #GstMLClassBatch.
 *
 * Convenient wrapper function used on plugin level to call the module
 * 'gst_ml_module_process' API via 'gst_ml_module_execute' wrapper in order
 * to process input tensors.
 *
 * Post-processing module must define the 3rd argument of the implemented
 * 'gst_ml_module_process' API as 'GArray *'.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_module_audio_classification_execute (GstMLModule * module, GstMLFrame * mlframe,
                                            GArray * predictions);

G_END_DECLS

#endif // __GST_QTI_ML_MODULE_AUDIO_CLASSIFICATION_H__
