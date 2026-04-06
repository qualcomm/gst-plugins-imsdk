/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ml-post-process-engine.h"

#include <dlfcn.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>

#include <json-glib/json-glib.h>

#include "ml-post-process-utils.h"
#include "modules/qti-ml-post-process.h"

GST_DEBUG_CATEGORY_EXTERN (gst_ml_post_process_debug);
#define GST_CAT_DEFAULT gst_ml_post_process_debug

#define GST_ML_MODULES_PREFIX         "ml-postprocess-"

typedef IModule *(*NewIModule)(LogCallback logger);

static const std::unordered_map<LogLevel, GstDebugLevel> kLogLevelMap = {
  { LogLevel::kError,   GST_LEVEL_ERROR },
  { LogLevel::kWarning, GST_LEVEL_WARNING },
  { LogLevel::kInfo,    GST_LEVEL_INFO },
  { LogLevel::kDebug,   GST_LEVEL_DEBUG },
  { LogLevel::kLog,     GST_LEVEL_LOG },
  { LogLevel::kTrace,   GST_LEVEL_TRACE }
};

static const std::unordered_map<GstMLType, TensorType> kTensorTypeMap = {
  { GST_ML_TYPE_INT8,    TensorType::kInt8 },
  { GST_ML_TYPE_UINT8,   TensorType::kUint8 },
  { GST_ML_TYPE_INT16,   TensorType::kInt16 },
  { GST_ML_TYPE_UINT16,  TensorType::kUint16 },
  { GST_ML_TYPE_INT32,   TensorType::kInt32 },
  { GST_ML_TYPE_UINT32,  TensorType::kUint32 },
  { GST_ML_TYPE_INT64,   TensorType::kInt64 },
  { GST_ML_TYPE_UINT64,  TensorType::kUint64 },
  { GST_ML_TYPE_FLOAT16, TensorType::kFloat16 },
  { GST_ML_TYPE_FLOAT32, TensorType::kFloat32 },
};

static const std::unordered_map<GstVideoFormat, VideoFormat> kVideoFormatMap = {
  { GST_VIDEO_FORMAT_BGRA, VideoFormat::kBGRA8888 },
  { GST_VIDEO_FORMAT_RGBA, VideoFormat::kRGBA8888 },
  { GST_VIDEO_FORMAT_BGRx, VideoFormat::kBGRX8888 },
  { GST_VIDEO_FORMAT_RGBx, VideoFormat::kRGBX8888 },
};

using ProcessFn =
    std::function<gboolean(GstMLEngine*, guint, GstMLFrame*, GstStructure*, gpointer)>;

/**
 * GstMLEngine:
 * @handle: Library handle.
 * @submodule: Pointer to instance of submodule.
 * @type: Module type.
 * @caps: Module capabilities.
 * @stage_id: The ID of this stage of ML inference.
 * @n_results: Maximum number of results to process
 * @mode: Output mode (Video, Text or Tensors)
 * @stabilization: Whether to perform coordinates stabilization on the results
 * @stashedpredictions: Stashed prediction results used fot stabilization
 *
 * Machine learning interface for post-processing submodule.
 */
struct _GstMLEngine {
  gpointer  handle;
  IModule   *submodule;

  GQuark    type;
  GstCaps   *caps;

  guint     stage_id;
  guint     n_results;
  guint     outmode;

  gboolean  stabilization;
  std::any  stashedpredictions;

  // Common processing function.
  ProcessFn process;
};

static void
gst_module_logging (uint32_t level, const char * message)
{
  GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT,
      kLogLevelMap.at(static_cast<LogLevel>(level)), NULL, "%s", message);
}

