/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application to demonstrate camera face detection usecase.
 *
 * Description:
 * This application creates Face detection stream and
 * overlays the bounding boxes on faces detected which can be viewed on display
 * or filesink.
 *
 * Usage:
 * gst-camera-fd-stream-example -d -w 1280 -h 720
 *
 * Help:
 * gst-camera-fd-stream-example --help
 *
 * *****************************************************************
 * Pipeline for face detection: qtiqmmfsrc->qtivoverlay->waylanksink/multifilesink
 * *****************************************************************
 */

#include <glib-unix.h>
#include <gst/gst.h>
#include <unordered_map>
#include <gst/sampleapps/gst_sample_apps_utils.h>
#include <qmmf-sdk/qmmf_camera_metadata.h>

namespace camera = qmmf;

#define TERMINATE_MESSAGE      "APP_TERMINATE_MSG"
#define PIPELINE_STATE_MESSAGE "APP_PIPELINE_STATE_MSG"

#define GST_APP_CONTEXT_CAST(obj)           ((GstAppCtx*)(obj))

/**
 * Default values, if not provided by user
 */
#define DEFAULT_OUTPUT_WIDTH 1824
#define DEFAULT_OUTPUT_HEIGHT 1536
#define DEFAULT_THRESHOLD 1

#define FACE_DETECT_MODE 1

typedef struct _GstAppCtx GstAppCtx;

// Structure to hold the application context
struct _GstAppCtx
{
  // Main application event loop.
  GMainLoop   *mloop;

  // GStreamer pipeline instance.
  GstElement  *pipeline;

  // Asynchronous queue thread communication.
  GAsyncQueue *messages;

  // Confidence threshold
  gint threshold;
};

static gboolean eos_on_shutdown = TRUE;
static gboolean display = FALSE;
static gint stream_width = DEFAULT_OUTPUT_WIDTH;
static gint stream_height = DEFAULT_OUTPUT_HEIGHT;

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstAppCtx *
gst_app_context_new ()
{
  GstAppCtx *ctx = NULL;
  g_return_val_if_fail ((ctx = g_new0 (GstAppCtx, 1)) != NULL, NULL);

  ctx->mloop = NULL;
  ctx->pipeline = NULL;
  ctx->messages = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);
  ctx->threshold = DEFAULT_THRESHOLD;

  return ctx;
}

/**
 * Free Application context:
 *
 * @param ctx Application Context object
 */
static void
gst_app_context_free (GstAppCtx * ctx)
{
  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  if (ctx->pipeline != NULL)
    gst_object_unref (ctx->pipeline);

  g_async_queue_unref (ctx->messages);

  g_free (ctx);
  return;
}

/**
 * Get the element reference from the pipeline:
 *
 * @param pipeline Pipeline object
 * @param factory_name Factory name of the element
 * @return Element object
 */
static GstElement*
get_element_from_pipeline (GstElement * pipeline, const gchar * factory_name)
{
  GstElement *element = NULL;
  GstElementFactory *elem_factory = gst_element_factory_find (factory_name);
  GstIterator *it = NULL;
  GValue value = G_VALUE_INIT;

  // Iterate the pipeline and check factory of each element.
  for (it = gst_bin_iterate_elements (GST_BIN (pipeline));
      gst_iterator_next (it, &value) == GST_ITERATOR_OK;
      g_value_reset (&value)) {
    element = GST_ELEMENT (g_value_get_object (&value));

    if (gst_element_get_factory (element) == elem_factory)
      goto free;
  }
  g_value_reset (&value);
  element = NULL;

free:
  gst_iterator_free (it);
  gst_object_unref (elem_factory);

  return element;
}

