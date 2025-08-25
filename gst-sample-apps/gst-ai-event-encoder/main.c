/**
 * Copyright (c) Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * Capturing video snapshot when human is encountered on live stream.
 *
 * Description:
 * The application takes live video stream from camera/file/rtsp and gives same
 * to Object Detection LiteRT model. Postprocessing is carried out on the output
 * of the Object Detection model, and the detection labels and bounding boxes
 * are overlayed over original stream for display preview.
 * Metadata obtained from post-processing is used to check if human is encountered,
 * if yes then video snapshot is taken.
 *
 * Pipeline for Gstreamer with Camera:
 * qtiqmmfsrc  -> | qmmfsrc_caps (Preview)    -> qtivcomposer
 *                | qmmfsrc_caps (Inference)  -> Pre-process -> Inference
 *
 * Pipeline for Gstreamer with File source:
 * filesrc -> qtdemux -> h264parse -> v4l2h264dec -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Preprocess -> Inference -> Post-process -> qtivcomposer
 *
 * Pipeline for Gstreamer with RTSP source:
 * rtspsrc -> rtph264depay -> h264parse -> v4l2h264dec -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre-process -> Inference
 *
 *     Inference -> | Post-process -> qtivcomposer
 *                  | Post-process -> appsink (parse metadata)
 *
 *     qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
 *
 *     Pre-process: qtimlvconverter
 *     ML Framework: qtimltflite
 *     Post-process: qtimlvdetection -> detection_filter
 */

#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <gst/sampleapps/gst_sample_apps_utils.h>
#include <gst/app/gstappsrc.h>

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_TFLITE_MODEL "/etc/models/yolox_quantized.tflite"
#define DEFAULT_LABELS "/etc/labels/yolox.json"

/**
 * Default settings of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input
 */
#define DEFAULT_INFERENCE_WIDTH 640
#define DEFAULT_INFERENCE_HEIGHT 360
#define DEFAULT_CAMERA_OUTPUT_WIDTH 1280
#define DEFAULT_CAMERA_OUTPUT_HEIGHT 720
#define SECONDARY_CAMERA_OUTPUT_WIDTH 1280
#define SECONDARY_CAMERA_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Default wayland display width and height
 */
#define DEFAULT_DISPLAY_WIDTH 1920
#define DEFAULT_DISPLAY_HEIGHT 1080

#define OUTPUT_WIDTH 1280
#define OUTPUT_HEIGHT 720

/**
* Default path of config file
*/
#define DEFAULT_CONFIG_FILE "/etc/configs/config-event-encoder.json"

/**
* Number of Queues used for buffer caching between elements
*/
#define QUEUE_COUNT 10
#define SNAPSHOT_QUEUE_COUNT 5

/**
 * Number of post-processing plugins used
 */
#define DETECTION_COUNT 2

/**
 * Defalut value of threshold
 */
#define DEFAULT_THRESHOLD_VALUE  40.0

gboolean start_recording = FALSE;

/**
 * RecordingPipelineState:
 * @PAUSED: PAUSED state of Recording Pipeline.
 * @RUNNING: RUNNING state of Recording Pipeline.
 * @NULL_STATE: NULL state of Recording Pipeline.
 *
 * States of Recording Piepline.
 */
typedef enum
{
  PAUSED,
  RUNNING,
  NULL_STATE
} RecordingPipelineState;

/**
 * RecordingStatus:
 * @STARTED: starting status of Recording.
 * @STOPPED: stopped staus of Recording.
 *
 * Status of Recording.
 */
typedef enum
{
  STOPPED,
  STARTED
} RecordingStatus;

/**
* Structure to hold two pipeline, video count, recording status
* and other info like mutexes.
*/
typedef struct
{
  GMainLoop *mloop;
  GstElement *pipeline_main, *pipeline_recoding;
  RecordingPipelineState recording_pipeline_state;
  RecordingStatus recording_status;
  gint video_count;
  gint wait_frame_count;
  GMutex lock;
} GstAppsContext;

/**
 * Structure for various application specific options
 */
typedef struct
{
  gchar *file_path;
  gchar *rtsp_ip_port;
  gchar *model_path;
  gchar *labels_path;
  GstCameraSourceType camera_type;
  gdouble threshold;
  gint delegate_type;
  gboolean use_cpu;
  gboolean use_gpu;
  gboolean use_dsp;
  gboolean use_file;
  gboolean use_rtsp;
  gboolean use_camera;
} GstAppOptions;

/**
 * State Change Function:
 *
 * @param element Application Context object
 */
static gboolean
wait_for_state_change (GstElement * element)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  g_print ("Element is PREROLLING ...\n");

  ret = gst_element_get_state (element, NULL, NULL, GST_CLOCK_TIME_NONE);
  g_print ("State changes successsful ...\n");

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Element failed to PREROLL!\n");
    return FALSE;
  }
  return TRUE;
}

/**
* Handles interrupt by CTRL+C.
*
* @param userdata pointer to AppContext.
*/
gboolean
interrupt_handler (gpointer userdata)
{
  GstAppsContext *appctx = (GstAppsContext *) userdata;
  GstState state1, state2, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (appctx->pipeline_main, &state1, &pending,
          GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (appctx->pipeline_main, gst_event_new_eos ());
    return TRUE;
  }

  if (!gst_element_get_state (appctx->pipeline_recoding, &state2, &pending,
          GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (appctx->pipeline_recoding, gst_event_new_eos ());
    return TRUE;
  }

  if (state1 == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline_main, gst_event_new_eos ());
  }

  if (state2 == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline_recoding, gst_event_new_eos ());
  }

  g_main_loop_quit (appctx->mloop);

  return TRUE;
}

/**
* Recording pipeline end-of-stream. Call-back function
*
* @param bus pipeline bus.
* @param message eos Message
* @param userdata Userdata
*/
void
recording_eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppsContext *appctx = (GstAppsContext *) userdata;
  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->pipeline_recoding, GST_STATE_NULL)) {
    wait_for_state_change (appctx->pipeline_recoding);
  }
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 * @param GstAppOptions Application Options
 * @param config_file Config File
 */
