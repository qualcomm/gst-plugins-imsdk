/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gstcvmeta.h"

#define GST_CAT_DEFAULT gst_cv_meta_debug_category()
static GstDebugCategory *
gst_cv_meta_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("cvmeta", 0, "CV Meta");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

static gboolean
gst_cv_optclflow_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstCvOptclFlowMeta *cvmeta = GST_CV_OPTCLFLOW_META_CAST (meta);

  cvmeta->id = 0;

  cvmeta->mvectors = NULL;
  cvmeta->stats = NULL;

  return TRUE;
}

static void
gst_cv_optclflow_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstCvOptclFlowMeta *cvmeta = GST_CV_OPTCLFLOW_META_CAST (meta);

  g_array_free (cvmeta->mvectors, TRUE);

  if (NULL != cvmeta->stats)
    g_array_free (cvmeta->stats, TRUE);
}

static gboolean
gst_cv_optclflow_meta_transform (GstBuffer * transbuffer, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstCvOptclFlowMeta *dmeta, *smeta;
  GArray *mvectors = NULL, *stats = NULL;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    smeta = GST_CV_OPTCLFLOW_META_CAST (meta);

    // TODO: replace with g_array_copy() in glib version > 2.62
    mvectors = g_array_sized_new (FALSE, FALSE, sizeof (GstCvMotionVector),
        smeta->mvectors->len);
    mvectors->len = smeta->mvectors->len;

    if (smeta->mvectors->len > 0) {
      guint n_bytes = mvectors->len * sizeof (GstCvMotionVector);
      memcpy (mvectors->data, smeta->mvectors->data, n_bytes);
    }

    if (smeta->stats != NULL) {
      // TODO: replace with g_array_copy() in glib version > 2.62
      stats = g_array_sized_new (FALSE, FALSE, sizeof (GstCvOptclFlowStats),
          smeta->stats->len);
      stats->len = smeta->stats->len;

      if (smeta->stats->len > 0) {
        guint n_bytes = stats->len * sizeof (GstCvOptclFlowStats);
        memcpy (stats->data, smeta->stats->data, n_bytes);
      }
    }

    dmeta = gst_buffer_add_cv_optclflow_meta (transbuffer,
        mvectors, stats);

    if (NULL == dmeta)
      return FALSE;

    dmeta->id = smeta->id;

    GST_DEBUG ("Duplicate CV Optical Flow metadata");
  } else {
    // Return FALSE, if transform type is not supported.
    return FALSE;
  }
  return TRUE;
}

GType
gst_cv_optclflow_meta_api_get_type (void)
{
  static GType gtype = 0;
  static const gchar *tags[] = { GST_META_TAG_MEMORY_STR, NULL };

  if (g_once_init_enter (&gtype)) {
    GType type = gst_meta_api_type_register ("GstCvOptclFlowMetaAPI", tags);
    g_once_init_leave (&gtype, type);
  }
  return gtype;
}

const GstMetaInfo *
gst_cv_optclflow_meta_get_info (void)
{
  static const GstMetaInfo *minfo = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &minfo)) {
    const GstMetaInfo *info =
        gst_meta_register (GST_CV_OPTCLFLOW_META_API_TYPE, "GstCvOptclFlowMeta",
        sizeof (GstCvOptclFlowMeta), gst_cv_optclflow_meta_init,
        gst_cv_optclflow_meta_free, gst_cv_optclflow_meta_transform);
    g_once_init_leave ((GstMetaInfo **) &minfo, (GstMetaInfo *) info);
  }
  return minfo;
}

GstCvOptclFlowMeta *
gst_buffer_add_cv_optclflow_meta (GstBuffer * buffer, GArray * mvectors,
    GArray * stats)
{
  GstCvOptclFlowMeta *meta = NULL;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (mvectors != NULL, NULL);

  meta = GST_CV_OPTCLFLOW_META_CAST (
      gst_buffer_add_meta (buffer, GST_CV_OPTCLFLOW_META_INFO, NULL));

  if (NULL == meta) {
    GST_ERROR ("Failed to add CV Optical Flow meta to buffer %p!", buffer);
    return NULL;
  }

  meta->mvectors = mvectors;
  meta->stats = stats;

  return meta;
}

GstCvOptclFlowMeta *
gst_buffer_get_cv_optclflow_meta (GstBuffer * buffer)
{
  GstMeta *meta = NULL;
  GstCvOptclFlowMeta *cvmeta = NULL;
  gpointer state = NULL;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_CV_OPTCLFLOW_META_API_TYPE))) {
    if (GST_CV_OPTCLFLOW_META_CAST (meta)->id == 0)
      return GST_CV_OPTCLFLOW_META_CAST (meta);

    if (cvmeta == NULL || GST_CV_OPTCLFLOW_META_CAST (meta)->id < cvmeta->id)
      cvmeta = GST_CV_OPTCLFLOW_META_CAST (meta);
  }
  return NULL;
}

GstCvOptclFlowMeta *
gst_buffer_get_cv_optclflow_meta_id (GstBuffer * buffer, guint id)
{
  GstMeta *meta = NULL;
  gpointer state = NULL;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_CV_OPTCLFLOW_META_API_TYPE))) {
    if (GST_CV_OPTCLFLOW_META_CAST (meta)->id == id)
      return GST_CV_OPTCLFLOW_META_CAST (meta);
  }
  return NULL;
}

