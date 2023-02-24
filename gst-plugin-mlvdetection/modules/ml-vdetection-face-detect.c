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

#include "ml-video-detection-module.h"
#include <math.h>
#include <stdio.h>

// output_layers='neuron_47, pool_0, convolution_43, convolution_44'
// sigma = 1 / 0.014005602337, mean = -113.000000000000

#define MAX_FACE_CNT 256
#define MIN_FACE_SIZE 400
#define CONF_THRESHOLD 0.2
#define TENSOR_STRIDE 8

#define INPUT_TENSOR_W 640
#define INPUT_TENSOR_H 480

#define FD_HM_TENSOR 0
#define FD_HM_POOL_TENSOR 1
#define FD_LANDMARK_TENSOR 2
#define FD_BBOXES_TENSOR 3


// Set the default debug category.
#define GST_CAT_DEFAULT gst_ml_module_debug

#define GFLOAT_PTR_CAST(data)       ((gfloat*) data)
#define GST_ML_SUB_MODULE_CAST(obj) ((GstMLSubModule*)(obj))

#define GST_ML_MODULE_CAPS \
    "neural-network/tensors, " \
    "type = (string) { FLOAT32 }, " \
    "dimensions = (int) < < 1, 60, 80, 1 >, < 1, 60, 80, 1 >, < 1, 60, 80, 10 >, < 1, 60, 80, 4 > >; "

// Module caps instance
static GstStaticCaps modulecaps = GST_STATIC_CAPS (GST_ML_MODULE_CAPS);

typedef struct _GstMLSubModule GstMLSubModule;
typedef struct _ScorePair ScorePair;

struct _GstMLSubModule {
  GHashTable *labels;
};

struct _ScorePair {
    float   first;
    int     second;
};

gpointer
gst_ml_module_open (void)
{
  GstMLSubModule *submodule = NULL;

  submodule = g_slice_new0 (GstMLSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  return (gpointer) submodule;
}

void
gst_ml_module_close (gpointer instance)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);

  if (NULL == submodule)
    return;

  if (submodule->labels != NULL)
    g_hash_table_destroy (submodule->labels);

  g_slice_free (GstMLSubModule, submodule);
}

GstCaps *
gst_ml_module_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&modulecaps);
    g_once_init_leave (&inited, 1);
  }

  return caps;
}

gboolean
gst_ml_module_configure (gpointer instance, GstStructure * settings)
{
  GstMLSubModule *submodule = GST_ML_SUB_MODULE_CAST (instance);
  const gchar *input = NULL;
  GValue list = G_VALUE_INIT;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (settings != NULL, FALSE);

  input = gst_structure_get_string (settings, GST_ML_MODULE_OPT_LABELS);
  g_return_val_if_fail (gst_ml_parse_labels (input, &list), FALSE);

  submodule->labels = gst_ml_load_labels (&list);
  g_return_val_if_fail (submodule->labels != NULL, FALSE);

  g_value_unset (&list);
  gst_structure_free (settings);
  return TRUE;
}

static float
computeIOU (GstMLPrediction* face1, GstMLPrediction* face2)
{
  float cx1 = face1->top, cy1 = face1->left, cx2 = face1->bottom, cy2 = face1->right;
  float gx1 = face2->top, gy1 = face2->left, gx2 = face2->bottom, gy2 = face2->right;
  float S_obj1 = (cx2 - cx1 + 1) * (cy2 - cy1 + 1);
  float S_obj2 = (gx2 - gx1 + 1) * (gy2 - gy1 + 1);
  float x1 = cx1 > gx1 ? cx1 : gx1;
  float y1 = cy1 > gy1 ? cy1 : gy1;
  float x2 = cx2 < gx2 ? cx2 : gx2;
  float y2 = cy2 < gy2 ? cy2 : gy2;

  float zero_f = 0.0;
  float w = zero_f > (x2 - x1 + 1) ? zero_f : (x2 - x1 + 1);
  float h = zero_f > (y2 - y1 + 1) ? zero_f : (y2 - y1 + 1);
  float area = w * h;
  return area / (S_obj1 + S_obj2 - area);
};