static GQuark
gst_ml_module_parse_capabilities (const gchar * string, GstCaps ** caps)
{
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *object = NULL;
  JsonArray *tensors = NULL, *types = NULL, *dimensions = NULL;
  GError *error = NULL;
  GstStructure *structure = NULL;
  GQuark mltype = 0;
  guint idx = 0, num = 0, dim = 0;

  parser = json_parser_new ();

  if (!json_parser_load_from_data (parser, string, -1, &error)) {
    GST_ERROR ("Failed to parse JSON string, error: '%s'!",
        GST_STR_NULL (error->message));
    goto cleanup;
  }

  node = json_parser_get_root (parser);

  if (!JSON_NODE_HOLDS_OBJECT (node)) {
    GST_ERROR ("JSON string does not hold an object!");
    goto cleanup;
  }

  object = json_node_get_object (node);

  if ((string = json_object_get_string_member (object, "type")) == NULL) {
    GST_ERROR ("JSON string does not contain 'type' member!");
    goto cleanup;
  }

  mltype = g_quark_from_string (string);

  if ((tensors = json_object_get_array_member (object, "tensors")) == NULL) {
    GST_ERROR ("JSON string does not contain 'tensors' member!");
    goto cleanup;
  }

  for (idx = 0; idx < json_array_get_length (tensors); idx++) {
    GValue value = G_VALUE_INIT, subvalue = G_VALUE_INIT;

    object = json_array_get_object_element (tensors, idx);

    if ((object = json_array_get_object_element (tensors, idx)) == NULL) {
      GST_ERROR ("The JSON 'tensors' object doesn't hold objects!");
      goto cleanup;
    }

    types = json_object_get_array_member (object, "format");
    dimensions = json_object_get_array_member (object, "dimensions");

    if ((types == NULL) || (dimensions == NULL)) {
      GST_ERROR ("The JSON 'tensors' object doesn't hold format and/or dimensions!");
      goto cleanup;
    }

    structure = gst_structure_new_empty ("neural-network/tensors");
    g_value_init (&value, GST_TYPE_LIST);

    for (num = 0; num < json_array_get_length (types); num++) {
      json_node_get_value (json_array_get_element (types, num), &subvalue);
      gst_value_list_append_and_take_value (&value, &subvalue);
    }

    gst_structure_take_value (structure, "type", &value);
    g_value_init (&value, GST_TYPE_ARRAY);

    for (num = 0; num < json_array_get_length (dimensions); num++) {
      JsonArray *array = json_array_get_array_element (dimensions, num);
      GValue tensor = G_VALUE_INIT, dimension = G_VALUE_INIT;

      g_value_init (&tensor, GST_TYPE_ARRAY);

      for (dim = 0; dim < json_array_get_length (array); dim++) {
        node = json_array_get_element (array, dim);

        if (JSON_NODE_HOLDS_ARRAY (node)) {
          JsonArray *subarray = json_node_get_array (node);
          gint min = 0, max = 0;

          if ((subarray == NULL) || (json_array_get_length (subarray) != 2)) {
            GST_ERROR ("An JSON 'dimensions' element is not proper range!");
            goto cleanup;
          }

          min = json_array_get_int_element (subarray, 0);
          max = json_array_get_int_element (subarray, 1);

          g_value_init (&dimension, GST_TYPE_INT_RANGE);
          gst_value_set_int_range (&dimension, min, max);
        } else if (JSON_NODE_HOLDS_VALUE (node)) {
          g_value_init (&dimension, G_TYPE_INT);
          g_value_set_int (&dimension, json_node_get_int (node));
        } else {
          GST_ERROR ("JSON 'dimensions' has unsupported element!");
          goto cleanup;
        }

        gst_value_array_append_and_take_value (&tensor, &dimension);
      }

      gst_value_array_append_and_take_value (&value, &tensor);
    }

    gst_structure_take_value (structure, "dimensions", &value);
    gst_caps_append_structure (*caps, structure);

    // Null the structure value as ownership was taken by caps.
    structure = NULL;
  }

cleanup:
  if (error != NULL)
    g_clear_error (&error);

  if (structure != NULL)
    gst_structure_free (structure);

  g_object_unref (parser);
  return mltype;
}

static GEnumValue *
gst_ml_postprocess_enumarate_modules (const gchar * type)
{
  const gchar *filename = NULL;
  guint idx = 0;

  guint n_bytes = sizeof (GEnumValue);
  GEnumValue *variants = (GEnumValue *) g_malloc (n_bytes * 2);

  // Initialize the default value.
  variants[idx].value = idx;
  variants[idx].value_name = "No engine, usage of signal callback is allowed";
  variants[idx].value_nick = "none";

  idx++;

  GDir *directory = g_dir_open (GST_ML_MODULES_DIR, 0, NULL);
  gchar *prefix = g_strdup_printf ("lib%s", type);

  while ((directory != NULL) && (filename = g_dir_read_name (directory))) {
    if (!g_str_has_prefix (filename, prefix))
      continue;

    if (!g_str_has_suffix (filename, ".so"))
      continue;

    GFileTest flags =
        static_cast<GFileTest>(G_FILE_TEST_IS_DIR | G_FILE_TEST_IS_SYMLINK);

    gchar *string = g_strdup_printf ("%s/%s", GST_ML_MODULES_DIR, filename);
    gboolean isvalid = !g_file_test (string, flags);
    g_free (string);

    if (!isvalid)
      continue;

    // Trim the 'lib' prefix and '.so' suffix.
    gchar *name = g_strndup (filename + 3, strlen (filename) - 6);
    // Extract only the unique engine name.
    gchar *shortname = g_utf8_strdown (name + strlen (type), -1);

    // Init engine
    gchar *location = g_strdup_printf("%s/lib%s.so", GST_ML_MODULES_DIR, name);
    g_free (name);

    gpointer handle = dlopen (location, RTLD_NOW);
    g_free (location);

    if (handle == NULL)
      continue;

    NewIModule NewModule = reinterpret_cast<NewIModule>(
        dlsym (handle, ML_POST_PROCESS_MODULE_NEW_FUNC));

    if (NewModule == NULL) {
      dlclose (handle);
      continue;
    }

    IModule *submodule = nullptr;

    try {
      submodule = NewModule(gst_module_logging);
    } catch (std::exception& e) {
      dlclose (handle);
      continue;
    }

    GstCaps *caps = gst_caps_new_empty ();
    gst_ml_module_parse_capabilities (submodule->Caps().c_str(), &caps);

    variants =
        reinterpret_cast<GEnumValue*>(g_realloc (variants, n_bytes * (idx + 2)));

    variants[idx].value = idx;
    variants[idx].value_name = gst_ml_caps_to_string (caps);
    variants[idx].value_nick = shortname;

    idx++;
    gst_caps_unref (caps);

    delete submodule;
    dlclose (handle);
  }

  // Last enum entry should be zero.
  variants[idx].value = 0;
  variants[idx].value_name = NULL;
  variants[idx].value_nick = NULL;

  g_free (prefix);

  if (directory != NULL)
    g_dir_close (directory);

  return variants;
}

