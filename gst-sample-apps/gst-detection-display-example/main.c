/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Application:
 * GStreamer single-stream TfLite YOLO detection display example.
 *
 * Description:
 * This sample builds a simple file-based video pipeline:
 *
 *   filesrc -> qtdemux -> h264parse -> v4l2h264dec
 *           -> qtimlvideotflitebin -> qtivoverlay -> waylandsink
 *
 * The pipeline reads an MP4 file containing an H.264 video stream, decodes it
 * with the platform V4L2 decoder, runs YOLOv8 object detection through the QTI
 * ML video TfLite bin, draws the detection results with qtivoverlay, and shows
 * the final video on Wayland.
 *
 * Educational goal:
 * The code intentionally keeps the pipeline explicit and programmatic. It does
 * not use gst_parse_launch(), so every element, property, link, bus handler, and
 * dynamic pad connection is visible and easy to study.
 *
 * Usage:
 *   gst-detection-display-example
 *
 * Help:
 *   gst-detection-display-example --help
 */

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

/*
 * Command-line description printed by --help.
 *
 * Keep this text aligned with the actual pipeline below. It is not a generic
 * multi-stage demo: this file performs one detection stage and then overlays the
 * generated metadata on top of the decoded video.
 */
#define DESCRIPTION \
  "This application sets up a GStreamer pipeline that reads an H.264 MP4 file, " \
  "decodes it, runs YOLOv8 TfLite object detection, overlays the results, " \
  "and displays the output with waylandsink."

/*
 * Default runtime assets.
 *
 * These values are only defaults. The user can override them through the
 * command-line options declared in main().
 */
#define DEFAULT_INPUT_FILE "%s/media/Draw_1080p_180s_30FPS.mp4"

typedef struct _GstAppContext GstAppContext;

/*
 * Application-owned state shared between setup code and callbacks.
 *
 * GStreamer callbacks usually receive only a small user-data pointer. Keeping
 * the important objects here makes callback code simple and avoids global
 * variables.
 */
struct _GstAppContext
{
  /*
   * Top-level bin that owns all pipeline elements.
   *
   * Once an element is added to this pipeline with gst_bin_add_many(), the
   * pipeline owns a reference to it and will release the element when the
   * pipeline itself is unreffed.
   */
  GstElement *pipeline;

  /*
   * GLib main loop used to keep the process alive while GStreamer works
   * asynchronously in streaming threads.
   */
  GMainLoop *mloop;

  /*
   * Runtime configuration. These strings are heap-allocated because GLib's
   * command-line parser can replace them when the user passes options.
   */
  gchar *file;
  gchar *model;
  gchar *labels;

  /*
   * h264parse is stored here because qtdemux creates its source pad later.
   *
   * The pad-added callback needs to link the dynamic qtdemux video pad to this
   * parser after qtdemux discovers the H.264 stream inside the MP4 container.
   */
  GstElement *parse;
};

/*
 * Set an enum property on a GstElement using the enum value name.
 *
 * Many GStreamer properties are enums. g_object_set() can set them by numeric
 * value, but for samples and educational code a string such as "external" or
 * "yolov8" is easier to read. This helper converts the string into the exact
 * enum GValue type expected by the property and then applies it.
 */
static void
gst_element_set_enum_property (GstElement * element, const gchar * propname,
    const gchar * valname)
{
  GValue value = G_VALUE_INIT;
  GParamSpec *propspec = NULL;

  propspec =
      g_object_class_find_property (G_OBJECT_GET_CLASS (element), propname);
  if (propspec == NULL) {
    g_printerr ("ERROR: Element '%s' does not have property '%s'.\n",
        GST_ELEMENT_NAME (element), propname);
    return;
  }

  /*
   * Initialize a GValue with the same type as the destination property, then
   * let GStreamer deserialize the textual enum name into that value.
   */
  g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (propspec));

  if (!gst_value_deserialize (&value, valname)) {
    g_printerr ("ERROR: Failed to deserialize value '%s' for property '%s'.\n",
        valname, propname);
    g_value_unset (&value);
    return;
  }

  g_object_set_property (G_OBJECT (element), propname, &value);
  g_value_unset (&value);
}

