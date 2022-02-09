/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "gstcvpmeta.h"

#define GST_CAT_DEFAULT gst_cvp_meta_debug_category()
static GstDebugCategory *
gst_cvp_meta_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("cvpmeta", 0, "CVP Meta");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

static gboolean
gst_cvp_optclflow_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstCvpOptclFlowMeta *cvpmeta = GST_CVP_OPTCLFLOW_META_CAST (meta);

  cvpmeta->id = 0;

  cvpmeta->mvectors = NULL;
  cvpmeta->n_vectors = 0;

  cvpmeta->stats = NULL;
  cvpmeta->n_stats = 0;

  return TRUE;
}

static void
gst_cvp_optclflow_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstCvpOptclFlowMeta *cvpmeta = GST_CVP_OPTCLFLOW_META_CAST (meta);

  g_array_unref (cvpmeta->mvectors);
  g_array_unref (cvpmeta->stats);
}

static gboolean
gst_cvp_optclflow_meta_transform (GstBuffer * transbuffer, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstCvpOptclFlowMeta *dmeta, *smeta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    smeta = GST_CVP_OPTCLFLOW_META_CAST (meta);
    dmeta = gst_buffer_add_cvp_optclflow_meta (transbuffer,
        g_array_ref (smeta->mvectors), smeta->n_vectors,
        g_array_ref (smeta->stats), smeta->n_stats);

    if (NULL == dmeta)
      return FALSE;

    dmeta->id = smeta->id;

    GST_DEBUG ("Duplicate CVP Optical Flow metadata");
  } else {
    // Return FALSE, if transform type is not supported.
    return FALSE;
  }
  return TRUE;
}

GType
gst_cvp_optclflow_meta_api_get_type (void)
{
  static volatile GType gtype = 0;
  static const gchar *tags[] = { GST_META_TAG_MEMORY_STR, NULL };

  if (g_once_init_enter (&gtype)) {
    GType type = gst_meta_api_type_register ("GstCvpOptclFlowMetaAPI", tags);
    g_once_init_leave (&gtype, type);
  }
  return gtype;
}

const GstMetaInfo *
gst_cvp_optclflow_meta_get_info (void)
{
  static const GstMetaInfo *minfo = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &minfo)) {
    const GstMetaInfo *info =
        gst_meta_register (GST_CVP_OPTCLFLOW_META_API_TYPE, "GstCvpOptclFlowMeta",
        sizeof (GstCvpOptclFlowMeta), gst_cvp_optclflow_meta_init,
        gst_cvp_optclflow_meta_free, gst_cvp_optclflow_meta_transform);
    g_once_init_leave ((GstMetaInfo **) &minfo, (GstMetaInfo *) info);
  }
  return minfo;
}

GstCvpOptclFlowMeta *
gst_buffer_add_cvp_optclflow_meta (GstBuffer * buffer, GArray * mvectors,
    guint n_vectors, GArray * stats, guint n_stats)
{
  GstCvpOptclFlowMeta *meta = NULL;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail ((mvectors != NULL) && (n_vectors != 0), NULL);

  meta = GST_CVP_OPTCLFLOW_META_CAST (
      gst_buffer_add_meta (buffer, GST_CVP_OPTCLFLOW_META_INFO, NULL));

  if (NULL == meta) {
    GST_ERROR ("Failed to add CVP Optical Flow meta to buffer %p!", buffer);
    return NULL;
  }

  meta->mvectors = mvectors;
  meta->n_vectors = n_vectors;

  meta->stats = stats;
  meta->n_stats = n_stats;

  return meta;
}

GstCvpOptclFlowMeta *
gst_buffer_get_cvp_optclflow_meta (GstBuffer * buffer)
{
  GstMeta *meta = NULL;
  GstCvpOptclFlowMeta *cvpmeta = NULL;
  gpointer state = NULL;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_CVP_OPTCLFLOW_META_API_TYPE))) {
    if (GST_CVP_OPTCLFLOW_META_CAST (meta)->id == 0)
      return GST_CVP_OPTCLFLOW_META_CAST (meta);

    if (cvpmeta == NULL || GST_CVP_OPTCLFLOW_META_CAST (meta)->id < cvpmeta->id)
      cvpmeta = GST_CVP_OPTCLFLOW_META_CAST (meta);
  }
  return NULL;
}

GstCvpOptclFlowMeta *
gst_buffer_get_cvp_optclflow_meta_id (GstBuffer * buffer, guint id)
{
  GstMeta *meta = NULL;
  gpointer state = NULL;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_CVP_OPTCLFLOW_META_API_TYPE))) {
    if (GST_CVP_OPTCLFLOW_META_CAST (meta)->id == id)
      return GST_CVP_OPTCLFLOW_META_CAST (meta);
  }
  return NULL;
}