static GstStructure*
gst_ml_xtraparams_translate (std::optional<Dictionary>& dictionary)
{
  if (!dictionary)
    return NULL;

  GstStructure *structure = gst_structure_new_empty("xtraparams");

  for (const auto& [key, val] : dictionary.value()) {
    if (val.type() == typeid(int32_t)) {
      gst_structure_set(structure, key.c_str(),
          G_TYPE_INT, std::any_cast<int32_t>(val), NULL);
    } else if (val.type() == typeid(float)) {
      gst_structure_set(structure, key.c_str(),
          G_TYPE_FLOAT, std::any_cast<float>(val), NULL);
    } else if (val.type() == typeid(double)) {
      gst_structure_set(structure, key.c_str(),
          G_TYPE_DOUBLE, std::any_cast<double>(val), NULL);
    } else if (val.type() == typeid(bool)) {
      gst_structure_set(structure, key.c_str(),
          G_TYPE_BOOLEAN, std::any_cast<bool>(val), NULL);
    } else if (val.type() == typeid(std::string)) {
      gst_structure_set(structure, key.c_str(),
          G_TYPE_STRING, std::any_cast<std::string>(val).c_str(), NULL);
    } else {
      g_warning("Unsupported type for key '%s'", key.c_str());
    }
  }

  return structure;
}

static Dictionary
gst_ml_param_structure_translate (const GstStructure * structure)
{
  Dictionary mlparams;

  // Extract the source tensor region with actual video data.
  if (gst_ml_structure_has_source_region (structure)) {
    GstVideoRectangle region = {};

    gst_ml_structure_get_source_region (structure, &region);
    mlparams["input-tensor-region"] =
        Region (region.x, region.y, region.w, region.h);
  }

  // Extract the full dimensions of the input video tensor.
  if (gst_ml_structure_has_source_dimensions (structure)) {
    guint width = 0, height = 0;

    gst_ml_structure_get_source_dimensions (structure, &width, &height);
    mlparams["input-tensor-dimensions"] = Resolution (width, height);
  }

  return mlparams;
}

static Tensors
gst_ml_frame_translate (const GstMLFrame * mlframe)
{
  Tensors tensors(GST_ML_FRAME_N_TENSORS (mlframe));

  for (guint idx = 0; idx < GST_ML_FRAME_N_TENSORS (mlframe); ++idx) {
    Tensor& tensor = tensors[idx];

    GstMLTensorMeta *mlmeta =
        gst_buffer_get_ml_tensor_meta_id (mlframe->buffer, idx);

    // Overwrite default name with the information from the meta.
    if (mlmeta->name != 0)
      tensor.name = std::string(g_quark_to_string (mlmeta->name));

    for (guint num = 0; num < GST_ML_FRAME_N_DIMENSIONS (mlframe, idx); ++num)
      tensor.dimensions.push_back(GST_ML_FRAME_DIM (mlframe, idx, num));

    tensor.type = kTensorTypeMap.at(GST_ML_FRAME_TYPE (mlframe));
    tensor.data = GST_ML_FRAME_BLOCK_DATA (mlframe, idx);

    // Add dequantization parameters
    tensor.qscale = mlmeta->qscale;
    tensor.qoffset = mlmeta->qoffset;
  }

  return tensors;
}

static VideoFrame
gst_video_frame_translate (const GstVideoFrame * vframe)
{
  VideoFormat format = kVideoFormatMap.at(GST_VIDEO_FRAME_FORMAT (vframe));
  std::vector<Plane> planes(GST_VIDEO_FRAME_N_PLANES (vframe));

  for (guint idx = 0; idx < GST_VIDEO_FRAME_N_PLANES (vframe); idx++) {
    auto& plane = planes[idx];

    plane.data =
        reinterpret_cast<uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA (vframe, idx));
    plane.offset = GST_VIDEO_FRAME_PLANE_OFFSET (vframe, idx);
    plane.stride = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, idx);

    // Size of current plane is offset to the next one (or total size for last)
    // minus the offset to previous (or 0 in the case of the first plane).
    plane.size = ((idx + 1) < GST_VIDEO_FRAME_N_PLANES (vframe)) ?
        GST_VIDEO_FRAME_PLANE_OFFSET (vframe, idx + 1) :
            GST_VIDEO_FRAME_SIZE (vframe);
    plane.size -= (idx == 0) ? 0 : GST_VIDEO_FRAME_PLANE_OFFSET (vframe, idx);

    std::memset (plane.data, 0, plane.size);
  }

  return VideoFrame(GST_VIDEO_FRAME_WIDTH (vframe),
      GST_VIDEO_FRAME_HEIGHT (vframe), format, planes);
}