gboolean
handle_interrupt_signal (gpointer userdata)
{
  g_print ("\n\nhandle_interrupt_signal ...\n");
  GstAppCtx *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstState state = GST_STATE_VOID_PENDING;
  static gboolean waiting_eos = FALSE;

  // Get the current state of the pipeline.
  gst_element_get_state (appctx->pipeline, &state, NULL, 0);

  if (eos_on_shutdown && !waiting_eos && (state == GST_STATE_PLAYING)) {
    g_print ("\nEOS enabled -- Sending EOS on the pipeline\n");

    gst_element_post_message (GST_ELEMENT (appctx->pipeline),
        gst_message_new_custom (GST_MESSAGE_EOS, GST_OBJECT (appctx->pipeline),
            gst_structure_new_empty ("GST_PIPELINE_INTERRUPT")));

    g_print ("\nWaiting for EOS ...\n");
    waiting_eos = TRUE;
  } else if (eos_on_shutdown && waiting_eos) {
    g_print ("\nInterrupt while waiting for EOS - quit main loop...\n");

    gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
    g_main_loop_quit (appctx->mloop);

    waiting_eos = FALSE;
  } else {
    g_print ("\n\nReceived an interrupt signal, stopping pipeline ...\n");
    gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
    g_print ("\n\nPipeline set to NULL ...\n");
    g_main_loop_quit (appctx->mloop);
  }

  return TRUE;
}

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppCtx *appctx = GST_APP_CONTEXT_CAST (userdata);
  static GstState target_state = GST_STATE_VOID_PENDING;
  static gboolean in_progress = FALSE, buffering = FALSE;

  (void)in_progress;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR: {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_error (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);

      g_print ("\nSetting pipeline to NULL ...\n");
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (TERMINATE_MESSAGE));
      g_main_loop_quit (appctx->mloop);
      break;
    }
    case GST_MESSAGE_WARNING: {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_warning (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);

      break;
    }
    case GST_MESSAGE_EOS: {
      g_print ("\nReceived End-of-Stream from '%s' ...\n",
          GST_MESSAGE_SRC_NAME (message));
      g_print ("\nSetting pipeline to NULL ...\n");
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
      g_main_loop_quit (appctx->mloop);
      break;
    }
    case GST_MESSAGE_REQUEST_STATE: {
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      GstState state;

      gst_message_parse_request_state (message, &state);
      g_print ("\nSetting pipeline state to %s as requested by %s...\n",
          gst_element_state_get_name (state), name);

      gst_element_set_state (appctx->pipeline, state);
      target_state = state;

      g_free (name);

      break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
      GstState oldstate, newstate, pending;

      // Handle state changes only for the pipeline.
      if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (appctx->pipeline))
        break;

      gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
      g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
          gst_element_state_get_name (oldstate),
          gst_element_state_get_name (newstate),
          gst_element_state_get_name (pending));

      g_async_queue_push (appctx->messages, gst_structure_new (
          PIPELINE_STATE_MESSAGE, "new", G_TYPE_UINT, newstate,
          "pending", G_TYPE_UINT, pending, NULL));

      break;
    }
    case GST_MESSAGE_BUFFERING: {
      gint percent = 0;

      gst_message_parse_buffering (message, &percent);
      g_print ("\nBuffering... %d%%  \r", percent);

      if (percent == 100) {
        // Clear the BUFFERING status.
        buffering = FALSE;

        // Done buffering, if the pending state is playing, go back.
        if (target_state == GST_STATE_PLAYING) {
          g_print ("\nFinished buffering, setting state to PLAYING.\n");
          gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);
        }
      } else {
        // Busy buffering...
        gst_element_get_state (appctx->pipeline, NULL, &target_state, 0);

        if (!buffering && target_state == GST_STATE_PLAYING) {
          g_print ("\nBuffering, setting pipeline to PAUSED state.\n");
          gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED);
          target_state = GST_STATE_PAUSED;
        }

        buffering = TRUE;
      }

      break;
    }
    case GST_MESSAGE_PROGRESS: {
      GstProgressType type;
      gchar *code = NULL, *text = NULL;

      gst_message_parse_progress (message, &type, &code, &text);
      g_print ("\nProgress: (%s) %s\n", code, text);

      switch (type) {
        case GST_PROGRESS_TYPE_START:
        case GST_PROGRESS_TYPE_CONTINUE:
          in_progress = TRUE;
          break;
        case GST_PROGRESS_TYPE_COMPLETE:
        case GST_PROGRESS_TYPE_CANCELED:
        case GST_PROGRESS_TYPE_ERROR:
          in_progress = FALSE;
          break;
      }

      g_free (code);
      g_free (text);

      break;
    }
    default:
      break;
  }

  return TRUE;
}

/**
 * Callback when signal_connect to qtiqmmfsrc is called
 */
