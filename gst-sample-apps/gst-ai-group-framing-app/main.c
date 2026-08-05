/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
/*
 * GStreamer Application:
 * GStreamer AI Group Framing Application
 *
 * Description:
 * This application demonstrates an AI-based group framing camera use case.
 * It captures video from qticamsrc, runs YOLOv8 person detection through the
 * Qualcomm TFLite external delegate path, merges person ROIs, applies automatic
 * ROI-based framing, and displays the framed output through Wayland.
 *
 * Features:
 *   -- Camera source selection through camera-id
 *   -- Configurable input capture resolution
 *   -- YOLOv8 object detection using qtimltflite
 *   -- Person ROI merge and ROI auto-framing metadata transforms
 *   -- Optional debug overlay for visualizing detection/framing metadata
 *
 * Pipeline:
 *   qticamsrc(camera=<camera-id>) -> capsfilter -> tee
 *     tee branch 1:
 *       queue -> qtimlvconverter -> queue -> qtimltflite -> qtimlpostprocess
 *         -> qtimetamux metadata pad
 *     tee branch 2:
 *       queue -> qtimetamux -> roi-person-merge -> [qtivoverlay] -> roi-auto-framing
 *         -> qtivsplit -> waylandsink
 *
 * Usage:
 *   gst-ai-group-framing-app [OPTIONS]
 *
 * Example:
 *   gst-ai-group-framing-app --camera-id=0 --width=3840 --height=2160 \
 *     --model-path=/path/to/model.tflite --labels-path=/path/to/labels.json
 *
 * Options:
 *   -c, --camera-id=id                 Camera ID for qticamsrc (default: 0)
 *   -w, --width=width                  Input camera width (default: 3840)
 *   -h, --height=height                Input camera height (default: 2160)
 *   -m, --model-path=path              Path to model file
 *   -l, --labels-path=path             Path to labels file
 *       --mode=debug                   Enable debug overlay
 *       --confidence=value             YOLOv8 confidence threshold (default: 45.0)
 *       --filter-size=size             Auto-framing filter size (default: 240)
 *       --filter-average-size=size     Auto-framing filter average size (default: 48)
 *       --movement-speed=value         Auto-framing movement speed (default: 30)
 *       --pos-threshold=value          Position threshold (default: 0)
 *       --dims-threshold=value         Dimension threshold (default: 0)
 *       --pos-moving-threshold=value   Moving position threshold (default: 0)
 *       --dims-moving-threshold=value  Moving dimension threshold (default: 0)
 *       --max-move-step=value          Maximum move step (default: 0)
 *       --max-crop-ratio=value         Maximum crop ratio (default: 10.0)
 *       --first-rect-start             Start from first rectangle (default: false)
 */
#include <gst/gst.h>

#define DEFAULT_DEBUG_MODE FALSE
#define DEFAULT_CONFIDENCE 45.0
#define DEFAULT_FILTER_SIZE 240
#define DEFAULT_FILTER_AVERAGE_SIZE 48
#define DEFAULT_MOVEMENT_SPEED 30
#define DEFAULT_POS_THRESHOLD 0
#define DEFAULT_DIMS_THRESHOLD 0
#define DEFAULT_POS_MOVING_THRESHOLD 0
#define DEFAULT_DIMS_MOVING_THRESHOLD 0
#define DEFAULT_MAX_MOVE_STEP 0
#define DEFAULT_MAX_CROP_RATIO 10.0
#define DEFAULT_FIRST_RECT_START FALSE
#define DEFAULT_CAMERA_ID 0

#define APP_SUCCESS 0
#define APP_FAILURE -1

#define POSTPROCESS_SETTINGS_SIZE 128
#define AUTO_FRAMING_SETTINGS_SIZE 512

static gboolean
link_src_to_sink (GstElement * src, GstElement * sink)
{
  GstPad *src_pad = gst_element_request_pad_simple (src, "src_%u");
  GstPad *sink_pad = gst_element_get_static_pad (sink, "sink");
  gboolean linked = FALSE;

  if (src_pad && sink_pad)
    linked = (gst_pad_link (src_pad, sink_pad) == GST_PAD_LINK_OK);

  if (sink_pad)
    gst_object_unref (sink_pad);

  if (src_pad)
    gst_object_unref (src_pad);

  return linked;
}

