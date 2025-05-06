/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based super resolution
 *
 * Description:
 * This application accepts a file stream as input, processes it through the
 * superresolution module, and displays the input and output side by side.
 *
 * Pipeline for Gstreamer with File source and Waylandsink:
 * filesrc -> qtdemux -> h264parse -> v4l2h264dec  -> tee (2 splits)
 *            | -> qtivcomposer
 *      tee ->|
 *            | -> Pre process-> ML Framework -> Post process -> qtivcomposer

 *     qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *
 * Pipeline for Gstreamer with File source and File sink:
 * filesrc -> qtdemux -> h264parse -> v4l2h264dec  -> tee (2 splits)
 *            | -> qtivcomposer
 *      tee ->|
 *            | -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *
 *     qtivcomposer (COMPOSITION) -> v4l2h264enc -> h264parse
 *                                               -> mp4mux -> filesink
 *  Pre process : qtimlvconverter
 *  ML Framework: qtimltflite
 *  Post process: qtimlvsuperresolution
 */

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

/**
 * Default model and video, if not provided by user
 */
#define DEFAULT_TFLITE_MODEL \
    "/etc/models/quicksrnetsmall_quantized.tflite"
#define DEFAULT_INPUT_FILE_PATH "/etc/media/video.mp4"

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 4

/**
 * Number of static sinks for qtivcomposer
 */
#define COMPOSER_SINK_COUNT 2

/**
 * Scale and Offset value for post processing
 */
#define DEFAULT_CONSTANTS "srnet,q-offsets=<0.0>,q-scales=<1.0>;"

/**
 * Default path of config file
 */
#define DEFAULT_CONFIG_FILE "/etc/configs/config-superresolution.json"

/**
 * Output dimensions of output stream
*/
#define OUTPUT_WIDTH 1920
#define OUTPUT_HEIGHT 1080

/**
 * Structure for various application specific options
 */
typedef struct {
  const gchar *input_file_path;
  const gchar *model_path;
  const gchar *constants;
  const gchar *output_file_path;
  enum GstSinkType sink_type;
  gboolean display;
} GstAppOptions;

/**
 * Static grid points to display split stream
 */
static GstVideoRectangle composer_sink_position[COMPOSER_SINK_COUNT] = {
  {0, 0, OUTPUT_WIDTH / 2, OUTPUT_HEIGHT},
  {OUTPUT_WIDTH / 2, 0, OUTPUT_WIDTH / 2, OUTPUT_HEIGHT}
};

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 * @param options GstAppOptions object
 */
static void
gst_app_context_free (GstAppContext * appctx, GstAppOptions * options, gchar * config_file)
{
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  if (options->constants != (gchar *)(&DEFAULT_CONSTANTS) &&
      options->constants != NULL) {
    g_free ((gpointer)options->constants);
    options->constants = NULL;
  }

  if (options->model_path != (gchar *)(&DEFAULT_TFLITE_MODEL) &&
      options->model_path != NULL) {
    g_free ((gpointer)options->model_path);
    options->model_path = NULL;
  }

  if (options->input_file_path != (gchar *)(&DEFAULT_INPUT_FILE_PATH) &&
      options->input_file_path != NULL) {
    g_free ((gpointer)options->input_file_path);
    options->input_file_path = NULL;
  }

  if (config_file != NULL &&
      config_file != (gchar *)(&DEFAULT_CONFIG_FILE)) {
    g_free ((gpointer)config_file);
    config_file = NULL;
  }

  if (options->output_file_path != NULL) {
    g_free ((gpointer)options->output_file_path);
    options->output_file_path = NULL;
  }
}

/**
 * Build Property for pad.
 *
 * @param property Property Name.
 * @param values Value of Property.
 * @param num count of Property Values.
 */
static void
build_pad_property (GValue * property, gint values[], gint num)
{
  GValue val = G_VALUE_INIT;
  g_value_init (&val, G_TYPE_INT);

  for (gint idx = 0; idx < num; idx++) {
    g_value_set_int (&val, values[idx]);
    gst_value_array_append_value (property, &val);
  }

  g_value_unset (&val);
}

