/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application for SmartCodec usecases
 *
 * Description:
 * This Application Smartly reduce network bandwidth/storage
 * from camera input using Qualcomm GStreamer plugins.
 *
 * Usage:
 * gst-smartcodec-example --width=1920 --height=1080
 *
 * Help:
 * gst-smartcodec-example --help
 *
 * *******************************************************************
 * Pipeline for camera stream:
 *             |capsfilter->sink_ctrl(qtismartvencbin)
 * qtiqmmfsrc->|
 *             |capsfilter->sink(qtismartvencbin)->v4l2h264enc->h264parse->mp4mux->filesink
 *
 * *******************************************************************
 */

#include <glib-unix.h>

#include <gst/gst.h>
#include <gst/sampleapps/gst_sample_apps_utils.h>

#define DEFAULT_OUTPUT_FILENAME      "/etc/media/video.mp4"
#define DEFAULT_WIDTH                1280
#define DEFAULT_HEIGHT               720
#define NOISE_REDUCTION_HIGH_QUALITY 2
#define STREAM_TYPE_PREVIEW          1   // camera preview stream
#define STREAM_TYPE_VIDEO          0
#define DEFAULT_MODEL                "/etc/models/object_detection.tflite"
#define DEFAULT_THRESHOLD            50.0
#define DEFAULT_RESULTS              5
#define DEFAULT_LABELS               "/etc/labels/coco_labels.txt"
#define DEFAULT_CONSTANTS_YOLOV8 \
  "YOLOv8,q-offsets=<21.0, 0.0, 0.0>,q-scales=<3.0546178817749023, 0.003793874057009816, 1.0>;"
#define QUEUE_COUNT 6

#define GST_APP_SUMMARY                                                   \
  "This Application Smartly reduce network \n"                            \
  "bandwidth/storage from camera input and also from filesource"          \
  " using Qualcomm SmartCodec plugins"                                    \
  "\nCommand For camera source :\n"                                       \
  "gst-ai-smartcodec-example -w 1920 -h 1080 -o video.mp4 "               \
  "-m /etc/models/YOLOv8-Detection-Quantized.tflite "                     \
  "-l /etc/labels/coco_labels.txt \n"                                     \
  "\nOutput :\n"                                                          \
  " Upon execution,application will generates output as encoded mp4 file"

// Structure to hold the application context
struct _GstSmartCodecContext
{
  GstElement *pipeline;
  GMainLoop *mloop;
  gchar *output_file;
  gchar *model_path;
  gchar *labels_path;
  gint width;
  gint height;
};