static GArray *
fdNMS (GArray * facePrediction, float iou)
{
  if (facePrediction->len < 2)
    return facePrediction;

  GArray* faceKeep;
  faceKeep = g_array_new (FALSE, FALSE, sizeof (GstMLPrediction));

  GArray* flag = g_array_new (FALSE, TRUE, sizeof (gboolean));
  g_array_set_size (flag, facePrediction->len);

  for (size_t i = 0; i < facePrediction->len; ++i) {
    if (g_array_index (flag, gboolean, i))
      continue;

    g_array_append_val(faceKeep, g_array_index (facePrediction, GstMLPrediction, i));
    for (size_t j = i + 1; j < facePrediction->len; ++j) {
      if (!g_array_index (flag, gboolean, j) &&
          computeIOU (&g_array_index (facePrediction, GstMLPrediction, i),
                      &g_array_index (facePrediction, GstMLPrediction, j)) > iou) {
        gboolean* setFlag = &g_array_index (flag, gboolean, j);
        *setFlag = TRUE;
      }
    }
  }

  g_array_free (facePrediction, TRUE);

  return faceKeep;
}

static int
sortScorePair (const void *p_val1, const void *p_val2)
{
  const ScorePair *p_confidence1 = (const ScorePair *) (p_val1);
  const ScorePair *p_confidence2 = (const ScorePair *) (p_val2);

  if (p_confidence1->first > p_confidence2->first)
      return -1;
  if (p_confidence1->first < p_confidence2->first)
      return 1;
  return 0;
}

