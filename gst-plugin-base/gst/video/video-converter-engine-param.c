/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "video-converter-engine-param.h"

#include <gst/utils/common-utils.h>

GST_DEBUG_CATEGORY_EXTERN (gst_video_converter_engine_debug);
#define GST_CAT_DEFAULT gst_video_converter_engine_debug

/**
 * GstVideoBlits:
 * @refcount: thread safe reference counter
 * @entries: (element-type GstVideoBlit): A #GArray of #GstVideoBlit
 *
 * Information describing a group of video blit elements.
 */
struct _GstVideoBlits {
  gatomicrefcount refcount;
  GArray          *entries;
};

G_DEFINE_BOXED_TYPE (GstVideoBlits, gst_video_blits,
    (GBoxedCopyFunc) gst_video_blits_ref, (GBoxedFreeFunc) gst_video_blits_unref);

GType
gst_fcv_op_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_FCV_OP_MODE_LOW_POWER,
        "Uses lowest power consuming implementation", "low-power"
    },
    { GST_FCV_OP_MODE_PERFORMANCE,
        "Uses highest performance implementation", "performance"
    },
    {
      GST_FCV_OP_MODE_CPU_OFFLOAD,
        "Uses highest performance implementation", "cpu-offload"
    },
    {
      GST_FCV_OP_MODE_CPU_PERFORMANCE,
        "Uses CPU highest performance implementation", "cpu-performance"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
      gtype = g_enum_register_static ("GstFcvOpMode", variants);

  return gtype;
}

GstVideoBlits*
gst_video_blits_new (void)
{
  GstVideoBlits *vblits = g_slice_new (GstVideoBlits);

  g_atomic_ref_count_init (&vblits->refcount);
  vblits->entries = g_array_new (FALSE, TRUE, sizeof (GstVideoBlit));

  return vblits;
}

GstVideoBlits*
gst_video_blits_new_sized (guint size)
{
  GstVideoBlits *vblits = g_slice_new (GstVideoBlits);

  g_atomic_ref_count_init (&vblits->refcount);
  vblits->entries = g_array_sized_new (FALSE, TRUE, sizeof (GstVideoBlit), size);
  g_array_set_size (vblits->entries, size);

  return vblits;
}

GstVideoBlits*
gst_video_blits_new_take (gpointer data, guint size)
{
  GstVideoBlits *vblits = g_slice_new (GstVideoBlits);

  g_atomic_ref_count_init (&vblits->refcount);
  vblits->entries = g_array_new_take (data, size, TRUE, sizeof (GstVideoBlit));

  return vblits;
}

GstVideoBlits*
gst_video_blits_ref (GstVideoBlits * vblits)
{
  g_return_val_if_fail (vblits != NULL, NULL);
  g_atomic_ref_count_inc (&vblits->refcount);

  return vblits;
}

void
gst_video_blits_unref (GstVideoBlits * vblits)
{
  g_return_if_fail (vblits != NULL);

  if (g_atomic_ref_count_dec (&vblits->refcount)) {
    g_array_free (vblits->entries, TRUE);
    g_slice_free (GstVideoBlits, vblits);
  }
}

gpointer
gst_video_blits_steal (GstVideoBlits * vblits, guint * size)
{
  gpointer data = NULL;
  gsize length = 0;

  g_return_val_if_fail (vblits != NULL, NULL);

  data = g_array_steal (vblits->entries, &length);
  *size = length;

  g_array_free (vblits->entries, TRUE);
  g_slice_free (GstVideoBlits, vblits);

  return data;
}

GstVideoBlits *
gst_video_blits_copy (const GstVideoBlits * vblits)
{
  GstVideoBlits *newvblits = NULL;

  g_return_val_if_fail (vblits != NULL, NULL);

  newvblits = g_slice_new (GstVideoBlits);
  newvblits->entries = g_array_copy (vblits->entries);
  g_atomic_ref_count_init (&newvblits->refcount);

  return newvblits;
}

void
gst_video_blits_append (GstVideoBlits * vblits, const GstVideoBlit * vblit)
{
  g_return_if_fail (vblits != NULL);
  g_array_append_vals (vblits->entries, vblit, 1);
}

void
gst_video_blits_insert (GstVideoBlits * vblits, guint index,
    const GstVideoBlit * vblit)
{
  g_return_if_fail (vblits != NULL);
  g_array_insert_vals (vblits->entries, index, vblit, 1);
}

void
gst_video_blits_remove (GstVideoBlits * vblits, guint index)
{
  g_return_if_fail (vblits != NULL);
  g_array_remove_index (vblits->entries, index);
}

GstVideoBlit*
gst_video_blits_entry (GstVideoBlits * vblits, guint index)
{
  g_return_val_if_fail (vblits != NULL, NULL);
  return &(g_array_index (vblits->entries, GstVideoBlit, index));
}

guint
gst_video_blits_size (GstVideoBlits * vblits)
{
  g_return_val_if_fail (vblits != NULL, 0);
  return vblits->entries->len;
}

void
gst_video_blits_resize (GstVideoBlits * vblits, guint size)
{
  g_return_if_fail (vblits != NULL);
  g_array_set_size (vblits->entries, size);
}