/*
 * Link a newly-created dynamic source pad to the static sink pad of target.
 *
 * Educational note:
 * Dynamic pads cannot be linked during normal pipeline construction because
 * they do not exist yet. Demuxers such as qtdemux inspect the input container
 * first and only then emit "pad-added" for each discovered stream.
 */
static gboolean
gst_app_link_dynamic_src_pad (GstPad * srcpad, GstElement * target)
{
  GstPad *sinkpad = NULL;
  GstPadLinkReturn link_ret = GST_PAD_LINK_REFUSED;

  /*
   * Most simple elements have a static sink pad named "sink". We obtain a
   * reference to it, link the dynamic source pad to it, and unref it afterward.
   */
  sinkpad = gst_element_get_static_pad (target, "sink");
  if (sinkpad == NULL) {
    g_printerr ("ERROR: Target element '%s' does not have a static sink pad.\n",
        GST_ELEMENT_NAME (target));
    return FALSE;
  }

  /*
   * qtdemux may expose more than one pad. If the parser is already linked, do
   * not try to link another stream to the same sink pad.
   */
  if (gst_pad_is_linked (sinkpad)) {
    gst_object_unref (sinkpad);
    return TRUE;
  }

  link_ret = gst_pad_link (srcpad, sinkpad);
  gst_object_unref (sinkpad);

  if (GST_PAD_LINK_FAILED (link_ret)) {
    g_printerr ("ERROR: Failed to link dynamic pad to '%s'.\n",
        GST_ELEMENT_NAME (target));
    return FALSE;
  }

  return TRUE;
}

/*
 * Handle qtdemux dynamic pads and link the first H.264 video stream.
 *
 * qtdemux can expose multiple streams, for example video, audio, subtitles, or
 * metadata. This sample intentionally accepts only video/x-h264 because the
 * file input branch is designed around h264parse followed by v4l2h264dec.
 */
static void
gst_app_qtdemux_pad_added_cb (GstElement * qtdemux, GstPad * srcpad,
    gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstCaps *caps = NULL;
  GstStructure *structure = NULL;
  const gchar *name = NULL;

  /*
   * The callback signature is fixed by GStreamer. qtdemux is not needed because
   * the source pad already identifies the newly exposed stream.
   */
  (void) qtdemux;

  /*
   * Prefer current caps when available. If negotiation has not completed yet,
   * query the pad capabilities instead.
   */
  caps = gst_pad_get_current_caps (srcpad);
  if (caps == NULL)
    caps = gst_pad_query_caps (srcpad, NULL);

  if (caps == NULL || gst_caps_is_empty (caps))
    goto cleanup;

  structure = gst_caps_get_structure (caps, 0);
  if (structure == NULL)
    goto cleanup;

  /*
   * Only connect H.264 video to h264parse. Audio or metadata pads are ignored by
   * this sample because no downstream branch exists for them.
   */
  name = gst_structure_get_name (structure);
  if (!g_str_has_prefix (name, "video/x-h264"))
    goto cleanup;

  gst_app_link_dynamic_src_pad (srcpad, appctx->parse);

cleanup:
  if (caps != NULL)
    gst_caps_unref (caps);
}

/*
 * Handle Ctrl+C from the terminal.
 *
 * When the pipeline is already PLAYING, send EOS first. This gives elements a
 * chance to drain and shut down cleanly. If the pipeline is not playing, quit
 * the main loop immediately.
 */
static gboolean
gst_app_handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstState state = GST_STATE_NULL;
  GstState pending = GST_STATE_VOID_PENDING;

  g_print ("\n\nReceived an interrupt signal, sending EOS ...\n");

  if (!gst_element_get_state (
      appctx->pipeline, &state, &pending, GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: Failed to get current pipeline state.\n");
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  } else {
    g_main_loop_quit (appctx->mloop);
  }

  return TRUE;
}

