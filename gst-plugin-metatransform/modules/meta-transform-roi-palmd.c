/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <gst/gst.h>
#include <gst/utils/common-utils.h>
#include <gst/video/video.h>
#include <gst/video/video-utils.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

// Set the default debug category.
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
  GstStructure *objparam = NULL, *xtraparams = NULL;
  GValue matrix = G_VALUE_INIT, value = G_VALUE_INIT, boxed = G_VALUE_INIT;
  GArray *keypoints = NULL;
  GstVideoKeypoint *kp = NULL;
  gpointer state = NULL;
  guint cx = 0, cy = 0, w = 0, h = 0;
  gdouble x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0, angle = 0.0, a = 1.3, b = 1.3;
  double denom = 0.0;

  // Iterate over the metas available in the buffer and process them.
  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    // Filter only the palm detection ROI result.
    if (roimeta->roi_type != g_quark_from_static_string ("palm"))
      continue;

    objparam = gst_video_region_of_interest_meta_get_param (roimeta,
        "ObjectDetection");

    gst_structure_get (objparam, "landmarks", G_TYPE_ARRAY, &keypoints, NULL);
    gst_structure_remove_field (objparam, "landmarks");

    cx = roimeta->x + (roimeta->w / 2);
    cy = roimeta->y + (roimeta->h / 2);
    w = roimeta->w;
    h = roimeta->h;

    w *= 2.6;
    h *= 2.6;

    // wrist center
    kp = &(g_array_index (keypoints, GstVideoKeypoint, 0));
    x1 = kp->x;
    y1 = kp->y;

    // middle finger base
    kp = &(g_array_index (keypoints, GstVideoKeypoint, 2));
    x2 = kp->x;
    y2 = kp->y;

    g_array_unref (keypoints);

    denom = sqrt (pow ((x2 - x1), 2) + pow ((y1 - y2), 2));
    if (denom != 0) {
      angle = acos ((y1 - y2) / denom);
    }

    if (x1 > x2)
      angle = -angle;

    cx -= 0.5 * roimeta->w * sin (-angle);
    cy -= 0.5 * roimeta->h * cos (-angle);

    roimeta->x = cx - w / 2;
    roimeta->y = cy - h / 2;
    roimeta->w = w;
    roimeta->h = h;

    // TODO: this conversion of the roi to square might not be needed
    if (roimeta->w > roimeta->h)
      roimeta->h = roimeta->w;
    else
      roimeta->w = roimeta->h;

    xtraparams = gst_structure_new_empty ("ExtraParams");

    g_value_init (&matrix, GST_TYPE_ARRAY);
    g_value_init (&value, G_TYPE_DOUBLE);

    g_value_set_double (&value, a * (cos (angle)));
    gst_value_array_append_value (&matrix, &value);

    g_value_set_double (&value, - b * sin (angle));
    gst_value_array_append_value (&matrix, &value);

    g_value_set_double (&value, cx * (1 - a * cos (angle)) + cy * b * sin (angle));
    gst_value_array_append_value (&matrix, &value);

    g_value_set_double (&value, a * (sin (angle)));
    gst_value_array_append_value (&matrix, &value);

    g_value_set_double (&value, b * (cos (angle)));
    gst_value_array_append_value (&matrix, &value);

    g_value_set_double (&value, cy * (1 - b * cos (angle)) - cx * a * sin (angle));
    gst_value_array_append_value (&matrix, &value);

    g_value_set_double (&value, 0.0);
    gst_value_array_append_value (&matrix, &value);
    gst_value_array_append_value (&matrix, &value);

    g_value_set_double (&value, 1.0);
    gst_value_array_append_value (&matrix, &value);

    gst_structure_set_value (xtraparams, "affine-matrix", &matrix);

    g_value_init(&boxed, GST_TYPE_STRUCTURE);
    g_value_set_boxed(&boxed, xtraparams);
    gst_structure_set_value (objparam, "xtraparams", &boxed);

    gst_structure_free(xtraparams);

    g_value_unset (&boxed);
    g_value_unset (&value);
    g_value_unset (&matrix);

    objparam = gst_structure_copy (objparam);

    gst_video_region_of_interest_meta_add_param (roimeta, objparam);
  }

  return TRUE;
}