static gfloat
gst_ml_detections_intersection_score (ObjectDetection& l_entry,
    ObjectDetection& r_entry)
{
  gfloat width = 0, height = 0, intersection = 0, l_area = 0, r_area = 0;

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  width = MIN (l_entry.right, r_entry.right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= MAX (l_entry.left, r_entry.left);

  // Negative width means that there is no overlapping.
  if (width <= 0.0F)
    return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  height = MIN (l_entry.bottom, r_entry.bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= MAX (l_entry.top, r_entry.top);

  // Negative height means that there is no overlapping.
  if (height <= 0.0F)
    return 0.0F;

  // Calculate intersection area.
  intersection = width * height;

  // Calculate the area of the 2 objects.
  l_area = (l_entry.right - l_entry.left) * (l_entry.bottom - l_entry.top);
  r_area = (r_entry.right - r_entry.left) * (r_entry.bottom - r_entry.top);

  // Intersection over Union score.
  return intersection / (l_area + r_area - intersection);
}

static inline void
gst_ml_detection_copy_into (GstMLDetection& destination,
    ObjectDetection& source, gboolean correction, gdouble matrix[3][3])
{
  destination.name = g_quark_from_string (source.name.c_str());
  destination.color = source.color.value_or(0xFF0000FF);
  destination.confidence = source.confidence;
  destination.xtraparams = gst_ml_xtraparams_translate (source.xtraparams);

  destination.left = source.left;
  destination.top = source.top;
  destination.right = source.right;
  destination.bottom = source.bottom;

  if (correction)
    gst_ml_detection_affine_transform (&destination, matrix);
}

static inline void
gst_ml_keypoint_copy_into (GstMLKeypoint& destination, Keypoint& source,
    gboolean correction, gdouble matrix[3][3])
{
  destination.name = g_quark_from_string(source.name.c_str());
  destination.color = source.color.value_or(0xFF0000FF);
  destination.x = source.x;
  destination.y = source.y;
  destination.confidence = source.confidence;

  if (correction)
    gst_ml_keypoint_affine_transform (&destination, matrix);
}

static inline void
gst_ml_link_copy_into (GstMLKeypointLink& destination, KeypointLink& source,
    gboolean correction, gdouble matrix[3][3])
{
  gst_ml_keypoint_copy_into (destination.l_kp, source.l_kp, correction, matrix);
  gst_ml_keypoint_copy_into (destination.r_kp, source.r_kp, correction, matrix);
  destination.color = source.color.value_or(0xFF0000FF);
}

static inline void
gst_ml_value_array_append_keypoint (GValue * array, Keypoint& keypoint,
    gboolean correction, gdouble matrix[3][3])
{
  GValue value = G_VALUE_INIT;
  GstMLKeypoint newkeypoint = {};

  GST_TRACE ("Keypoint: '%s' [%f %f] Confidence: %f", keypoint.name.c_str(),
      keypoint.x, keypoint.y, keypoint.confidence);

  g_value_init (&value, GST_TYPE_STRUCTURE);
  gst_ml_keypoint_copy_into (newkeypoint, keypoint, correction, matrix);

  g_value_take_boxed (&value, gst_ml_keypoint_to_structure (&newkeypoint));
  gst_value_array_append_and_take_value (array, &value);
}

static inline void
gst_ml_value_array_append_link (GValue * array, KeypointLink& link)
{
  GValue connection = G_VALUE_INIT, value = G_VALUE_INIT;

  GST_TRACE ("Link: '%s' [%.1f x %.2f] <--> '%s' [%.1f x %.1f]",
      link.l_kp.name.c_str(), link.l_kp.x, link.l_kp.y,
      link.r_kp.name.c_str(), link.r_kp.x, link.r_kp.y);

  g_value_init (&connection, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_STRING);

  g_value_set_string (&value, link.l_kp.name.c_str());
  gst_value_array_append_and_take_value (&connection, &value);

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_string (&value, link.r_kp.name.c_str());
  gst_value_array_append_and_take_value (&connection, &value);

  gst_value_array_append_and_take_value (array, &connection);
}

// Common function for AudioClassification and ImageClassification
template<typename T> static inline void
gst_ml_classifications_serialize (std::vector<T>& predictions, guint stage_id,
    GstStructure * mlparam, GValue * list)
{
  GstMLClassification classification = {};
  guint sequence_idx = 0;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  for (size_t idx = 0; idx < predictions.size(); idx++) {
    auto& entry = predictions[idx];

    classification.name = g_quark_from_string (entry.name.c_str());
    classification.color = entry.color.value_or(0xFF0000FF);
    classification.confidence = entry.confidence;
    classification.xtraparams = gst_ml_xtraparams_translate (entry.xtraparams);

    GST_TRACE ("Label %s Confidence %f", entry.name.c_str(), entry.confidence);
    GstStructure *structure = gst_ml_classification_to_structure (&classification);

    guint32 id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

// Common function for AudioClassification and ImageClassification
template<typename T> static inline gboolean
gst_ml_classifications_visualize (std::vector<T>& predictions,
    G_GNUC_UNUSED GstStructure * mlparam, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;

  gboolean success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  for (size_t idx = 0; (idx < predictions.size()) && success; idx++) {
    auto& entry = predictions[idx];

    success = gst_cairo_draw_label (context, idx, entry.name.c_str(),
        entry.color.value_or(0xFF0000FF));
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static inline void
gst_ml_detections_serialize (std::vector<ObjectDetection>& predictions,
    guint stage_id, GstStructure * mlparam, GValue * list)
{
  GstMLDetection detection = {};
  guint sequence_idx = 0;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  gdouble matrix[3][3] = {};
  auto correction = gst_ml_structure_get_inverse_affine_matrix (mlparam, matrix);

  for (size_t idx = 0; idx < predictions.size(); idx++) {
    auto& entry = predictions[idx];

    gst_ml_detection_copy_into (detection, entry, correction, matrix);
    GstStructure *structure = gst_ml_detection_to_structure (&detection);

    GST_TRACE ("Object: '%s' [%f %f %f %f] Confidence: %f", entry.name.c_str(),
        entry.left, entry.top, entry.right, entry.bottom, entry.confidence);

    if (entry.landmarks && !entry.landmarks->empty()) {
      GValue array = G_VALUE_INIT;
      g_value_init (&array, GST_TYPE_ARRAY);

      for (auto& kp : entry.landmarks.value())
        gst_ml_value_array_append_keypoint (&array, kp, correction, matrix);

      gst_structure_take_value (structure, "landmarks", &array);
    }

    guint32 id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

static inline gboolean
gst_ml_detections_visualize (std::vector<ObjectDetection>& predictions,
    GstStructure * mlparam, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  GstMLDetection detection = {};
  GstMLKeypoint kp = {};

  GstVideoRegionOfInterestMeta *roimeta =
      gst_buffer_setup_image_region (vframe->buffer, mlparam);

  gdouble matrix[3][3] = {};
  auto correction = gst_ml_structure_get_inverse_affine_matrix (mlparam, matrix);

  gboolean success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  for (size_t idx = 0; (idx < predictions.size()) && success; idx++) {
    auto& entry = predictions[idx];

    gst_ml_detection_copy_into (detection, entry, correction, matrix);
    success = gst_cairo_draw_detection (context, &detection, roimeta);

    if (!entry.landmarks || !success)
      continue;

    for (size_t num = 0; (num < entry.landmarks->size()) && success; num++) {
      gst_ml_keypoint_copy_into (kp, entry.landmarks->at(num), correction, matrix);
      success = gst_cairo_draw_keypoint (context, &kp, roimeta);
    }
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static inline void
gst_ml_poses_serialize (std::vector<PoseEstimation>& predictions,
    guint stage_id, GstStructure * mlparam, GValue * list)
{
  GstMLPose pose = {};
  guint sequence_idx = 0;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  gdouble matrix[3][3] = {};
  auto correction = gst_ml_structure_get_inverse_affine_matrix (mlparam, matrix);

  for (size_t idx = 0; idx < predictions.size(); idx++) {
    auto& entry = predictions[idx];

    pose.name = g_quark_from_string (entry.name.c_str());
    pose.confidence = entry.confidence;
    pose.xtraparams = gst_ml_xtraparams_translate (entry.xtraparams);

    GstStructure *structure = gst_ml_pose_to_structure (&pose);
    GValue array = G_VALUE_INIT;

    g_value_init (&array, GST_TYPE_ARRAY);

    for (auto& kp : entry.keypoints)
      gst_ml_value_array_append_keypoint (&array, kp, correction, matrix);

    gst_structure_take_value (structure, "keypoints", &array);
    g_value_init (&array, GST_TYPE_ARRAY);

    for (size_t num = 0; entry.links && num < entry.links->size(); num++)
      gst_ml_value_array_append_link (&array, entry.links->at(num));

    gst_structure_take_value (structure, "connections", &array);

    guint32 id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

static inline gboolean
gst_ml_poses_visualize (std::vector<PoseEstimation>& predictions,
    GstStructure * mlparam, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;
  GstMLKeypoint kp = {};
  GstMLKeypointLink link = {};

  GstVideoRegionOfInterestMeta *roimeta =
      gst_buffer_setup_image_region (vframe->buffer, mlparam);

  gdouble matrix[3][3] = {};
  auto correction = gst_ml_structure_get_inverse_affine_matrix (mlparam, matrix);

  gboolean success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  for (size_t idx = 0; (idx < predictions.size()) && success; idx++) {
    auto& entry = predictions[idx];

    for (size_t num = 0; (num < entry.keypoints.size()) && success; num++) {
      gst_ml_keypoint_copy_into (kp, entry.keypoints[num], correction, matrix);
      success = gst_cairo_draw_keypoint (context, &kp, roimeta);
    }

    if (!entry.links || !success)
      continue;

    for (size_t num = 0; (num < entry.links->size()) && success; num++) {
      gst_ml_link_copy_into (link, entry.links->at(num), correction, matrix);
      success = gst_cairo_draw_link (context, &link, roimeta);
    }
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static inline void
gst_ml_segmentations_serialize (std::vector<Segmentation>& predictions,
    guint stage_id, GstStructure * mlparam, GValue * list)
{
  GstMLSegmentation segmentation = {};
  guint sequence_idx = 0, size = 0;
  gchar *string = NULL;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  for (size_t idx = 0; idx < predictions.size(); idx++) {
    auto& entry = predictions[idx];

    segmentation.n_rows = entry.n_rows;
    segmentation.n_columns = entry.n_columns;
    segmentation.xtraparams = gst_ml_xtraparams_translate (entry.xtraparams);

    GstStructure *structure = gst_ml_segmentation_to_structure (&segmentation);
    GValue value = G_VALUE_INIT;

    GArray *labels =
        g_array_sized_new (FALSE, FALSE, sizeof (GQuark), entry.labels.size());
    g_array_set_size (labels, entry.labels.size());

    for (guint num = 0; num < labels->len; num++) {
      const gchar *label = entry.labels[num].c_str();
      g_array_index (labels, GQuark, num) = g_quark_from_string (label);
    }

    size = entry.n_rows * entry.n_columns * sizeof(GQuark);
    string = g_base64_encode (reinterpret_cast<guchar*>(labels->data), size);
    g_array_free (labels, TRUE);

    g_value_init (&value, G_TYPE_STRING);
    g_value_take_string (&value, string);
    gst_structure_take_value (structure, "labels", &value);

    size = entry.n_rows * entry.n_columns * sizeof(uint32_t);
    string = g_base64_encode (reinterpret_cast<guchar*>(entry.colors.data()), size);

    g_value_init (&value, G_TYPE_STRING);
    g_value_take_string (&value, string);
    gst_structure_take_value (structure, "colors", &value);

    guint32 id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

static inline gboolean
gst_ml_segmentations_visualize (std::vector<Segmentation>& predictions,
    GstStructure * mlparam, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;

  GstVideoRegionOfInterestMeta *roimeta =
      gst_buffer_setup_image_region (vframe->buffer, mlparam);

  gboolean success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  for (size_t idx = 0; (idx < predictions.size()) && success; idx++) {
    auto& entry = predictions[idx];

    success = gst_cairo_draw_mask (context, entry.colors.data(),
        entry.n_rows, entry.n_columns, roimeta);
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static inline void
gst_ml_depth_maps_serialize (std::vector<DepthMap>& predictions,
    guint stage_id, GstStructure * mlparam, GValue * list)
{
  GstMLDepthMap depthmap = {};
  guint sequence_idx = 0, size = 0;
  gchar *string = NULL;

  if (gst_structure_has_field (mlparam, "sequence-index"))
    gst_structure_get_uint (mlparam, "sequence-index", &sequence_idx);

  for (size_t idx = 0; idx < predictions.size(); idx++) {
    auto& entry = predictions[idx];

    depthmap.n_rows = entry.n_rows;
    depthmap.n_columns = entry.n_columns;
    depthmap.xtraparams = gst_ml_xtraparams_translate (entry.xtraparams);

    GstStructure *structure = gst_ml_depth_map_to_structure (&depthmap);
    GValue value = G_VALUE_INIT;

    size = entry.n_rows * entry.n_columns * sizeof(double);
    string = g_base64_encode (reinterpret_cast<guchar*>(entry.values.data()), size);

    g_value_init (&value, G_TYPE_STRING);
    g_value_take_string (&value, string);
    gst_structure_take_value (structure, "values", &value);

    size = entry.n_rows * entry.n_columns * sizeof(uint32_t);
    string = g_base64_encode (reinterpret_cast<guchar*>(entry.colors.data()), size);

    g_value_init (&value, G_TYPE_STRING);
    g_value_take_string (&value, string);
    gst_structure_take_value (structure, "colors", &value);

    guint32 id = GST_META_ID (stage_id, sequence_idx, idx);
    gst_value_array_append_and_take_ml_structure (list, id, structure);
  }
}

static inline gboolean
gst_ml_depth_maps_visualize (std::vector<DepthMap>& predictions,
    GstStructure * mlparam, GstVideoFrame * vframe)
{
  cairo_surface_t* surface = NULL;
  cairo_t* context = NULL;

  GstVideoRegionOfInterestMeta *roimeta =
      gst_buffer_setup_image_region (vframe->buffer, mlparam);

  gboolean success = gst_cairo_draw_setup (vframe, &surface, &context);
  g_return_val_if_fail (success, FALSE);

  for (size_t idx = 0; (idx < predictions.size()) && success; idx++) {
    auto& entry = predictions[idx];

    success = gst_cairo_draw_mask (context, entry.colors.data(),
        entry.n_rows, entry.n_columns, roimeta);
  }

  gst_cairo_draw_cleanup (surface, context);
  return success;
}

static void
gst_ml_engine_detections_stabilization (GstMLEngine * engine, guint batch_idx,
    std::vector<ObjectDetection>& predictions)
{
  if (!engine->stashedpredictions.has_value())
    engine->stashedpredictions = std::vector<std::vector<ObjectDetection>>();

  auto& stashedmlboxes = std::any_cast<
      std::vector<std::vector<ObjectDetection>>&>(engine->stashedpredictions);

  if (batch_idx >= static_cast<guint>(stashedmlboxes.size()))
    stashedmlboxes.resize(batch_idx + 1);

  std::vector<ObjectDetection>& stashed = stashedmlboxes[batch_idx];

  for (auto& l_entry : predictions) {
    // Overwrite current box with previously detected one if intersects.
    for (auto& r_entry : stashed) {
      // If labels do not match, continue with next list entry.
      if (l_entry.name != r_entry.name)
        continue;

      auto score = gst_ml_detections_intersection_score (l_entry, r_entry);

      // If the score is below the threshold, continue with next list entry.
      if (score <= DISPLACEMENT_THRESHOLD)
        continue;

      // Previously detected box overlaps at ~95 % with current one, use it.
      l_entry.top = r_entry.top;
      l_entry.left = r_entry.left;
      l_entry.bottom = r_entry.bottom;
      l_entry.right = r_entry.right;

      break;
    }
  }

  // Stash the previous prediction results.
  stashed = predictions;
}

template<typename T> static gboolean
gst_ml_engine_classification (GstMLEngine * engine, guint batch_idx,
    GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  Tensors tensors = gst_ml_frame_translate (mlframe);
  Dictionary params = gst_ml_param_structure_translate (mlparam);
  std::any data = std::make_any<std::vector<T>>();

  if (!engine->submodule->Process(tensors, params, data)) {
    GST_ERROR ("Failed to process batch %u!", batch_idx);
    return FALSE;
  }

  auto& predictions = std::any_cast<std::vector<T>&>(data);

  std::sort(predictions.begin(), predictions.end(),
      [](auto& a, auto& b) { return (a.confidence > b.confidence);});

  // Limit the number of prediction entries if necessary.
  if (predictions.size() > engine->n_results)
    predictions.resize(engine->n_results);

  if (engine->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = reinterpret_cast<GstVideoFrame*>(output);
    return gst_ml_classifications_visualize (predictions, mlparam, vframe);
  } else if (engine->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *list = reinterpret_cast<GValue*>(output);
    gst_ml_classifications_serialize (predictions, engine->stage_id, mlparam, list);
  }

  return TRUE;
}

static gboolean
gst_ml_engine_object_detection (GstMLEngine * engine, guint batch_idx,
    GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  Tensors tensors = gst_ml_frame_translate (mlframe);
  Dictionary params = gst_ml_param_structure_translate (mlparam);
  std::any data = std::make_any<std::vector<ObjectDetection>>();

  if (!engine->submodule->Process(tensors, params, data)) {
    GST_ERROR ("Failed to process batch %u!", batch_idx);
    return FALSE;
  }

  auto& predictions = std::any_cast<std::vector<ObjectDetection>&>(data);

  std::sort(predictions.begin(), predictions.end(),
      [](auto& a, auto& b) { return (a.confidence > b.confidence);});

  // Limit the number of prediction entries if necessary.
  if (predictions.size() > engine->n_results)
    predictions.resize(engine->n_results);

  if (engine->stabilization)
    gst_ml_engine_detections_stabilization (engine, batch_idx, predictions);

  if (engine->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = reinterpret_cast<GstVideoFrame*>(output);
    return gst_ml_detections_visualize (predictions, mlparam, vframe);
  } else if (engine->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *list = reinterpret_cast<GValue*>(output);
    gst_ml_detections_serialize (predictions, engine->stage_id, mlparam, list);
  }

  return TRUE;
}

static gboolean
gst_ml_engine_pose_estimation (GstMLEngine * engine, guint batch_idx,
    GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  Tensors tensors = gst_ml_frame_translate (mlframe);
  Dictionary params = gst_ml_param_structure_translate (mlparam);
  std::any data = std::make_any<std::vector<PoseEstimation>>();

  if (!engine->submodule->Process(tensors, params, data)) {
    GST_ERROR ("Failed to process batch %u!", batch_idx);
    return FALSE;
  }

  auto& predictions = std::any_cast<std::vector<PoseEstimation>&>(data);

  std::sort(predictions.begin(), predictions.end(),
      [](auto& a, auto& b) { return (a.confidence > b.confidence);});

  // Limit the number of prediction entries if necessary.
  if (predictions.size() > engine->n_results)
    predictions.resize(engine->n_results);

  if (engine->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = reinterpret_cast<GstVideoFrame*>(output);
    return gst_ml_poses_visualize (predictions, mlparam, vframe);
  } else if (engine->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *list = reinterpret_cast<GValue*>(output);
    gst_ml_poses_serialize (predictions, engine->stage_id, mlparam, list);
  }

  return TRUE;
}

static gboolean
gst_ml_engine_segmentation (GstMLEngine * engine, guint batch_idx,
    GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  Tensors tensors = gst_ml_frame_translate (mlframe);
  Dictionary params = gst_ml_param_structure_translate (mlparam);
  std::any data = std::make_any<std::vector<Segmentation>>();

  if (!engine->submodule->Process(tensors, params, data)) {
    GST_ERROR ("Failed to process batch %u!", batch_idx);
    return FALSE;
  }

  auto& predictions = std::any_cast<std::vector<Segmentation>&>(data);

  // Limit the number of prediction entries if necessary.
  if (predictions.size() > engine->n_results)
    predictions.resize(engine->n_results);

  if (engine->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = reinterpret_cast<GstVideoFrame*>(output);
    return gst_ml_segmentations_visualize (predictions, mlparam, vframe);
  } else if (engine->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *list = reinterpret_cast<GValue*>(output);
    gst_ml_segmentations_serialize (predictions, engine->stage_id, mlparam, list);
  }

  return TRUE;
}

static gboolean
gst_ml_engine_depth_estimation (GstMLEngine * engine, guint batch_idx,
    GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  Tensors tensors = gst_ml_frame_translate (mlframe);
  Dictionary params = gst_ml_param_structure_translate (mlparam);
  std::any data = std::make_any<std::vector<DepthMap>>();

  if (!engine->submodule->Process(tensors, params, data)) {
    GST_ERROR ("Failed to process batch %u!", batch_idx);
    return FALSE;
  }

  auto& predictions = std::any_cast<std::vector<DepthMap>&>(data);

  // Limit the number of prediction entries if necessary.
  if (predictions.size() > engine->n_results)
    predictions.resize(engine->n_results);

  if (engine->outmode == GST_OUTPUT_MODE_VIDEO) {
    GstVideoFrame *vframe = reinterpret_cast<GstVideoFrame*>(output);
    return gst_ml_depth_maps_visualize (predictions, mlparam, vframe);
  } else if (engine->outmode == GST_OUTPUT_MODE_TEXT) {
    GValue *list = reinterpret_cast<GValue*>(output);
    gst_ml_depth_maps_serialize (predictions, engine->stage_id, mlparam, list);
  }

  return TRUE;
}

static gboolean
gst_ml_engine_super_resolution (GstMLEngine * engine, guint batch_idx,
    GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  Tensors tensors = gst_ml_frame_translate (mlframe);
  Dictionary params = gst_ml_param_structure_translate (mlparam);

  GstVideoFrame *vframe = reinterpret_cast<GstVideoFrame*>(output);
  std::any data = gst_video_frame_translate (vframe);

  if (!engine->submodule->Process(tensors, params, data)) {
    GST_ERROR ("Failed to process batch %u!", batch_idx);
    return FALSE;
  }

  gst_buffer_setup_image_region (vframe->buffer, mlparam);
  return TRUE;
}

static gboolean
gst_ml_engine_tensors_reshape (GstMLEngine * engine, guint batch_idx,
    GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  Tensors intensors = gst_ml_frame_translate (mlframe);
  Dictionary params = gst_ml_param_structure_translate (mlparam);

  GstMLFrame *outmlframe = reinterpret_cast<GstMLFrame*>(output);
  std::any data = gst_ml_frame_translate (outmlframe);

  if (!engine->submodule->Process(intensors, params, data)) {
    GST_ERROR ("Failed to process batch %u!", batch_idx);
    return FALSE;
  }

  return TRUE;
}

GType
gst_ml_modules_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue *variants = NULL;

  if (gtype)
    return gtype;

  variants = gst_ml_postprocess_enumarate_modules (GST_ML_MODULES_PREFIX);
  gtype = g_enum_register_static ("GstMLPostProcessModules", variants);

  return gtype;
}

GstMLEngine *
gst_ml_engine_new (const gchar * name)
{
  GstMLEngine *engine = new GstMLEngine();
  NewIModule NewModule = nullptr;

  gchar *location = g_strdup_printf ("%s/lib%s%s.so", GST_ML_MODULES_DIR,
      GST_ML_MODULES_PREFIX, name);

  engine->handle = dlopen (location, RTLD_NOW);
  g_free (location);

  if (engine->handle == NULL) {
    GST_ERROR ("Failed to open %s library, error: %s!", name, dlerror ());
    goto error;
  }

  NewModule = reinterpret_cast<NewIModule>(
      dlsym (engine->handle, ML_POST_PROCESS_MODULE_NEW_FUNC));
  if (NewModule == NULL) {
    GST_ERROR ("Failed to link library method %s, error: %s!", name, dlerror ());
    goto error;
  }

  try {
    engine->submodule = NewModule(gst_module_logging);
  } catch (std::exception& e) {
    GST_ERROR ("Failed to create and init new submodule, error: %s!", e.what());
    goto error;
  }

  engine->caps = gst_caps_new_empty ();
  engine->type = gst_ml_module_parse_capabilities (
      engine->submodule->Caps().c_str(), &engine->caps);

  if (gst_caps_is_empty (engine->caps)) {
    GST_ERROR ("Failed to convert engine capabilities to GstCaps!");
    goto error;
  }

  if (engine->type == 0) {
    GST_ERROR ("Failed to extract ML engine type from its capabilities!");
    goto error;
  }

  // Choose processing function based on module type.
  if (GST_IS_IMAGE_CLASSIFICATION (engine->type))
    engine->process = gst_ml_engine_classification<ImageClassification>;
  else if (GST_IS_AUDIO_CLASSIFICATION (engine->type))
    engine->process = gst_ml_engine_classification<AudioClassification>;
  else if (GST_IS_DETECTION (engine->type))
    engine->process = gst_ml_engine_object_detection;
  else if (GST_IS_POSE (engine->type))
    engine->process = gst_ml_engine_pose_estimation;
  else if (GST_IS_SEGMENTATION (engine->type))
    engine->process = gst_ml_engine_segmentation;
  else if (GST_IS_DEPTH_MAP (engine->type))
    engine->process = gst_ml_engine_depth_estimation;
  else if (GST_IS_SUPER_RESOLUTION (engine->type))
    engine->process = gst_ml_engine_super_resolution;
  else if (GST_IS_TENSOR (engine->type))
    engine->process = gst_ml_engine_tensors_reshape;

  GST_INFO ("Created %s submodule.", name);
  return engine;

error:
  gst_ml_engine_free (engine);
  return NULL;
}

void
gst_ml_engine_free (GstMLEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->caps != NULL)
    gst_caps_unref (engine->caps);

  if (engine->submodule != NULL)
    delete engine->submodule;

  if (engine->handle != NULL)
    dlclose (engine->handle);

  delete engine;
}

GstCaps *
gst_ml_engine_get_caps (GstMLEngine * engine)
{
  g_return_val_if_fail (engine != NULL, NULL);
  return gst_caps_ref (engine->caps);
}

GQuark
gst_ml_engine_get_type (GstMLEngine * engine)
{
  g_return_val_if_fail (engine != NULL, 0);
  return engine->type;
}

gboolean
gst_ml_engine_configure (GstMLEngine *engine, guint stage_id, guint n_results,
    guint mode, gboolean stabilization, const gchar *labels, const gchar *opts)
{
  g_return_val_if_fail (engine != NULL, FALSE);

  engine->stage_id = stage_id;
  engine->n_results = n_results;
  engine->outmode = mode;
  engine->stabilization = stabilization;

  std::string settings;

  if ((opts != NULL) && g_file_test (opts, G_FILE_TEST_IS_REGULAR)) {
    GError *error = NULL;
    gchar *contents = NULL;

    if (!g_file_get_contents (opts, &contents, NULL, &error)) {
      GST_ERROR ("Failed to get settings file contents, error: %s!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    }

    settings = std::string(contents);
    g_free (contents);
  } else if (opts != NULL) {
    settings = std::string(opts);
  }

  std::string labelfile = std::string(labels != NULL ? labels : "");

  return engine->submodule->Configure (labelfile, settings);
}

gboolean
gst_ml_engine_execute (GstMLEngine * engine, guint batch_idx,
    GstMLFrame * mlframe, GstStructure * mlparam, gpointer output)
{
  g_return_val_if_fail(engine != NULL, FALSE);
  g_return_val_if_fail(mlframe != NULL, FALSE);
  g_return_val_if_fail(mlparam != NULL, FALSE);
  g_return_val_if_fail(output != NULL, FALSE);

  return engine->process (engine, batch_idx, mlframe, mlparam, output);
}