/**
 * Callback function used for demuxer dynamic pad.
 *
 * @param element Plugin supporting dynamic pad.
 * @param pad The source pad that is added.
 * @param data Userdata set at callback registration.
 */
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad = NULL;
  gchar *caps_str = NULL;
  GstElement *queue = (GstElement *) data;
  GstCaps *caps = gst_pad_get_current_caps (pad);
  if (!caps) {
    caps = gst_pad_query_caps (pad, NULL);
  }

  if (caps) {
    caps_str = gst_caps_to_string (caps);
  } else {
    g_print ("No caps available for this pad\n");
  }

  // Check if caps contains video
  if (caps_str) {
    if (g_strrstr (caps_str, "video")) {
      // Get the static sink pad from the queue
      sinkpad = gst_element_get_static_pad (queue, "sink");
      // Get the static sink pad from the queue
      g_assert (gst_pad_link (pad, sinkpad) == GST_PAD_LINK_OK);
      gst_object_unref (sinkpad);
    } else {
      g_print ("Ignoring caps\n");
    }
  }
  g_free (caps_str);
  gst_caps_unref (caps);
}

/**
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer.
 * @param options GstAppOptions object
 */
static gboolean
create_pipe (GstAppContext * appctx, const GstAppOptions options)
{
  GstElement *filesrc = NULL, *qtdemux = NULL, *h264parse_decode = NULL;
  GstElement *v4l2h264dec = NULL, *tee = NULL, *qtivcomposer = NULL;
  GstElement *v4l2h264dec_caps = NULL;
  GstElement *fpsdisplaysink = NULL, *waylandsink = NULL;
  GstElement *qtimlvconverter = NULL, *qtimlelement = NULL;
  GstElement *qtimlvsuperresolution = NULL, *sink_filter = NULL;
  GstElement *filter = NULL, *queue[QUEUE_COUNT] = {NULL};
  GstElement *v4l2h264enc = NULL, *mp4mux = NULL, *filesink = NULL;
  GstElement *h264parse_encode = NULL;
  GstCaps *pad_filter = NULL;
  GstStructure *delegate_options = NULL;
  gboolean ret = FALSE;
  gchar element_name[128];
  gint module_id;

  // 1. Create the elements for Plugins
  // Create file source element for file stream
  filesrc = gst_element_factory_make ("filesrc", "filesrc");
  if (!filesrc ) {
    g_printerr ("Failed to create filesrc\n");
    goto error_clean_elements;
  }

  // Create qtdemux for demuxing the filesrc
  qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");
  if (!qtdemux ) {
    g_printerr ("Failed to create qtdemux\n");
    goto error_clean_elements;
  }

  // Create h264parse element for parsing the stream
  h264parse_decode =
      gst_element_factory_make ("h264parse", "h264parse_decode");
  if (!h264parse_decode) {
    g_printerr ("Failed to create h264parse\n");
    goto error_clean_elements;
  }

  // Create v4l2h264dec element for encoding the stream
  v4l2h264dec = gst_element_factory_make ("v4l2h264dec", "v4l2h264dec");
  if (!v4l2h264dec) {
    g_printerr ("Failed to create v4l2h264dec\n");
    goto error_clean_elements;
  }

  v4l2h264dec_caps = gst_element_factory_make ("capsfilter", "v4l2h264dec_caps");
  if (!v4l2h264dec_caps) {
    g_printerr ("Failed to create v4l2h264dec_caps\n");
    goto error_clean_elements;
  }

  // Create qtivcomposer to combine input with ML post proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    goto error_clean_elements;
  }

  // Create queue element for processing
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create tee to send same data buffer to mulitple elements
  tee = gst_element_factory_make ("tee", "tee");
  if (!tee) {
    g_printerr ("Failed to create tee\n");
    goto error_clean_elements;
  }

  // Create qtimlvconverter for Input preprocessing
  qtimlvconverter =
      gst_element_factory_make ("qtimlvconverter", "qtimlvconverter");
  if (!qtimlvconverter) {
    g_printerr ("Failed to create qtimlvconverter\n");
    goto error_clean_elements;
  }

  // Create the ML inferencing plugin TFLite
  qtimlelement = gst_element_factory_make ("qtimltflite", "qtimltflite");
  if (!qtimlelement) {
    g_printerr ("Failed to create qtimlelement\n");
    goto error_clean_elements;
  }

  // Create plugin for ML postprocessing for super resolution
  qtimlvsuperresolution =
      gst_element_factory_make ("qtimlvsuperresolution", "qtimlvsuperresolution");
  if (!qtimlvsuperresolution) {
    g_printerr ("Failed to create qtimlvsuperresolution\n");
    goto error_clean_elements;
  }

  filter = gst_element_factory_make ("capsfilter", "capsfilter");
  if (!filter) {
    g_printerr ("Failed to create filter\n");
    goto error_clean_elements;
  }

  if (options.sink_type == GST_WAYLANDSINK) {
    // Create Wayland compositor to render output on Display
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    if (!waylandsink) {
      g_printerr ("Failed to create waylandsink \n");
      goto error_clean_elements;
    }

    // Create fpsdisplaysink to display the current and
    // average framerate as a text overlay
    fpsdisplaysink =
        gst_element_factory_make ("fpsdisplaysink", "fpsdisplaysink");
    if (!fpsdisplaysink ) {
      g_printerr ("Failed to create fpsdisplaysink\n");
      goto error_clean_elements;
    }
  } else if (options.sink_type == GST_VIDEO_ENCODE) {
     // Create h264parse element for parsing the stream
    h264parse_encode = gst_element_factory_make ("h264parse", "h264parse_encode");
    if (!h264parse_encode) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }

    sink_filter = gst_element_factory_make ("capsfilter", "capsfilter-sink");
    if (!sink_filter) {
      g_printerr ("Failed to create filter\n");
      goto error_clean_elements;
    }

    // Create H.264 Encoder plugin for file and rtsp output
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    if (!v4l2h264enc) {
      g_printerr ("Failed to create v4l2h264enc\n");
      goto error_clean_elements;
    }

    mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
    if (!mp4mux) {
      g_printerr ("Failed to create mp4mux\n");
      goto error_clean_elements;
    }

    // Generic filesink plugin to write file on disk
    filesink = gst_element_factory_make ("filesink", "filesink");
    if (!filesink) {
      g_printerr ("Failed to create filesink\n");
      goto error_clean_elements;
    }
  }

  // 2. Set properties for all GST plugin elements
  // 2.1 Set the capabilities of file stream
  g_object_set (G_OBJECT (filesrc), "location", options.input_file_path, NULL);
  gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
  gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12", NULL);
  g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 2.2 Select the HW to DSP for model inferencing using delegate property
  g_object_set (G_OBJECT (qtimlelement),
      "model", options.model_path,
      "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
  delegate_options = gst_structure_from_string (
      "QNNExternalDelegate,backend_type=htp;", NULL);
  g_object_set (G_OBJECT (qtimlelement),
      "external-delegate-path", "libQnnTFLiteDelegate.so", NULL);
  g_object_set (G_OBJECT (qtimlelement),
      "external-delegate-options", delegate_options, NULL);
  gst_structure_free (delegate_options);

  // 2.3 Set properties for postproc plugins- module and constants
  module_id = get_enum_value (qtimlvsuperresolution, "module", "srnet");
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlvsuperresolution),
        "module", module_id, "constants", options.constants, NULL);
  }
  else {
    g_printerr ("Module srnet is not available in qtimlvsuperresolution.\n");
    goto error_clean_elements;
  }

  // 2.4 Set filter capabilities
  pad_filter = gst_caps_new_simple ("video/x-raw",
    "format", G_TYPE_STRING, "RGB", NULL);
  g_object_set (G_OBJECT (filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  if (options.sink_type == GST_WAYLANDSINK) {
    // 2.5 Set the properties of Wayland compositor
    g_object_set (G_OBJECT (waylandsink), "sync", TRUE, NULL);
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

    // 2.6 Set the properties of fpsdisplaysink plugin- sync,
    // signal-fps-measurements, text-overlay and video-sink
    g_object_set (G_OBJECT (fpsdisplaysink), "sync", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements",
        TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
    g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);
  } else if (options.sink_type == GST_VIDEO_ENCODE) {
    gst_element_set_enum_property (v4l2h264enc, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264enc, "output-io-mode", "dmabuf-import");

    pad_filter = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, OUTPUT_WIDTH,
        "height", G_TYPE_INT, OUTPUT_HEIGHT,
        "interlace-mode", G_TYPE_STRING, "progressive",
        "colorimetry", G_TYPE_STRING, "bt601", NULL);
    g_object_set (G_OBJECT (sink_filter), "caps", pad_filter, NULL);
    gst_caps_unref (pad_filter);

    g_object_set (G_OBJECT (filesink), "location", options.output_file_path,
        NULL);
  }

  // 3. Setup the pipeline
  // 3.1 Adding elements to pipeline
  g_print ("Adding all elements to the pipeline...\n");

  gst_bin_add_many (GST_BIN (appctx->pipeline),
      filesrc, qtdemux, h264parse_decode, v4l2h264dec, v4l2h264dec_caps,
      tee, qtimlelement, qtimlvconverter, qtimlvsuperresolution,
      filter, qtivcomposer, NULL);

  if (options.sink_type == GST_WAYLANDSINK) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), fpsdisplaysink,
        waylandsink, NULL);
  } else if (options.sink_type == GST_VIDEO_ENCODE) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), sink_filter,
        v4l2h264enc, h264parse_encode, mp4mux, filesink, NULL);
  }

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  // 3.2 Link pipeline elements for Inferencing
  g_print ("Linking elements...\n");
  ret = gst_element_link_many (filesrc, qtdemux, NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements filesrc and qtdemux elements "
        "cannot be linked. Exiting.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (queue[0], h264parse_decode, v4l2h264dec,
      v4l2h264dec_caps, tee, NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements qtdemux -> v4l2h264dec cannot be linked."
        "Exiting.\n");
    goto error_clean_pipeline;
  }

  if (options.sink_type == GST_WAYLANDSINK) {
    ret = gst_element_link_many (tee, queue[1], qtivcomposer,
        fpsdisplaysink, NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements tee -> qtivcomposer -> fpsdisplaysink"
          "cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }
  } else if (options.sink_type == GST_VIDEO_ENCODE) {
    ret = gst_element_link_many (tee, queue[1], qtivcomposer,
        sink_filter, v4l2h264enc, h264parse_encode, mp4mux, filesink,
        NULL);
    if (!ret) {
      g_printerr ("\n pipeline elements tee -> qtivcomposer -> encode"
          " ->filesink cannot be linked. Exiting.\n");
      goto error_clean_pipeline;
    }
  }

  ret = gst_element_link_many (tee, qtimlvconverter, queue[2], qtimlelement,
      qtimlvsuperresolution, filter, queue[3], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("\n pipeline elements tee -> qtimlvconverter -> qtimlelement"
        " -> qtimlvsuperresolution -> qtivcomposer cannot be linked."
        " Exiting.\n");
    goto error_clean_pipeline;
  }

  g_print ("All elements are linked successfully\n");

  // 3.3 Setup dynamic pad to link qtdemux to queue
  g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added), queue[0]);

  // 3.4 Setup position and dimension for qtivcomposer to split display
  for (gint i = 0; i < COMPOSER_SINK_COUNT; i++) {
    GstPad *vcomposer_sink;
    GValue position = G_VALUE_INIT;
    GValue dimension = G_VALUE_INIT;
    gint pos_vals[2], dim_vals[2];

    snprintf (element_name, 127, "sink_%d", i);
    vcomposer_sink = gst_element_get_static_pad (qtivcomposer, element_name);
    if (vcomposer_sink == NULL) {
      g_printerr ("Sink pad %d of vcomposer couldn't be retrieved\n",i);
      goto error_clean_pipeline;
    }

    g_value_init (&position, GST_TYPE_ARRAY);
    g_value_init (&dimension, GST_TYPE_ARRAY);

    GstVideoRectangle pos = composer_sink_position[i];
    pos_vals[0] = pos.x; pos_vals[1] = pos.y;
    dim_vals[0] = pos.w; dim_vals[1] = pos.h;

    build_pad_property (&position, pos_vals, 2);
    build_pad_property (&dimension, dim_vals, 2);

    g_object_set_property (G_OBJECT (vcomposer_sink), "position", &position);
    g_object_set_property (G_OBJECT (vcomposer_sink), "dimensions", &dimension);

    g_value_unset (&position);
    g_value_unset (&dimension);
    gst_object_unref (vcomposer_sink);
  }

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  return FALSE;

