/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <gst/gst.h>
#include <gst/utils/common-utils.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

/* Set the default debug category (shared with the module loader). */
#define GST_CAT_DEFAULT gst_meta_module_debug
GST_DEBUG_CATEGORY_STATIC (gst_meta_module_debug);

#define GST_META_SUB_MODULE_CAST(obj) ((GstMetaSubModule*)(obj))

typedef struct _GstMetaSubModule GstMetaSubModule;

struct _GstMetaSubModule {
  guint placeholder;
};

gpointer
gst_meta_module_open (GstStructure * settings)
{
  GstMetaSubModule *submodule = NULL;

  GST_DEBUG_CATEGORY_GET (gst_meta_module_debug, "meta-transform-module");

  submodule = g_slice_new0 (GstMetaSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  return (gpointer) submodule;
}

void
gst_meta_module_close (gpointer instance)
{
  GstMetaSubModule *submodule = GST_META_SUB_MODULE_CAST (instance);

  if (NULL == submodule)
    return;

  g_slice_free (GstMetaSubModule, submodule);
}

gboolean
gst_meta_module_process (gpointer instance, GstBuffer * buffer)
{
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  GstVideoRegionOfInterestMeta *merged  = NULL;
  GstStructure *objparam = NULL;
  gpointer state = NULL;

  gint x1 = G_MAXINT, y1 = G_MAXINT, x2 = G_MININT, y2 = G_MININT;
  gint num_persons = 0;

  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {

    if (roimeta->parent_id != -1)
      continue;

    if (roimeta->roi_type != g_quark_from_static_string ("person"))
      continue;

    num_persons++;

    x1 = MIN (x1, (gint) roimeta->x);
    y1 = MIN (y1, (gint) roimeta->y);
    x2 = MAX (x2, (gint) (roimeta->x + roimeta->w));
    y2 = MAX (y2, (gint) (roimeta->y + roimeta->h));
  }

  if (num_persons == 0) {
    GST_DEBUG ("No person ROI found — skipping merged ROI attachment.");
    return TRUE;
  }

  /* Remove all person ROIs safely. Classification, landmark, and all other
   * non-person ROI metas are left untouched. */
  {
    GstMeta *meta = NULL;
    gpointer rm_state = NULL;
    GQuark person_q = g_quark_from_static_string ("person");
    GList *to_remove = NULL;
    GList *l = NULL;

    while ((meta = gst_buffer_iterate_meta_filtered (buffer, &rm_state,
            GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE)) != NULL) {
      GstVideoRegionOfInterestMeta *roi = (GstVideoRegionOfInterestMeta *) meta;
      if (roi->roi_type == person_q)
        to_remove = g_list_prepend (to_remove, meta);
    }

    for (l = to_remove; l != NULL; l = l->next)
      gst_buffer_remove_meta (buffer, (GstMeta *) l->data);

    g_list_free (to_remove);
  }

  x1 = MAX (x1, 0);
  y1 = MAX (y1, 0);
  x2 = MAX (x2, x1);
  y2 = MAX (y2, y1);

  merged = gst_buffer_add_video_region_of_interest_meta (buffer, "person",
      (guint) x1, (guint) y1,
      (guint) (x2 - x1), (guint) (y2 - y1));

  if (merged == NULL) {
    GST_ERROR ("Failed to add merged person ROI meta to buffer!");
    return FALSE;
  }

  objparam = gst_structure_new ("ObjectDetection",
      "label", G_TYPE_STRING, "person", NULL);

  gst_video_region_of_interest_meta_add_param (merged, objparam);

  GST_DEBUG ("Merged person ROI id=0x%X [%u, %u, %u x %u] (person_found=%d)\n",
      merged->id, merged->x, merged->y, merged->w, merged->h,
      (gint) num_persons);

  return TRUE;
}
