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

#include "ml-post-process-classification.h"

/**
 * GstMLClassifications:
 * @refcount: thread safe reference counter
 * @entries: (element-type GstMLClassification): A #GArray of #GstMLClassification
 *
 * Information describing a group of prediction results beloging to the same batch.
 */
struct _GstMLClassifications {
  gatomicrefcount refcount;
  GArray          *entries;
};

G_DEFINE_BOXED_TYPE (GstMLClassification, gst_ml_classification,
    (GBoxedCopyFunc) gst_ml_classification_copy,
    (GBoxedFreeFunc) gst_ml_classification_free);

G_DEFINE_BOXED_TYPE (GstMLClassifications, gst_ml_classifications,
    (GBoxedCopyFunc) gst_ml_classifications_ref,
    (GBoxedFreeFunc) gst_ml_classifications_unref);

static gint
gst_ml_classifications_compare (const GstMLClassification * l_classification,
    const GstMLClassification * r_classification)
{
  if (l_classification->confidence > r_classification->confidence)
    return -1;
  else if (l_classification->confidence < r_classification->confidence)
    return 1;

  return 0;
}

void
gst_ml_classification_reset (GstMLClassification * classification)
{
  g_return_if_fail (classification != NULL);

  classification->name = 0;
  classification->confidence = 0.0;
  classification->color = 0;

  g_clear_pointer (&classification->xtraparams, gst_structure_free);
}

GstMLClassification*
gst_ml_classification_copy (const GstMLClassification * classification)
{
  GstMLClassification *newclassification = NULL;

  g_return_val_if_fail (classification != NULL, NULL);

  newclassification = g_slice_new (GstMLClassification);

  newclassification->name = classification->name;
  newclassification->confidence = classification->confidence;
  newclassification->color = classification->color;
  newclassification->xtraparams = gst_structure_copy (classification->xtraparams);

  return newclassification;
}

void
gst_ml_classification_free (GstMLClassification * classification)
{
  if (classification == NULL)
    return;

  gst_ml_classification_reset (classification);
  g_slice_free (GstMLClassification, classification);
}

GstStructure *
gst_ml_classification_to_structure (GstMLClassification * classification)
{
  GstStructure *structure = NULL;
  gchar *name = NULL;

  // Replace empty spaces otherwise subsequent stream parse call will fail.
  name = g_strdup (g_quark_to_string (classification->name));
  name = g_strdelimit (name, " ", '.');

  structure = gst_structure_new (name, "confidence", G_TYPE_DOUBLE,
      classification->confidence, "color", G_TYPE_UINT, classification->color,
      NULL);

  if (classification->xtraparams != NULL) {
    GValue value = G_VALUE_INIT;

    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, g_steal_pointer (&classification->xtraparams));

    gst_structure_take_value (structure, "xtraparams", &value);
  }

  g_free (name);
  gst_ml_classification_reset (classification);

  return structure;
}

GstMLClassifications*
gst_ml_classifications_new (void)
{
  GstMLClassifications *classifications = g_slice_new (GstMLClassifications);

  g_atomic_ref_count_init (&classifications->refcount);
  classifications->entries =
      g_array_new (FALSE, TRUE, sizeof (GstMLClassification));

  g_array_set_clear_func (classifications->entries,
      (GDestroyNotify) gst_ml_classification_reset);

  return classifications;
}

GstMLClassifications*
gst_ml_classifications_new_sized (guint size)
{
  GstMLClassifications *classifications = g_slice_new (GstMLClassifications);

  g_atomic_ref_count_init (&classifications->refcount);
  classifications->entries =
      g_array_sized_new (FALSE, TRUE, sizeof (GstMLClassification), size);

  g_array_set_clear_func (classifications->entries,
      (GDestroyNotify) gst_ml_classification_reset);
  g_array_set_size (classifications->entries, size);

  return classifications;
}

GstMLClassifications*
gst_ml_classifications_ref (GstMLClassifications * classifications)
{
  g_return_val_if_fail (classifications != NULL, NULL);
  g_atomic_ref_count_inc (&classifications->refcount);

  return classifications;
}

void
gst_ml_classifications_unref (GstMLClassifications * classifications)
{
  g_return_if_fail (classifications != NULL);

  if (g_atomic_ref_count_dec (&classifications->refcount)) {
    g_array_free (classifications->entries, TRUE);
    g_slice_free (GstMLClassifications, classifications);
  }
}

GstMLClassifications *
gst_ml_classifications_copy (const GstMLClassifications * classifications)
{
  GstMLClassifications *newclassifications = NULL;

  g_return_val_if_fail (classifications != NULL, NULL);

  newclassifications = g_slice_new (GstMLClassifications);
  newclassifications->entries = g_array_copy (classifications->entries);
  g_atomic_ref_count_init (&newclassifications->refcount);

  return newclassifications;
}

void
gst_ml_classifications_append (GstMLClassifications * classifications,
                               const GstMLClassification * classification)
{
  g_return_if_fail (classifications != NULL);
  g_array_append_vals (classifications->entries, classification, 1);
}

void
gst_ml_classifications_insert (GstMLClassifications * classifications, guint index,
                               const GstMLClassification * classification)
{
  g_return_if_fail (classifications != NULL);
  g_array_insert_vals (classifications->entries, index, classification, 1);
}

void
gst_ml_classifications_remove (GstMLClassifications * classifications, guint index)
{
  g_return_if_fail (classifications != NULL);
  g_array_remove_index (classifications->entries, index);
}

GstMLClassification*
gst_ml_classifications_entry (GstMLClassifications * classifications, guint index)
{
  g_return_val_if_fail (classifications != NULL, NULL);
  return &(g_array_index (classifications->entries, GstMLClassification, index));
}

guint
gst_ml_classifications_size (GstMLClassifications * classifications)
{
  g_return_val_if_fail (classifications != NULL, 0);
  return classifications->entries->len;
}

void
gst_ml_classifications_resize (GstMLClassifications * classifications, guint size)
{
  g_return_if_fail (classifications != NULL);
  g_array_set_size (classifications->entries, size);
}

void
gst_ml_classifications_sort (GstMLClassifications * classifications)
{
  g_return_if_fail (classifications != NULL);
  g_array_sort (classifications->entries,
      (GCompareFunc) gst_ml_classifications_compare);
}