error_clean_elements:
  g_printerr ("Error: Pipeline elements cannot be created\n");

  cleanup_gst (&filesrc, &qtdemux, &h264parse_decode, &v4l2h264dec,
      &v4l2h264dec_caps, &tee, &qtivcomposer, &fpsdisplaysink, &waylandsink,
      &qtimlvconverter, &qtimlelement, &qtimlvsuperresolution, &filter,
      &sink_filter, &v4l2h264enc, &h264parse_encode, &mp4mux, &filesink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    if (queue[i]) {
       gst_object_unref (queue[i]);
    }
  }

  return FALSE;
}

/**
 * Parse JSON file to read input parameters
 *
 * @param config_file Path to config file
 * @param options Application specific options
 */
gint
parse_json(gchar * config_file, GstAppOptions * options)
{
  JsonParser *parser = NULL;
  JsonNode *root = NULL;
  JsonObject *root_obj = NULL;
  GError *error = NULL;

  parser = json_parser_new ();

  // Load the JSON file
  if (!json_parser_load_from_file (parser, config_file, &error)) {
    g_printerr ("Unable to parse JSON file: %s\n", error->message);
    g_error_free (error);
    g_object_unref (parser);
    return -1;
  }

  // Get the root object
  root = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root)) {
    gst_printerr ("Failed to load json object\n");
    g_object_unref (parser);
    return -1;
  }

  root_obj = json_node_get_object (root);

  if (json_object_has_member (root_obj, "input-file-path")) {
    options->input_file_path =
        g_strdup (json_object_get_string_member (root_obj, "input-file-path"));
  }

  if (json_object_has_member (root_obj, "model")) {
    options->model_path =
        g_strdup (json_object_get_string_member (root_obj, "model"));
  }

  if (json_object_has_member (root_obj, "constants")) {
    options->constants =
        g_strdup (json_object_get_string_member (root_obj, "constants"));
  }

  if (json_object_has_member (root_obj, "output-file-path")) {
    options->output_file_path =
        g_strdup (json_object_get_string_member (root_obj, "output-file-path"));
  }

  g_object_unref (parser);
  return 0;
}