static void
gst_app_context_free (GstAppsContext * appctx, GstAppOptions * options, gchar * config_file)
{
  // If specific pointer is not NULL, unref it
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (options->file_path != NULL) {
    g_free ((gpointer) options->file_path);
  }

  if (options->rtsp_ip_port != NULL) {
    g_free ((gpointer) options->rtsp_ip_port);
  }

  if (options->model_path != (gchar *) (&DEFAULT_TFLITE_MODEL) &&
      options->model_path != NULL) {
    g_free ((gpointer) options->model_path);
  }

  if (options->labels_path != (gchar *) (&DEFAULT_LABELS) &&
      options->labels_path != NULL) {
    g_free ((gpointer) options->labels_path);
  }

  if (config_file != NULL && config_file != (gchar *) (&DEFAULT_CONFIG_FILE)) {
    g_free ((gpointer) config_file);
    config_file = NULL;
  }

  if (appctx->pipeline_main != NULL) {
    gst_object_unref (appctx->pipeline_main);
    appctx->pipeline_main = NULL;
  }

  if (appctx->pipeline_recoding != NULL) {
    gst_object_unref (appctx->pipeline_recoding);
    appctx->pipeline_recoding = NULL;
  }
}

/**
 * Function to link the dynamic video pad of demux to queue:
 *
 * @param element Gstreamer source element
 * @param pad Gstreamer source element pad
 * @param data sink element data
 */
static void
on_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad;
  GstElement *queue = (GstElement *) data;
  GstPadLinkReturn ret;

  // Get the static sink pad from the queue
  sinkpad = gst_element_get_static_pad (queue, "sink");
  ret = gst_pad_link (pad, sinkpad);
  if (ret != GST_PAD_LINK_OK) {
    g_printerr ("Failed to link pad to sinkpad\n");
  }

  gst_object_unref (sinkpad);
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
 * Callback function for appsink to parse metadata obtained from
 * post-processing plugin
 *
 * @param appsink Appsink which receives metadata
 * @param user_data Pointer to allow user to pass data to callback function
 */
GstFlowReturn
appsink_detection (GstElement * appsink, gpointer user_data)
{
  GValue vlist = G_VALUE_INIT;
  GstMapInfo memmap = { };
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstStructure *entry = NULL;
  const GValue *bboxes = NULL, *value = NULL;
  const GValue *bbox_value = NULL;
  GstStructure *bbox_entry = NULL;
  gchar *label = NULL, *ctx = NULL;
  gchar element_name[128];
  gchar *data = NULL, *token = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  guint size = 0, idx = 0, people_count = 0;
  gint display_width, display_height;
  GstAppsContext *appctx = (GstAppsContext *) user_data;

  g_signal_emit_by_name (appsink, "pull-sample", &sample, &ret);
  if (ret != GST_FLOW_OK) {
    g_printerr ("Cannot pull GstSample\n");
    goto exit;
  }

  if (sample) {
    buffer = gst_sample_get_buffer (sample);
    if (buffer == NULL) {
      g_printerr ("Cannot get buffer from sample");
      goto exit;
    }

    if (!gst_buffer_map (buffer, &memmap, GST_MAP_READ)) {
      g_printerr ("buffer mapping failed");
      goto exit;
    }

    if (!get_active_display_mode (&display_width, &display_height)) {
      g_warning
          ("Failed to get active display mode, using 1080p default config");
      display_width = DEFAULT_DISPLAY_WIDTH;
      display_height = DEFAULT_DISPLAY_HEIGHT;
    }

    size = memmap.size + 1;
    data = g_new0 (gchar, size);
    memcpy ((gpointer) (data), memmap.data, memmap.size);

    token = strtok_r (data, "\n", &ctx);

    g_value_init (&vlist, GST_TYPE_LIST);
    if (!gst_value_deserialize (&vlist, token)) {
      g_printerr ("Deserialization failed\n");
      goto exit;
    }

    size = gst_value_list_get_size (&vlist);
    people_count = 0;
    for (idx = 0; idx < size; idx++) {
      value = gst_value_list_get_value (&vlist, idx);
      entry = GST_STRUCTURE (g_value_get_boxed (value));

      guint seqnum = 0, n_entries = 0;
      gst_structure_get_uint (entry, "sequence-index", &seqnum);
      gst_structure_get_uint (entry, "sequence-num-entries", &n_entries);
      GST_INFO_OBJECT (appsink, "seqnum: %d, n_entries: %d", seqnum, n_entries);

      bboxes = gst_structure_get_value (entry, "bounding-boxes");
      guint bbox_size = gst_value_array_get_size (bboxes);
      for (guint i = 0; i < bbox_size; i++) {
        gfloat x = 0, y = 0, width = 0, height = 0;
        gdouble confidence;

        value = gst_value_array_get_value (bboxes, i);
        bbox_entry = GST_STRUCTURE (g_value_get_boxed (value));

        label = g_strdup (gst_structure_get_name (bbox_entry));
        GST_INFO_OBJECT (appsink, "Bounding box label: %s", label);

        gst_structure_get_double (bbox_entry, "confidence", &confidence);
        GST_INFO_OBJECT (appsink, "Confidence: %f", confidence);

        bbox_value = gst_structure_get_value (bbox_entry, "rectangle");
        x = g_value_get_float (gst_value_array_get_value (bbox_value, 0));
        y = g_value_get_float (gst_value_array_get_value (bbox_value, 1));
        width = g_value_get_float (gst_value_array_get_value (bbox_value, 2));
        height = g_value_get_float (gst_value_array_get_value (bbox_value, 3));
        GST_INFO_OBJECT (appsink, "Box: [x: %d, y: %d, width: %d, height: %d]",
            (gint) (x * display_width), (gint) (y * display_height),
            (gint) (width * display_width), (gint) (height * display_height));

        if (g_strcmp0 (label, "person") == 0) {
          people_count++;
        }
      }
    }

    if (people_count != 0) {
      appctx->recording_status = STARTED;
      appctx->wait_frame_count = 0;
    }

    if ((people_count == 0) && (appctx->recording_status == STARTED)) {
      appctx->wait_frame_count++;
    }

    // Send EOS to pipeline if there is no person detection in 150 frames
    if ((appctx->wait_frame_count >= 150) &&
        (appctx->recording_pipeline_state == RUNNING)) {
      gst_element_send_event (appctx->pipeline_recoding,
          gst_event_new_eos ());
      g_mutex_lock (&appctx->lock);
      appctx->recording_pipeline_state = PAUSED;
      g_mutex_unlock (&appctx->lock);
      appctx->recording_status = STOPPED;
      g_print ("Recording Stopped video_count=%d\n", appctx->video_count);
    }

    // Start the recording pipeline if person found in the frame
    if ((appctx->recording_pipeline_state == PAUSED) &&
        (appctx->recording_status == STARTED)) {
      GstElement *filesink = NULL;
      GstState state = GST_STATE_NULL;
      filesink = gst_bin_get_by_name (GST_BIN (appctx->pipeline_recoding),
          "filesink");

      appctx->video_count = appctx->video_count + 1;
      snprintf (element_name, 127, "/etc/media/output-%d.mp4",
          appctx->video_count);
      g_object_set (G_OBJECT (filesink), "location", element_name, NULL);
      g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);
      g_object_set (G_OBJECT (filesink), "async", FALSE, NULL);

      gst_element_get_state (appctx->pipeline_recoding, &state, NULL,
          GST_CLOCK_TIME_NONE);

      if (GST_STATE_CHANGE_ASYNC ==
          gst_element_set_state (appctx->pipeline_recoding,
              GST_STATE_PLAYING)) {
        wait_for_state_change (appctx->pipeline_recoding);
      }

      gst_element_get_state (appctx->pipeline_recoding, &state, NULL,
          GST_CLOCK_TIME_NONE);

      g_mutex_lock (&appctx->lock);
      appctx->recording_pipeline_state = RUNNING;
      g_mutex_unlock (&appctx->lock);
      g_print ("Recording Started video_count=%d\n", appctx->video_count);
    }
  }
  g_free (data);
  g_free (label);
  g_value_unset (&vlist);