/*
 * Print top-level pipeline state transitions.
 *
 * The GStreamer bus receives state-changed messages from every element. For
 * readable logs, this callback filters out messages from child elements and
 * prints only transitions emitted by the pipeline itself.
 */
static void
gst_app_state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old_state = GST_STATE_NULL;
  GstState new_state = GST_STATE_NULL;
  GstState pending = GST_STATE_VOID_PENDING;

  (void) bus;

  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old_state, &new_state, &pending);

  g_print ("Pipeline state changed from %s to %s.\n",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (new_state));
}

/*
 * Handle warning messages from the pipeline bus.
 *
 * Warnings do not stop the application. They are printed with GStreamer's
 * standard formatting so the source element and debug details are visible.
 */
static void
gst_app_warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  (void) bus;
  (void) userdata;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

/*
 * Handle error messages from the pipeline bus.
 *
 * Errors are fatal for this simple sample. After printing the error, the main
 * loop is stopped so main() can move the pipeline to NULL and release resources.
 */
static void
gst_app_error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  (void) bus;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (mloop);
}

/*
 * Handle normal End-of-Stream.
 *
 * EOS means all data was consumed or a graceful shutdown was requested. The main
 * loop can stop because there is no more streaming work to perform.
 */
static void
gst_app_eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  (void) bus;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

/*
 * Release the top-level pipeline reference.
 *
 * The pipeline owns references to all elements that were added to it, so unrefing
 * the pipeline is enough to release the complete element graph.
 */
static void
gst_app_destroy_pipe (GstAppContext *appctx)
{
  gst_object_unref (appctx->pipeline);
}

/*
 * Create, configure, add, and link all GStreamer elements.
 *
 * The only delayed link is qtdemux -> h264parse because qtdemux creates its
 * source pads dynamically after inspecting the input file.
 */
static gboolean
gst_app_create_pipe (GstAppContext * appctx)
{
  GstElement *source = NULL;
  GstElement *demux = NULL;
  GstElement *parse = NULL;
  GstElement *decoder = NULL;
  GstElement *mlbin = NULL;
  GstElement *overlay = NULL;
  GstElement *waylandsink = NULL;
  GstStructure *delegate_options = NULL;
  gboolean ret = FALSE;

  /*
   * Create all elements by factory name.
   *
   * The second argument is the instance name. Good names make logs and pipeline
   * dumps much easier to understand during debugging.
   */
  source = gst_element_factory_make ("filesrc", "file_src");
  demux = gst_element_factory_make ("qtdemux", "file_qtdemux");
  parse = gst_element_factory_make ("h264parse", "file_h264_parse");
  decoder = gst_element_factory_make ("v4l2h264dec", "v4l2h264dec");
  mlbin = gst_element_factory_make ("qtimlvideotflitebin", "qtimlvideotflitebin");
  overlay = gst_element_factory_make ("qtivoverlay", "overlay");
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");

  if (!source || !demux || !parse || !decoder || !mlbin || !overlay ||
      !waylandsink) {
    g_printerr ("ERROR: One or more elements could not be created.\n");
    return FALSE;
  }

  /*
   * Save h264parse in the app context so the qtdemux pad-added callback can
   * complete the delayed demuxer link.
   */
  appctx->parse = parse;

  /*
   * filesrc only needs the location of the input file. Container parsing and
   * codec handling are performed by the downstream demuxer/parser/decoder.
   */
  g_object_set (G_OBJECT (source), "location", appctx->file, NULL);

  /*
   * Configure the QTI ML TfLite bin.
   *
   * - inference-delegate=external tells the element to use an external TfLite
   *   delegate instead of plain CPU execution.
   * - inference-external-delegate-path points to the QNN TfLite delegate.
   * - inference-external-delegate-options configures the QNN backend.
   * - postprocess-module=yolov8 selects the matching post-processing logic.
   */
  delegate_options = gst_structure_from_string (
      "QNNExternalDelegate,backend_type=htp;", NULL);

  gst_element_set_enum_property (mlbin, "inference-delegate", "external");
  gst_element_set_enum_property (mlbin, "postprocess-module", "yolov8");

  g_object_set (mlbin,
      "inference-external-delegate-path", "libQnnTFLiteDelegate.so",
      "inference-external-delegate-options", delegate_options,
      "inference-model", appctx->model,
      "postprocess-labels", appctx->labels,
      NULL);

  if (delegate_options != NULL)
    gst_structure_free (delegate_options);

  /*
   * sync=TRUE keeps video rendering synchronized to timestamps.
   * fullscreen=TRUE makes the display sink cover the whole output surface.
   */
  g_object_set (G_OBJECT (waylandsink), "sync", TRUE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

  /*
   * Add every element to the pipeline before linking. Linking elements that are
   * not in the same bin is a common source of setup errors.
   */
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      source, demux, parse, decoder, mlbin, overlay, waylandsink, NULL);

  /*
   * filesrc has a static source pad and qtdemux has a static sink pad, so this
   * link can be created immediately.
   */
  ret = gst_element_link (source, demux);
  if (!ret) {
    g_printerr ("ERROR: Failed to link filesrc to qtdemux.\n");
    return FALSE;
  }

  /*
   * This part is fully static:
   *
   *   h264parse -> v4l2h264dec -> qtimlvideotflitebin
   *             -> qtivoverlay -> waylandsink
   *
   * The missing upstream qtdemux -> h264parse link is completed later in the
   * pad-added callback.
   */
  g_print ("Linking static elements...\n");
  ret = gst_element_link_many (parse, decoder, mlbin, overlay, waylandsink, NULL);
  if (!ret) {
    g_printerr ("ERROR: Static pipeline elements cannot be linked.\n");
    return FALSE;
  }

  /*
   * Connect after adding/linking the static branch. When qtdemux discovers the
   * MP4 streams, this callback will select the H.264 video pad and link it to
   * h264parse.
   */
  g_signal_connect (demux, "pad-added",
      G_CALLBACK (gst_app_qtdemux_pad_added_cb), appctx);

  g_print ("All static elements are linked successfully.\n");

  return TRUE;
}