typedef struct _GstSmartCodecContext GstSmartCodecContext;

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstSmartCodecContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstSmartCodecContext *ctx = (GstSmartCodecContext *)
      g_new0 (GstSmartCodecContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;
  ctx->model_path = DEFAULT_MODEL;
  ctx->labels_path = DEFAULT_LABELS;
  ctx->output_file = NULL;
  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx application context object
 */
static void
gst_app_context_free (GstSmartCodecContext * appctx)
{
  // If specific pointer is not NULL, unref it

  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  if (appctx->output_file != NULL)
    g_free ((gpointer)appctx->output_file);

  if (appctx->model_path != NULL)
    g_free ((gpointer)appctx->model_path);

  if (appctx->labels_path != NULL)
    g_free (appctx->labels_path);

  if (appctx != NULL)
    g_free (appctx);
}

/**
 * Create GST pipeline involves 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context object.
 *
 */
static gboolean
create_pipe (GstSmartCodecContext * appctx)
{
  GstElement *qtiqmmfsrc, *capsfilter_ctrl, *capsfilter_enc, *qtismartvencbin,
      *tee, *h264parse, *mp4mux,
      *filesink, *queue_ctrl, *queue_sc, *queue_tee, *queue_ml, *detection_filter,
      *qtimlvconverter, *qtimlelement, *qtimlvdetection,
      *queue[QUEUE_COUNT];
  GstCaps *filtercaps, *pad_filter;
  GstPad *qmmf_pad, *pqmmf_pad, *sc_src, *ctrl_src, *sc_sink, *ctrl_sink;
  GstStructure *delegate_options = NULL;
  gchar element_name[128];
  gint module_id;
  gboolean ret = FALSE;

  detection_filter = NULL;

  // Create qtismartvencbin element for the smart encoder
  qtismartvencbin = gst_element_factory_make ("qtismartvencbin",
      "qtismartvencbin");

  // Create ML element for the smart encoder ml pad
  qtimlvconverter = gst_element_factory_make ("qtimlvconverter",
      "qtimlvconverter");
  qtimlelement = gst_element_factory_make ("qtimltflite", "qtimlelement");
  qtimlvdetection = gst_element_factory_make ("qtimlvdetection",
      "qtimlvdetection");

  // Create h264parse element for parsing the stream
  h264parse = gst_element_factory_make ("h264parse", "h264parse");

  // Create mp4mux element for muxing the stream
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");

  // Create filesink element for storing the encoding stream
  filesink = gst_element_factory_make ("filesink", "filesink");

  // Create queue to decouple the processing on sink and source pad
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      return FALSE;
    }
  }

  // creating tee element to split the data
  tee = gst_element_factory_make ("tee", "tee");

  // creating queue elements
  queue_tee = gst_element_factory_make ("queue", "queue_tee");
  queue_ml = gst_element_factory_make ("queue", "queue_ml");

  // Create capsfilter element
  queue_ctrl = gst_element_factory_make ("queue", "queue_ctrl");
  queue_sc = gst_element_factory_make ("queue", "queue_sc");

  // Check if all elements are created successfully
  if (!queue_ctrl || !queue_sc || !qtismartvencbin || !h264parse || !mp4mux ||
      !filesink || !tee || !queue_tee || !queue_ml || !qtimlvconverter ||
      !qtimlelement || !qtimlvdetection) {
    g_printerr ("\n One element could not be created. Exiting experiment.\n");
    return FALSE;
  }

  // Set properties for qtismartvencbin
  g_object_set (G_OBJECT (qtismartvencbin), "default-gop", 30, NULL);
  g_object_set (G_OBJECT (qtismartvencbin), "max-gop", 600, NULL);

  // Set properties for ML element
  g_print ("Using DSP delegate\n");

  // Set delegate and model for AI framework
  delegate_options = gst_structure_from_string (
    "QNNExternalDelegate,backend_type=htp,htp_device_id=(string)0,\
    htp_performance_mode=(string)2,htp_precision=(string)1;",
    NULL);
  g_object_set (G_OBJECT (qtimlelement), "model", appctx->model_path, "delegate",
      GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
  g_object_set (G_OBJECT (qtimlelement), "external_delegate_path",
      "libQnnTFLiteDelegate.so", NULL);
  g_object_set (G_OBJECT (qtimlelement), "external_delegate_options",
      delegate_options, NULL);
  gst_structure_free (delegate_options);

  // set qtimlvdetection properties
  g_object_set (G_OBJECT (qtimlvdetection), "labels", appctx->labels_path, NULL);
  module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
  if (module_id != -1) {
    g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
  } else {
    g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
  }
  g_object_set (G_OBJECT (qtimlvdetection), "threshold", DEFAULT_THRESHOLD, NULL);
  g_object_set (G_OBJECT (qtimlvdetection), "results", DEFAULT_RESULTS, NULL);
  g_object_set (G_OBJECT (qtimlvdetection), "constants", DEFAULT_CONSTANTS_YOLOV8,
      NULL);

  // Set the properties of pad_filter for detection
  pad_filter = gst_caps_new_simple ("text/x-raw", NULL, NULL);
  g_object_set (G_OBJECT (detection_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  g_object_set (G_OBJECT (qtismartvencbin), "encoder", 2, NULL);
  g_object_set (G_OBJECT (qtismartvencbin), "max-bitrate", 1000000, NULL);

  // Set filesink_enc properties
  g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);

  // Create first source element set the first camera
    qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");

  // Create capsfilter element for the encoder to set properties
  capsfilter_enc = gst_element_factory_make ("capsfilter", "capsfilter_enc");
  capsfilter_ctrl = gst_element_factory_make ("capsfilter", "capsfilter_ctrl");

  if (!qtiqmmfsrc || !capsfilter_ctrl || !capsfilter_enc) {
    g_printerr ("\n One element could not be created. Exiting experiment.\n");
    return FALSE;
  }

  // Configure the capsfilter_ctrl stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
      "NV12_Q08C", "width", G_TYPE_INT, 640, "height", G_TYPE_INT, 480, "framerate",
      GST_TYPE_FRACTION, 15, 1, NULL);

  g_object_set (G_OBJECT (capsfilter_ctrl), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the encode stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
      "NV12_Q08C", "width", G_TYPE_INT, appctx->width, "height", G_TYPE_INT,
      appctx->height, "framerate", GST_TYPE_FRACTION, 30, 1, NULL);

  g_object_set (G_OBJECT (capsfilter_enc), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Get qmmfsrc Element class
  GstElementClass *qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);

  // Get qmmfsrc pad template
  GstPadTemplate *
      qtiqmmfsrc_template = gst_element_class_get_pad_template (qtiqmmfsrc_klass,
          "video_%u");

  // Request a pad from qmmfsrc
  qmmf_pad = gst_element_request_pad (qtiqmmfsrc, qtiqmmfsrc_template,
      "video_%u", NULL);
  if (!qmmf_pad) {
    g_printerr ("Error: pad cannot be retrieved from qmmfsrc!\n");
  }
  g_print ("Pad received - %s\n", gst_pad_get_name (qmmf_pad));

  // Get qmmfsrc pad template
  GstPadTemplate *pqtiqmmfsrc_template =
      gst_element_class_get_pad_template (qtiqmmfsrc_klass,
      "video_%u");

  // Request a pad from qmmfsrc
  pqmmf_pad = gst_element_request_pad (qtiqmmfsrc, pqtiqmmfsrc_template,
      "video_%u", NULL);
  if (!pqmmf_pad) {
    g_printerr ("Error: pad cannot be retrieved from qmmfsrc!\n");
  }
  g_print ("Pad received - %s\n", gst_pad_get_name (pqmmf_pad));

  g_object_set (G_OBJECT (pqmmf_pad), "type", STREAM_TYPE_PREVIEW, NULL);
  g_object_set (G_OBJECT (qmmf_pad), "type", STREAM_TYPE_VIDEO, NULL);
  //g_object_set (G_OBJECT (pqmmf_pad), "extra-buffers", 20, NULL);
  g_object_set (G_OBJECT (qmmf_pad), "extra-buffers", 20, NULL);
  g_object_set (G_OBJECT (qtiqmmfsrc), "noise-reduction", NOISE_REDUCTION_HIGH_QUALITY, NULL);
  gst_object_unref (qmmf_pad);
  gst_object_unref (pqmmf_pad);

  g_print ("\n Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter_ctrl,
      capsfilter_enc, h264parse, mp4mux, filesink, queue_sc, queue_ctrl,
      qtismartvencbin, tee, queue_tee, queue_ml, qtimlvconverter, qtimlelement,
      qtimlvdetection, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("\n Link Smart Codec elements...\n");
  // Linking the encoder stream
  ret = gst_element_link_many (qtiqmmfsrc, capsfilter_enc, queue_sc, NULL);
  if (!ret) {
    g_printerr (
        "\n Video Smart Codec Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter_enc,
        queue_sc, NULL);
    return FALSE;
  }

  g_print ("\n Link encoder elements...\n");
  // Linking the encoder stream
  ret = gst_element_link_many (qtismartvencbin, queue[0], h264parse, mp4mux,
      queue[1], filesink, NULL);
  if (!ret) {
    g_printerr ("\n Video Encoder Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtismartvencbin, h264parse,
        mp4mux, filesink, queue[0], queue[1], NULL);
    return FALSE;
  }

  sc_src = gst_element_get_static_pad (queue_sc, "src");
  sc_sink = gst_element_get_static_pad (qtismartvencbin, "sink");
  g_print ("\n smart code pad link %d \n", gst_pad_link (sc_src, sc_sink));

  g_print ("\n Link sink_ctrl elements...\n");
  // Linking the display stream
  ret = gst_element_link_many (qtiqmmfsrc, capsfilter_ctrl, queue[2], tee,
      queue_ctrl, NULL);
  if (!ret) {
    g_printerr ("\n sink_ctrl Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter_ctrl,
        tee, queue[2], queue_ctrl, NULL);
    return FALSE;
  }

  ctrl_src = gst_element_get_static_pad (queue_ctrl, "src");
  ctrl_sink = gst_element_get_static_pad (qtismartvencbin, "sink_ctrl");
  g_print ("\n smart code pad link %d \n", gst_pad_link (ctrl_src, ctrl_sink));
  gst_object_unref (ctrl_src);
  gst_object_unref (ctrl_sink);

  g_print ("\n Link sink_ml elements...\n");
  // Linking the ml stream
  ret = gst_element_link_many (tee, queue_tee, qtimlvconverter, queue[3],
      qtimlelement, queue[4], qtimlvdetection, NULL);
  if (!ret) {
    g_printerr ("\n sink_ml Pipeline elements cannot be linked. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), tee, queue_tee,
        qtimlvconverter, qtimlelement, queue[3], queue[4], qtimlvdetection,
        NULL);
    return FALSE;
  }

  filtercaps = gst_caps_from_string ("text/x-raw");
  ret = gst_element_link_filtered (qtimlvdetection, queue_ml, filtercaps);
  if (!ret) {
    g_printerr ("\n pipeline elements qtimlvdetection -> qtimetamux "
                "cannot be linked. Exiting.\n");
  }

  ctrl_src = gst_element_get_static_pad (queue_ml, "src");
  ctrl_sink = gst_element_get_static_pad (qtismartvencbin, "sink_ml");
  g_print ("\n smart code ml pad link %d \n",
      gst_pad_link (ctrl_src, ctrl_sink));

  gst_caps_unref (filtercaps);

  gst_object_unref (sc_src);
  gst_object_unref (sc_sink);
  gst_object_unref (ctrl_src);
  gst_object_unref (ctrl_sink);

  g_print ("\n All elements are linked successfully\n");

  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  gboolean ret = FALSE;
  GstSmartCodecContext *appctx = NULL;
  guint intrpt_watch_id = 0;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
  {"width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
      "width", "image width"},
  {"height", 'h', 0, G_OPTION_ARG_INT, &appctx->height, "height",
      "image height"},
  {"output_file", 'o', 0, G_OPTION_ARG_STRING, &appctx->output_file,
      "Output Filename" ,
      "-o /etc/media/video.mp4"},
   { "model", 'm', 0, G_OPTION_ARG_STRING,
    &appctx->model_path,
    "This is an optional parameter and overrides default path\n",
    "    Default model path for YOLOV8 TFLITE: "DEFAULT_MODEL"\n",},
  { "labels", 'l', 0, G_OPTION_ARG_STRING,
    &appctx->labels_path,
    "This is an optional parameter and overrides default path\n",
    "    Default labels path for YOLOV8: "DEFAULT_LABELS"\n"},
  { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new (GST_APP_SUMMARY)) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("\n Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (appctx);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("\n Failed Initializing: Unknown error!\n");
      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("\n Failed to create options context!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  gboolean camera_is_available = is_camera_available ();

  // Check for input source
  if (camera_is_available)
    g_print ("TARGET Can support camera source only \n");

  // set the Output Filename
  if (appctx->output_file == NULL)
    appctx->output_file = g_strdup (DEFAULT_OUTPUT_FILENAME);

  if (appctx->model_path == NULL)
    appctx->model_path = g_strdup (DEFAULT_MODEL);

  if (appctx->labels_path == NULL)
    appctx->labels_path = g_strdup (DEFAULT_LABELS);

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline
  pipeline = gst_pipeline_new ("gst-smartcodec-example");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  appctx->pipeline = pipeline;

  // Build the pipeline
  ret = create_pipe (appctx);
  if (!ret) {
    g_printerr ("\n failed to create GST pipe.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("\n Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("\n Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (state_changed_cb),
      pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("\n Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\n Failed to transition to PAUSED state!\n");
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      gst_app_context_free (appctx);
      return -1;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("\n Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("\n Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("\n Pipeline state change was successful\n");
      break;
  }

  // Start the main loop
  g_print ("\n Application is running... \n");
  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // Set the pipeline to the NULL state
  g_print ("\n Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  g_print ("Encoded mp4 File %s\n", appctx->output_file);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return 0;
}
