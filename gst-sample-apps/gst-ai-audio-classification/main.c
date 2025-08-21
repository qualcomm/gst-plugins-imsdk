/**
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Audio Classification on Live stream.
 *
 * Description:
 * The application takes live stream from file/microphone and gives same to
 * classification LiteRT model for classifying audio
 * and display preview with overlayed AI Model output/classification
 * labels.
 *
 * Pipeline for Gstreamer with pulsesrc:
 * pulsesrc -> audiobuffersplit -> Pre-process -> ML inference
 *          -> Post-process -> Display (waylandsink)
 *
 * Pipeline for Gstreamer with File source:
 * filesrc -> | qtdemux -> h264parse -> v4l2h264dec -> qtivcomposer
 *            | qtdemux -> audio_parse -> audio_dec -> audioconvert
 *              -> audioresample -> audiobuffersplit -> Pre-process
 *              -> ML Inference -> Post-process -> qtivcomposer
 *     qtivcomposer -> Display (waylandsink)
 *
 *     Pre-process: qtimlaconverter
 *     ML Framework: qtimltflite
 *     Post-process: qtimlaclassification -> classification_filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>
#include <json-glib/json-glib.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

/**
* Default models and labels path, if not provided by user
*/
#define DEFAULT_TFLITE_CLASSIFICATION_MODEL \
    "/etc/models/yamnet.tflite"
#define DEFAULT_CLASSIFICATION_LABELS "/etc/labels/yamnet.json"

/**
* Default path of config file
*/
#define DEFAULT_CONFIG_FILE \
    "/etc/configs/config-audio-classification.json"

/**
* Number of Queues used for buffer caching between elements
*/
#define QUEUE_COUNT 5

/**
* Default value of Threshold
*/
#define DEFAULT_THRESHOLD_VALUE 20.0

/**
* Structure for various application specific options
*/
typedef struct
{
  gchar *file_path;
  gchar *model_path;
  gchar *labels_path;
  enum GstAudioPlayerCodecType audio_codec;
  gdouble threshold;
  gboolean use_cpu;
  gboolean use_gpu;
  gboolean use_file;
  gboolean use_pulsesrc;
} GstAppOptions;

/**
* Static grid points to display multiple input stream
*/
static GstVideoRectangle position_data[2] = {
  {0, 0, 1920, 1080},
  {30, 30, 480, 270}
};

/**
* Free Application context:
*
* @param appctx Application Context object.
* @param options Application specific options.
* @param config_file Path to config file
*/
static void
gst_app_context_free (GstAppContext * appctx, GstAppOptions * options,
    gchar * config_file)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (options->file_path != NULL) {
    g_free ((gpointer) options->file_path);
  }

  if (options->model_path != (gchar *) (&DEFAULT_TFLITE_CLASSIFICATION_MODEL) &&
      options->model_path != NULL) {
    g_free ((gpointer) options->model_path);
  }

  if (options->labels_path != (gchar *) (&DEFAULT_CLASSIFICATION_LABELS) &&
      options->labels_path != NULL) {
    g_free ((gpointer) options->labels_path);
  }

  if (config_file != NULL && config_file != (gchar *) (&DEFAULT_CONFIG_FILE)) {
    g_free ((gpointer) config_file);
    config_file = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
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
* Function to link the dynamic video pad of demux to queue:
*
* @param element GStreamer source element
* @param pad GStreamer source element pad
* @param data sink element object
*/
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad;
  GstElement *queue = (GstElement *) data;
  GstPadLinkReturn ret;

  // Get the static sink pad from the queue
  sinkpad = gst_element_get_static_pad (queue, "sink");

  // Link the source pad to the sink pad
  ret = gst_pad_link (pad, sinkpad);
  if (!ret) {
    g_printerr ("Failed to link pad to sinkpad\n");
  }

  gst_object_unref (sinkpad);
}