static void
result_metadata (GstElement *element, gpointer metadata, gpointer userdata) {
    ::camera::CameraMetadata *meta =
      static_cast<::camera::CameraMetadata*> (metadata);
  GstAppCtx *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstElement *pipeline = appctx->pipeline;
  GstElement *overlay;
  GString *bbox_string;
  guint threshold = appctx->threshold;

  static std::unordered_map<std::string, bool> previous_boxes;
  std::unordered_map<std::string, bool> current_boxes;

  if ((overlay = get_element_from_pipeline (pipeline, "qtivoverlay")) == NULL) {
    g_printerr ("ERROR: No overlay plugin found in pipeline, can't proceed.\n");
    return;
  }

  g_object_set (G_OBJECT (overlay), "bboxes", "{ }", NULL);
  bbox_string = g_string_new ("{");

  if (!meta->exists (ANDROID_STATISTICS_FACE_RECTANGLES)) {
    // No rectangles found - disable all previous boxes
    for (const auto& [box_id, _] : previous_boxes) {
      g_string_append_printf (bbox_string, "(structure)\"%s,enable=false;\"",
          box_id.c_str());
      g_string_append (bbox_string, ", ");
    }

    // Remove trailing comma and space
    if (bbox_string->len > 2) {
      g_string_truncate (bbox_string, bbox_string->len - 2);
    }

    g_string_append (bbox_string, "}");
    g_object_set (G_OBJECT (overlay), "bboxes", bbox_string->str, NULL);
    g_string_free (bbox_string, TRUE);
    // Keep previous_boxes as-is to allow reactivation
    return;
  }

  auto rectangles = meta->find (ANDROID_STATISTICS_FACE_RECTANGLES).data.i32;
  auto scores = meta->find (ANDROID_STATISTICS_FACE_SCORES).data.u8;
  auto num_faces = meta->find (ANDROID_STATISTICS_FACE_RECTANGLES).count / 4;

  for (gint i = 0; i < static_cast<int> (num_faces); i++) {
    if ((scores[i] > threshold) && (scores[i] <= 100)) {
      std::string box_id = "Box" + std::to_string (i + 1);
      current_boxes[box_id] = true;

      gint x = rectangles[i * 4];
      gint y = rectangles[i * 4 + 1];
      gint width = rectangles[i * 4 + 2] - rectangles[i * 4];
      gint height = rectangles[i * 4 + 3] - rectangles[i * 4 + 1];

      // Always send full structure with enable=true
      g_string_append_printf (bbox_string,
          "(structure)\"%s,position=<%d,%d>,dimensions=<%d,%d>,enable=true;\"",
          box_id.c_str(), x, y, width, height);
      g_string_append (bbox_string, ", ");
    }
  }

  // Disable boxes that disappeared
  for (const auto& [box_id, _] : previous_boxes) {
    if (current_boxes.find (box_id) == current_boxes.end()) {
      g_string_append_printf (bbox_string,
          "(structure)\"%s,enable=false;\"", box_id.c_str());
      g_string_append (bbox_string, ", ");
    }
  }

  // Remove trailing comma and space
  if (bbox_string->len > 2) {
      g_string_truncate (bbox_string, bbox_string->len - 2);
  }

  g_string_append (bbox_string, "}");

  g_object_set (G_OBJECT (overlay), "bboxes", bbox_string->str, NULL);
  g_string_free (bbox_string, TRUE);
  gst_object_unref (overlay);

  previous_boxes = current_boxes;
}

/**
 * Create GST pipeline involves 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Parameters for plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Object.
 */
static gboolean
create_pipeline (GstAppCtx *appctx)
{
  GstElement *pipeline = NULL;
  GstElement *qtiqmmfsrc = NULL, *capsfilter = NULL, *overlay = NULL;
  GstElement *queue = NULL, *sink = NULL;
  GstCaps *caps = NULL;

  // Create pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("Failed to create pipeline\n");
    return FALSE;
  }

  // Create elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "camera");
  if (!qtiqmmfsrc) {
    g_printerr ("Failed to create qtiqmmfsrc\n");
    goto error_clean_elements;
  }

  capsfilter = gst_element_factory_make ("capsfilter", "camera_caps");
  if (!capsfilter) {
    g_printerr ("Failed to create capsfilter\n");
    goto error_clean_elements;
  }

  overlay = gst_element_factory_make ("qtivoverlay", "overlay");
  if (!overlay) {
    g_printerr ("Failed to create overlay\n");
    goto error_clean_elements;
  }

  queue = gst_element_factory_make ("queue", "queue");
  if (!queue) {
    g_printerr ("Failed to create queue\n");
    goto error_clean_elements;
  }

  if (display)
    sink = gst_element_factory_make ("waylandsink", "waylandsink");
  else
    sink = gst_element_factory_make ("multifilesink", "filesink");

  if (!sink) {
    g_printerr ("Failed to create sink\n");
    goto error_clean_elements;
  }

  //Set plugin properties
  if (display) {
    g_object_set (G_OBJECT (sink), "fullscreen", TRUE, NULL);
  } else {
    g_object_set (sink, "sync", TRUE, "max-files", 2, NULL);
    g_object_set (sink, "location", "/opt/frame%d.yuv", NULL);
  }

  // Set caps
  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, stream_width,
      "height", G_TYPE_INT, stream_height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  // Add elements to pipeline
  gst_bin_add_many (GST_BIN (pipeline), qtiqmmfsrc, capsfilter, overlay, queue,
      sink, NULL);

  // Link elements
  if (!gst_element_link_many (qtiqmmfsrc, capsfilter, overlay, queue, sink, NULL)) {
    g_printerr ("Failed to link elements\n");
    goto error;
  }

  appctx->pipeline = pipeline;

  return TRUE;