exit:
  if (buffer) {
    gst_buffer_unmap (buffer, &memmap);
  }
  if (sample) {
    gst_sample_unref (sample);
  }
  return GST_FLOW_OK;
}

/**
 * Gstsample Pointer Release.
 *
 * @param sample: Pointer to Gstsample where new buffer will be stored.
 */
static void
sample_release (GstSample * sample)
{
  gst_sample_unref (sample);
  gst_sample_set_buffer (sample, NULL);
}

/**
 * Signal Connection to handle new samples from appsink in Pipeline.
 *
 * @param appsink: Allows to pull buffer from pipeline.
 * @param user_data: Application specific data
 */
GstFlowReturn
appsink_recording (GstElement * appsink, gpointer user_data)
{
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstElement *appsrc = NULL;
  GstBuffer *copybuffer = NULL;
  GstAppsContext *appctx = (GstAppsContext *) user_data;
  GstFlowReturn ret = GST_FLOW_OK;

  // Emit available sample and retrieve it
  g_signal_emit_by_name (appsink, "pull-sample", &sample);
  if (!sample) {
    g_printerr ("ERROR: Failed to pull sample.\n");
    sample_release (sample);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&appctx->lock);
  if (appctx->recording_pipeline_state == PAUSED) {
    sample_release (sample);
    g_mutex_unlock (&appctx->lock);
    return GST_FLOW_OK;
  }
  g_mutex_unlock (&appctx->lock);

  appsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline_recoding), "appsrc");
  if (appsrc == NULL) {
    g_printerr ("ERROR: Failed to get appsrc.\n");
    sample_release (sample);
    return GST_FLOW_ERROR;
  }

  buffer = gst_sample_get_buffer (sample);
  if (!buffer) {
    g_printerr ("ERROR: Failed to get buffer from sample.\n");
    sample_release (sample);
    return GST_FLOW_ERROR;
  }

  copybuffer = gst_buffer_copy (buffer);
  sample_release (sample);

  g_signal_emit_by_name (appsrc, "push-buffer", copybuffer, &ret);
  if (ret != GST_FLOW_OK) {
    g_printerr ("ERROR: Failed to emit push-buffer signal.\n");
    return GST_FLOW_ERROR;
  }

  gst_buffer_unref (copybuffer);
  gst_object_unref (appsrc);
  return GST_FLOW_OK;
}

/**
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST plugins
 * 2. Set Parameters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer
 * @param options Application specific options
 */
