/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_POST_PROCESS_ENGINE_H__
#define __GST_QTI_ML_POST_PROCESS_ENGINE_H__

#include <gst/gst.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_MODULES (gst_ml_modules_get_type ())
GST_API GType gst_ml_modules_get_type (void);

typedef struct _GstMLEngine GstMLEngine;

/**
 * gst_ml_engine_new:
 * @name: Name of the engine.
 *
 * Allocate an instance of ML post-processing engine and intialize the engine.
 *
 * Returns: Pointer to ML post-processing engine on success or NULL on failure
 */
GST_API GstMLEngine *
gst_ml_engine_new (const gchar * name);

/**
 * gst_ml_engine_free:
 * @engine: Pointer to the ML post-processing engine.
 *
 * De-initialize and free the memory associated with the engine.
 */
GST_API void
gst_ml_engine_free (GstMLEngine * engine);

/**
 * gst_ml_engine_get_caps:
 * @engine: Pointer to ML post-processing engine.
 *
 * Convenient wrapper function used on plugin level to get engine GstCaps.
 *
 * Returns: The #GstCaps of this mnodule. Unref after usage.
 */
GST_API GstCaps *
gst_ml_engine_get_caps (GstMLEngine * engine);

/**
 * gst_ml_engine_get_type:
 * @engine: Pointer to ML post-processing engine.
 *
 * Convenient wrapper function used on plugin level to get engine type.
 *
 * Returns: Module type as GQuark
 */
GST_API GQuark
gst_ml_engine_get_type (GstMLEngine * engine);

/**
 * gst_ml_engine_set_opts:
 * @engine: Pointer to ML post-processing engine.
 * @stage_id: The ID of this stage of ML inference.
 * @n_results: Maximum number of results to process
 * @mode: Output mode (Video, Text or Tensors)
 * @stabilization: Whether to perform coordinates stabilization on the results
 * @labels: File or string is JSON format.
 * @opts: Module specific settings as file or string in JSON format.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'Configure' API to set various options.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_engine_configure (GstMLEngine * engine, guint stage_id, guint n_results,
                         guint mode, gboolean stabilization,
                         const gchar * labels, const gchar *opts);

/**
 * gst_ml_engine_execute:
 * @engine: Pointer to ML post-processing engine.
 * @batch_idx: Index of current batch for processing.
 * @mlframe: Frame containing mapped tensor memory blocks that need processing.
 * @mlparam: A #GstStructure cotaining ML params current batch.
 * @output: Output that depends on negotiated caps (Text, Video or Tensors).
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'Process' API in order to process input tensors and produce a engine
 * specific output.
 *
 * Returns: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_engine_execute (GstMLEngine * engine, guint batch_idx, GstMLFrame * mlframe,
                       GstStructure * mlparam, gpointer output);

G_END_DECLS

#endif // __GST_QTI_ML_POST_PROCESS_ENGINE_H__
