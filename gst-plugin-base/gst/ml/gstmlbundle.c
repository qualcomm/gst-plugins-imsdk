/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "gstmlbundle.h"

G_DEFINE_BOXED_TYPE (GstMLBundle, gst_ml_bundle,
    (GBoxedCopyFunc) gst_ml_bundle_ref, (GBoxedFreeFunc) gst_ml_bundle_unref);

struct _GstMLBundle
{
  gatomicrefcount refcount;

  GstBuffer       *buffer;
  GstMLInfo       *info;

  GstMLFrame      frame;
};

GstMLBundle*
gst_ml_bundle_new (GstBuffer * buffer, const GstMLInfo * info)
{
  GstMLBundle *bundle = g_slice_new0 (GstMLBundle);

  if (buffer != NULL)
    bundle->buffer = gst_buffer_ref (buffer);

  if (info != NULL)
    bundle->info = gst_ml_info_copy (info);

  g_atomic_ref_count_init (&bundle->refcount);
  return bundle;
}

GstMLBundle*
gst_ml_bundle_ref (GstMLBundle * bundle)
{
  g_return_val_if_fail (bundle != NULL, NULL);

  g_atomic_ref_count_inc (&bundle->refcount);
  return bundle;
}

void
gst_ml_bundle_unref (GstMLBundle * bundle)
{
  g_return_if_fail (bundle != NULL);

  if (!g_atomic_ref_count_dec (&bundle->refcount))
    return;

  if (bundle->frame.buffer != NULL)
    gst_ml_frame_unmap (&bundle->frame);

  g_clear_pointer (&bundle->buffer, gst_buffer_unref);
  g_clear_pointer (&bundle->info, gst_ml_info_free);

  g_slice_free (GstMLBundle, bundle);
}

GstBuffer*
gst_ml_bundle_get_buffer (GstMLBundle * bundle)
{
  g_return_val_if_fail (bundle != NULL, NULL);
  return bundle->buffer;
}

GstMLInfo*
gst_ml_bundle_get_info (GstMLBundle * bundle)
{
  g_return_val_if_fail (bundle != NULL, NULL);
  return bundle->info;
}

GstMLFrame*
gst_ml_bundle_get_frame (GstMLBundle * bundle, GstMapFlags flags)
{
  g_return_val_if_fail (bundle != NULL, NULL);

  // If frame was already mapped then return immediately.
  if (bundle->buffer == NULL)
    return NULL;

  // If buffer hasn't been already mapped try to map it now.
  if ((bundle->frame.buffer == NULL) &&
      !gst_ml_frame_map (&bundle->frame, bundle->info, bundle->buffer, flags)) {
    GST_ERROR ("Failed to map buffer!");
    return NULL;
  }

  if ((bundle->frame.mapinfo[0].flags & flags) == 0) {
    GST_ERROR ("Invalid flags 0x%X ! Buffer has been previously mapped with "
        "flags 0x%X", flags, bundle->frame.mapinfo[0].flags);
    return NULL;
  }

  return &bundle->frame;
}