error:
  if (pipeline) {
    gst_object_unref (pipeline);
  }
  return FALSE;

error_clean_elements:
  cleanup_gst (&qtiqmmfsrc, &capsfilter, &overlay, &queue, &sink, NULL);
  return FALSE;
}

gint
main (gint argc, gchar *argv[])
{
  GstAppCtx *appctx;
  GOptionContext *optctx;
  GstElement *element = NULL;
  GstBus *bus = NULL;
  GError *error = NULL;
  gchar *pipeline = NULL;
  guint bus_watch_id = 0, intrpt_watch_id = 0;
  gint status = -1;
  gint threshold = DEFAULT_THRESHOLD;
  ::camera::CameraMetadata *meta = nullptr;
  uint8_t tag_val;
  gchar help_description[2048];
  g_set_prgname ("gst-camera-fd-stream-example");

  // Initialize GST library.
  gst_init (&argc, &argv);

  GOptionEntry options[] = {
    {"display", 'd', 0, G_OPTION_ARG_NONE, &display,
        "Show preview on display", NULL},
    {"width", 'w', 0, G_OPTION_ARG_INT, &stream_width,
        "Set the width", NULL},
    {"height", 'h', 0, G_OPTION_ARG_INT, &stream_height,
        "Set the height", NULL},
    {"threshold", 't', 0, G_OPTION_ARG_INT, &threshold,
        "Set the confidence threshold", NULL},
    { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
  };

  snprintf (help_description, 2047,
      "This application helps to create Face detection stream\n"
      "Command:\n"
      "For Display:\n"
      "  gst-camera-fd-stream-example -d -w 1280 -h 720\n"
      "For Filesink:\n"
      "  gst-camera-fd-stream-example -w 1280 -h 720\n"
      "\nOutput:\n"
      "  Upon execution, the application will overlay bounding boxes \n"
      "  on faces detected and generate an output for preview on the display.\n");
  help_description[2047] = '\0';

  optctx = g_option_context_new (help_description);
  g_option_context_add_main_entries (optctx, options, NULL);
  g_option_context_add_group (optctx, gst_init_get_option_group ());

  if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
    g_printerr ("ERROR: Couldn't initialize: %s\n",
        GST_STR_NULL (error->message));

    g_option_context_free (optctx);
    g_clear_error (&error);

    return -1;
  }
  g_option_context_free (optctx);

  if ((appctx = gst_app_context_new ()) == NULL) {
    g_printerr ("ERROR: Couldn't create app context!\n");
    return -1;
  }
  appctx->threshold = threshold;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Create pipeline using factory-based function
  if (!create_pipeline (appctx)) {
    g_printerr ("ERROR: Failed to create pipeline using factory method\n");
    goto exit;
  }

  gst_element_set_state (appctx->pipeline, GST_STATE_READY);

  if ((element =
      get_element_from_pipeline (appctx->pipeline, "qtiqmmfsrc")) == NULL) {
    g_printerr ("ERROR: No camera plugin found in pipeline, can't proceed.\n");
    goto exit;
  }
  g_signal_connect (element, "result-metadata",
    G_CALLBACK (result_metadata), appctx);

  tag_val = FACE_DETECT_MODE;

  gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);

  g_object_get (G_OBJECT (element), "video-metadata", &meta, NULL);

  meta->update (ANDROID_STATISTICS_FACE_DETECT_MODE, &tag_val, 1);
  g_object_set (G_OBJECT (element), "video-metadata", meta, NULL);

  gst_object_unref (element);

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    goto exit;
  }

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    goto exit;
  }

  // Watch for messages on the pipeline's bus.
  bus_watch_id = gst_bus_add_watch (bus, handle_bus_message, appctx);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Run main loop.
  g_main_loop_run (appctx->mloop);

  g_source_remove (bus_watch_id);
  g_source_remove (intrpt_watch_id);

  status = 0;

exit:
  g_free (pipeline);

  gst_app_context_free (appctx);

  gst_deinit ();
  return status;
}
