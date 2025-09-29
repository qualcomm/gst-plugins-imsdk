/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "mlpostprocess-utils.h"

#include <gst/ml/ml-module-utils.h>
#include <gst/utils/common-utils.h>
#include <gst/utils/batch-utils.h>

GST_DEBUG_CATEGORY_EXTERN (gst_ml_post_process_debug);
#define GST_CAT_DEFAULT gst_ml_post_process_debug

void
gst_module_logging (uint32_t level, const char * msg)
{
  switch (level) {
    case kError:
      GST_ERROR ("%s", msg);
      break;
    case kWarning:
      GST_WARNING ("%s", msg);
      break;
    case kInfo:
      GST_INFO ("%s", msg);
      break;
    case kDebug:
      GST_DEBUG ("%s", msg);
      break;
    case kTrace:
      GST_TRACE ("%s", msg);
      break;
    case kLog:
      GST_LOG ("%s", msg);
      break;
    default:
      GST_LOG ("%s", msg);
      break;
  }
}

GstStructure*
gst_structure_from_dictionary (const Dictionary& dict)
{
  GstStructure *structure = gst_structure_new_empty("xtraparams");

  for (const auto& [key, val] : dict) {
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

Dictionary
gst_ml_structure_to_module_params (const GstStructure * structure)
{
  Dictionary mlparams;

  // Extract the source tensor region with actual video data.
  if (gst_ml_structure_has_source_region (structure)) {
    GstVideoRectangle region = { 0, };

    gst_ml_structure_get_source_region (structure, &region);
    mlparams["input-tensor-region"] =
        Region (region.x, region.y, region.w, region.h);
  }

  // Extract the full dimensions of the input video tensor.
  if (gst_ml_structure_has_source_dimensions (structure)) {
    guint width = 0, height = 0;

    gst_ml_structure_get_source_dimensions (structure, &width, &height);
    mlparams["input-tensor-dimensions"] =
        Resolution (width, height);
  }

  if (gst_structure_has_field (structure, "input-context-index")) {
    guint index = 0;

    gst_structure_get_uint (structure, "input-context-index", &index);
    mlparams["input-context-index"] = index;
  }

  if (gst_structure_has_field (structure, "input-context-tokens")) {
    GPtrArray *ctx_tokens = NULL;
    std::vector<std::string> tokens;

    gst_structure_get (structure, "input-context-tokens", G_TYPE_PTR_ARRAY,
        &ctx_tokens, NULL);

    for (guint idx = 0; idx < ctx_tokens->len; idx++) {
      auto token = reinterpret_cast<gchar*>(g_ptr_array_index (ctx_tokens, idx));
      tokens.push_back (token);
    }

    mlparams["input-context-tokens"] = tokens;
    g_ptr_array_unref (ctx_tokens);
  }

  return mlparams;
}

GstCaps *
gst_ml_caps_from_json (const std::string& json)
{
  auto root = JsonValue::Parse (json);
  if (!root || root->GetType () != JsonType::Object)
    return NULL;

  auto tensors = root->GetArray ("tensors");
  if (tensors.empty ())
    return NULL;

  std::ostringstream out;

  for (const auto& tensor : tensors) {
    if (!tensor || tensor->GetType () != JsonType::Object)
      continue;

    auto type_arr = tensor->GetArray ("format");
    auto dims_arr = tensor->GetArray ("dimensions");

    if (type_arr.empty () || dims_arr.empty ())
      continue;

    out << "neural-network/tensors, ";

    out << "type = (string) { ";
    for (size_t j = 0; j < type_arr.size (); ++j) {
      out << type_arr[j]->AsString ();
      if (j < type_arr.size () - 1) out << ", ";
    }
    out << " }, ";

    out << "dimensions = (int) < ";
    for (size_t d = 0; d < dims_arr.size (); ++d) {
      const auto& dim_entry = dims_arr[d];

      if (!dim_entry || dim_entry->GetType () != JsonType::Array)
        continue;

      out << "<";
      const auto& inner = dim_entry->AsArray ();

      for (size_t v = 0; v < inner.size (); ++v) {
        auto val = inner[v];

        if (val->GetType () == JsonType::Number) {
          out << static_cast<int32_t>(val->AsNumber ());
        } else if (val->GetType () == JsonType::Array) {
          const auto& range = val->AsArray ();

          out << "[";
          for (size_t r = 0; r < range.size (); ++r) {
            out << static_cast<int32_t>(range[r]->AsNumber ());
            if (r < range.size () - 1) out << ", ";
          }
          out << "]";
        }

        if (v < inner.size () - 1) out << ", ";
      }
      out << ">";

      if (d < dims_arr.size () - 1) out << ", ";
    }
    out << " >; ";
  }

  return gst_caps_from_string (out.str ().c_str ());
}

void
gst_ml_module_get_type (GstStructure * structure, GString * result)
{
  const GValue *list = NULL;
  guint length = 0, idx = 0;

  if (!gst_structure_has_field (structure, "type")) {
    GST_WARNING ("No field named 'type' in ml module caps!");
    return;
  }

  list = gst_structure_get_value (structure, "type");
  length = gst_value_list_get_size (list);

  g_string_append_printf (result, "%sType: ", CAPS_IDENTATION);

  for (idx = 0; idx < length; idx++) {
    const GValue *value = gst_value_list_get_value (list, idx);

    g_string_append (result, g_value_get_string (value));

    if ((idx + 1) < length)
      g_string_append (result, ", ");
  }

  g_string_append (result, "\n");
}

void
gst_ml_module_get_dimensions (GstStructure * structure, GString * result)
{
  const GValue *dimensions = NULL;
  guint length = 0, idx = 0;

  if (!gst_structure_has_field (structure, "dimensions")) {
    GST_WARNING ("No field named 'dimensions' in ml module caps!");
    return;
  }

  dimensions = gst_structure_get_value (structure, "dimensions");
  length = gst_value_array_get_size (dimensions);

  for (idx = 0; idx < length; idx++) {
    const GValue *array = NULL;
    guint size = 0, num = 0;

    array = gst_value_array_get_value (dimensions, idx);

    if (array == NULL || !G_VALUE_HOLDS (array, GST_TYPE_ARRAY))
      continue;

    g_string_append_printf (result, "%sTensor %d: ", CAPS_IDENTATION, idx);
    size = gst_value_array_get_size (array);

    for (num = 0; num < size; num++) {
      const GValue *value = gst_value_array_get_value (array, num);

      if (value == NULL)
        continue;

      if (G_VALUE_HOLDS (value, GST_TYPE_INT_RANGE)) {
        gint min_value = gst_value_get_int_range_min (value);
        gint max_value = gst_value_get_int_range_max (value);

        g_string_append_printf (result, "%d-%d", min_value, max_value);
      } else {
        g_string_append_printf (result, "%d", g_value_get_int (value));
      }

      if ((num + 1) < size)
        g_string_append (result, ", ");
    }

    g_string_append (result, "\n");
  }
}

gchar *
gst_ml_module_parse_caps (const GstCaps *caps)
{
  GstStructure *structure = NULL;
  GString *result = g_string_new ("");
  guint size = gst_caps_get_size (caps);
  guint idx = 0;

  g_string_append_printf (result, "\n%sSupported tensors:\n",
      SUPPORTED_TENSORS_IDENTATION);

  for (idx = 0; idx < size; idx++) {
    structure = gst_caps_get_structure (caps, idx);

    gst_ml_module_get_type (structure, result);
    gst_ml_module_get_dimensions (structure, result);
  }

  return g_string_free (result, FALSE);
}

GEnumValue *
gst_ml_enumarate_modules (const gchar * type)
{
  const gchar *filename = NULL;
  guint idx = 0;
  IModule *module;

  guint n_bytes = sizeof (GEnumValue);
  GEnumValue *variants = (GEnumValue *) g_malloc (n_bytes * 2);

  // Initialize the default value.
  variants[idx].value = idx;
  variants[idx].value_name = "No module, default invalid mode";
  variants[idx].value_nick = "none";

  idx++;

  GDir *directory = g_dir_open (GST_ML_MODULES_DIR, 0, NULL);
  gchar *prefix = g_strdup_printf ("lib%s", type);

  while ((directory != NULL) && (filename = g_dir_read_name (directory))) {
    gboolean isvalid = FALSE;

    if (!g_str_has_prefix (filename, prefix))
      continue;

    if (!g_str_has_suffix (filename, ".so"))
      continue;

    gchar *string = g_strdup_printf ("%s/%s", GST_ML_MODULES_DIR, filename);
    isvalid = !g_file_test (string, (GFileTest) (G_FILE_TEST_IS_DIR |
        G_FILE_TEST_IS_SYMLINK));
    g_free (string);

    if (!isvalid)
      continue;

    // Trim the 'lib' prefix and '.so' suffix.
    gchar *name = g_strndup (filename + 3, strlen (filename) - 6);
    // Extract only the unique module name.
    gchar *shortname = g_utf8_strdown (name + strlen (type), -1);

    // Init module
    gchar *location = g_strdup_printf("%s/lib%s.so", GST_ML_MODULES_DIR, name);
    gpointer handle = dlopen (location, RTLD_NOW);
    g_free (location);
    g_free (name);

    if (handle == NULL)
      continue;

    NewIModule NewModule = (NewIModule) dlsym (handle,
        ML_POST_PROCESS_MODULE_NEW_FUNC);
    if (NewModule == NULL) {
      dlclose (handle);
      continue;
    }

    try {
      module = NewModule(gst_module_logging);
    } catch (std::exception& e) {
      dlclose (handle);
      continue;
    }

    GstCaps *caps = gst_ml_caps_from_json (module->Caps ());

    variants = (GEnumValue *) g_realloc (variants, n_bytes * (idx + 2));

    variants[idx].value = idx;
    variants[idx].value_name = gst_ml_module_parse_caps (caps);
    variants[idx].value_nick = shortname;

    idx++;

    delete module;
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

GType
gst_ml_modules_get_type (void)
{
  static GType gtype = 0;
  static GEnumValue *variants = NULL;

  if (gtype)
    return gtype;

  variants = gst_ml_enumarate_modules (GST_ML_MODULES_PREFIX);
  gtype = g_enum_register_static ("GstMLPostProcessModules", variants);

  return gtype;
}

gfloat
gst_ml_post_process_boxes_intersection_score (ObjectDetection& l_box,
    ObjectDetection& r_box)
{
  gfloat width = 0, height = 0, intersection = 0, l_area = 0, r_area = 0;

  // Figure out the width of the intersecting rectangle.
  // 1st: Find out the X axis coordinate of left most Top-Right point.
  width = MIN (l_box.right, r_box.right);
  // 2nd: Find out the X axis coordinate of right most Top-Left point
  // and substract from the previously found value.
  width -= MAX (l_box.left, r_box.left);

  // Negative width means that there is no overlapping.
  if (width <= 0.0F)
    return 0.0F;

  // Figure out the height of the intersecting rectangle.
  // 1st: Find out the Y axis coordinate of bottom most Left-Top point.
  height = MIN (l_box.bottom, r_box.bottom);
  // 2nd: Find out the Y axis coordinate of top most Left-Bottom point
  // and substract from the previously found value.
  height -= MAX (l_box.top, r_box.top);

  // Negative height means that there is no overlapping.
  if (height <= 0.0F)
    return 0.0F;

  // Calculate intersection area.
  intersection = width * height;

  // Calculate the area of the 2 objects.
  l_area = (l_box.right - l_box.left) * (l_box.bottom - l_box.top);
  r_area = (r_box.right - r_box.left) * (r_box.bottom - r_box.top);

  // Intersection over Union score.
  return intersection / (l_area + r_area - intersection);
}

void
gst_ml_post_process_box_displacement_correction (ObjectDetection &l_box,
    ObjectDetections& boxes)
{
  gdouble score = 0.0;
  guint idx = 0;

  for (idx = 0; idx < boxes.size();  idx++) {
    ObjectDetection& r_box = boxes[idx];

    // If labels do not match, continue with next list entry.
    if (l_box.name != r_box.name)
      continue;

    score = gst_ml_post_process_boxes_intersection_score (l_box, r_box);

    // If the score is below the threshold, continue with next list entry.
    if (score <= DISPLACEMENT_THRESHOLD)
      continue;

    // Previously detected box overlaps at ~95 % with current one, use it.
    l_box.top = r_box.top;
    l_box.left = r_box.left;
    l_box.bottom = r_box.bottom;
    l_box.right = r_box.right;

    break;
  }

  return;
}

GQuark
gst_ml_module_caps_get_type (const std::string& json)
{
  auto root = JsonValue::Parse (json);
  if (!root || root->GetType() != JsonType::Object)
    return g_quark_from_string ("");

  try {
    return g_quark_from_string (root->GetString ("type").c_str ());
  } catch (...) {
    return g_quark_from_string ("");
  }
}

void
gst_ml_object_detections_sort_and_push (std::any& output, std::any& predictions)
{
  auto& detections =
      std::any_cast<ObjectDetections&>(predictions);

  std::sort (detections.begin (), detections.end (),
      [](ObjectDetection& l_entry, ObjectDetection& r_entry) {
        return (l_entry.confidence > r_entry.confidence);
      });

  std::any_cast<DetectionPrediction&>(output).push_back (detections);
}

void
gst_ml_image_classifications_sort_and_push (std::any& output, std::any& predictions)
{
  auto& classifications =
      std::any_cast<ImageClassifications&>(predictions);

  std::sort (classifications.begin (), classifications.end (),
      [](ImageClassification& l_entry, ImageClassification& r_entry) {
        return (l_entry.confidence > r_entry.confidence);
      });

  std::any_cast<ImageClassPrediction&>(output).push_back (classifications);
}

void
gst_ml_audio_classifications_sort_and_push (std::any& output, std::any& predictions)
{
  auto& classifications =
      std::any_cast<AudioClassifications&>(predictions);

  std::sort (classifications.begin (), classifications.end (),
      [](AudioClassification& l_entry, AudioClassification& r_entry) {
        return (l_entry.confidence > r_entry.confidence);
      });

  std::any_cast<AudioClassPrediction&>(output).push_back (classifications);
}

void
gst_ml_pose_estimation_sort_and_push (std::any& output, std::any& predictions)
{
  auto& poses =
      std::any_cast<PoseEstimations&>(predictions);

  std::sort (poses.begin (), poses.end (),
      [](PoseEstimation& l_entry, PoseEstimation& r_entry) {
        return (l_entry.confidence > r_entry.confidence);
      });

  std::any_cast<PosePrediction&>(output).push_back (poses);
}

void
gst_ml_text_generation_sort_and_push (std::any& output, std::any& predictions)
{
  auto& texts = std::any_cast<TextGenerations&>(predictions);

  std::sort (texts.begin (), texts.end (),
      [](TextGeneration& l_entry, TextGeneration& r_entry) {
        return (l_entry.confidence > r_entry.confidence);
      });

  std::any_cast<TextPrediction&>(output).push_back (texts);
}

gboolean
gst_video_frame_to_module_frame (const GstVideoFrame &vframe, VideoFrame &frame)
{
  guint idx = 0;

  frame.width = GST_VIDEO_FRAME_WIDTH (&vframe);
  frame.height = GST_VIDEO_FRAME_HEIGHT (&vframe);
  frame.bits = GST_VIDEO_FORMAT_INFO_BITS (vframe.info.finfo);
  frame.n_components = GST_VIDEO_INFO_N_COMPONENTS (&vframe.info);

  switch (GST_VIDEO_FRAME_FORMAT (&vframe)) {
    case GST_VIDEO_FORMAT_GRAY8:
      frame.format = kGRAY8;
      break;
    case GST_VIDEO_FORMAT_RGB:
      frame.format = kRGB888;
      break;
    case GST_VIDEO_FORMAT_BGR:
      frame.format = kBGR888;
      break;
    case GST_VIDEO_FORMAT_ARGB:
      frame.format = kARGB8888;
      break;
    case GST_VIDEO_FORMAT_ABGR:
      frame.format = kABGR8888;
      break;
    case GST_VIDEO_FORMAT_xRGB:
      frame.format = kXRGB8888;
      break;
    case GST_VIDEO_FORMAT_xBGR:
      frame.format = kXBGR8888;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      frame.format = kRGBA8888;
      break;
    case GST_VIDEO_FORMAT_RGBx:
      frame.format = kRGBX8888;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      frame.format = kBGRA8888;
      break;
    case GST_VIDEO_FORMAT_BGRx:
      frame.format = kBGRX8888;
      break;
    default:
      GST_ERROR ("Unsupported video format: %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (&vframe)));
      return FALSE;
  }

  for (idx = 0; idx < GST_VIDEO_FRAME_N_PLANES (&vframe);  idx++) {
    Plane plane;
    plane.data = (uint8_t *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, idx);
    plane.offset = GST_VIDEO_FRAME_PLANE_OFFSET (&vframe, idx);
    plane.stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, idx);

    frame.planes.push_back (plane);
  }

  return TRUE;
}

gboolean
gst_cairo_draw_setup (GstVideoFrame * frame, cairo_surface_t ** surface,
    cairo_t ** context)
{
  cairo_format_t format;
  cairo_font_options_t *options = NULL;

#ifdef HAVE_LINUX_DMA_BUF_H
    if (gst_is_fd_memory (gst_buffer_peek_memory (frame->buffer, 0))) {
      struct dma_buf_sync bufsync;
      gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (frame->buffer, 0));

      bufsync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;

      if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
        GST_WARNING ("DMA IOCTL SYNC START failed!");
    }
#endif // HAVE_LINUX_DMA_BUF_H

  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
    case GST_VIDEO_FORMAT_BGRA:
      format = CAIRO_FORMAT_ARGB32;
      break;
    case GST_VIDEO_FORMAT_BGRx:
      format = CAIRO_FORMAT_RGB24;
      break;
    case GST_VIDEO_FORMAT_BGR16:
      format = CAIRO_FORMAT_RGB16_565;
      break;
    default:
      GST_ERROR ("Unsupported format: %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));
      return FALSE;
  }

  *surface = cairo_image_surface_create_for_data (
      (uint8_t *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0), format,
      GST_VIDEO_FRAME_WIDTH (frame), GST_VIDEO_FRAME_HEIGHT (frame),
      GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0));
  g_return_val_if_fail (*surface, FALSE);

  *context = cairo_create (*surface);
  g_return_val_if_fail (*context, FALSE);

  // Select font.
  cairo_select_font_face (*context, "@cairo:Georgia",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_antialias (*context, CAIRO_ANTIALIAS_BEST);

  // Set font options.
  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_BEST);

  cairo_set_font_options (*context, options);
  cairo_font_options_destroy (options);

  // Clear any leftovers from previous operations.
  cairo_set_operator (*context, CAIRO_OPERATOR_CLEAR);
  cairo_paint (*context);
  // Flush to ensure all writing to the surface has been done.
  cairo_surface_flush (*surface);

  // Set operator to draw over the source.
  cairo_set_operator (*context, CAIRO_OPERATOR_OVER);
  // Mark the surface dirty so Cairo clears its caches.
  cairo_surface_mark_dirty (*surface);

  return TRUE;
}

void
gst_cairo_draw_cleanup (GstVideoFrame * frame, cairo_surface_t * surface,
    cairo_t * context)
{
  // Flush to ensure all writing to the surface has been done.
  cairo_surface_flush (surface);

  cairo_destroy (context);
  cairo_surface_destroy (surface);

#ifdef HAVE_LINUX_DMA_BUF_H
  if (gst_is_fd_memory (gst_buffer_peek_memory (frame->buffer, 0))) {
    struct dma_buf_sync bufsync;
    gint fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (frame->buffer, 0));

    bufsync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;

    if (ioctl (fd, DMA_BUF_IOCTL_SYNC, &bufsync) != 0)
      GST_WARNING ("DMA IOCTL SYNC END failed!");
  }
#endif // HAVE_LINUX_DMA_BUF_H
}

gboolean
gst_ml_tensors_convert (const GstMLFrame& mlframe, GstBuffer * buffer,
    Tensors& tensors)
{
  for (guint num = 0; num < GST_ML_FRAME_N_TENSORS (&mlframe); ++num) {
    TensorType type;
    std::string name;
    std::vector<uint32_t> dimensions;
    void* data = NULL;
    GstMLTensorMeta *mlmeta = NULL;
    guint size = 1;

    mlmeta = gst_buffer_get_ml_tensor_meta_id (buffer, num);

    if (mlmeta == NULL) {
      GST_ERROR ("Invalid tensor meta: %p", mlmeta);
      return FALSE;
    }

    switch (GST_ML_FRAME_TYPE (&mlframe)) {
      case GST_ML_TYPE_INT8:
        type = kInt8;
        break;
      case GST_ML_TYPE_UINT8:
        type = kUint8;
        break;
      case GST_ML_TYPE_INT32:
        type = kInt32;
        break;
      case GST_ML_TYPE_UINT32:
        type = kUint32;
        break;
      case GST_ML_TYPE_FLOAT16:
        type = kFloat16;
        break;
      case GST_ML_TYPE_FLOAT32:
        type = kFloat32;
        break;
      default:
        GST_ERROR ("Unsupported ML type!");
        return FALSE;
    }

    // Workaround: Sometimes mlmeta->name is NULL
    const char *meta_name = g_quark_to_string (mlmeta->name);
    name = std::string ((meta_name != NULL) ? meta_name : "");

    // Always set batch index to 1, the postprocess will not process batching
    dimensions.push_back (1);

    for (guint pos = 1; pos < GST_ML_FRAME_N_DIMENSIONS (&mlframe, num); ++pos) {
      dimensions.push_back (GST_ML_FRAME_DIM (&mlframe, num, pos));
      size *= GST_ML_FRAME_DIM (&mlframe, num, pos);
    }

    // Increment the pointer with the size of single batch and current index.
    data = GST_ML_FRAME_BLOCK_DATA (&mlframe, num);

    tensors.emplace_back(type, name, dimensions, data);
  }

  return TRUE;
}

gboolean
gst_is_valid_protection_meta (const GstProtectionMeta *pmeta)
{
  g_return_val_if_fail (pmeta != NULL, FALSE);

  const GstStructure *structure = pmeta->info;
  gboolean success = TRUE;

  // Check all required fields for protection meta
  success &= gst_structure_has_field (structure, "timestamp");
  if (!success)
    GST_ERROR ("Protection meta has no timestamp!");

  success &= gst_structure_has_field (structure, "sequence-index");
  if (!success)
    GST_ERROR ("Protection meta has no sequence-index!");

  success &= gst_structure_has_field (structure, "sequence-num-entries");
  if (!success)
    GST_ERROR ("Protection meta has no sequence-num-entries!");

  return success;
}
