/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_POST_PROCESS_CLASSIFICATION_H__
#define __GST_QTI_ML_POST_PROCESS_CLASSIFICATION_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_ML_CLASSIFICATIONS_CAST(obj)      ((GstMLClassifications*)(obj))

typedef struct _GstMLClassification GstMLClassification;
typedef struct _GstMLClassifications GstMLClassifications;

#define GST_TYPE_ML_CLASSIFICATION            (gst_ml_classification_get_type ())
GST_API GType gst_ml_classification_get_type  (void);

#define GST_TYPE_ML_CLASSIFICATIONS           (gst_ml_classifications_get_type ())
GST_API GType gst_ml_classifications_get_type (void);

/**
 * GstMLClassification:
 * @name: Name of the classification.
 * @confidence: Percentage certainty that the classification is accurate.
 * @color: Possible color that is associated with this classification.
 * @xtraparams: (optional): A #GstStructure with custom parameters. The names
 *              for the those parameters inside the #GstStructure must be lower
 *              case with dash ('-') for whitepace e.g. "param-example-name".
 *              The following parameter names are forbidden: 'confidence',
 *              'keypoints' and 'links'. The name given to the structure
 *              on creation must use the reserved naming "ExtraParams".
 *
 *
 * Information describing classification result from image classification models.
 */
struct _GstMLClassification {
  GQuark       name;
  gfloat       confidence;

  guint32      color;

  GstStructure *xtraparams;
};

/**
 * gst_ml_classification_reset:
 * @classification: A #GstMLClassification
 *
 * Helper function for freeing any allocated resources owned by the class.
 */
GST_API void
gst_ml_classification_reset (GstMLClassification * classification);

/**
 * gst_ml_classification_copy:
 * @classification: A #GstMLClassification
 *
 * Copy a GstMLClassification structure.
 *
 * Returns: (transfer full): A new #GstMLClassification.
 */
GST_API GstMLClassification *
gst_ml_classification_copy (const GstMLClassification * classification);

/**
 * gst_ml_classification_free:
 * @classification: A #GstMLClassification
 *
 * Free a GstMLClassification structure previously allocated with
 * gst_ml_classification_copy().
 */
GST_API void
gst_ml_classification_free (GstMLClassification * classification);

/**
 * gst_ml_classification_to_structure:
 * @classification: A #GstMLClassification
 *
 * Converts GstMLClassification to a GstStructure representation.
 * All internal fields are reset and allocated memory freed.
 *
 * Returns: (transfer full): A new #GstStructure.
 */
GST_API GstStructure *
gst_ml_classification_to_structure (GstMLClassification * classification);

/**
 * gst_ml_classifications_new: (constructor)
 *
 * Allocate a new #GstMLClassifications that is also initialized.
 *
 * Returns: (transfer full): A new #GstMLClassifications.
 */
GST_API GstMLClassifications*
gst_ml_classifications_new (void);

/**
 * gst_ml_classifications_new_sized: (constructor)
 * @size: number of elements preallocated
 *
 * Allocate a new #GstMLClassifications with @size elements preallocated.
 *
 * Returns: (transfer full): A new #GstMLClassifications.
 */

GST_API GstMLClassifications*
gst_ml_classifications_new_sized (guint size);

/**
 * gst_ml_classifications_ref: (skip)
 * @classifications: (transfer none): A #GstMLClassifications
 *
 * Atomically increments the reference count of @classifications by one.
 * This function is thread-safe and may be called from any thread.
 *
 * Returns: (transfer none): A pointer to the object passed in @classifications
 */
GST_API GstMLClassifications*
gst_ml_classifications_ref (GstMLClassifications * classifications);

/**
 * gst_ml_classifications_unref: (skip)
 * @classifications: (transfer none): A #GstMLClassifications
 *
 * Atomically decrements the reference count of @classifications by one. If the
 * reference count drops to 0, free the GstMLClassifications.
 *
 * This function is thread-safe and may be called from any thread.
 */
GST_API void
gst_ml_classifications_unref (GstMLClassifications * classifications);

/**
 * gst_ml_classifications_copy:
 * @classifications: A #GstMLClassifications
 *
 * Copy a GstMLClassifications structure.
 *
 * Returns: (transfer full): A new #GstMLClassifications.
 */
GST_API GstMLClassifications *
gst_ml_classifications_copy (const GstMLClassifications * classifications);

/**
 * gst_ml_classifications_append:
 * @classifications: A #GstMLClassifications
 * @classification: A #GstMLClassification
 *
 * Adds the value on to the end of the GstMLClassifications list.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_classifications_append (GstMLClassifications * classifications,
                               const GstMLClassification * classification);

/**
 * gst_ml_classifications_insert:
 * @classifications: A #GstMLClassifications
 * @index: the index at which to insert the new element
 * @classification: A #GstMLClassification
 *
 * Insert element into a GstMLClassifications at the given index.
 * The list will grow in size automatically if necessary.
 */
GST_API void
gst_ml_classifications_insert (GstMLClassifications * classifications, guint index,
                               const GstMLClassification * classification);

/**
 * gst_ml_classifications_remove:
 * @classifications: A #GstMLClassifications
 * @index: the index of the element to remove
 *
 * Removes the element at the given index from the classifications list.
 * The following elements are moved down one place.
 */
GST_API void
gst_ml_classifications_remove (GstMLClassifications * classifications, guint index);

/**
 * gst_ml_classifications_entry:
 * @classifications: A #GstMLClassifications
 * @index: the index of the element to return
 *
 * Returns: (transfer none): the #GstMLClassification at the given index.
 */
GST_API GstMLClassification*
gst_ml_classifications_entry (GstMLClassifications * classifications, guint index);

/**
 * gst_ml_classifications_size:
 * @classifications: A #GstMLClassifications
 *
 * Returns: number of elements in A #GstMLClassifications
 */
GST_API guint
gst_ml_classifications_size (GstMLClassifications * classifications);

/**
 * gst_ml_classifications_resize:
 * @classifications: A #GstMLClassifications
 * @size: the new size of the GstMLClassifications list
 *
 * Sets the size of the array, expanding it if necessary.
 */
GST_API void
gst_ml_classifications_resize (GstMLClassifications * classifications, guint size);

/**
 * gst_ml_classifications_sort:
 * @classifications: A #GstMLClassifications
 *
 * Sort elements in a GstMLClassifications list by confidence score.
 */
GST_API void
gst_ml_classifications_sort (GstMLClassifications * classifications);

G_END_DECLS

#endif // __GST_QTI_ML_POST_PROCESS_CLASSIFICATION_H__
