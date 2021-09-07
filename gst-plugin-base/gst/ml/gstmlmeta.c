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
*/

#include "gstmlmeta.h"

#define GST_CAT_DEFAULT gst_ml_meta_debug_category()
static GstDebugCategory *
gst_ml_meta_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("mlmeta", 0, "ML Meta");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

static gboolean
gst_ml_tensor_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstMLTensorMeta *mlmeta = GST_ML_TENSOR_META_CAST (meta);

  mlmeta->id = 0;

  mlmeta->type = GST_ML_TYPE_UNKNOWN;
  mlmeta->n_dimensions = 0;
  memset (mlmeta->dimensions, 0, sizeof (mlmeta->dimensions));

  return TRUE;
}

static gboolean
gst_ml_tensor_meta_transform (GstBuffer * transbuffer, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstMLTensorMeta *dmeta, *smeta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    smeta = GST_ML_TENSOR_META_CAST (meta);
    dmeta = gst_buffer_add_ml_tensor_meta (transbuffer, smeta->type,
        smeta->n_dimensions, smeta->dimensions);

    if (NULL == dmeta)
      return FALSE;

    dmeta->id = smeta->id;

    GST_DEBUG ("Duplicate ML Tensor metadata");
  } else {
    // Return FALSE, if transform type is not supported.
    return FALSE;
  }
  return TRUE;
}

GType
gst_ml_tensor_meta_api_get_type (void)
{
  static volatile GType gtype = 0;
  static const gchar *tags[] = { GST_META_TAG_MEMORY_STR, NULL };

  if (g_once_init_enter (&gtype)) {
    GType type = gst_meta_api_type_register ("GstMlTensorMetaAPI", tags);
    g_once_init_leave (&gtype, type);
  }
  return gtype;
}

const GstMetaInfo *
gst_ml_tensor_meta_get_info (void)
{
  static const GstMetaInfo *minfo = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &minfo)) {
    const GstMetaInfo *info =
        gst_meta_register (GST_ML_TENSOR_META_API_TYPE, "GstMlTensorMeta",
        sizeof (GstMLTensorMeta), (GstMetaInitFunction) gst_ml_tensor_meta_init,
        (GstMetaFreeFunction) NULL, gst_ml_tensor_meta_transform);
    g_once_init_leave ((GstMetaInfo **) &minfo, (GstMetaInfo *) info);
  }
  return minfo;
}

GstMLTensorMeta *
gst_buffer_add_ml_tensor_meta (GstBuffer * buffer, GstMLType type,
    guint n_dimensions, guint dimensions[GST_ML_TENSOR_MAX_DIMENSIONS])
{
  GstMLTensorMeta *meta;
  guint idx;

  meta = GST_ML_TENSOR_META_CAST (
      gst_buffer_add_meta (buffer, GST_ML_TENSOR_META_INFO, NULL));

  if (NULL == meta) {
    GST_ERROR ("Failed to add ML Tensor meta to buffer %p!", buffer);
    return NULL;
  }

  meta->type = type;
  meta->n_dimensions = n_dimensions;

  for (idx = 0; idx < meta->n_dimensions; idx++) {
    meta->dimensions[idx] = dimensions[idx];
    GST_LOG ("Dimension %d, value %u", idx, meta->dimensions[idx]);
  }

  return meta;
}

GstMLTensorMeta *
gst_buffer_get_ml_tensor_meta (GstBuffer * buffer)
{
  const GstMetaInfo *info = GST_ML_TENSOR_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;
  GstMLTensorMeta *outmeta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      if (GST_ML_TENSOR_META_CAST (meta)->id == 0)
        return GST_ML_TENSOR_META_CAST (meta);

      if (outmeta == NULL || GST_ML_TENSOR_META_CAST (meta)->id < outmeta->id)
        outmeta = GST_ML_TENSOR_META_CAST (meta);
    }
  }
  return NULL;
}

GstMLTensorMeta *
gst_buffer_get_ml_tensor_meta_id (GstBuffer * buffer, guint id)
{
  const GstMetaInfo *info = GST_ML_TENSOR_META_INFO;
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      if (GST_ML_TENSOR_META_CAST (meta)->id == id)
        return GST_ML_TENSOR_META_CAST (meta);
    }
  }
  return NULL;
}

gsize
gst_ml_meta_tensor_size  (const GstMLTensorMeta * meta)
{
  guint idx = 0, value = 0;
  gsize size = 0;

  for (idx = 0; idx < meta->n_dimensions; idx++) {
    value = (meta->dimensions[idx] != 0) ? meta->dimensions[idx] : 1;
    size = (size != 0) ? (size * value) : value;
  }

  size *= gst_ml_type_get_size (meta->type);
  GST_LOG ("Tensor size %" G_GSIZE_FORMAT, size);

  return size;
}