gboolean
gst_ml_module_process (gpointer instance, GstMLFrame * mlframe,
    gpointer output)
{
  GstMLSubModule *submodule = instance;
  GArray *predictions = (GArray *)output;
  GstProtectionMeta *pmeta = NULL;
  guint sar_n = 0, sar_d = 0;
  guint t = 0, idx = 0;
  guint hm_width = 0, class_num = 0;

  g_return_val_if_fail (submodule != NULL, FALSE);
  g_return_val_if_fail (mlframe != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  gfloat * hm_data =
      GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, FD_HM_TENSOR));
  gfloat * hm_pool_data =
      GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, FD_HM_POOL_TENSOR));
  gfloat * landmark_data =
      GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, FD_LANDMARK_TENSOR));
  gfloat * bboxes_data =
      GFLOAT_PTR_CAST (GST_ML_FRAME_BLOCK_DATA (mlframe, FD_BBOXES_TENSOR));

  GstMLTensorMeta *mlmeta = NULL;
  if (!(mlmeta = gst_buffer_get_ml_tensor_meta_id (mlframe->buffer, 0))) {
    GST_ERROR ("Buffer has no ML meta for tensor %u!", idx);
    return FALSE;
  }

  hm_width = mlmeta->dimensions[2];
  class_num = mlmeta->dimensions[3];

  ScorePair confidenceIndex[MAX_FACE_CNT];
  guint size = GST_ML_FRAME_BLOCK_SIZE (mlframe, FD_HM_TENSOR) / sizeof (gfloat);

  GST_INFO ("%s: hm_width:  %u, class_num: %u", __func__, hm_width, class_num);
  GST_INFO ("%s: Size of hm:  %i, hm_pool: %lu, bboxes: %lu", __func__, size,
      GST_ML_FRAME_BLOCK_SIZE (mlframe, FD_HM_POOL_TENSOR),
      GST_ML_FRAME_BLOCK_SIZE (mlframe, FD_BBOXES_TENSOR));

  for (t = 0, idx = 0; t < size && idx < MAX_FACE_CNT; ++t) {
    if (hm_data[t] == hm_pool_data[t]) {
      if (hm_data[t] < CONF_THRESHOLD)
        continue;

      confidenceIndex[idx].first = hm_data[t];
      confidenceIndex[idx].second = t;
      GST_INFO ("%s:kmotov: idx: %u", __func__, idx);
      idx++;
    }
  }

  qsort(confidenceIndex, idx, sizeof(ScorePair), sortScorePair);

  guint confidenceIndexSize = idx;
  GArray *facePrediction;
  GstLabel *label = NULL;
  facePrediction = g_array_new (FALSE, FALSE, sizeof (GstMLPrediction));

  for (idx = 0; idx < confidenceIndexSize; idx++) {

    GST_INFO ("%s: Face detection confidence[%d] %f",__func__, idx,
       confidenceIndex[idx].first);

    int index = confidenceIndex[idx].second;
    int cx = (index / class_num) % hm_width;
    int cy = (index / class_num) / hm_width;

    label = g_hash_table_lookup (submodule->labels,
        GUINT_TO_POINTER (index % class_num));

    GstMLPrediction face;
    face.left = (cx - bboxes_data[(index * 4)]) * TENSOR_STRIDE;
    face.top = (cy - bboxes_data[(index * 4) + 1]) * TENSOR_STRIDE;
    face.right = (cx + bboxes_data[(index * 4) + 2]) * TENSOR_STRIDE;
    face.bottom = (cy + bboxes_data[(index * 4) + 3]) * TENSOR_STRIDE;
    face.confidence = confidenceIndex[idx].first * 100.0; // convert in percent
    face.label = g_strdup (label ? label->name : "unknown");
    face.color = label ? label->color : 0x000000FF;

    g_array_append_val (facePrediction, face);

    for (int k = 0; k < 5; ++k) {
      if (index % class_num == 0) {
        float lx =
            (cx + landmark_data[index / class_num * 10 + k]) * TENSOR_STRIDE;
        float ly =
            (cy + landmark_data[index / class_num * 10 + k + 5]) * TENSOR_STRIDE;
        GST_INFO ("%s: ladnmark: [ %.2f %.2f ] ", __func__, lx, ly);
      }
    }
  }

  facePrediction = fdNMS (facePrediction, 0.3);

  GST_INFO ("%s: Detected %u faces", __func__, facePrediction->len);

  // Extract the SAR (Source Aspect Ratio).
  if ((pmeta = gst_buffer_get_protection_meta (mlframe->buffer)) != NULL) {
    sar_n = gst_value_get_fraction_numerator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));
    sar_d = gst_value_get_fraction_denominator (
        gst_structure_get_value (pmeta->info, "source-aspect-ratio"));
  }

  for (size_t i = 0; i < facePrediction->len; ++i) {
    GstMLPrediction* prediction =
        &g_array_index (facePrediction, GstMLPrediction, i);

    GST_INFO ("%s: BBox: [ %.2f %.2f %.2f %.2f %.6f] ", __func__,
        prediction->left, prediction->top, prediction->right,
        prediction->bottom, prediction->confidence);

    float bb_width = prediction->right - prediction->left;
    float bb_height = prediction->bottom - prediction->top;

    if (bb_width * bb_height < MIN_FACE_SIZE) {
      g_free (prediction->label);
      continue;
    }

    if (prediction->left < 0) {
      prediction->left = 0;
    }
    if (prediction->top < 0) {
      prediction->top = 0;
    }
    if (prediction->right > INPUT_TENSOR_W - 1) {
      prediction->right = INPUT_TENSOR_W - 1;
    }
    if (prediction->bottom > INPUT_TENSOR_H - 1) {
      prediction->bottom = INPUT_TENSOR_H - 1;
    }

    // Convert from absolute to relative coordinages
    if (sar_n > sar_d) {
      gdouble coeficient = 0.0;

      gst_util_fraction_to_double (sar_n, sar_d, &coeficient);

      prediction->top /= (gdouble)INPUT_TENSOR_W / coeficient;
      prediction->bottom /= (gdouble)INPUT_TENSOR_W / coeficient;
      prediction->left /= (gdouble)INPUT_TENSOR_W;
      prediction->right /= (gdouble)INPUT_TENSOR_W;
    } else if (sar_n < sar_d) {
      gdouble coeficient = 0.0;

      gst_util_fraction_to_double (sar_d, sar_n, &coeficient);

      prediction->top /= (gdouble)INPUT_TENSOR_H;
      prediction->bottom /= (gdouble)INPUT_TENSOR_H;
      prediction->left /= (gdouble)INPUT_TENSOR_H / coeficient;
      prediction->right /= (gdouble)INPUT_TENSOR_H / coeficient;
    }

    predictions = g_array_append_val (predictions, *prediction);
  }

  g_array_free (facePrediction, TRUE);

  return TRUE;
}