/*
 * Release heap memory owned by the runtime configuration.
 *
 * This does not release GStreamer elements. Pipeline elements are owned by the
 * GstPipeline and are released by gst_app_destroy_pipe().
 */
static void
gst_app_config_free (GstAppContext * appctx)
{
  g_free (appctx->file);
  g_free (appctx->model);
  g_free (appctx->labels);
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *option_ctx = NULL;
  GError *error = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstElement *pipeline = NULL;
  gboolean ret = FALSE;
  GstAppContext appctx = {};
  const gchar *home = g_getenv ("HOME");
  gchar *model_base_path = g_strdup_printf ("%s/", home);

  /*
   * Initialize defaults before option parsing. GOption can replace these
   * pointers if the user provides --file, --model_file, or --label_file.
   */
  appctx.file = g_strdup_printf (DEFAULT_INPUT_FILE, home);

  /*
   * Command-line options.
   *
   * Each option writes directly into the corresponding field in appctx. This is
   * why the fields are heap-allocated strings and later released explicitly.
   */
  GOptionEntry entries[] = {
    { "file", 'f', 0, G_OPTION_ARG_STRING, &appctx.file,
      "Input file - by default takes ${HOME}/media/Draw_1080p_180s_30FPS.mp4"
    },
    { "model-base-path", 0, 0, G_OPTION_ARG_STRING, &model_base_path,
      "Directory containing models/ and labels/", NULL },
    { NULL }
  };

  /*
   * Create a GLib option context and attach the GStreamer option group. This
   * lets the application accept both its own options and standard GStreamer
   * options such as --gst-debug.
   */
  option_ctx = g_option_context_new (DESCRIPTION);
  if (option_ctx == NULL) {
    g_printerr ("ERROR: Failed to create option context.\n");
    gst_app_config_free (&appctx);
    return -EFAULT;
  }

  g_option_context_add_main_entries (option_ctx, entries, NULL);
  g_option_context_add_group (option_ctx, gst_init_get_option_group ());

  ret = g_option_context_parse (option_ctx, &argc, &argv, &error);
  g_option_context_free (option_ctx);

  if (!ret) {
    g_printerr ("ERROR: Failed to parse options: %s\n",
        error != NULL ? error->message : "unknown error");
    g_clear_error (&error);
    gst_app_config_free (&appctx);
    return -EFAULT;
  }

  appctx.model = g_strdup_printf (
      "%s/models/yolov8_det_quantized.tflite", model_base_path);
  appctx.labels = g_strdup_printf ("%s/labels/yolov8.json", model_base_path);

  /*
   * Initialize GStreamer explicitly. The option group above also allows
   * GStreamer arguments to be parsed correctly.
   */
  gst_init (&argc, &argv);

  /*
   * Create the top-level pipeline. All elements created in gst_app_create_pipe()
   * will be added to this bin.
   */
  pipeline = gst_pipeline_new ("gst-detection-display-example");
  if (!pipeline) {
    g_printerr ("ERROR: Failed to create pipeline.\n");
    gst_app_config_free (&appctx);
    return -EFAULT;
  }

  appctx.pipeline = pipeline;

  /*
   * Build the element graph before starting the GLib main loop.
   */
  ret = gst_app_create_pipe (&appctx);
  if (!ret) {
    g_printerr ("ERROR: Failed to create GStreamer pipeline graph.\n");
    gst_app_config_free (&appctx);
    gst_app_destroy_pipe (&appctx);
    return -EFAULT;
  }

  /*
   * The main loop dispatches bus messages, signal callbacks, and Ctrl+C signal
   * handling. Without it, the process would exit immediately after starting the
   * pipeline.
   */
  mloop = g_main_loop_new (NULL, FALSE);
  if (mloop == NULL) {
    g_printerr ("ERROR: Failed to create main loop.\n");
    gst_app_config_free (&appctx);
    gst_app_destroy_pipe (&appctx);
    return -EFAULT;
  }
  appctx.mloop = mloop;

  /*
   * The bus is the pipeline's message channel. Errors, warnings, EOS, and state
   * changes are delivered here from streaming threads back to the application.
   */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  if (bus == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus.\n");
    g_main_loop_unref (mloop);
    gst_app_config_free (&appctx);
    gst_app_destroy_pipe (&appctx);
    return -EFAULT;
  }

  /*
   * Convert bus messages into GLib signals and connect only the message types
   * this sample needs to handle.
   */
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (gst_app_state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning",
      G_CALLBACK (gst_app_warning_cb), NULL);
  g_signal_connect (bus, "message::error",
      G_CALLBACK (gst_app_error_cb), mloop);
  g_signal_connect (bus, "message::eos",
      G_CALLBACK (gst_app_eos_cb), mloop);
  gst_object_unref (bus);

  /*
   * Integrate SIGINT with the GLib main loop. This avoids doing unsafe work
   * directly inside a POSIX signal handler.
   */
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, gst_app_handle_interrupt_signal, &appctx);

  g_print ("Setting pipeline to PLAYING state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PLAYING)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PLAYING state.\n");
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need preroll.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline state change is asynchronous.\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful.\n");
      break;
  }

  g_print ("Running main loop ...\n");
  /*
   * From this point the application is event-driven. Bus messages, dynamic pads,
   * and Ctrl+C are handled by callbacks registered earlier.
   */
  g_main_loop_run (mloop);
  g_print ("Main loop stopped.\n");

  /*
   * Remove the SIGINT source before destroying the main loop and pipeline. This
   * prevents callbacks from running after their user data becomes invalid.
   */
  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  /*
   * Always stop the pipeline before unrefing it. Setting NULL releases hardware
   * devices, closes files, and stops internal streaming threads cleanly.
   */
  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_free (model_base_path);
  gst_app_config_free (&appctx);

  /*
   * Release application-owned resources after the pipeline has been set to
   * NULL, so elements have already stopped using devices, files, and buffers.
   */
  gst_app_destroy_pipe (&appctx);
  gst_deinit ();

  return 0;
}
