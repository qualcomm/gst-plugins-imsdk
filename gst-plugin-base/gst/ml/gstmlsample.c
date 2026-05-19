/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gstmlsample.h"

G_DEFINE_BOXED_TYPE (GstMLSample, gst_ml_sample,
    (GBoxedCopyFunc) gst_ml_sample_ref, (GBoxedFreeFunc) gst_ml_sample_unref);

struct _GstMLSample
{
  gatomicrefcount refcount;

  GstBuffer       *buffer;
  GstMLInfo       *info;

  GstMLFrame      frame;
};

GstMLSample*
gst_ml_sample_new (GstBuffer * buffer, const GstMLInfo * info)
{
  GstMLSample *sample = g_slice_new0 (GstMLSample);

  if (buffer != NULL)
    sample->buffer = gst_buffer_ref (buffer);

  if (info != NULL)
    sample->info = gst_ml_info_copy (info);

  g_atomic_ref_count_init (&sample->refcount);
  return sample;
}

GstMLSample*
gst_ml_sample_ref (GstMLSample * sample)
{
  g_return_val_if_fail (sample != NULL, NULL);

  g_atomic_ref_count_inc (&sample->refcount);
  return sample;
}

void
gst_ml_sample_unref (GstMLSample * sample)
{
  g_return_if_fail (sample != NULL);

  if (!g_atomic_ref_count_dec (&sample->refcount))
    return;

  if (sample->frame.buffer != NULL)
    gst_ml_frame_unmap (&sample->frame);

  g_clear_pointer (&sample->buffer, gst_buffer_unref);
  g_clear_pointer (&sample->info, gst_ml_info_free);

  g_slice_free (GstMLSample, sample);
}

GstBuffer*
gst_ml_sample_get_buffer (GstMLSample * sample)
{
  g_return_val_if_fail (sample != NULL, NULL);
  return sample->buffer;
}

GstMLInfo*
gst_ml_sample_get_info (GstMLSample * sample)
{
  g_return_val_if_fail (sample != NULL, NULL);
  return sample->info;
}

GstMLFrame*
gst_ml_sample_get_frame (GstMLSample * sample, GstMapFlags flags)
{
  g_return_val_if_fail (sample != NULL, NULL);

  // If frame was already mapped then return immediately.
  if (sample->buffer == NULL)
    return NULL;

  // If buffer hasn't been already mapped try to map it now.
  if ((sample->frame.buffer == NULL) &&
      !gst_ml_frame_map (&sample->frame, sample->info, sample->buffer, flags)) {
    GST_ERROR ("Failed to map buffer!");
    return NULL;
  }

  if ((sample->frame.mapinfo[0].flags & flags) == 0) {
    GST_ERROR ("Invalid flags 0x%X ! Buffer has been previously mapped with "
        "flags 0x%X", flags, sample->frame.mapinfo[0].flags);
    return NULL;
  }

  return &sample->frame;
}