/**
* Create GST pipeline: has 3 main steps
* 1. Create all elements/GST Plugins
* 2. Set Paramters for each plugin
* 3. Link plugins to create GST pipeline
*
* @param appctx Application Context Pointer.
* @param options Application specific options.
*/
static gboolean
create_pipe (GstAppContext * appctx, GstAppOptions * options)
{
  GstElement *audio_parse = NULL, *audio_dec = NULL;
  GstElement *audioresample = NULL, *audioconvert = NULL;
  GstElement *audio_caps = NULL, *pulsesrc = NULL;
  GstElement *queue[QUEUE_COUNT], *audiobuffersplit = NULL;
  GstElement *qtimlaconverter = NULL, *qtimltflite = NULL;
  GstElement *qtimlaclassification = NULL, *classification_filter = NULL;
  GstElement *qtivcomposer = NULL, *waylandsink = NULL;
  GstElement *filesrc = NULL, *qtdemux = NULL, *h264parse = NULL;
  GstElement *v4l2h264dec = NULL;
  GstElement *v4l2h264dec_caps = NULL;
  GstCaps *pad_filter = NULL, *filtercaps = NULL;
  GstPad *vcomposer_sink[2];
  GstStructure *delegate_options = NULL;
  gboolean ret = FALSE;
  gchar element_name[128], settings[128];
  gint pos_vals[2], dim_vals[2];
  gint module_id;

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    queue[i] = NULL;
  }

  // 1. Create the elements or Plugins
  if (options->use_file) {
    // Create file source element for file stream
    filesrc = gst_element_factory_make ("filesrc", "filesrc");
    if (!filesrc) {
      g_printerr ("Failed to create filesrc\n");
      goto error_clean_elements;
    }

    // Create qtdemux for demuxing the filesrc
    qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");
    if (!qtdemux) {
      g_printerr ("Failed to create qtdemux\n");
      goto error_clean_elements;
    }

    // Create h264parse element for parsing the stream
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    if (!h264parse) {
      g_printerr ("Failed to create h264parse\n");
      goto error_clean_elements;
    }

    // Create v4l2h264dec element for encoding the stream
    v4l2h264dec = gst_element_factory_make ("v4l2h264dec", "v4l2h264dec");
    if (!v4l2h264dec) {
      g_printerr ("Failed to create v4l2h264dec\n");
      goto error_clean_elements;
    }

    // Create caps for v4l2h264dec
    v4l2h264dec_caps =
        gst_element_factory_make ("capsfilter", "v4l2h264dec_caps");
    if (!v4l2h264dec_caps) {
      g_printerr ("Failed to create v4l2h264dec_caps\n");
      goto error_clean_elements;
    }

    // Create elements for parsing and decoding audio
    if (options->audio_codec == GST_ACODEC_MP3) {
      // Create mpegaudioparse for parsing mp3 audio
      audio_parse = gst_element_factory_make ("mpegaudioparse", "audio_parse");
      if (!audio_parse) {
        g_printerr ("Failed to create mpegaudioparse\n");
        goto error_clean_elements;
      }
      // Create mpg123audiodec for decoding mp3 audio
      audio_dec = gst_element_factory_make ("mpg123audiodec", "audio_dec");
      if (!audio_dec) {
        g_printerr ("Failed to create mpg123audiodec\n");
        goto error_clean_elements;
      }
    } else if (options->audio_codec == GST_ACODEC_FLAC) {
      // Create flacparse for parsing flac audio
      audio_parse = gst_element_factory_make ("flacparse", "audio_parse");
      if (!audio_parse) {
        g_printerr ("Failed to create flacparse\n");
        goto error_clean_elements;
      }

      // Create flacdec for decoding flac audio
      audio_dec = gst_element_factory_make ("flacdec", "audio_dec");
      if (!audio_dec) {
        g_printerr ("Failed to create flacdec\n");
        goto error_clean_elements;
      }
    } else {
      g_printerr ("Invalid input codec type\n");
      goto error_clean_elements;
    }

    // Create audioconvert to allow conversion b/w formats
    audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
    if (!audioconvert) {
      g_printerr ("Failed to create audioconvert\n");
      goto error_clean_elements;
    }

    // Create audioresample to allow resampling to different sample rates
    audioresample = gst_element_factory_make ("audioresample", "audioresample");
    if (!audioresample) {
      g_printerr ("Failed to create audioresample\n");
      goto error_clean_elements;
    }

  } else if (options->use_pulsesrc) {
    pulsesrc = gst_element_factory_make ("pulsesrc", "pulsesrc");
    if (!pulsesrc) {
      g_printerr ("Failed to create pulsesrc\n");
      goto error_clean_elements;
    }

    // Create caps to negotiate b/w source and audiobuffersplit
    audio_caps = gst_element_factory_make ("capsfilter", "audio_caps");
    if (!audio_caps) {
      g_printerr ("Failed to create audio_caps\n");
      goto error_clean_elements;
    }
  } else {
    g_printerr ("Invalid source type\n");
    goto error_clean_elements;
  }

  // Create audiobuffersplit to split raw audio buffers
  audiobuffersplit =
      gst_element_factory_make ("audiobuffersplit", "audiobuffersplit");
  if (!audiobuffersplit) {
    g_printerr ("Failed to create audiobuffersplit\n");
    goto error_clean_elements;
  }

  // Create queue to decouple the processing on sink and source pad.
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      goto error_clean_elements;
    }
  }

  // Create qtimlaconverter for Input preprocessing
  qtimlaconverter = gst_element_factory_make ("qtimlaconverter",
      "qtimlaconverter");
  if (!qtimlaconverter) {
    g_printerr ("Failed to create qtimlaconverter\n");
    goto error_clean_elements;
  }

  // Create the ML inferencing plugin qtimltflite
  qtimltflite = gst_element_factory_make ("qtimltflite", "qtimltflite");
  if (!qtimltflite) {
    g_printerr ("Failed to create qtimltflite\n");
    goto error_clean_elements;
  }

  // Create plugin for ML postprocessing
  qtimlaclassification = gst_element_factory_make ("qtimlpostprocess",
      "qtimlpostprocess");
  if (!qtimlaclassification) {
    g_printerr ("Failed to create qtimlaclassification\n");
    goto error_clean_elements;
  }

  // Composer to combine video preview with ML post-proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    goto error_clean_elements;
  }

  // Used to negotiate between ML post proc o/p and qtivcomposer
  classification_filter = gst_element_factory_make ("capsfilter",
      "classification_filter");
  if (!classification_filter) {
    g_printerr ("Failed to create classification_filter\n");
    goto error_clean_elements;
  }

  // Create Wayland compositor to render output on Display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  if (!waylandsink) {
    g_printerr ("Failed to create waylandsink \n");
    goto error_clean_elements;
  }

  // 2. Set properties for all GST plugin elements
  if (options->use_file) {
    // 2.1 Set the capabilities of file stream
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    g_object_set (G_OBJECT (filesrc), "location", options->file_path, NULL);
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else if (options->use_pulsesrc) {
    // 2.2 Set properties for audio stream
    filtercaps = gst_caps_new_simple ("audio/x-raw",
        "format", G_TYPE_STRING, "S16LE", NULL);
    g_object_set (G_OBJECT (audio_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  }

  // 2.3 Set properties of audiobuffersplit
  g_object_set (G_OBJECT (audiobuffersplit), "output-buffer-size", 31200, NULL);

  // 2.4 Set properties of qtimlaconverter
  g_object_set (G_OBJECT (qtimlaconverter), "sample-rate", 16000, NULL);

  gst_element_set_enum_property (qtimlaconverter, "feature", "lmfe");

  g_object_set (G_OBJECT (qtimlaconverter),
      "params", "params,nfft=96,nhop=160,nmels=64,chunklen=0.96;", NULL);

  // 2.5 Select the HW to GPU/CPU for model inferencing using
  // delegate property
  if (options->use_cpu) {
    g_print ("Using CPU Delegate\n");
    g_object_set (G_OBJECT (qtimltflite), "model", options->model_path,
        "delegate", GST_ML_TFLITE_DELEGATE_NONE, NULL);
  } else if (options->use_gpu) {
    g_print ("Using GPU Delegate\n");
    delegate_options =
        gst_structure_from_string ("QNNExternalDelegate,backend_type=gpu;",
        NULL);
    g_object_set (G_OBJECT (qtimltflite), "model", options->model_path,
        "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
    g_object_set (G_OBJECT (qtimltflite), "external-delegate-path",
        "libQnnTFLiteDelegate.so", NULL);
    g_object_set (G_OBJECT (qtimltflite), "external-delegate-options",
        delegate_options, NULL);
    gst_structure_free (delegate_options);
  } else {
    g_printerr ("Invalid Runtime Selected\n");
    goto error_clean_elements;
  }

  // 2.6 Set properties for ML postproc plugins- module, threshold, labels
  module_id = get_enum_value (qtimlaclassification, "module", "yamnet");
  if (module_id != -1) {
    snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
    g_object_set (G_OBJECT (qtimlaclassification),
        "results", 3, "module", module_id,
        "labels", options->labels_path,
        "settings", settings, NULL);
  } else {
    g_printerr ("Module yamnet is not available in qtimlaclassification.\n");
    goto error_clean_elements;
  }

  // 2.7 Set the properties of Wayland compositor
  g_object_set (G_OBJECT (waylandsink), "sync", TRUE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

  // 2.9 Set the properties of pad_filter for negotiation with qtivcomposer
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 368, "height", G_TYPE_INT, 64, NULL);

  g_object_set (G_OBJECT (classification_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 3. Setup the pipeline
  g_print ("Adding all elements to the pipeline...\n");

  if (options->use_file) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), filesrc,
        qtdemux, h264parse, v4l2h264dec, v4l2h264dec_caps, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline), audio_parse,
        audio_dec, audioconvert, audioresample, NULL);
  } else if (options->use_pulsesrc) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), pulsesrc, audio_caps, NULL);
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }

  gst_bin_add_many (GST_BIN (appctx->pipeline), audiobuffersplit,
      qtimlaconverter, qtimltflite, qtimlaclassification,
      classification_filter, qtivcomposer, waylandsink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // Create Pipeline for Audio Classification
  if (options->use_file) {
    ret = gst_element_link_many (filesrc, qtdemux, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for "
          "filesource->qtdemux\n");
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (queue[0], h264parse, v4l2h264dec,
        v4l2h264dec_caps, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for parse->queue\n");
      goto error_clean_pipeline;
    }

    ret = gst_element_link_many (queue[1], audio_parse, audio_dec,
        audioconvert, audioresample, audiobuffersplit, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for audio_parse->"
          "audio_dec->audioconvert->audioresample->audiobuffersplit\n");
      goto error_clean_pipeline;
    }

  } else if (options->use_pulsesrc) {
    ret = gst_element_link_many (pulsesrc, audio_caps, audiobuffersplit, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for pulsesrc->"
          "audiobuffersplit\n");
      goto error_clean_pipeline;
    }

  } else {
    g_printerr ("Invalid input source selected\n");
    goto error_clean_elements;
  }

  ret = gst_element_link_many (audiobuffersplit, queue[2], qtimlaconverter,
      qtimltflite, qtimlaclassification,
      classification_filter, queue[3], NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for audiobuffersplit->"
        "mlaconverter->mlelement->mlaclassification\n");
    goto error_clean_elements;
  }
  if (options->use_file) {
    ret = gst_element_link_many (v4l2h264dec_caps, qtivcomposer, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for "
          "v4l2h264dec->qtivcomposer.\n");
      goto error_clean_pipeline;
    }
  }

  if (options->use_file) {
    ret = gst_element_link_many (queue[3], qtivcomposer,
        queue[4], waylandsink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "mlaclassification->qtivcomposer->waylandsink\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_pulsesrc) {
    ret = gst_element_link_many (queue[3], waylandsink, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "mlaclassification->waylandsink\n");
      goto error_clean_pipeline;
    }
  } else {
    g_printerr ("Invalid input source selected\n");
    goto error_clean_elements;
  }

  if (options->use_file) {
    g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added),
        queue[1]);
    g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added),
        queue[0]);

    // Set overlay window size for Classification to display text labels
    vcomposer_sink[0] = gst_element_get_static_pad (qtivcomposer, "sink_0");
    if (vcomposer_sink[0] == NULL) {
      g_printerr ("Sink pad 1 of vcomposer couldn't be retrieved\n");
      goto error_clean_pipeline;
    }

    vcomposer_sink[1] = gst_element_get_static_pad (qtivcomposer, "sink_1");
    if (vcomposer_sink[1] == NULL) {
      g_printerr ("Sink pad 1 of vcomposer couldn't be retrieved\n");
      goto error_clean_pipeline;
    }

    for (int i = 0; i < 2; i++) {
      GstVideoRectangle *positions = NULL;
      GValue position = G_VALUE_INIT;
      GValue dimension = G_VALUE_INIT;
      positions = position_data;

      g_value_init (&position, GST_TYPE_ARRAY);
      g_value_init (&dimension, GST_TYPE_ARRAY);

      pos_vals[0] = positions[i].x;
      pos_vals[1] = positions[i].y;
      dim_vals[0] = positions[i].w;
      dim_vals[1] = positions[i].h;

      build_pad_property (&position, pos_vals, 2);
      build_pad_property (&dimension, dim_vals, 2);

      g_object_set_property (G_OBJECT (vcomposer_sink[i]), "position",
          &position);
      g_object_set_property (G_OBJECT (vcomposer_sink[i]), "dimensions",
          &dimension);

      g_value_unset (&position);
      g_value_unset (&dimension);
      gst_object_unref (vcomposer_sink[i]);
    }
  }

  return TRUE;