static gboolean
create_pipe (GstAppsContext * appctx, GstAppOptions * options)
{
  GstElement *qtiqmmfsrc = NULL, *qmmfsrc_caps = NULL;
  GstElement *qmmfsrc_caps_preview = NULL;
  GstElement *queue[QUEUE_COUNT], *tee = NULL;
  GstElement *qtimlvconverter = NULL, *qtimlelement = NULL;
  GstElement *qtimlvdetection[DETECTION_COUNT], *detection_filter = NULL;
  GstElement *qtivcomposer = NULL, *fpsdisplaysink = NULL, *waylandsink = NULL;
  GstElement *filesrc = NULL, *qtdemux = NULL, *h264parse = NULL;
  GstElement *v4l2h264dec = NULL, *rtspsrc = NULL, *rtph264depay = NULL;
  GstElement *v4l2h264dec_caps = NULL;
  GstElement *detection_tee = NULL, *appsink_caps = NULL, *appsink = NULL;
  GstElement *composer_tee = NULL, *composer_appsink = NULL;
  GstCaps *pad_filter = NULL, *filtercaps = NULL, *appsink_filter = NULL;
  GstPad *vcomposer_sink;
  GstStructure *delegate_options = NULL;
  GstPad *qtiqmmfsrc_type = NULL;
  gboolean ret = FALSE;
  gchar element_name[128], settings[128];
  GValue layers = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  gint pos_vals[2], dim_vals[2];
  gint primary_camera_width = DEFAULT_CAMERA_OUTPUT_WIDTH;
  gint primary_camera_height = DEFAULT_CAMERA_OUTPUT_HEIGHT;
  gint secondary_camera_width = SECONDARY_CAMERA_OUTPUT_WIDTH;
  gint secondary_camera_height = SECONDARY_CAMERA_OUTPUT_HEIGHT;
  gint inference_width = DEFAULT_INFERENCE_WIDTH;
  gint inference_height = DEFAULT_INFERENCE_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;
  gint module_id;
  GValue video_type = G_VALUE_INIT;

  // pipeline recording elements
  GstElement *appsrc = NULL;
  GstElement *v4l2h264enc = NULL;
  GstElement *qtivtransform = NULL;
  GstElement *file_enc_h264parse = NULL;
  GstElement *mp4mux = NULL;
  GstElement *filesink = NULL;
  GstElement *appsrc_filter1 = NULL;
  GstElement *appsrc_filter2 = NULL;
  GstElement *snapshot_queue[QUEUE_COUNT];
  GstElement *videoconvert = NULL;

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    queue[i] = NULL;
  }
  for (gint i = 0; i < DETECTION_COUNT; i++) {
    qtimlvdetection[i] = NULL;
  }
  for (gint i = 0; i < SNAPSHOT_QUEUE_COUNT; i++) {
    snapshot_queue[i] = NULL;
  }

  // 1. Create the elements or Plugins
  if (options->use_file) {
    // Create file source element for file stream
    filesrc = gst_element_factory_make ("filesrc", "filesrc");
    if (!filesrc) {
      g_printerr ("Failed to create filesrc\n");
      goto error_clean_elements;
    }
    // Create qtdemux or demuxing the filesrc
    qtdemux = gst_element_factory_make ("qtdemux", "qtdemux");
    if (!qtdemux) {
      g_printerr ("Failed to create qtdemux\n");
      goto error_clean_elements;
    }
    // Create h264parse elment for parsing the stream
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
    // Create caps for v4l2h264dec element
    v4l2h264dec_caps =
        gst_element_factory_make ("capsfilter", "v4l2h264dec_caps");
    if (!v4l2h264dec_caps) {
      g_printerr ("Failed to create v4l2h264dec_caps\n");
      goto error_clean_elements;
    }

  } else if (options->use_rtsp) {
    // create rtspsrc plugin for rtsp input
    rtspsrc = gst_element_factory_make ("rtspsrc", "rtspsrc");
    if (!rtspsrc) {
      g_printerr ("Failed to create rtspsrc\n");
      goto error_clean_elements;
    }
    // rtph264depay plugin for rtsp payload parsing
    rtph264depay = gst_element_factory_make ("rtph264depay", "rtph264depay");
    if (!rtph264depay) {
      g_printerr ("Failed to create rtph264depay\n");
      goto error_clean_elements;
    }
    // Create h264parse elment for parsing the stream
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
    // Create caps for v4l2h264dec element
    v4l2h264dec_caps =
        gst_element_factory_make ("capsfilter", "v4l2h264dec_caps");
    if (!v4l2h264dec_caps) {
      g_printerr ("Failed to create v4l2h264dec_caps\n");
      goto error_clean_elements;
    }

  } else if (options->use_camera) {
    // Create plugin for camera stream
    qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
    if (!qtiqmmfsrc) {
      g_printerr ("Failed to create qtiqmmfsrc\n");
      goto error_clean_elements;
    }
    // Use capsfilter to define the camera output settings for inference
    qmmfsrc_caps = gst_element_factory_make ("capsfilter", "qmmfsrc_caps");
    if (!qmmfsrc_caps) {
      g_printerr ("Failed to create qmmfsrc_caps\n");
      goto error_clean_elements;
    }
    // Use capsfilter to define the camera output settings for preview
    qmmfsrc_caps_preview = gst_element_factory_make ("capsfilter",
        "qmmfsrc_caps_preview");
    if (!qmmfsrc_caps_preview) {
      g_printerr ("Failed to create qmmfsrc_caps_preview\n");
      goto error_clean_elements;
    }
  } else {
    g_printerr ("Invalid source type\n");
    goto error_clean_elements;
  }

  // Create queue to decouple the processing on sink and source pad
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      goto error_clean_elements;
    }
  }

  // Use tee to send data same data buffer for file and rtsp use cases
  // one for AI inferencing, one for Display composition
  if (options->use_rtsp || options->use_file) {
    tee = gst_element_factory_make ("tee", "tee");
    if (!tee) {
      g_printerr ("Failed to create tee\n");
      goto error_clean_elements;
    }
  }
  // Create qtimlconverter for Input preprocessing
  qtimlvconverter = gst_element_factory_make ("qtimlvconverter",
      "qtimlvconverter");
  if (!qtimlvconverter) {
    g_printerr ("Failed to create qtimlvconverter\n");
    goto error_clean_elements;
  }
  // Create the ML inferencing plugin for TFLITE
  qtimlelement = gst_element_factory_make ("qtimltflite", "qtimlelement");
  if (!qtimlelement) {
    g_printerr ("Failed to create qtimlelement\n");
    goto error_clean_elements;
  }
  // Create plugin for ML postprocessing for object detection
  for (gint i = 0; i < DETECTION_COUNT; i++) {
    snprintf (element_name, 127, "qtimlvdetection-%d", i);
    qtimlvdetection[i] = gst_element_factory_make ("qtimlpostprocess",
        element_name);
    if (!qtimlvdetection[i]) {
      g_printerr ("Failed to create qtimlvdetection %d\n", i);
      goto error_clean_elements;
    }
  }

  // Composer to combine camera output with ML post proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    goto error_clean_elements;
  }
  // Used to negotiate between ML post proc o/p and qtivcomposer
  detection_filter =
      gst_element_factory_make ("capsfilter", "detection_filter");
  if (!detection_filter) {
    g_printerr ("Failed to create detection_filter\n");
    goto error_clean_elements;
  }
  // Create Wayland composer compositor to render output on Display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  if (!waylandsink) {
    g_printerr ("Failed to create waylandsink");
    goto error_clean_elements;
  }
  // Create fpsdisplaysink to display the current and
  // average framerate as a text overlay
  fpsdisplaysink =
      gst_element_factory_make ("fpsdisplaysink", "fpsdisplaysink");
  if (!fpsdisplaysink) {
    g_printerr ("Failed to create fpsdisplaysink\n");
    goto error_clean_elements;
  }
  // Create tee to split output of ML inference
  detection_tee = gst_element_factory_make ("tee", "detection_tee");
  if (!detection_tee) {
    g_printerr ("Failed to create detection_tee\n");
    goto error_clean_elements;
  }
  // Create caps to enable text output from detection plugin
  appsink_caps = gst_element_factory_make ("capsfilter", "appsink_caps");
  if (!appsink_caps) {
    g_printerr ("Failed to create appsink_caps\n");
    goto error_clean_elements;
  }
  // Create appsink to obtain metadata from detection plugin
  appsink = gst_element_factory_make ("appsink", "appsink");
  if (!appsink) {
    g_printerr ("Failed to create appsink\n");
    goto error_clean_elements;
  }
  // Create tee to split output of qtivcomposer
  composer_tee = gst_element_factory_make ("tee", "composer_tee");
  if (!composer_tee) {
    g_printerr ("Failed to create composer_tee\n");
    goto error_clean_elements;
  }
  // Create appsink to obtain buffer from qtivcomposer
  composer_appsink = gst_element_factory_make ("appsink", "composer_appsink");
  if (!composer_appsink) {
    g_printerr ("Failed to create composer_appsink\n");
    goto error_clean_elements;
  }
  // Create pipeline 2 components
  appsrc = gst_element_factory_make ("appsrc", "appsrc");
  if (!appsrc) {
    g_printerr ("Failed to create appsrc\n");
    goto error_clean_elements;
  }
  // Create v4l2h264dec element for encoding the stream
  v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
  if (!v4l2h264enc) {
    g_printerr ("Failed to create v4l2h264enc\n");
    goto error_clean_elements;
  }
  // Create h264parse elment for parsing the stream
  file_enc_h264parse =
      gst_element_factory_make ("h264parse", "file_enc_h264parse");
  if (!file_enc_h264parse) {
    g_printerr ("Failed to create file_enc_h264parse\n");
    goto error_clean_elements;
  }
  // multiplexer (muxer) element that takes audio and/or video streams and
  // combines them into a single MP4
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
  if (!mp4mux) {
    g_printerr ("Failed to create mp4mux\n");
    goto error_clean_elements;
  }
  // sink element that writes incoming data to a file on disk.
  filesink = gst_element_factory_make ("filesink", "filesink");
  if (!filesink) {
    g_printerr ("Failed to create filesink\n");
    goto error_clean_elements;
  }
  // App source capsfilter1
  appsrc_filter1 = gst_element_factory_make ("capsfilter", "appsrc_filter1");
  if (!appsrc_filter1) {
    g_printerr ("Failed to create appsrc_filter1\n");
    goto error_clean_elements;
  }
  // App source capsfilter2
  appsrc_filter2 = gst_element_factory_make ("capsfilter", "appsrc_filter2");
  if (!appsrc_filter2) {
    g_printerr ("Failed to create appsrc_filter2\n");
    goto error_clean_elements;
  }
  // Performs hardware-accelerated video transformations
  qtivtransform = gst_element_factory_make ("qtivtransform", "qtivtransform");
  if (!qtivtransform) {
    g_printerr ("Failed to create qtivtransform\n");
    goto error_clean_elements;
  }
  // video filter element used to convert video frames between different color
  // formats and pixel formats.
  videoconvert = gst_element_factory_make ("videoconvert", "videoconvert");
  if (!videoconvert) {
    g_printerr ("Failed to create videoconvert\n");
    goto error_clean_elements;
  }

  // Queue for added buffereing
  for (gint i = 0; i < SNAPSHOT_QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "snapshot_queue-%d", i);
    snapshot_queue[i] = gst_element_factory_make ("queue", element_name);
    if (!snapshot_queue[i]) {
      g_printerr ("Failed to create snapshot_queue %d\n", i);
      goto error_clean_elements;
    }
  }

  // 2. Set properties for all GST plugin elements
  if (options->use_file) {
    // 2.1 Set up the capabilities of file stream
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    g_object_set (G_OBJECT (filesrc), "location", options->file_path, NULL);
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else if (options->use_rtsp) {
    //2.2 Set the capabilities of RTSP stream
    gst_element_set_enum_property (v4l2h264dec, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264dec, "output-io-mode", "dmabuf");
    g_object_set (G_OBJECT (rtspsrc), "location", options->rtsp_ip_port, NULL);
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12", NULL);
    g_object_set (G_OBJECT (v4l2h264dec_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else if (options->use_camera) {
    //2.3 Set user provided Camera ID
    g_object_set (G_OBJECT (qtiqmmfsrc), "camera", options->camera_type, NULL);

    // 2.4 Set the capabilities of camera plugin output
    if (options->camera_type == GST_CAMERA_TYPE_PRIMARY) {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12_Q08C",
          "width", G_TYPE_INT, primary_camera_width,
          "height", G_TYPE_INT, primary_camera_height,
          "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
    } else {
      filtercaps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "NV12_Q08C",
          "width", G_TYPE_INT, secondary_camera_width,
          "height", G_TYPE_INT, secondary_camera_height,
          "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
    }
    g_object_set (G_OBJECT (qmmfsrc_caps_preview), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    // 2.4 Set the capabilities of camera plugin output for inference
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, inference_width,
        "height", G_TYPE_INT, inference_height,
        "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
    g_object_set (G_OBJECT (qmmfsrc_caps), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
  } else {
    g_printerr ("Invalid Source Type\n");
    goto error_clean_elements;
  }

  // 2.5 Select the HW to DSP/GPU/CPU for model inferencing using
  // delegate property
  GstMLTFLiteDelegate tflite_delegate;
  if (options->use_cpu) {
    tflite_delegate = GST_ML_TFLITE_DELEGATE_NONE;
    g_print ("Using CPU Delegate\n");
    g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
        "delegate", tflite_delegate, NULL);
  } else if (options->use_gpu) {
    g_print ("Using GPU delegate\n");
    tflite_delegate = GST_ML_TFLITE_DELEGATE_GPU;
    g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
        "delegate", tflite_delegate, NULL);
  } else if (options->use_dsp) {
    g_print ("Using DSP delegate\n");
    delegate_options =
        gst_structure_from_string ("QNNExternalDelegate,backend_type=htp",
        NULL);
    g_object_set (G_OBJECT (qtimlelement), "model", options->model_path,
        "delegate", GST_ML_TFLITE_DELEGATE_EXTERNAL, NULL);
    g_object_set (G_OBJECT (qtimlelement), "external_delegate_path",
        "libQnnTFLiteDelegate.so", NULL);
    g_object_set (G_OBJECT (qtimlelement), "external_delegate_options",
        delegate_options, NULL);
    gst_structure_free (delegate_options);
  }
  // 2.6 Set properties for ML postproc plugins - module, layers, threshold
  g_value_init (&layers, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_STRING);

  // YOLO_X specific settings
  for (gint i = 0; i < DETECTION_COUNT; i++) {
    g_object_set (G_OBJECT (qtimlvdetection[i]), "labels",
        options->labels_path, NULL);
  }
  module_id = get_enum_value (qtimlvdetection[0], "module", "yolov8");
  if (module_id != -1) {
    snprintf (settings, 127, "{\"confidence\": %.1f}", options->threshold);
    for (gint i = 0; i < DETECTION_COUNT; i++) {
      g_object_set (G_OBJECT (qtimlvdetection[i]), "module", module_id, NULL);
      g_object_set (G_OBJECT (qtimlvdetection[i]), "settings", settings, NULL);
    }
  } else {
    g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
    goto error_clean_elements;
  }
  for (gint i = 0; i < DETECTION_COUNT; i++) {
    g_object_set (G_OBJECT (qtimlvdetection[i]), "threshold",
        options->threshold, NULL);
    g_object_set (G_OBJECT (qtimlvdetection[i]), "results", 10, NULL);
  }

  // 2.7 Set the properties for Wayland compositer
  g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

  // 2.8 Set the properties of fpsdisplaysink plugin- sync,
  // signal-fps-measurements, text-overlay and video-sink
  g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements",
      TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", TRUE, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);

  // 2.9 Set the properties of pad_filter for negotiation with qtivcomposer
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640, "height", G_TYPE_INT, 360, NULL);

  g_object_set (G_OBJECT (detection_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 2.10 Set the properties for appsink_filter to obtain bounding metadata
  appsink_filter = gst_caps_new_simple ("text/x-raw", NULL, NULL);
  g_object_set (G_OBJECT (appsink_caps), "caps", appsink_filter, NULL);
  gst_caps_unref (appsink_filter);

  // 2.11 enable appsink to send signals for new-sample
  g_object_set (G_OBJECT (appsink), "emit-signals", TRUE, NULL);


  // 2.12 set values for 2nd pipeline
  g_object_set (G_OBJECT (composer_appsink), "emit-signals", TRUE, NULL);

  // Set appsrc properties
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, 1920,
      "height", G_TYPE_INT, 1088,
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
  g_object_set (G_OBJECT (appsrc), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  g_object_set (G_OBJECT (appsrc),
      "stream-type", 0, "format", GST_FORMAT_TIME, "is-live", TRUE, NULL);
  gst_element_set_enum_property (v4l2h264enc, "capture-io-mode", "dmabuf");
  gst_element_set_enum_property (v4l2h264enc, "output-io-mode",
      "dmabuf-import");

  snprintf (element_name, 127, "/etc/media/output-%d.mp4", appctx->video_count);
  g_object_set (G_OBJECT (filesink), "location", element_name, NULL);
  g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);
  g_object_set (G_OBJECT (filesink), "async", FALSE, NULL);

  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      "width", G_TYPE_INT, 1920,
      "height", G_TYPE_INT, 1088,
      "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);
  g_object_set (G_OBJECT (appsrc_filter1), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 3. Setup the pipeline_main
  g_print ("Adding all elements to the pipeline...\n");

  if (options->use_file) {
    gst_bin_add_many (GST_BIN (appctx->pipeline_main), filesrc,
        qtdemux, h264parse, v4l2h264dec, v4l2h264dec_caps, tee, NULL);
  } else if (options->use_rtsp) {
    gst_bin_add_many (GST_BIN (appctx->pipeline_main), rtspsrc,
        rtph264depay, h264parse, v4l2h264dec, v4l2h264dec_caps, tee, NULL);
  } else if (options->use_camera) {
    gst_bin_add_many (GST_BIN (appctx->pipeline_main), qtiqmmfsrc,
        qmmfsrc_caps, qmmfsrc_caps_preview, NULL);
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }

  gst_bin_add_many (GST_BIN (appctx->pipeline_main), qtimlvconverter,
      qtimlelement, qtimlvdetection[0], qtimlvdetection[1], detection_filter,
      qtivcomposer, fpsdisplaysink, waylandsink, appsrc_filter1, NULL);

  gst_bin_add_many (GST_BIN (appctx->pipeline_main), appsink,
      appsink_caps, detection_tee, composer_tee, composer_appsink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline_main), queue[i], NULL);
  }

  // 3.1 Adding elements in bin.
  gst_bin_add_many (GST_BIN (appctx->pipeline_recoding), appsrc, v4l2h264enc,
      file_enc_h264parse, mp4mux, filesink, NULL);

  for (gint i = 0; i < SNAPSHOT_QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline_recoding), snapshot_queue[i],
        NULL);
  }

  g_print ("Linking elements...\n");

  // Create pipeline for object detection and mdetadata parsing
  if (options->use_file) {
    ret = gst_element_link_many (filesrc, qtdemux, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements could not be linked"
          "for filesrc->qtdemux\n");
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (queue[0], h264parse,
        v4l2h264dec, v4l2h264dec_caps, queue[1], tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements could not be linked for parse->tee\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_rtsp) {
    ret = gst_element_link_many (queue[0], rtph264depay, h264parse,
        v4l2h264dec, v4l2h264dec_caps, queue[1], tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "rtspsource->rtph264depay\n");
      goto error_clean_pipeline;
    }
  } else {
    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps_preview,
        queue[2], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for "
          "qmmfsource->composer\n");
      goto error_clean_pipeline;
    }
    ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps, queue[4], NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for "
          "qmmfsource->converter\n");
      goto error_clean_pipeline;
    }
  }

  if (options->use_rtsp || options->use_file) {
    ret =
        gst_element_link_many (tee, queue[2], qtivcomposer, appsrc_filter1,
            composer_tee, NULL);
    if (!ret) {
      g_printerr
          ("Pipeline elements cannot be linked for tee->qtivcomposer->tee.\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_camera) {
    ret =
        gst_element_link_many (queue[2], qtivcomposer, appsrc_filter1,
        composer_tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          " qmmfsrc_caps_preview -> qtivcomposer -> tee\n");
      goto error_clean_pipeline;
    }
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }

  ret = gst_element_link_many (composer_tee, queue[3], fpsdisplaysink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        "qtivcomposer->fpsdisplaysink.\n");
    goto error_clean_pipeline;
  }

  if (options->use_rtsp || options->use_file) {
    ret = gst_element_link_many (tee, queue[4], qtimlvconverter,
        queue[5], qtimlelement, queue[6], detection_tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "pre proc -> ml framework -> detection_tee\n");
      goto error_clean_pipeline;
    }
  } else if (options->use_camera) {
    ret = gst_element_link_many (queue[4], qtimlvconverter,
        queue[5], qtimlelement, queue[6], detection_tee, NULL);
    if (!ret) {
      g_printerr ("Pipeline elements cannot be linked for"
          "pre proc -> ml framework -> detection_tee\n");
      goto error_clean_pipeline;
    }
  } else {
    g_printerr ("Incorrect input source type\n");
    goto error_clean_elements;
  }

  ret = gst_element_link_many (detection_tee, qtimlvdetection[0],
      detection_filter, queue[7], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for "
        "detection_tee -> post-proc -> composer.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (detection_tee, qtimlvdetection[1],
      appsink_caps, queue[8], appsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for "
        "detection_tee -> post proc -> appsink.\n");
    goto error_clean_pipeline;
  }

  ret = gst_element_link_many (composer_tee, queue[9], composer_appsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for "
        "detection_tee -> post proc -> appsink.\n");
    goto error_clean_pipeline;
  }
  ret = gst_element_link_many (appsrc, snapshot_queue[0], v4l2h264enc,
      file_enc_h264parse, snapshot_queue[3], mp4mux, filesink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for "
        "appsrc -> encode -> filesink\n");
    goto error_clean_pipeline;
  }

  if (options->use_file) {
    g_signal_connect (qtdemux, "pad-added", G_CALLBACK (on_pad_added),
        queue[0]);
  }

  if (options->use_rtsp) {
    g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added),
        queue[0]);
  }

  if (options->use_camera) {
    qtiqmmfsrc_type = gst_element_get_static_pad (qtiqmmfsrc, "video_0");
    if (!qtiqmmfsrc_type) {
      g_printerr ("video_0 of qtiqmmfsrc couldn't be retrieved\n");
      goto error_clean_pipeline;
    }

    g_value_init (&video_type, G_TYPE_INT);
    g_value_set_int (&video_type, GST_SOURCE_STREAM_TYPE_PREVIEW);
    g_object_set_property (G_OBJECT (qtiqmmfsrc_type), "type", &video_type);
    g_value_unset (&video_type);
    gst_object_unref (qtiqmmfsrc_type);
  }
  // Connect callback function to appsink
  g_signal_connect (appsink, "new-sample", G_CALLBACK (appsink_detection),
      appctx);
  g_signal_connect (composer_appsink, "new-sample",
      G_CALLBACK (appsink_recording), appctx);

  // Set overlay window size for Detection to display text labels
  vcomposer_sink = gst_element_get_static_pad (qtivcomposer, "sink_0");
  if (vcomposer_sink == NULL) {
    g_printerr ("Sink pad 0 of vcomposer couldnt' be retrieved\n");
    goto error_clean_pipeline;
  }

  GValue position = G_VALUE_INIT;
  GValue dimension = G_VALUE_INIT;

  g_value_init (&position, GST_TYPE_ARRAY);
  g_value_init (&dimension, GST_TYPE_ARRAY);

  pos_vals[0] = 0;
  pos_vals[1] = 0;
  dim_vals[0] = 1920;
  dim_vals[1] = 1080;

  build_pad_property (&position, pos_vals, 2);
  build_pad_property (&dimension, dim_vals, 2);

  g_object_set_property (G_OBJECT (vcomposer_sink), "position", &position);
  g_object_set_property (G_OBJECT (vcomposer_sink), "dimensions", &dimension);

  g_value_unset (&position);
  g_value_unset (&dimension);
  gst_object_unref (vcomposer_sink);

  return TRUE;