gint
main (gint argc, gchar * argv[])
{
  GstBus *bus = NULL;
  GMainLoop *mloop = NULL;
  GstElement *pipeline = NULL;
  GOptionContext *ctx = NULL;
  const gchar *app_name = NULL;
  gchar *config_file = NULL;
  GstAppOptions options = {};
  GstAppContext appctx = {};
  gboolean ret = FALSE;
  gchar help_description[1024];
  guint intrpt_watch_id = 0;

  options.constants = NULL;
  options.input_file_path = NULL;
  options.model_path = NULL;
  options.output_file_path = NULL;
  options.display = FALSE;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Structure to define the user options selection
  GOptionEntry entries[] = {
    { "config-file", 0, 0, G_OPTION_ARG_STRING,
      &config_file,
      "Path to config file\n",
      NULL
    },
    { NULL }
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  snprintf (help_description, 1023, "\nExample:\n"
    "  %s --config-file=%s\n"
    "\nThis Sample App demonstrates super resolution on video stream\n"
    "\nConfig file Fields:\n"
    "  input-file-path: \"/PATH\"\n"
    "      File source path\n"
    "      Default file source path: " DEFAULT_INPUT_FILE_PATH "\n"
    "  model: \"/PATH\"\n"
    "      This is an optional parameter and overrides default path\n"
    "      Default model path: " DEFAULT_TFLITE_MODEL"\n"
    "  constants: CONSTANTS\n"
    "      Constants, offsets and coefficients used by the chosen module \n"
    "      for post-processing of incoming tensors.\n"
    "      Default constants: \"" DEFAULT_CONSTANTS"\"\n"
    "  output-file-path: \"/PATH\"\n"
    "      Output file path. If not set, then display output is selected\n",
    app_name, DEFAULT_CONFIG_FILE);
help_description[1023] = '\0';

  // Parse command line entries.
  if ((ctx = g_option_context_new (help_description)) != NULL) {
    GError *error = NULL;
    gboolean success = FALSE;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (&appctx, &options, config_file);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      gst_app_context_free (&appctx, &options, config_file);
      return -EFAULT;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EFAULT;
  }

  // Choose default config file if config file not provided
  if (config_file == NULL) {
    config_file = DEFAULT_CONFIG_FILE;
  }

  if (!file_exists (config_file)) {
    g_printerr ("Invalid config file path: %s\n", config_file);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (parse_json (config_file, &options) != 0) {
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.display && options.output_file_path) {
    g_printerr ("Both Display and Output file are provided as input! - "
        "Select either Display or Output file\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  } else if (options.display) {
    options.sink_type = GST_WAYLANDSINK;
    g_print ("Selected sink type as Wayland Display\n");
  } else if (options.output_file_path) {
    options.sink_type = GST_VIDEO_ENCODE;
    g_print ("Selected sink type as Output file with path = %s\n",
        options.output_file_path);
  } else {
    options.sink_type = GST_WAYLANDSINK;
    g_print ("Using Wayland Display as Default\n");
  }

  if (options.input_file_path == NULL) {
    g_print ("Using Default file: %s\n",DEFAULT_INPUT_FILE_PATH);
    options.input_file_path = DEFAULT_INPUT_FILE_PATH;
  }

  if (options.model_path == NULL) {
    g_print ("Using Default model: %s\n",DEFAULT_TFLITE_MODEL);
    options.model_path = DEFAULT_TFLITE_MODEL;
  }

  if (options.constants == NULL) {
    g_print ("Using Default constants: %s\n",DEFAULT_CONSTANTS);
    options.constants = DEFAULT_CONSTANTS;
  }

  if (!file_exists (options.input_file_path)) {
    g_printerr ("Invalid video file source path: %s\n",
        options.input_file_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (!file_exists (options.model_path)) {
    g_printerr ("Invalid model file path: %s\n", options.model_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.output_file_path &&
      !file_location_exists (options.output_file_path)) {
    g_printerr ("Invalid output file location: %s\n",
        options.output_file_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  g_print ("Running app with model: %s \n", options.model_path);

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline that will form connection with other elements
  pipeline = gst_pipeline_new (app_name);
  if (!pipeline) {
    g_printerr ("ERROR: failed to create pipeline.\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  appctx.pipeline = pipeline;

  // Build the pipeline, link all elements in the pipeline
  ret = create_pipe (&appctx, options);
  if (!ret) {
    g_printerr ("ERROR: failed to create GST pipe.\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  // Bus is message queue for getting callback from gstreamer pipeline
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);

  // Register respective callback function based on message
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &appctx);

  // On successful transition to PAUSED state, state_changed_cb is called.
  // state_changed_cb callback is used to send pipeline to play state.
  g_print ("Set pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
      goto error;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  // Wait till pipeline encounters an error or EOS
  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

error:
  g_source_remove (intrpt_watch_id);

  g_print ("Set pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_app_context_free (&appctx, &options, config_file);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
