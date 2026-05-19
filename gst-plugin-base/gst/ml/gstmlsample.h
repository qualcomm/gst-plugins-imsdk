/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_SAMPLE_H__
#define __GST_QTI_ML_SAMPLE_H__

#include <gst/gst.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>

G_BEGIN_DECLS

#define GST_ML_SAMPLE_CAST(obj)      ((GstMLSample*)(obj))

typedef struct _GstMLSample GstMLSample;

#define GST_TYPE_ML_SAMPLE            (gst_ml_sample_get_type ())
GST_API GType gst_ml_sample_get_type  (void);

/**
 * gst_ml_sample_new: (constructor)
 * @buffer: (transfer none) (nullable): A #GstBuffer or NULL
 * @info: (transfer none) (nullable): A #GstMLInfo or NULL
 *
 * Allocate a new #GstMLSample that is also initialized.
 *
 * Returns: (transfer full): A new #GstMLSample.
 */
GST_API GstMLSample*
gst_ml_sample_new (GstBuffer * buffer, const GstMLInfo * info);

/**
 * gst_ml_sample_ref: (skip)
 * @sample: (transfer none): A #GstMLSample
 *
 * Atomically increments the reference count of @sample by one.
 * This function is thread-safe and may be called from any thread.
 *
 * Returns: (transfer none): The #GstMLSample passed in @sample
 */
GST_API GstMLSample*
gst_ml_sample_ref (GstMLSample * sample);

/**
 * gst_ml_sample_unref: (skip)
 * @sample: (transfer none): A #GstMLSample
 *
 * Atomically decrements the reference count of @sample by one. If the
 * reference count drops to 0, the GstMLSample apreviously allocated with
 * gst_ml_sample_new() will be deallocated.
 *
 * This function is thread-safe and may be called from any thread.
 */
GST_API void
gst_ml_sample_unref (GstMLSample * sample);

/**
 * gst_ml_sample_get_buffer:
 * @sample: A #GstMLSample
 *
 * Get the buffer associated with sample.
 *
 * Returns: (transfer none) (nullable): The buffer or %NULL when there is no
 *          buffer. The buffer remains valid as long as sample is valid.
 *          If you need to hold on to it for longer than that, take a ref to
 *          the buffer with %gst_buffer_ref.
 */
GST_API GstBuffer*
gst_ml_sample_get_buffer (GstMLSample * sample);

/**
 * gst_ml_sample_get_info:
 * @sample: A #GstMLSample
 *
 * Get the ML info associated with sample
 *
 * Returns: (transfer none) (nullable): The ML info or %NULL when there is no
 *          ML info. The info remains valid as long as sample is valid.
 */
GST_API GstMLInfo*
gst_ml_sample_get_info (GstMLSample * sample);

/**
 * gst_ml_sample_get_frame:
 * @sample: A #GstMLSample
 * @flags: The #GstMapFlags with which to get the ML frame
 *
 * Get the mapped buffer as #GstMLFrame.
 *
 * Returns: (transfer none) (nullable): The mapped ML frame or %NULL when there
 *          is no buffer. The frame remains valid as long as sample is valid.
 */
GST_API GstMLFrame*
gst_ml_sample_get_frame (GstMLSample * sample, GstMapFlags flags);

G_END_DECLS

#endif // __GST_QTI_ML_SAMPLE_H__