error_clean_pipeline:
  gst_object_unref (appctx->pipeline_main);
  appctx->pipeline_main = NULL;
  gst_object_unref (appctx->pipeline_recoding);
  appctx->pipeline_recoding = NULL;
  return FALSE;

error_clean_elements:
  cleanup_gst (&qtiqmmfsrc, &qmmfsrc_caps, &qmmfsrc_caps_preview,
      &filesrc, &qtdemux, &h264parse, &v4l2h264dec, &rtph264depay, &rtspsrc,
      &rtph264depay, &tee, &v4l2h264dec_caps, &qtimlvconverter, &qtimlelement,
      &qtivcomposer, &detection_filter, &waylandsink, &fpsdisplaysink, &appsink,
      &appsink_caps, &detection_tee, &appsrc, &v4l2h264enc, &qtivtransform,
      &file_enc_h264parse, &mp4mux, &filesink, &appsrc_filter1, &appsrc_filter2,
      &videoconvert, NULL);
  for (gint i = 0; i < QUEUE_COUNT; i++) {
    gst_object_unref (queue[i]);
  }
  for (gint i = 0; i < DETECTION_COUNT; i++) {
    gst_object_unref (qtimlvdetection[i]);
  }
  for (gint i = 0; i < SNAPSHOT_QUEUE_COUNT; i++) {
    gst_object_unref (snapshot_queue[i]);
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

  gboolean camera_is_available = is_camera_available ();

  if (camera_is_available) {
    if (json_object_has_member (root_obj, "camera"))
      options->camera_type = json_object_get_int_member (root_obj, "camera");
  }

  if (json_object_has_member (root_obj, "file-path")) {
    options->file_path =
        g_strdup (json_object_get_string_member (root_obj, "file-path"));
  }

  if (json_object_has_member (root_obj, "rtsp-ip-port")) {
    options->rtsp_ip_port =
        g_strdup (json_object_get_string_member (root_obj, "rtsp-ip-port"));
  }

  if (json_object_has_member (root_obj, "model")) {
    options->model_path =
        g_strdup (json_object_get_string_member (root_obj, "model"));
  }

  if (json_object_has_member (root_obj, "labels")) {
    options->labels_path =
        g_strdup (json_object_get_string_member (root_obj, "labels"));
  }

  if (json_object_has_member (root_obj, "threshold")) {
    options->threshold = json_object_get_int_member (root_obj, "threshold");
  }

  if (json_object_has_member (root_obj, "runtime")) {
    const gchar *delegate = json_object_get_string_member (root_obj, "runtime");

    if (g_strcmp0 (delegate, "cpu") == 0)
      options->use_cpu = TRUE;
    else if (g_strcmp0 (delegate, "dsp") == 0)
      options->use_dsp = TRUE;
    else if (g_strcmp0 (delegate, "gpu") == 0)
      options->use_gpu = TRUE;
    else {
      gst_printerr
          ("Runtime can only be one of \"cpu\", \"dsp\" and \"gpu\"\n");
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
  GstBus *bus1 = NULL, *bus2 = NULL;
  GMainLoop *mloop = NULL;
  GstElement *pipeline_main = NULL;
  GstElement *pipeline_recoding = NULL;
  GOptionContext *ctx = NULL;
  const gchar *app_name = NULL;
  GstAppsContext appctx = { };
  gchar help_description[4096];
  gboolean ret = FALSE;
  gboolean camera_is_available = FALSE;
  guint intrpt_watch_id = 0;
  GstAppOptions options = { };
  gchar *config_file = NULL;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // set default value
  options.file_path = NULL;
  options.rtsp_ip_port = NULL;
  options.use_cpu = FALSE, options.use_gpu = FALSE, options.use_dsp = FALSE;
  options.use_file = FALSE, options.use_rtsp = FALSE, options.use_camera =
      FALSE;
  options.threshold = DEFAULT_THRESHOLD_VALUE;
  options.camera_type = GST_CAMERA_TYPE_NONE;
  options.model_path = NULL;
  options.labels_path = NULL;

  appctx.recording_pipeline_state = PAUSED;
  appctx.video_count = 0;
  g_mutex_init (&appctx.lock);

  // Structure to define the user options selected
  GOptionEntry entries[] = {
    {"config-file", 0, 0, G_OPTION_ARG_STRING,
          &config_file,
          "Path to config file\n",
        NULL},
    {NULL}
  };

  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  camera_is_available = is_camera_available ();

  gchar camera_description[128] = { };

  if (camera_is_available) {
    snprintf (camera_description, sizeof (camera_description),
        "  camera: 0 or 1\n"
        "      Select (0) for Primary Camera and (1) for secondary one.\n");
  }

  snprintf (help_description, 4095, "\nExample:\n"
      "  %s --config-file=%s\n"
      "\nThis Sample App demonstrates the use case of Video Encoding when"
      "person is detection in the frame, if there is not person app will wait "
      "for 5 sec and save the recording. App will wait for next person event\n"
      "\nConfig file Fields:\n"
      "%s"
      "  file-path: \"/PATH\"\n"
      "      File source path\n"
      "  rtsp-ip-port: \"rtsp://<ip>:<port>/<stream>\"\n"
      "      Use this parameter to provide the rtsp input.\n"
      "      Input should be provided as rtsp://<ip>:<port>/<stream>,\n"
      "      eg: rtsp://192.168.1.110:8554/live.mkv\n"
      "  model: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default model path: " DEFAULT_TFLITE_MODEL "\n"
      "  labels: \"/PATH\"\n"
      "      This is an optional parameter and overrides default path\n"
      "      Default labels path: " DEFAULT_LABELS "\n"
      "  threshold: 0 to 100\n"
      "      This is an optional parameter and overides "
      "default threshold value 40\n"
      "  runtime: \"cpu\" or \"gpu\" or \"dsp\"\n"
      "      This is an optional parameter. If not filled, "
      "then default dsp runtime is selected\n",
      app_name, DEFAULT_CONFIG_FILE, camera_description);
  help_description[4095] = '\0';

  // Parse command line entries
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
  if (camera_is_available) {
    g_print ("TARGET Can support file source, RTSP source and camera source\n");
  } else {
    g_print ("TARGET Can only support file source and RTSP source.\n");
    if (options.file_path == NULL && options.rtsp_ip_port == NULL) {
      g_print ("User need to give proper input file as source\n");
      gst_app_context_free (&appctx, &options, config_file);
      return -EINVAL;
    }
  }

  if (options.file_path != NULL) {
    options.use_file = TRUE;
  }

  if (options.rtsp_ip_port != NULL) {
    options.use_rtsp = TRUE;
  }

  if (!(options.use_file || (options.camera_type != GST_CAMERA_TYPE_NONE) ||
          options.use_rtsp)) {
    options.use_camera = TRUE;
    options.camera_type = GST_CAMERA_TYPE_PRIMARY;
    g_print ("Using PRIMARY camera by default, Not valid camera id selected\n");
  }

  // Checking camera ID passed by the user.
  if (options.camera_type < GST_CAMERA_TYPE_NONE ||
      options.camera_type > GST_CAMERA_TYPE_SECONDARY) {
    g_printerr ("Invalid Camera ID selected\n"
        "Available options:\n"
        "    PRIMARY: %d\n"
        "    SECONDARY %d\n",
        GST_CAMERA_TYPE_PRIMARY, GST_CAMERA_TYPE_SECONDARY);
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  // Enable camera flag if user set the camera property
  if (options.camera_type == GST_CAMERA_TYPE_SECONDARY ||
      options.camera_type == GST_CAMERA_TYPE_PRIMARY)
    options.use_camera = TRUE;

  // Terminate if more than one source are there.
  if (options.use_file + options.use_camera + options.use_rtsp > 1) {
    g_printerr ("Select anyone source type either Camera or File or RTSP\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.use_file) {
    g_print ("File Source is Selected\n");
  } else if (options.use_rtsp) {
    g_print ("RTSP Source is Selected\n");
  } else {
    g_print ("Camera Source is Selected\n");
  }

  if (options.threshold < 0 || options.threshold > 100) {
    g_printerr ("Invalid threshold value selected\n"
        "Threshold Value lies between: \n" "    Min: 0\n" "    Max: 100\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if ((options.use_cpu + options.use_gpu + options.use_dsp) > 1) {
    g_print ("Select any one runtime from CPU or GPU or DSP\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -EINVAL;
  }

  if (options.use_cpu == FALSE && options.use_gpu == FALSE
      && options.use_dsp == FALSE) {
    g_print ("Setting DSP as default Runtime\n");
    options.use_dsp = TRUE;
  }

  // Set model path for execution
  if (options.model_path == NULL) {
    options.model_path = DEFAULT_TFLITE_MODEL;
  }

  // Set default label path for execution
  if (options.labels_path == NULL) {
    options.labels_path = DEFAULT_LABELS;
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
  pipeline_main = gst_pipeline_new (app_name);
  if (!pipeline_main) {
    g_printerr ("ERROR: failed to create pipeline_main.\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  pipeline_recoding = gst_pipeline_new (app_name);
  if (!pipeline_recoding) {
    g_printerr ("ERROR: failed to create pipeline_recoding.\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  appctx.pipeline_main = pipeline_main;
  appctx.pipeline_recoding = pipeline_recoding;

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
  if ((bus1 = gst_pipeline_get_bus (GST_PIPELINE (pipeline_main))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus1!\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }
  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus1);

  // Register respective callback function based on message
  g_signal_connect (bus1, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline_main);

  g_signal_connect (bus1, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus1, "message::warning", G_CALLBACK (warning_cb), mloop);

  g_signal_connect (bus1, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus1);

  // Retrieve reference to the pipeline's bus.
  // Bus is message queue for getting callback from gstreamer pipeline
  if ((bus2 = gst_pipeline_get_bus (GST_PIPELINE (pipeline_recoding))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus1!\n");
    gst_app_context_free (&appctx, &options, config_file);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus2);

  // Register respective callback function based on message
  g_signal_connect (bus2, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline_recoding);

  g_signal_connect (bus2, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus2, "message::warning", G_CALLBACK (warning_cb), mloop);

  g_signal_connect (bus2, "message::eos", G_CALLBACK (recording_eos_cb),
      &appctx);
  gst_object_unref (bus2);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, interrupt_handler, &appctx);

  // On successful transition to PAUSED state, state_changed_cb is called.
  // state_changed_cb callback is used to send pipeline to play state.
  g_print ("Set pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline_main, GST_STATE_PAUSED)) {
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
  gst_element_set_state (pipeline_main, GST_STATE_NULL);
  gst_element_set_state (pipeline_recoding, GST_STATE_NULL);

  g_mutex_clear (&appctx.lock);

  g_print ("Destroy pipeline\n");
  gst_app_context_free (&appctx, &options, config_file);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
