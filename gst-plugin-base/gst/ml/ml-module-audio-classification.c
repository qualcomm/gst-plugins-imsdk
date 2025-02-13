/*
* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-3-License-Identifier: BSD-3-Clause-Clear
*/

#include "ml-module-audio-classification.h"

void
gst_ml_class_audio_prediction_cleanup (GstMLClassPrediction * prediction)
{
  if (prediction->entries != NULL)
    g_array_free (prediction->entries, TRUE);
}

gint
gst_ml_class_audio_compare_entries (const GstMLClassEntry * l_entry,
    const GstMLClassEntry * r_entry)
{
  if (l_entry->confidence > r_entry->confidence)
    return -1;
  else if (l_entry->confidence < r_entry->confidence)
    return 1;

  return 0;
}

GST_API gboolean
gst_ml_module_audio_classification_execute (GstMLModule * module,
    GstMLFrame * mlframe, GArray * predictions)
{
  return gst_ml_module_execute (module, mlframe, (gpointer) predictions);
}