error_clean_elements:
  cleanup_gst (&audio_parse, &audio_dec, &audioresample, &audioconvert,
      &filesrc, &qtdemux, &h264parse, &v4l2h264dec, &v4l2h264dec_caps,
      &pulsesrc, &audio_caps, &qtimlaconverter, &qtimltflite,
      &qtimlaclassification, &qtivcomposer, &classification_filter,
      &waylandsink, NULL);
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_object_unref (queue[i]);
  }
  return FALSE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline);
  return FALSE;
}

/**
* Parse JSON file to read input parameters
*
* @param config_file Path to config file
* @param options Application specific options
*/
gint
parse_json (gchar * config_file, GstAppOptions * options)
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

  // Input source mp4 file path
  if (json_object_has_member (root_obj, "file-path")) {
    options->file_path =
        g_strdup (json_object_get_string_member (root_obj, "file-path"));
  }

  // ML model path for inference
  if (json_object_has_member (root_obj, "model")) {
    options->model_path =
        g_strdup (json_object_get_string_member (root_obj, "model"));
  }

  // Labels path
  if (json_object_has_member (root_obj, "labels")) {
    options->labels_path =
        g_strdup (json_object_get_string_member (root_obj, "labels"));
  }

  // Threshold for classification result
  if (json_object_has_member (root_obj, "threshold")) {
    options->threshold = json_object_get_int_member (root_obj, "threshold");
  }

  // HW inference runtime, cpu or gpu
  if (json_object_has_member (root_obj, "runtime")) {
    const gchar *delegate = json_object_get_string_member (root_obj, "runtime");

    if (g_strcmp0 (delegate, "cpu") == 0)
      options->use_cpu = TRUE;
    else if (g_strcmp0 (delegate, "gpu") == 0)
      options->use_gpu = TRUE;
    else {
      gst_printerr ("Runtime can only be one of \"cpu\" or \"gpu\"\n");
      g_object_unref (parser);
      return -1;
    }
  }

  // Input file audio codec, mp3 or flac
  if (json_object_has_member (root_obj, "codec")) {
    const gchar *codec = json_object_get_string_member (root_obj, "codec");

    if (g_strcmp0 (codec, "mp3") == 0)
      options->audio_codec = GST_ACODEC_MP3;
    else if (g_strcmp0 (codec, "flac") == 0)
      options->audio_codec = GST_ACODEC_FLAC;
    else {
      gst_printerr ("Codec can only be one of \"mp3\" or \"flac\"\n");
      g_object_unref (parser);
      return -1;
    }
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
  GstAppContext appctx = { };
  gchar help_description[2048];
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;
  GstAppOptions options = { };
  gchar *config_file = NULL;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // Set default value
  options.model_path = NULL;
  options.file_path = NULL;
  options.model_path = DEFAULT_TFLITE_CLASSIFICATION_MODEL;
  options.labels_path = DEFAULT_CLASSIFICATION_LABELS;
  options.use_cpu = FALSE, options.use_gpu = FALSE;
  options.use_file = FALSE, options.use_pulsesrc = FALSE;
  options.threshold = DEFAULT_THRESHOLD_VALUE;
  options.audio_codec = GST_ACODEC_MP3;

  // Structure to define the user options selection
  GOptionEntry entries[] = {
    { "config-file", 0, 0, G_OPTION_ARG_STRING,
      &config_file,
      "Path to config file\n",
      NULL
    },
    {NULL}
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];
  snprintf (help_description, 2047, "\nExample:\n"
      "  %s --config-file=%s\n"
      "\nThis Sample App demonstrates Audio Classification on input stream\n"
      "\nConfig file Fields:\n"
      "  file-path: \"/PATH\"\n"
      "      File source path\n"
      "  If file-path is not provided, "
      "then pulsesrc is selected as input source\n"
      "  model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path for TFLITE Model: "
      DEFAULT_TFLITE_CLASSIFICATION_MODEL"\n"
      "  labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default labels path: "DEFAULT_CLASSIFICATION_LABELS"\n"
      "  threshold: 0 to 100\n"
      "      This is an optional parameter and overides default threshold value 40\n"
      "  runtime: \"cpu\" or \"gpu\"\n"
      "      This is an optional parameter. If not filled, "
      "then default gpu runtime is selected\n"
      "  codec: \"mp3\" or \"flac\"\n"
      "      Define audio codec for input file. If not filled, "
      "then default mp3 is selected\n",
      app_name, DEFAULT_CONFIG_FILE);
  help_description[2047] = '\0';

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

  // Check for input source
  if (options.file_path != NULL) {
    options.use_file = TRUE;
  } else {
    options.use_pulsesrc = TRUE;
  }

  // Terminate if more than one source are there.
  if ((options.use_file + options.use_pulsesrc) > 1) {
    g_print ("Select any one input source from file or pulsesrc\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.threshold < 0 || options.threshold > 100) {
    g_printerr ("Invalid threshold value selected\n"
        "Threshold Value lies between: \n"
        "    Min: 0\n"
        "    Max: 100\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if ((options.use_cpu + options.use_gpu) > 1) {
    g_print ("Select any one runtime from CPU or GPU\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.use_cpu == FALSE && options.use_gpu == FALSE) {
    g_print ("Setting GPU as default Runtime\n");
    options.use_gpu = TRUE;
  }

  if (!file_exists (options.model_path)) {
    g_print ("Invalid model file path: %s\n", options.model_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (!file_exists (options.labels_path)) {
    g_print ("Invalid labels file path: %s\n", options.labels_path);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.file_path != NULL) {
    if (!file_exists (options.file_path)) {
      g_print ("Invalid file source path: %s\n", options.file_path);
      gst_app_context_free (&appctx, &options, config_file);
      return -EINVAL;
    }
  }

  g_print ("Running app with model: %s and labels: %s\n",
      options.model_path, options.labels_path);

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
  ret = create_pipe (&appctx, &options);
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
  // Remove the interrupt signal handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  g_print ("Set pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Destroy pipeline\n");
  gst_app_context_free (&appctx, &options, config_file);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