static gint
get_enum_value (const gchar * enum_type_name, const gchar * enum_nick)
{
  GEnumClass *enum_class;
  GEnumValue *enum_value;
  gint value = 0;

  enum_class = g_type_class_ref (g_type_from_name (enum_type_name));

  if (enum_class) {
    enum_value = g_enum_get_value_by_nick (enum_class, enum_nick);
    if (enum_value)
      value = enum_value->value;
    g_type_class_unref (enum_class);
  }

  return value;
}

gint
main (gint argc, gchar * argv[])
{
  gboolean debug_mode = DEFAULT_DEBUG_MODE;
  gdouble confidence = DEFAULT_CONFIDENCE;

  guint filter_size = DEFAULT_FILTER_SIZE;
  guint filter_average_size = DEFAULT_FILTER_AVERAGE_SIZE;
  guint movement_speed = DEFAULT_MOVEMENT_SPEED;
  guint pos_threshold = DEFAULT_POS_THRESHOLD;
  guint dims_threshold = DEFAULT_DIMS_THRESHOLD;
  guint pos_moving_threshold = DEFAULT_POS_MOVING_THRESHOLD;
  guint dims_moving_threshold = DEFAULT_DIMS_MOVING_THRESHOLD;
  guint max_move_step = DEFAULT_MAX_MOVE_STEP;
  gdouble max_crop_ratio = DEFAULT_MAX_CROP_RATIO;
  gboolean first_rect_start = DEFAULT_FIRST_RECT_START;
  guint camera_id = DEFAULT_CAMERA_ID;
  guint caps_width = 3840;
  guint caps_height = 2160;
  gchar *model_path = NULL;
  gchar *labels_path = NULL;
  GstElement *pipeline = NULL;
  GstElement *source = NULL;
  GstElement *capsfilter = NULL;
  GstElement *tee = NULL;
  GstElement *queue1 = NULL;
  GstElement *ml_converter = NULL;
  GstElement *queue2 = NULL;
  GstElement *tflite = NULL;
  GstElement *postprocess = NULL;
  GstElement *metamux = NULL;
  GstElement *queue3 = NULL;
  GstElement *person_merge_transform = NULL;
  GstElement *auto_framing_transform = NULL;
  GstElement *vsplit = NULL;
  GstElement *waylandsink = NULL;
  GstElement *overlay = NULL;
  GstStructure *delegate_options = NULL;
  GstCaps *caps = NULL;
  GstPad *text_src_pad = NULL;
  GstPad *meta_sink_pad = NULL;
  GstPad *vsplit_src_pad = NULL;
  GstPad *wayland_sink_pad = NULL;
  GstBus *bus = NULL;
  GOptionContext *ctx = NULL;
  GError *error = NULL;
  gchar *mode = NULL;
  gchar postprocess_settings[POSTPROCESS_SETTINGS_SIZE];
  gchar afr_settings[AUTO_FRAMING_SETTINGS_SIZE];

  GOptionEntry entries[] = {
    {
      "camera-id", 'c', 0, G_OPTION_ARG_INT, &camera_id,
      "Camera ID for qticamsrc (default: 0)", "id"
    },
    {
      "width", 'w', 0, G_OPTION_ARG_INT, &caps_width,
      "Input camera width (default: 3840)", "width"
    },
    {
      "height", 'h', 0, G_OPTION_ARG_INT, &caps_height,
      "Input camera height (default: 2160)", "height"
    },
    {
      "model-path", 'm', 0, G_OPTION_ARG_STRING, &model_path,
      "Path to model file", "PATH"
    },
    {
      "labels-path", 'l', 0, G_OPTION_ARG_STRING, &labels_path,
      "Path to labels file", "PATH"
    },
    {
      "mode", 0, 0, G_OPTION_ARG_STRING, &mode,
      "Application mode: debug enables overlay", "mode"
    },
    {
      "confidence", 0, 0, G_OPTION_ARG_DOUBLE, &confidence,
      "YOLOv8 confidence threshold (default: 45.0)", "value"
    },
    {
      "filter-size", 0, 0, G_OPTION_ARG_INT, &filter_size,
      "Auto-framing filter size (default: 240)", "size"
    },
    {
      "filter-average-size", 0, 0, G_OPTION_ARG_INT,
      &filter_average_size,
      "Auto-framing filter average size (default: 48)", "size"
    },
    {
      "movement-speed", 0, 0, G_OPTION_ARG_INT, &movement_speed,
      "Auto-framing movement speed (default: 30)", "value"
    },
    {
      "pos-threshold", 0, 0, G_OPTION_ARG_INT, &pos_threshold,
      "Position threshold (default: 0)", "value"
    },
    {
      "dims-threshold", 0, 0, G_OPTION_ARG_INT, &dims_threshold,
      "Dimension threshold (default: 0)", "value"
    },
    {
      "pos-moving-threshold", 0, 0, G_OPTION_ARG_INT,
      &pos_moving_threshold,
      "Moving position threshold (default: 0)", "value"
    },
    {
      "dims-moving-threshold", 0, 0, G_OPTION_ARG_INT,
      &dims_moving_threshold,
      "Moving dimension threshold (default: 0)", "value"
    },
    {
      "max-move-step", 0, 0, G_OPTION_ARG_INT, &max_move_step,
      "Maximum move step (default: 0)", "value"
    },
    {
      "max-crop-ratio", 0, 0, G_OPTION_ARG_DOUBLE, &max_crop_ratio,
      "Maximum crop ratio (default: 10.0)", "value"
    },
    {
      "first-rect-start", 0, 0, G_OPTION_ARG_NONE, &first_rect_start,
      "Start from first rectangle", NULL
    },
    { NULL }
  };

  ctx = g_option_context_new (
      "- GStreamer AI Group Framing Application");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    if (error) {
      g_printerr ("Failed to parse options: %s\n", error->message);
      g_clear_error (&error);
    } else {
      g_printerr ("Failed to parse options\n");
    }
    g_option_context_free (ctx);
    g_free (mode);
    g_free (model_path);
    g_free (labels_path);
    return APP_FAILURE;
  }

  g_option_context_free (ctx);

  if (mode) {
    if (g_strcmp0 (mode, "debug") == 0)
      debug_mode = TRUE;
    else {
      g_printerr ("Unsupported mode: %s\n", mode);
      g_free (mode);
      g_free (model_path);
      g_free (labels_path);
      return APP_FAILURE;
    }
    g_free (mode);
  }

  /* Model path and labels path must be provided via command-line */
  if (!model_path) {
    g_printerr ("ERROR: --model-path is required\n");
    g_free (labels_path);
    return APP_FAILURE;
  }

  if (!labels_path) {
    g_printerr ("ERROR: --labels-path is required\n");
    g_free (model_path);
    return APP_FAILURE;
  }

  pipeline = gst_pipeline_new ("ai-camera-pipeline");
  source = gst_element_factory_make ("qticamsrc", "camera-source");
  capsfilter = gst_element_factory_make ("capsfilter", "camera-caps");
  tee = gst_element_factory_make ("tee", "t");
  queue1 = gst_element_factory_make ("queue", "queue1");
  ml_converter = gst_element_factory_make ("qtimlvconverter",
      "ml-converter");
  queue2 = gst_element_factory_make ("queue", "queue2");
  tflite = gst_element_factory_make ("qtimltflite", "yolov8");
  postprocess = gst_element_factory_make ("qtimlpostprocess", "postprocess");
  metamux = gst_element_factory_make ("qtimetamux", "mux");
  queue3 = gst_element_factory_make ("queue", "queue3");
  person_merge_transform = gst_element_factory_make ("qtimetatransform",
      "person-merge");
  auto_framing_transform = gst_element_factory_make ("qtimetatransform",
      "auto-framing");
  vsplit = gst_element_factory_make ("qtivsplit", "vsplit");
  waylandsink = gst_element_factory_make ("waylandsink", "display");

  if (debug_mode)
    overlay = gst_element_factory_make ("qtivoverlay", "overlay");

  if (!pipeline || !source || !capsfilter || !tee || !queue1 ||
      !ml_converter || !queue2 || !tflite || !postprocess || !metamux ||
      !queue3 || !person_merge_transform || !auto_framing_transform ||
      !vsplit || !waylandsink || (debug_mode && !overlay))
    goto error;

  g_object_set (G_OBJECT (source), "camera", camera_id, NULL);

  delegate_options = gst_structure_new ("QNNExternalDelegate",
      "backend_type", G_TYPE_STRING, "htp", NULL);

  g_object_set (G_OBJECT (tflite),
      "delegate", 7,
      "external-delegate-path", "libQnnTFLiteDelegate.so",
      "external-delegate-options", delegate_options,
      "model", model_path,
      NULL);

  g_snprintf (postprocess_settings, sizeof (postprocess_settings),
      "{\"confidence\": %.1f}",
      confidence);

  g_object_set (G_OBJECT (postprocess),
      "module",
      get_enum_value ("GstMLPostProcessModules", "yolov8"),
      "labels", labels_path,
      "settings", postprocess_settings,
      "bbox-stabilization", TRUE,
      NULL);

  g_object_set (G_OBJECT (person_merge_transform),
      "module",
      get_enum_value ("GstMetaTranformModules", "roi-person-merge"),
      "module-params", "label=person,padding=0.1",
      NULL);

  g_snprintf (afr_settings, sizeof (afr_settings),
      "filter-size=%u,"
      "filter-average-size=%u,"
      "pos-threshold=%u,"
      "dims-threshold=%u,"
      "pos-moving-threshold=%u,"
      "dims-moving-threshold=%u,"
      "movement-speed=%u,"
      "max-move-step=%u,"
      "max-crop-ratio=%f,"
      "first-rect-start=%s",
      filter_size, filter_average_size, pos_threshold,
      dims_threshold, pos_moving_threshold,
      dims_moving_threshold, movement_speed, max_move_step,
      max_crop_ratio,
      first_rect_start ? "true" : "false");

  g_object_set (G_OBJECT (auto_framing_transform),
      "module",
      get_enum_value ("GstMetaTranformModules", "roi-auto-framing"),
      "module-params", afr_settings,
      NULL);

  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, caps_width,
      "height", G_TYPE_INT, caps_height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);
  caps = NULL;

  gst_bin_add_many (GST_BIN (pipeline),
      source, capsfilter, tee, queue1, ml_converter, queue2,
      tflite, postprocess, metamux, queue3,
      person_merge_transform, auto_framing_transform, vsplit,
      waylandsink,
      NULL);

  if (debug_mode)
    gst_bin_add (GST_BIN (pipeline), overlay);

  if (!gst_element_link_many (source, capsfilter, tee, NULL))
    goto error;

  if (!link_src_to_sink (tee, queue1))
    goto error;

  if (!link_src_to_sink (tee, queue3))
    goto error;

  if (!gst_element_link (queue3, metamux))
    goto error;

  text_src_pad = gst_element_get_static_pad (postprocess, "src");
  meta_sink_pad = gst_element_request_pad_simple (metamux, "data_%u");
  if (!text_src_pad || !meta_sink_pad ||
      gst_pad_link (text_src_pad, meta_sink_pad) != GST_PAD_LINK_OK)
    goto error;

  if (!gst_element_link (queue1, ml_converter))
    goto error;

  if (!gst_element_link_many (ml_converter, queue2, tflite,
          postprocess, NULL))
    goto error;

  if (debug_mode) {
    if (!gst_element_link_many (metamux, person_merge_transform,
            overlay, auto_framing_transform, vsplit, NULL))
      goto error;
  } else if (!gst_element_link_many (metamux,
          person_merge_transform, auto_framing_transform, vsplit,
          NULL))
    goto error;

  vsplit_src_pad = gst_element_request_pad_simple (vsplit, "src_%u");
  wayland_sink_pad = gst_element_get_static_pad (waylandsink, "sink");
  if (!vsplit_src_pad || !wayland_sink_pad)
    goto error;

  g_object_set (G_OBJECT (vsplit_src_pad), "mode", 2, NULL);

  if (gst_pad_link (vsplit_src_pad, wayland_sink_pad) !=
      GST_PAD_LINK_OK)
    goto error;

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  bus = gst_element_get_bus (pipeline);
  gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  gst_object_unref (bus);
  gst_object_unref (text_src_pad);
  gst_object_unref (meta_sink_pad);
  gst_object_unref (vsplit_src_pad);
  gst_object_unref (wayland_sink_pad);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  if (delegate_options) {
    gst_structure_free (delegate_options);
  }

  g_free (model_path);
  g_free (labels_path);

  return APP_SUCCESS;

error:
  if (bus)
    gst_object_unref (bus);

  if (caps)
    gst_caps_unref (caps);

  if (text_src_pad)
    gst_object_unref (text_src_pad);

  if (meta_sink_pad)
    gst_object_unref (meta_sink_pad);

  if (vsplit_src_pad)
    gst_object_unref (vsplit_src_pad);

  if (wayland_sink_pad)
    gst_object_unref (wayland_sink_pad);

  if (pipeline) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  if (delegate_options)
    gst_structure_free (delegate_options);

  g_free (model_path);
  g_free (labels_path);

  return APP_FAILURE;
}
