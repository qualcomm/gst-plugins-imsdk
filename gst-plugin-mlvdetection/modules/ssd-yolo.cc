/*
* Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <gst/ml/gstmlmeta.h>

#include "AnchorBoxProcessing.h"
#include "Interface.h"

#define NMS_CONFIG_PATH "/data/misc/camera/user-config-yolov5m.yml"

#define CAST_TO_GFLOAT(data) ((gfloat*)data)

typedef struct _GstPrivateModule GstPrivateModule;
typedef struct _GstLabel GstLabel;

struct _GstPrivateModule {
  GHashTable *labels;
};

struct _GstLabel {
  gchar *name;
  guint color;
};

static GstLabel *
gst_ml_label_new ()
{
  GstLabel *label = g_new (GstLabel, 1);

  label->name = NULL;
  label->color = 0x00000000;

  return label;
}

static void
gst_ml_label_free (GstLabel * label)
{
  if (label->name != NULL)
    g_free (label->name);

  g_free (label);
}

 gint
gst_ml_compare_predictions (gconstpointer a, gconstpointer b)
{
  const GstMLPrediction *l_prediction, *r_prediction;

  l_prediction = (const GstMLPrediction*)a;
  r_prediction = (const GstMLPrediction*)b;

  if (l_prediction->confidence > r_prediction->confidence)
    return -1;
  else if (l_prediction->confidence < r_prediction->confidence)
    return 1;

  return 0;
}

gpointer
gst_ml_video_detection_module_init (const gchar * labels)
{
  GstPrivateModule *module = NULL;
  GValue list = G_VALUE_INIT;
  guint idx = 0;

  g_value_init (&list, GST_TYPE_LIST);

  if (g_file_test (labels, G_FILE_TEST_IS_REGULAR)) {
    GString *string = NULL;
    GError *error = NULL;
    gchar *contents = NULL;
    gboolean success = FALSE;

    if (!g_file_get_contents (labels, &contents, NULL, &error)) {
      GST_ERROR ("Failed to get labels file contents, error: %s!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return NULL;
    }

    // Remove trailing space and replace new lines with a comma delimiter.
    contents = g_strstrip (contents);
    contents = g_strdelimit (contents, "\n", ',');

    string = g_string_new (contents);
    g_free (contents);

    // Add opening and closing brackets.
    string = g_string_prepend (string, "{ ");
    string = g_string_append (string, " }");

    // Get the raw character data.
    contents = g_string_free (string, FALSE);

    success = gst_value_deserialize (&list, contents);
    g_free (contents);

    if (!success) {
      GST_ERROR ("Failed to deserialize labels file contents!");
      return NULL;
    }
  } else if (!gst_value_deserialize (&list, labels)) {
    GST_ERROR ("Failed to deserialize labels!");
    return NULL;
  }

  module = g_slice_new0 (GstPrivateModule);
  g_return_val_if_fail (module != NULL, NULL);

  module->labels = g_hash_table_new_full (NULL, NULL, NULL,
        (GDestroyNotify) gst_ml_label_free);

  for (idx = 0; idx < gst_value_list_get_size (&list); idx++) {
    GstStructure *structure = NULL;
    GstLabel *label = NULL;
    guint id = 0;

    structure = GST_STRUCTURE (
        g_value_dup_boxed (gst_value_list_get_value (&list, idx)));

    if (structure == NULL) {
      GST_WARNING ("Failed to extract structure!");
      continue;
    } else if (!gst_structure_has_field (structure, "id") ||
        !gst_structure_has_field (structure, "color")) {
      GST_WARNING ("Structure does not contain 'id' and/or 'color' fields!");
      gst_structure_free (structure);
      continue;
    }

    if ((label = gst_ml_label_new ()) == NULL) {
      GST_ERROR ("Failed to allocate label memory!");
      gst_structure_free (structure);
      continue;
    }

    label->name = g_strdup (gst_structure_get_name (structure));
    label->name = g_strdelimit (label->name, "-", ' ');

    gst_structure_get_uint (structure, "color", &label->color);
    gst_structure_get_uint (structure, "id", &id);

    g_hash_table_insert (module->labels, GUINT_TO_POINTER (id), label);
    gst_structure_free (structure);
  }

  g_value_unset (&list);
  return module;
}

void
gst_ml_video_detection_module_deinit (gpointer instance)
{
  GstPrivateModule *module = (GstPrivateModule *) instance;

  if (NULL == module)
    return;

  g_hash_table_destroy (module->labels);
  g_slice_free (GstPrivateModule, module);
}

gboolean
gst_ml_video_detection_module_process (gpointer instance, GstBuffer * buffer,
    GList ** predictions)
{
  GstPrivateModule *module = (GstPrivateModule *) instance;
  GstProtectionMeta *pmeta = NULL;
  GstMapInfo bboxes;
  guint idx = 0, sar_n = 1, sar_d = 1;
  gsize output_tensors_sizes[3];
  uint8_t *output_tensors[3];

  g_return_val_if_fail (module != NULL, FALSE);
  g_return_val_if_fail (buffer != NULL, FALSE);
  g_return_val_if_fail (predictions != NULL, FALSE);

  if (gst_buffer_n_memory (buffer) != 1) {
    GST_ERROR ("Expecting 1 tensor memory blocks but received %u!",
        gst_buffer_n_memory (buffer));
    return FALSE;
  }

  // Harcoded to 3 because all 3 tensors are stored in one memory block
  //for (idx = 0; idx < gst_buffer_n_memory (buffer); idx++) {
  for (idx = 0; idx < 3; idx++) {
    GstMLTensorMeta *mlmeta = NULL;

    if (!(mlmeta = gst_buffer_get_ml_tensor_meta_id (buffer, idx))) {
      GST_ERROR ("Buffer has no ML meta for tensor %u!", idx);
      return FALSE;
    } else if (mlmeta->type != GST_ML_TYPE_UINT8) {
      GST_ERROR ("Buffer has unsupported type for tensor %u!", idx);
      return FALSE;
    } else {
      if (mlmeta->n_dimensions != 5) {
        GST_ERROR ("Incorrect dimensions size!");
        return FALSE;
      }
      output_tensors_sizes[idx] =
          mlmeta->dimensions[2] * mlmeta->dimensions[3] * 255;
    }
  }

  // Map buffer memory blocks.
  if (!gst_buffer_map_range (buffer, 0, 1, &bboxes, GST_MAP_READ)) {
    GST_ERROR ("Failed to map bboxes memory block!");
    return FALSE;
  }

  // Extract the SAR (Source Aspect Ratio).
  if ((pmeta = gst_buffer_get_protection_meta (buffer)) != NULL) {
    sar_n = gst_value_get_fraction_numerator (
        gst_structure_get_value (pmeta->info, g_quark_to_string (1)));
    sar_d = gst_value_get_fraction_denominator (
        gst_structure_get_value (pmeta->info, g_quark_to_string (1)));
  }

  // Separate the memory block to 3 tensors
  output_tensors[2] = (uint8_t *) bboxes.data;
  output_tensors[1] = output_tensors[2] + output_tensors_sizes[2];
  output_tensors[0] = output_tensors[1] + output_tensors_sizes[1];

  std::string configfilepath = NMS_CONFIG_PATH;
  InitParameters params (configfilepath);

  // Initiate the AnchorBoxProcessing class
  AnchorBoxProcessing<anchor::uTensor, uint8_t,
      anchor::fTensor, float, anchor::fTensor, float> anchorBoxProc (params);

  // Place holder for inputs to smart bp-nms
  // Available options:
  // anchor::fTensor, anchor::uTensor, anchor::iTensor, anchor::hfTensor
  std::vector<anchor::uTensor> detections1;
  std::vector<anchor::fTensor> logits1;
  std::vector<anchor::fTensor> lm1;

  //Placeholder for outputs from smartbp-nms
  // Output will store each vector of float
  /*
      [0] -> threadid
      [1] -> box_coordinate_1
      [2] -> box_coordinate_2
      [3] -> box_coordinate_3
      [4] -> box_coordinate_4
      [5] -> box_score
      [6] -> box_class_label
      [7] -> batch_identifier
  */
  std::vector<std::vector<float>> results (0, std::vector<float> (7,0));
  std::vector<std::vector<std::vector<float>>>
      landmarksResults (0, std::vector<std::vector<float>>(
        params.num_landmarks, std::vector<float> (2, 0)));

  for(auto det_name : params.bbox_output_names) {
    auto det_shape = params.outputShapesMap_[det_name];
    if (strcmp (det_name.c_str(), "A524") == 0) {
      anchor::uTensor det_tensor =
          anchor::uTensor ({det_name , det_shape, output_tensors[0]});
      detections1.push_back (det_tensor);
    } else if (strcmp (det_name.c_str(), "A544") == 0) {
      anchor::uTensor det_tensor =
          anchor::uTensor ({det_name , det_shape, output_tensors[1]});
      detections1.push_back (det_tensor);
    } else if (strcmp (det_name.c_str(), "A564") == 0) {
      anchor::uTensor det_tensor =
          anchor::uTensor ({det_name , det_shape, output_tensors[2]});
      detections1.push_back (det_tensor);
    }
  }

  // call the anchor box processing handle
  float batchIdx = 1;
  (anchorBoxProc.*(anchorBoxProc.handle)) (detections1, logits1, lm1,
      results, landmarksResults, batchIdx);

  for(uint32_t k = 0; k < results.size(); k++) {
      uint32_t cls = (uint32_t) results[k][6];
      // The threshold is configured in the config file
      float confidence = results[k][5];
      float top = results[k][2];
      float left = results[k][1];
      float bottom = results[k][4];
      float right = results[k][3];
      GstMLPrediction *prediction = NULL;
      GstLabel *label = NULL;

      label = (GstLabel *) g_hash_table_lookup (module->labels,
          GUINT_TO_POINTER (cls + 1));

      prediction = g_new0 (GstMLPrediction, 1);

      prediction->confidence = confidence * 100.0;
      prediction->label = g_strdup (label ? label->name : "unknown");
      prediction->color = label ? label->color : 0x000000FF;

      prediction->top = top;
      prediction->left = left;
      prediction->bottom = bottom;
      prediction->right = right;

      // Adjust bounding box dimensions with extracted source aspect ratio.
      if (sar_n > sar_d) {
        gdouble coeficient = 0.0;
        gst_util_fraction_to_double (sar_n, sar_d, &coeficient);
        prediction->top /= 384.0 / coeficient;
        prediction->bottom /= 384.0 / coeficient;
        prediction->left /= 384.0;
        prediction->right /= 384.0;
      } else if (sar_n < sar_d) {
        gdouble coeficient = 0.0;
        gst_util_fraction_to_double (sar_d, sar_n, &coeficient);
        prediction->top /= 640.0 / coeficient;
        prediction->bottom /= 640.0 / coeficient;
        prediction->left /= 640.0;
        prediction->right /= 640.0;
      }

      *predictions = g_list_insert_sorted (
          *predictions, prediction, gst_ml_compare_predictions);
  }

  gst_buffer_unmap (buffer, &bboxes);

  return TRUE;
}
