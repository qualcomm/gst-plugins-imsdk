/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_BUNDLE_H__
#define __GST_QTI_ML_BUNDLE_H__

#include <gst/gst.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>

G_BEGIN_DECLS

#define GST_ML_BUNDLE_CAST(obj)      ((GstMLBundle*)(obj))

typedef struct _GstMLBundle GstMLBundle;

#define GST_TYPE_ML_BUNDLE            (gst_ml_bundle_get_type ())
GST_API GType gst_ml_bundle_get_type  (void);

/**
 * gst_ml_bundle_new: (constructor)
 * @buffer: (transfer none) (nullable): A #GstBuffer or NULL
 * @info: (transfer none) (nullable): A #GstMLInfo or NULL
 *
 * Allocate a new #GstMLBundle that is also initialized.
 *
 * Returns: (transfer full): A new #GstMLBundle.
 */
GST_API GstMLBundle*
gst_ml_bundle_new (GstBuffer * buffer, const GstMLInfo * info);

/**
 * gst_ml_bundle_ref: (skip)
 * @bundle: (transfer none): A #GstMLBundle
 *
 * Atomically increments the reference count of @bundle by one.
 * This function is thread-safe and may be called from any thread.
 *
 * Returns: (transfer none): The #GstMLBundle passed in @bundle
 */
GST_API GstMLBundle*
gst_ml_bundle_ref (GstMLBundle * bundle);

/**
 * gst_ml_bundle_unref: (skip)
 * @bundle: (transfer none): A #GstMLBundle
 *
 * Atomically decrements the reference count of @bundle by one. If the
 * reference count drops to 0, the GstMLBundle apreviously allocated with
 * gst_ml_bundle_new() will be deallocated.
 *
 * This function is thread-safe and may be called from any thread.
 */
GST_API void
gst_ml_bundle_unref (GstMLBundle * bundle);

/**
 * gst_ml_bundle_get_buffer:
 * @bundle: A #GstMLBundle
 *
 * Get the buffer associated with bundle.
 *
 * Returns: (transfer none) (nullable): The buffer or %NULL when there is no
 *          buffer. The buffer remains valid as long as bundle is valid.
 *          If you need to hold on to it for longer than that, take a ref to
 *          the buffer with %gst_buffer_ref.
 */
GST_API GstBuffer*
gst_ml_bundle_get_buffer (GstMLBundle * bundle);

/**
 * gst_ml_bundle_get_info:
 * @bundle: A #GstMLBundle
 *
 * Get the ML info associated with bundle
 *
 * Returns: (transfer none) (nullable): The ML info or %NULL when there is no
 *          ML info. The info remains valid as long as bundle is valid.
 */
GST_API GstMLInfo*
gst_ml_bundle_get_info (GstMLBundle * bundle);

/**
 * gst_ml_bundle_get_frame:
 * @bundle: A #GstMLBundle
 * @flags: The #GstMapFlags with which to get the ML frame
 *
 * Get the mapped buffer as #GstMLFrame.
 *
 * Returns: (transfer none) (nullable): The mapped ML frame or %NULL when there
 *          is no buffer. The frame remains valid as long as bundle is valid.
 */
GST_API GstMLFrame*
gst_ml_bundle_get_frame (GstMLBundle * bundle, GstMapFlags flags);

G_END_DECLS

#endif // __GST_QTI_ML_BUNDLE_H__
