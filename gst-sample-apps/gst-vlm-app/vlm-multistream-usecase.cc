/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <glib-unix.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/webrtc/webrtc.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <queue>
#include <string>
#include <algorithm>

#include "GenieVLM.h"

#define DETECTION_MODEL "/etc/models/yolov8_det_quantized.tflite"
#define DETECTION_LABELS "/etc/models/yolov8.json"
#define DEFAULT_THRESHOLD_VALUE 80.0

#define MLVCONV_WIDTH 504
#define MLVCONV_HEIGHT 336

#define RESPONSE_TIMEOUT_MS 20000 // per-request timeout

#define MAX_FILESRCS 30
#define CHAR_BUFFER_SIZE 128
#define CAMERA_PREVIEW_OUTPUT_WIDTH 640
#define CAMERA_PREVIEW_OUTPUT_HEIGHT 480

#define ASSISTANT_QUERY                                                        \
"You are a vision description assistant. Always answer with one short "      \
"sentence under 100 characters using only letters and spaces with no "       \
"punctuation or symbols."

#define QUESTION_PERSON "Describe only the people in this image"

#define QUESTION_CAR "Describe only the cars in this image"

#define QUESTION_DEFAULT "Describe the main scene in this image"

struct VLMBufferBundle;

typedef enum {
  GST_CAMERA_TYPE_NONE = -1,
  GST_CAMERA_TYPE_PRIMARY,
  GST_CAMERA_TYPE_SECONDARY
} GstCameraSourceType;

typedef enum {
  GST_ML_TFLITE_DELEGATE_NONE,
  GST_ML_TFLITE_DELEGATE_NNAPI_DSP,
  GST_ML_TFLITE_DELEGATE_NNAPI_GPU,
  GST_ML_TFLITE_DELEGATE_NNAPI_NPU,
  GST_ML_TFLITE_DELEGATE_HEXAGON,
  GST_ML_TFLITE_DELEGATE_GPU,
  GST_ML_TFLITE_DELEGATE_XNNPACK,
  GST_ML_TFLITE_DELEGATE_EXTERNAL,
} GstMLTFLiteDelegate;

struct VLMBufferQueueItem {
  GstBuffer * buffer;
  GstElement * appsrc;
};

struct StreamConfig {
  // Type of stream (file, rtsp, csi camera)
  std::string type;
  // File path, camera type, RTSP IP
  std::string source_info;
  // Type of the stre
  std::string pipeline_type;
  // WebRTC peer id for this stream (from JSON: sources[i].local_id)
  guint local_id = 0;
};

struct InputBufferBundle {
  GstElement * appsrc_vlm;
  GstElement * appsrc_jpeg;
  GstBuffer * vlm_buffer;
  GstBuffer * jpeg_buffer;

  InputBufferBundle ()
      : appsrc_vlm (nullptr), appsrc_jpeg (nullptr), vlm_buffer (nullptr),
        jpeg_buffer (nullptr) {}
};

struct SceneDet {
  gboolean has_prev;
  gfloat prev[64];

  gint cooldown;        // frames left
  gint cooldown_frames; // e.g. 4
  gfloat th;            // chi2 threshold e.g. 0.30

  gint above_hist[3];
  gint idx;

  // optional: store last distance for debugging
  gfloat last_d;
};

struct VLMAppContext {
  //          **** VLM related members ****
  GenieVLM * vlm;
  gchar * config_file;
  //          **** Stream related members ****
  // Vector to store pipelines
  std::vector<GstElement *> pipelines;
  // Vector to store stream configs
  std::vector<StreamConfig> stream_configs;
  // A vector of appsrc_vlm elements for all pipelines for easier lookup
  std::vector<GstElement *> appsrces;
  // A vector of appsrc_jpeg elements for all pipelines for easier lookup
  std::vector<GstElement *> appsrces_jpeg;
  // Vector to store flags about which streams have propagated EOS
  std::vector<gboolean> eos_flags;
  GMainLoop * mloop;
  GMutex mutex;
  // Number of streams going into VLM
  guint num_streams;
  // Boolean flag which when set, stops the application
  gboolean exit;
  //          **** Scene detection related members ****
  std::queue<guint> stream_scheduler;
  std::vector<SceneDet> sd;
  std::unordered_map<guint, GstBuffer *> sd_buffers;
  std::vector<guint> sd_streams;
  //          **** WebRTC related members ****
  // WebRTC output (one per stream)
  std::vector<GstElement *> webrtcs;
  std::vector<GstElement *> meta_sinks;
  std::vector<GstWebRTCDataChannel *> data_channels;
  std::vector<InputBufferBundle> buffer_bundles;
  // Per-stream signalling (python protocol). Each stream has its own
  // websocket session so each webrtcbin can "SESSION <local_id>" independently.
  std::vector<SoupSession *> ws_sessions;
  std::vector<SoupWebsocketConnection *> ws_conns;
  std::vector<guint> ws_local_ids; // from JSON sources[i].local_id
  std::string ws_url;
  //          **** Thumbnail-related members ****
  // Per-stream counters for correlating meta + thumbnail chunks
  std::vector<guint64> dc_frame_ids;
  std::vector<guint64> dc_last_thumb_sent;
  // Thumb controls
  guint thumb_every_n_frames;
  gsize max_b64;
  //          **** Config JSON related members ****
  std::string assistant_query;
  std::string question_person;
  std::string question_car;
  std::string question_default;
  std::vector<std::string> vlm_engines;
  std::string logger_level;
  gboolean profile_enabled;
  // ROI control from JSON (optional - used by downstream meta module too)
  gboolean attach_jpeg;
  guint process_interval_ms;
  guint bbox_threshold;
  gdouble bbox_margin;
  gboolean scene_change_detection;
  gdouble scene_change_detection_th;
};

struct VLMBufferBundle {
  VLMAppContext * appctx;
  GstElement * appsrc_vlm;
  GstElement * appsrc_jpeg;
  GstBuffer * vlm_buffer;
  GstBuffer * jpeg_buffer;
};

typedef struct {
  VLMAppContext * appctx;
  guint stream_idx;
} MetaSinkUserData;

typedef struct {
  VLMAppContext * appctx;
  guint stream_idx;
} SignallingCtx;

/**
 * Free MetaSinkUserData object
 *
 * @param data Object to free
 * @param closure Mandatory GClosure object to be passed
 *
 */
static void
destroy_msud (gpointer data, GClosure * closure)
{
  (void) closure;
  g_free (data);
}

/**
 * Make any string lowercase
 *
 * @param value Input string
 *
 */
static std::string
to_lower_copy (std::string value)
{
  std::transform (
      value.begin (), value.end (), value.begin (),
      [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
  return value;
}

/**
 * Parse log level from string
 *
 * @param value Log level (string)
 * @param level Object to write log level to
 *
 */
static bool
logger_level_from_string (const std::string & value, GenieLog_Level_t & level)
{
  const std::string normalized = to_lower_copy (value);
  if (normalized.empty () || normalized == "none")
    return false;

  if (normalized == "error") {
    level = GENIE_LOG_LEVEL_ERROR;
    return true;
  }

  if (normalized == "warn" || normalized == "warning") {
    level = GENIE_LOG_LEVEL_WARN;
    return true;
  }

  if (normalized == "info") {
    level = GENIE_LOG_LEVEL_INFO;
    return true;
  }

  level = GENIE_LOG_LEVEL_VERBOSE;
  return true;
}

/**
 * Compute a 64-bin histogram from a grey-8 image
 *
 * @param data Pointer to the image buffer
 * @param stride Image stride
 * @param w Image width
 * @param h Image height
 * @param p Output buffer
 *
 */
static inline void
hist64_gray8 (const guint8 * data, gint stride, gint w, gint h, gfloat p[64])
{
  const gint B = 64;
  const gint alpha = 1; // Laplace smoothing; set 0 if you want
  gint hist[B];

  for (gint i = 0; i < B; i++)
    hist[i] = alpha;

  for (gint y = 0; y < h; y++) {
    const guint8 * row = data + y * stride;
    for (gint x = 0; x < w; x++) {
      guint8 v = row[x];
      hist[v >> 2]++; // 0..63
    }
  }

  const gfloat inv_sum = 1.0f / (gfloat)(w * h + B * alpha);
  for (gint i = 0; i < B; i++)
    p[i] = hist[i] * inv_sum;
}

/**
 * Compute Chi square distance between 2 64-bin distributions
 *
 * @param p First normalized histogram
 * @param q Second normalized histogram
 *
 */
static inline gfloat
chi2_64 (const gfloat p[64], const gfloat q[64])
{
  const gfloat eps = 1e-12f;
  gfloat s = 0.0f;

  for (gint i = 0; i < 64; i++) {
    const gfloat a = p[i];
    const gfloat b = q[i];
    const gfloat d = a - b;
    s += (d * d) / (a + b + eps);
  }
  return 0.5f * s;
}

/**
 * Compute whether the scene has changed
 *
 * @param sd Pointer to the SceneDetection object
 * @param data Pointer to the image buffer
 * @param stride Image stride
 * @param w Image width
 * @param h Image height
 *
 */
static inline gboolean
scene_det_process_gray8 (SceneDet * sd, const guint8 * data, gint stride, gint w,
  gint h)
{
  gfloat cur[64];
  hist64_gray8 (data, stride, w, h, cur);

  if (!sd->has_prev) {
    memcpy (sd->prev, cur, sizeof (sd->prev));
    sd->has_prev = TRUE;
    sd->last_d = 0.0f;
    return FALSE;
  }

  const gfloat d = chi2_64 (cur, sd->prev);
  sd->last_d = d;

  if (sd->cooldown > 0) {
    sd->cooldown--;
    return FALSE;
  }

  const gint above = (d > sd->th) ? 1 : 0;

  sd->above_hist[sd->idx] = above;
  sd->idx = (sd->idx + 1) % 3;

  const gint sum = sd->above_hist[0] + sd->above_hist[1] + sd->above_hist[2];

  if (sum >= 1) {
    sd->cooldown = sd->cooldown_frames;
    sd->above_hist[0] = sd->above_hist[1] = sd->above_hist[2] = 0;

    // update prev
    memcpy (sd->prev, cur, sizeof (sd->prev));
    return TRUE;
  }

  return FALSE;
}

/**
 * Parse JSON file to read input parameters
 *
 * @param config_file Path to config file
 * @param appctx Pointer to the application context object
 *
 */
gint
parse_json (const gchar * config_file, VLMAppContext * appctx)
{
  JsonParser * parser = NULL;
  JsonNode * root = NULL;
  JsonObject * root_obj = NULL;
  GError * error = NULL;
  JsonArray * input_file_path_array = NULL;
  guint num_streams;
  JsonArray * engines_array = NULL;

  parser = json_parser_new ();

  // Load the JSON file
  if (!json_parser_load_from_file (parser, config_file, &error)) {
    g_printerr ("Unable to parse JSON file: %s\n", error->message);
    g_error_free (error);
    g_object_unref (parser);
    return -EINVAL;
  }

  // Get the root object
  root = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root)) {
    g_printerr ("Failed to load json object\n");
    g_object_unref (parser);
    return -EINVAL;
  }
  root_obj = json_node_get_object (root);

  if (json_object_has_member (root_obj, "profile"))
    appctx->profile_enabled =
        json_object_get_boolean_member (root_obj, "profile");
  else
    appctx->profile_enabled = FALSE;

  if (json_object_has_member (root_obj, "processing_engines")) {
    engines_array =
        json_object_get_array_member (root_obj, "processing_engines");

    for (guint i = 0; i < json_array_get_length (engines_array); i++) {
      JsonNode * elem_node = json_array_get_element (engines_array, i);
      const gchar * engine = json_node_get_string (elem_node);

      appctx->vlm_engines.push_back (std::string (engine));
    }
  }

  if (json_object_has_member (root_obj, "sources")) {
    input_file_path_array = json_object_get_array_member (root_obj, "sources");

    num_streams = json_array_get_length (input_file_path_array);
    if (num_streams > MAX_FILESRCS) {
      g_printerr ("Number of input files has to be <= %d\n", MAX_FILESRCS);
      g_object_unref (parser);
      return -EINVAL;
    }

    appctx->stream_configs.resize (num_streams);
    appctx->num_streams = num_streams;

    for (guint i = 0; i < num_streams; i++) {
      JsonNode * elem_node = json_array_get_element (input_file_path_array, i);
      JsonObject * obj;
      std::string type;
      std::string source_info;
      std::string pipeline_type;
      StreamConfig config;

      if (!JSON_NODE_HOLDS_OBJECT (elem_node)) {
        g_printerr ("Config error: sources[%u] is not a valid object.\n", i);
        continue;
      }

      obj = json_node_get_object (elem_node);

      // Guard against missing/NULL members (json-glib returns NULL and
      // std::string ctor would crash)
      if (!json_object_has_member (obj, "type") ||
          !json_object_has_member (obj, "source_info") ||
          !json_object_has_member (obj, "pipeline_type")) {
        g_printerr ("Config error: missing required fields in sources[%u].\n",
                    i);
        continue;
      }

      {
        const gchar * t = json_object_get_string_member (obj, "type");
        const gchar * s = json_object_get_string_member (obj, "source_info");
        const gchar * p = json_object_get_string_member (obj, "pipeline_type");
        type = t ? t : "";
        source_info = s ? s : "";
        pipeline_type = p ? p : "";
      }

      // Optional per-stream peer id for WebRTC signalling
      guint local_id = 0;
      if (json_object_has_member (obj, "local_id"))
        local_id = (guint) json_object_get_int_member (obj, "local_id");

      if (type.empty () || source_info.empty () || pipeline_type.empty ()) {
        g_printerr (
            "Config error: missing 'type' or 'source' in sources[%u].\n", i);
        continue;
      }

      config.source_info = source_info;
      config.pipeline_type = pipeline_type;
      config.type = type;
      config.local_id = local_id;

      appctx->stream_configs[i] = std::move (config);
    }
  }

  // Optional prompt overrides
  if (json_object_has_member (root_obj, "assistant_query"))
    appctx->assistant_query =
        json_object_get_string_member (root_obj, "assistant_query");
  else
    appctx->assistant_query = ASSISTANT_QUERY;

  if (json_object_has_member (root_obj, "question_person"))
    appctx->question_person =
        json_object_get_string_member (root_obj, "question_person");
  else
    appctx->question_person = QUESTION_PERSON;

  if (json_object_has_member (root_obj, "question_car"))
    appctx->question_car =
        json_object_get_string_member (root_obj, "question_car");
  else
    appctx->question_car = QUESTION_CAR;

  if (json_object_has_member (root_obj, "question_default"))
    appctx->question_default =
        json_object_get_string_member (root_obj, "question_default");
  else
    appctx->question_default = QUESTION_DEFAULT;

  if (json_object_has_member (root_obj, "scene_change_detection"))
    appctx->scene_change_detection =
        json_object_get_boolean_member (root_obj, "scene_change_detection");

  if (json_object_has_member (root_obj, "scene_change_detection_th"))
    appctx->scene_change_detection_th =
        json_object_get_double_member (root_obj, "scene_change_detection_th");

  // Optional ROI controls (also useful for downstream meta selection module)
  if (json_object_has_member (root_obj, "attach_jpeg"))
    appctx->attach_jpeg =
        json_object_get_boolean_member (root_obj, "attach_jpeg");

  if (json_object_has_member (root_obj, "process_interval_ms"))
    appctx->process_interval_ms =
        (guint) json_object_get_int_member (root_obj, "process_interval_ms");

  if (json_object_has_member (root_obj, "bbox_threshold"))
    appctx->bbox_threshold =
        (guint) json_object_get_int_member (root_obj, "bbox_threshold");

  if (json_object_has_member (root_obj, "bbox_margin"))
    appctx->bbox_margin =
        json_object_get_double_member (root_obj, "bbox_margin");

  if (json_object_has_member (root_obj, "logger")) {
    const gchar * logger = json_object_get_string_member (root_obj, "logger");
    if (logger)
      appctx->logger_level = logger;
  } else {
    appctx->logger_level = "none";
  }
  g_object_unref (parser);
  return 0;
}

/**
 * Check if file exists
 *
 * @param path Path to file
 *
 */
gboolean
file_exists (const gchar * path)
{
  FILE * fp = fopen (path, "r");
  if (fp) {
    fclose (fp);
    return TRUE;
  }
  return FALSE;
}

/**
 * Extract numerical index from GstElement name
 *
 * @param name Element name
 * @param index_out Output index
 *
 */
bool
extract_index_from_name (const char * name, unsigned * index_out)
{
  if (!name || !index_out)
    return false;

  const char * dash = strrchr (name, '-');
  if (!dash || !dash[1])
    return false;

  const char * p = dash + 1;

  if (!isdigit ((unsigned char)*p))
    return false;

  errno = 0;
  char * end = NULL;
  unsigned long val = strtoul (p, &end, 10);

  if (p == end || *end != '\0' || errno == ERANGE ||
      val > (unsigned long) UINT_MAX)
    return false;

  *index_out = (unsigned) val;
  return true;
}

/**
 * Handles interrupt by CTRL+C.
 *
 * @param userdata pointer to AppContext.
 * @return FALSE if the source should be removed, else TRUE.
 *
 */
gboolean
handle_interrupt_signal (gpointer userdata)
{
  VLMAppContext * appctx = (VLMAppContext *) userdata;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal ...\n");

  for (guint i = 0; i < appctx->num_streams; i++) {
    if (!gst_element_get_state (appctx->pipelines[i], &state, &pending,
        GST_CLOCK_TIME_NONE)) {
      g_printerr ("ERROR: get current state!\n");
      gst_element_send_event (appctx->pipelines[i], gst_event_new_eos ());
      return TRUE;
    }
  }

  g_mutex_lock (&appctx->mutex);
  appctx->exit = TRUE;
  g_mutex_unlock (&appctx->mutex);

  g_main_loop_quit (appctx->mloop);

  return TRUE;
}

// Forward declaration
static void
ws_send_text (VLMAppContext * appctx, guint stream_idx, const gchar * text);

/**
 * Callback function for ICE candidate
 *
 * @param webrtcbin Pointer to the webrtcbin element
 * @param mlineindex SDP index
 * @param candidate ICE candidate string
 * @param appctx Pointer to AppContext.
 *
 */
static void
on_webrtc_ice_candidate (GstElement * webrtcbin, guint mlineindex,
  gchar * candidate, VLMAppContext * appctx)
{
  if (!appctx || !candidate)
    return;

  guint stream_idx = 0;
  const gchar * name = gst_element_get_name (webrtcbin);
  if (!extract_index_from_name (name, &stream_idx)) {
    g_printerr ("[webrtc] ice-candidate: could not extract index from %s",
                name ? name : "(null)");
    return;
  }

  if (stream_idx >= appctx->ws_conns.size () || !appctx->ws_conns[stream_idx])
    return;

  JsonBuilder * b = json_builder_new ();
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "ice");
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "candidate");
  json_builder_add_string_value (b, candidate);
  json_builder_set_member_name (b, "sdpMLineIndex");
  json_builder_add_int_value (b, (gint) mlineindex);
  json_builder_end_object (b);
  json_builder_end_object (b);

  JsonGenerator * gen = json_generator_new ();
  JsonNode * root = json_builder_get_root (b);
  json_generator_set_root (gen, root);
  gchar * msg = json_generator_to_data (gen, NULL);

  ws_send_text (appctx, stream_idx, msg);

  g_free (msg);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (b);
}

/**
 * Callback function to send offer to peer
 *
 * @param promise GstPromise containing SDP offer
 * @param user_data Pointer to SignallingCtx object
 *
 */
static void
on_offer_created (GstPromise * promise, gpointer user_data)
{
  SignallingCtx * sctx = (SignallingCtx *) user_data;
  if (!sctx || !sctx->appctx)
    return;

  VLMAppContext * appctx = sctx->appctx;
  guint stream_idx = sctx->stream_idx;

  const GstStructure * reply;
  GstWebRTCSessionDescription * offer = NULL;

  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
    &offer, NULL);
  gst_promise_unref (promise);

  if (!offer)
    return;

  if (stream_idx >= appctx->webrtcs.size () ||
      stream_idx >= appctx->ws_conns.size () || !appctx->webrtcs[stream_idx] ||
      !appctx->ws_conns[stream_idx]) {
    gst_webrtc_session_description_free (offer);
    return;
  }

  GstElement * webrtcbin = appctx->webrtcs[stream_idx];

  // Set local description
  GstPromise * local_promise = gst_promise_new ();
  g_signal_emit_by_name (webrtcbin, "set-local-description", offer,
                         local_promise);
  gst_promise_interrupt (local_promise);
  gst_promise_unref (local_promise);

  gchar * sdp_str = gst_sdp_message_as_text (offer->sdp);

  JsonBuilder * b = json_builder_new ();
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "sdp");
  json_builder_begin_object (b);
  json_builder_set_member_name (b, "type");
  json_builder_add_string_value (b, "offer");
  json_builder_set_member_name (b, "sdp");
  json_builder_add_string_value (b, sdp_str);
  json_builder_end_object (b);
  json_builder_end_object (b);

  JsonGenerator * gen = json_generator_new ();
  JsonNode * root = json_builder_get_root (b);
  json_generator_set_root (gen, root);
  gchar * msg = json_generator_to_data (gen, NULL);

  ws_send_text (appctx, stream_idx, msg);

  g_free (msg);
  g_free (sdp_str);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (b);

  gst_webrtc_session_description_free (offer);
}

/**
 * Callback function to store a reference to the data channel inside app context
 *
 * @param webrtcbin Pointer to the webrtcbin element
 * @param channel Newly created data channel object
 * @param appctx Pointer to App context object
 *
 */
static void
on_data_channel (GstElement * webrtcbin, GstWebRTCDataChannel * channel,
                VLMAppContext * appctx)
{
  if (!appctx || !webrtcbin || !channel)
    return;

  guint stream_idx = 0;
  const gchar * name = gst_element_get_name (webrtcbin);
  if (!extract_index_from_name (name, &stream_idx))
    stream_idx = 0;

  if (stream_idx >= appctx->data_channels.size ())
    appctx->data_channels.resize (stream_idx + 1, NULL);

  if (appctx->data_channels[stream_idx])
    g_object_unref (appctx->data_channels[stream_idx]);

  appctx->data_channels[stream_idx] =
      (GstWebRTCDataChannel *) g_object_ref (channel);

  g_print ("[webrtc] on-data-channel (remote) stream=%u", stream_idx);
}

/**
 * Callback function to handle "on-open" event
 *
 * @param channel Data channel object
 * @param appctx Pointer to App context object
 *
 */
static void
on_data_channel_open (GstWebRTCDataChannel * channel, VLMAppContext * appctx)
{
  if (!appctx || !channel)
    return;

  guint stream_idx;
  gchar * label = NULL;
  g_object_get (channel, "label", &label, NULL);

  if (!extract_index_from_name (label, &stream_idx)) {
    g_printerr ("Could not extract index from element name!\n");
    g_free (label);
    return;
  }

  g_free (label);

  if (stream_idx >= appctx->data_channels.size ())
    appctx->data_channels.resize (stream_idx + 1);

  // Keep a reference; channel can outlive this callback.
  if (appctx->data_channels[stream_idx])
    g_object_unref (appctx->data_channels[stream_idx]);

  appctx->data_channels[stream_idx] =
      (GstWebRTCDataChannel *) g_object_ref (channel);

  g_print ("[webrtc] on_data_channel_open\n");
}

/**
 * Callback function to handle "on-close" event
 *
 * @param channel Data channel object
 * @param appctx Pointer to App context object
 *
 */
static void
on_data_channel_close (GstWebRTCDataChannel * channel, VLMAppContext * appctx)
{
  if (!appctx || !channel)
    return;

  gchar * label = NULL;
  g_object_get (channel, "label", &label, NULL);

  g_print ("[webrtc] on_data_channel_close label - %s\n", label);

  g_free (label);
}

/**
 * Create a data channel and set open/close callbacks
 *
 * @param appctx Pointer to App context object
 * @param stream_idx Index of current stream
 *
 */
static void
webrtc_setup_data_channel (VLMAppContext * appctx, guint stream_idx)
{
  if (!appctx || stream_idx >= appctx->webrtcs.size ())
    return;

  GstElement * webrtcbin = appctx->webrtcs[stream_idx];
  gchar name[128];

  // Create DataChannel 'meta' before offer
  GstWebRTCDataChannel * ch = NULL;
  g_snprintf (name, sizeof (name), "meta-%u", stream_idx);
  g_signal_emit_by_name (webrtcbin, "create-data-channel", name, NULL, &ch);

  if (ch) {
    g_signal_connect (ch, "on-open", G_CALLBACK (on_data_channel_open), appctx);
    g_signal_connect (ch, "on-close", G_CALLBACK (on_data_channel_close),
      appctx);

    g_object_unref (ch);
    g_print ("[webrtc] Created DataChannel 'meta' for stream %u\n", stream_idx);
  }

  g_signal_connect (webrtcbin, "on-data-channel", G_CALLBACK (on_data_channel),
    appctx);
}

/**
 * Create and send new SDP offer for the given webrtcbin element
 *
 * @param webrtcbin Pointer to the webrtcbin element
 * @param appctx Pointer to App context object
 *
 */
static void
send_offer (GstElement * webrtcbin, VLMAppContext * appctx)
{
  guint element_index;
  const gchar * element_name = NULL;

  element_name = gst_element_get_name (webrtcbin);

  if (!extract_index_from_name (element_name, &element_index)) {
    g_printerr ("Could not extract index from element name!\n");
    return;
  }

  SignallingCtx * sctx = (SignallingCtx *) g_malloc0 (sizeof (SignallingCtx));
  sctx->appctx = appctx;
  sctx->stream_idx = element_index;
  GstPromise * promise = gst_promise_new_with_change_func (
      on_offer_created, sctx, (GDestroyNotify) g_free);
  g_signal_emit_by_name (webrtcbin, "create-offer", NULL, promise);
}

/**
 * Handles on-negotiation-needed signal
 *
 * @param webrtcbin Pointer to the webrtcbin element
 * @param appctx Pointer to App context object
 *
 */
static void
on_negotiation_needed (GstElement * webrtcbin, VLMAppContext * appctx)
{
  (void) appctx;

  guint element_index;
  const gchar * element_name = NULL;

  g_print ("[webrtc] on_negotiation_needed\n");

  element_name = gst_element_get_name (webrtcbin);

  if (!extract_index_from_name (element_name, &element_index)) {
    g_printerr ("Could not extract index from element name!\n");
    return;
  }
}

/**
 * Process incoming WebSocket signalling messages
 *
 * @param conn Web socket connection that received the message
 * @param type Web socket message type
 * @param message Raw message
 * @param user_data Pointer to SignallingCtx object
 *
 */
static void
on_ws_message (SoupWebsocketConnection * conn,
  SoupWebsocketDataType type, GBytes * message, gpointer user_data)
{
  (void) conn;

  SignallingCtx * sctx = (SignallingCtx *) user_data;
  if (!sctx || !sctx->appctx)
    return;

  VLMAppContext * appctx = sctx->appctx;
  guint stream_idx = sctx->stream_idx;

  if (type != SOUP_WEBSOCKET_DATA_TEXT || !message)
    return;
  if (stream_idx >= appctx->webrtcs.size ())
    return;

  gsize sz = 0;
  const gchar * txt = (const gchar *) g_bytes_get_data (message, &sz);
  if (!txt || sz == 0)
    return;

  g_print ("[webrtc][%u] signalling rx: %s\n", stream_idx, txt);

  if (g_str_has_prefix (txt, "HELLO")) {
    g_print ("[webrtc][%u] Registration successful\n", stream_idx);
    return;
  } else if (g_strcmp0 (txt, "SESSION_OK") == 0) {
    g_print ("[webrtc][%u] Peer connected", stream_idx);
    return;
  } else if (g_strcmp0 (txt, "OFFER_REQUEST") == 0) {
    g_print ("[webrtc][%u] OFFER_REQUEST", stream_idx);
    return;
  } else if (g_str_has_prefix (txt, "ERROR")) {
    g_printerr ("[webrtc][%u] %s", stream_idx, txt);
    return;
  }

  // Parse JSON
  JsonParser * parser = json_parser_new ();
  GError * error = NULL;
  if (!json_parser_load_from_data (parser, txt, (gssize) sz, &error)) {
    if (error) {
      g_printerr ("[webrtc][%u] signalling parse error: %s", stream_idx,
        error->message);
      g_error_free (error);
    }
    g_object_unref (parser);
    return;
  }

  JsonNode * root = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root)) {
    g_object_unref (parser);
    return;
  }
  JsonObject * obj = json_node_get_object (root);

  GstElement * webrtcbin = appctx->webrtcs[stream_idx];

  if (json_object_has_member (obj, "sdp")) {
    JsonObject * sdp = json_object_get_object_member (obj, "sdp");
    const gchar * sdp_type = json_object_get_string_member (sdp, "type");
    const gchar * sdp_str = json_object_get_string_member (sdp, "sdp");

    if (sdp_type && sdp_str && g_strcmp0 (sdp_type, "answer") == 0) {
      GstSDPMessage * sdp_msg = NULL;
      gst_sdp_message_new (&sdp_msg);
      if (gst_sdp_message_parse_buffer ((guint8 *) sdp_str, strlen (sdp_str),
          sdp_msg) == GST_SDP_OK) {
        GstWebRTCSessionDescription * answer =
          gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER, sdp_msg);

        GstPromise * p = gst_promise_new ();

        g_signal_emit_by_name (webrtcbin, "set-remote-description", answer, p);
        gst_promise_interrupt (p);
        gst_promise_unref (p);
        gst_webrtc_session_description_free (answer);
      } else {
        gst_sdp_message_free (sdp_msg);
        g_printerr ("[webrtc][%u] Failed to parse SDP answer", stream_idx);
      }
    }
  } else if (json_object_has_member (obj, "ice")) {
    JsonObject * ice = json_object_get_object_member (obj, "ice");
    const gchar * candidate = json_object_get_string_member (ice, "candidate");
    gint mline = json_object_get_int_member (ice, "sdpMLineIndex");

    if (candidate) {
      g_signal_emit_by_name (webrtcbin, "add-ice-candidate", (guint) mline,
        candidate);
    }
  } else if (json_object_has_member (obj, "cmd")) {
    const gchar * cmd = json_object_get_string_member (obj, "cmd");

    g_print ("[webrtc][%u] cmd - %s\n", stream_idx, cmd);
    if (g_strcmp0 (cmd, "READY") == 0) {
      guint element_index;
      const gchar * element_name = NULL;

      element_name = gst_element_get_name (webrtcbin);

      if (!extract_index_from_name (element_name, &element_index)) {
        g_printerr ("Could not extract index from element name!\n");
        return;
      }

      webrtc_setup_data_channel (appctx, element_index);

      send_offer (webrtcbin, appctx);

      gst_element_set_state (appctx->pipelines[element_index],
        GST_STATE_PLAYING);
    }
  }

  g_object_unref (parser);
}

/**
 * Handle Web Socket closed "event"
 *
 * @param conn Web socket connection that was closed
 * @param user_data Pointer to SignallingCtx object
 *
 */
static void
on_ws_closed (SoupWebsocketConnection * conn, gpointer user_data)
{
  (void) conn;

  SignallingCtx * sctx = (SignallingCtx *) user_data;
  if (!sctx || !sctx->appctx)
    return;

  VLMAppContext * appctx = sctx->appctx;
  guint stream_idx = sctx->stream_idx;

  g_printerr ("[webrtc][%u] signalling websocket closed\n", stream_idx);

  if (stream_idx < appctx->ws_conns.size ())
    appctx->ws_conns[stream_idx] = NULL;
}

/**
 * Send a text message over the Web Socket connection for the given stream
 *
 * @param appctx Pointer to App context object
 * @param stream_idx Index of the given stream
 * @param text text string
 *
 */
static void
ws_send_text (VLMAppContext * appctx, guint stream_idx, const gchar * text)
{
  if (!appctx || !text)
    return;
  if (stream_idx >= appctx->ws_conns.size () || !appctx->ws_conns[stream_idx])
    return;
  soup_websocket_connection_send_text (appctx->ws_conns[stream_idx], text);
}

/**
 * Finalize Web Socket connection
 *
 * @param session SoupSession that initiated the connection
 * @param res Asynchronous result
 * @param userdata Pointer to SignallingCtx object
 *
 */
static void
on_server_connected (SoupSession * session, GAsyncResult * res, gpointer userdata)
{
  GError * error = NULL;
  SignallingCtx * sctx = (SignallingCtx *) userdata;
  if (!sctx || !sctx->appctx)
    return;

  VLMAppContext * appctx = sctx->appctx;
  guint stream_idx = sctx->stream_idx;

  SoupWebsocketConnection * conn =
      soup_session_websocket_connect_finish (session, res, &error);

  if (error) {
    g_printerr ("[webrtc][%u] %s", stream_idx, error->message);
    g_error_free (error);
    return;
  }

  if (!conn) {
    g_printerr ("[webrtc][%u] soup_session_websocket_connect_finish failed",
      stream_idx);
    return;
  }

  if (stream_idx >= appctx->ws_conns.size ())
    appctx->ws_conns.resize (stream_idx + 1, NULL);

  appctx->ws_conns[stream_idx] = conn;

  // Own the SignallingCtx for the lifetime of this websocket connection.
  // It will be freed when the connection is finalized.
  g_object_set_data_full (G_OBJECT (conn), "signalling-ctx", sctx,
    (GDestroyNotify) g_free);

  g_print ("[webrtc][%u] Connected to signalling server", stream_idx);

  g_signal_connect (conn, "message", G_CALLBACK (on_ws_message), sctx);
  g_signal_connect (conn, "closed", G_CALLBACK (on_ws_closed), sctx);

  // HELLO + SESSION (python protocol)
  guint local_id = (stream_idx < appctx->ws_local_ids.size ())
    ? appctx->ws_local_ids[stream_idx]
    : 0;

  gchar * hello = g_strdup_printf ("HELLO %u", local_id);
  soup_websocket_connection_send_text (conn, hello);
  g_free (hello);

  g_print ("[webrtc][%u] signalling ready: %s (id=%u)\n", stream_idx,
    appctx->ws_url.c_str (), local_id);
}

/**
 * Initialize Web Socket signalling for all streams
 *
 * @param appctx Pointer to App context object
 *
 */
static gboolean
webrtc_connect_signalling (VLMAppContext * appctx)
{
  if (!appctx)
    return FALSE;

  const gchar * url = g_getenv ("WEBRTC_SERVER");
  // fallback peer if not in JSON
  const gchar * peer_env = g_getenv ("WEBRTC_PEER");

  appctx->ws_url = url ? url : "ws://127.0.0.1:8443";
  guint fallback_peer =
    peer_env ? (guint) g_ascii_strtoll (peer_env, NULL, 10) : 0;

  // Prepare per-stream vectors
  appctx->ws_sessions.assign (appctx->num_streams, NULL);
  appctx->ws_conns.assign (appctx->num_streams, NULL);
  appctx->ws_local_ids.resize (appctx->num_streams);

  for (guint i = 0; i < appctx->num_streams; i++) {
    guint local_id = 0;
    if (i < appctx->stream_configs.size ())
      local_id = appctx->stream_configs[i].local_id;

    if (local_id == 0)
      local_id = fallback_peer;

    appctx->ws_local_ids[i] = local_id;

    if (local_id == 0) {
      g_printerr ("[webrtc][%u] Missing local_id (set sources[%u].local_id in "
        "JSON or WEBRTC_PEER env)",
        i, i);
      return FALSE;
    }

    appctx->ws_sessions[i] = soup_session_new ();
    SoupMessage * msg = soup_message_new ("GET", appctx->ws_url.c_str ());
    if (!msg) {
      g_printerr ("[webrtc][%u] Failed to create SoupMessage for %s", i,
        appctx->ws_url.c_str ());
      return FALSE;
    }

    // One SignallingCtx per stream - owned by the app for lifetime
    // (we free on shutdown)
    SignallingCtx * sctx = (SignallingCtx *) g_malloc0 (sizeof (SignallingCtx));
    sctx->appctx = appctx;
    sctx->stream_idx = i;

    soup_session_websocket_connect_async (
      appctx->ws_sessions[i], msg, NULL, NULL, G_PRIORITY_DEFAULT, NULL,
      (GAsyncReadyCallback) on_server_connected, sctx);

    g_object_unref (msg);
  }

  return TRUE;
}

/**
 * Closes up and cleans all Web Socket signalling connections
 *
 * @param appctx Pointer to App context object
 *
 */
static gboolean
webrtc_disconnect_signalling (VLMAppContext * appctx)
{
  if (!appctx)
    return FALSE;

  for (guint i = 0; i < appctx->num_streams; i++) {
    if (i < appctx->ws_conns.size () && appctx->ws_conns[i]) {
      soup_websocket_connection_close (appctx->ws_conns[i],
        SOUP_WEBSOCKET_CLOSE_NO_STATUS, NULL);

      g_object_unref (appctx->ws_conns[i]);
      appctx->ws_conns[i] = NULL;
    }
    if (i < appctx->ws_sessions.size () && appctx->ws_sessions[i]) {
      g_object_unref (appctx->ws_sessions[i]);
      appctx->ws_sessions[i] = NULL;
    }
  }

  return TRUE;
}

/**
 * Send a string over the given data channel
 *
 * @param dc Data channel
 * @param text Text to send
 *
 */
static void
dc_send_string (GstWebRTCDataChannel * dc, const gchar * text)
{
  if (!dc || !text)
    return;
  g_signal_emit_by_name (dc, "send-string", text);
}

/**
 * Serialize and send a JSON object over the given data channel
 *
 * @param dc Data channel
 * @param obj JSON object to serialize and send
 *
 */
static void
dc_send_json_object (GstWebRTCDataChannel * dc, JsonObject * obj)
{
  if (!dc || !obj)
    return;

  JsonNode * n = json_node_new (JSON_NODE_OBJECT);
  json_node_set_object (n, obj);

  JsonGenerator * gen = json_generator_new ();
  json_generator_set_root (gen, n);
  gchar * s = json_generator_to_data (gen, NULL);

  if (s)
    dc_send_string (dc, s);

  g_free (s);
  g_object_unref (gen);
  json_node_free (n);
}

/**
 * Send a simple status JSON message over the data channel (if open)
 *
 * @param dc Data channel
 * @param status Status string to embed in JSON object and send
 *
 */
static void
dc_send_status (GstWebRTCDataChannel * dc, const gchar * status)
{
  GstWebRTCDataChannelState state;
  g_object_get (dc, "ready-state", &state, NULL);
  if (state != GST_WEBRTC_DATA_CHANNEL_STATE_OPEN) {
    g_printerr ("Channel is closed\n");
    return;
  }

  gchar * part = g_strdup (status);

  JsonObject * chunk = json_object_new ();
  json_object_set_string_member (chunk, "type", "status");
  json_object_set_string_member (chunk, "data", part);
  dc_send_json_object (dc, chunk);
  json_object_unref (chunk);

  g_free (part);
}

/**
 * Split a base64 string into JSON chunks and send over the data channel
 *
 * @param dc Data channel
 * @param sid Stream identifier
 * @param fid Frame identifier
 * @param b64 Raw buffer
 * @param chunk_size Maximum chunk size
 *
 */
static void
dc_send_base64_chunks (GstWebRTCDataChannel * dc, guint sid, guint64 fid,
  const gchar * b64, gsize chunk_size = 12000)
{
  if (!dc || !b64 || b64[0] == '\0')
    return;

  gsize len = strlen (b64);
  if (len == 0)
    return;

  if (chunk_size < 256)
    chunk_size = 256;

  // Calculate number of chunks
  guint n = (guint) ((len + chunk_size - 1) / chunk_size);

  // thumb-begin
  {
    JsonObject * begin = json_object_new ();
    json_object_set_string_member (begin, "type", "thumb-begin");
    json_object_set_int_member (begin, "sid", (gint) sid);
    json_object_set_int_member (begin, "id", (gint64) fid);
    json_object_set_int_member (begin, "n", (gint) n);
    dc_send_json_object (dc, begin);
    json_object_unref (begin);
  }

  // thumb-chunk
  for (guint i = 0; i < n; i++) {
    gsize off = (gsize) i * chunk_size;
    gsize take = std::min (chunk_size, len - off);

    gchar * part = g_strndup (b64 + off, take);

    JsonObject * chunk = json_object_new ();
    json_object_set_string_member (chunk, "type", "thumb-chunk");
    json_object_set_int_member (chunk, "sid", (gint) sid);
    json_object_set_int_member (chunk, "id", (gint64) fid);
    json_object_set_int_member (chunk, "i", (gint) i);
    json_object_set_string_member (chunk, "data", part);
    dc_send_json_object (dc, chunk);
    json_object_unref (chunk);

    g_free (part);
  }

  // thumb-end
  {
    JsonObject * end = json_object_new ();
    json_object_set_string_member (end, "type", "thumb-end");
    json_object_set_int_member (end, "sid", (gint) sid);
    json_object_set_int_member (end, "id", (gint64) fid);
    dc_send_json_object (dc, end);
    json_object_unref (end);
  }
}

/**
 * Process metadata sample from appsink, pull JSON and send over Web Socket
 *
 * @param appsink Appsink element, from which the metadata is coming
 * @param user_data Pointer to MetaSinkUserData object, containing App context
 * and stream idx
 *
 */
static GstFlowReturn
on_meta_new_sample (GstElement * appsink, gpointer user_data)
{
  MetaSinkUserData * ud = (MetaSinkUserData *) user_data;
  GstSample * sample = NULL;

  if (!ud || !ud->appctx)
    return GST_FLOW_OK;

  guint idx = ud->stream_idx;
  if (idx >= ud->appctx->data_channels.size () ||
      !ud->appctx->data_channels[idx]) {
    g_signal_emit_by_name (appsink, "pull-sample", &sample);
    if (!sample)
      return GST_FLOW_OK;

    gst_sample_unref (sample);

    return GST_FLOW_OK;
  }

  g_signal_emit_by_name (appsink, "pull-sample", &sample);
  if (!sample)
    return GST_FLOW_OK;

  GstBuffer * buf = gst_sample_get_buffer (sample);
  if (!buf) {
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }

  GstMapInfo map;
  if (gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GstWebRTCDataChannel * dc = ud->appctx->data_channels[idx];

    GstWebRTCDataChannelState state;
    g_object_get (dc, "ready-state", &state, NULL);
    if (state != GST_WEBRTC_DATA_CHANNEL_STATE_OPEN) {
      g_printerr ("[WebRTC] Channel is closed\n");
      gst_buffer_unmap (buf, &map);
      gst_sample_unref (sample);
      return GST_FLOW_OK;
    }

    // Pull JSON text
    gchar * s = g_strndup ((const gchar *) map.data, map.size);

    // Increment per-stream frame id (used to correlate meta + thumbnail chunks)
    if (idx >= ud->appctx->dc_frame_ids.size ())
      ud->appctx->dc_frame_ids.resize (idx + 1, 0);
    guint64 fid = ++ud->appctx->dc_frame_ids[idx];

    // Try to parse JSON. If it isn't valid JSON, fall back to raw send.
    JsonParser * parser = json_parser_new ();
    GError * perr = NULL;
    if (!json_parser_load_from_data (parser, s, -1, &perr)) {
      dc_send_string (dc, s);
      if (perr)
        g_error_free (perr);
      g_object_unref (parser);
      g_free (s);
      gst_buffer_unmap (buf, &map);
      gst_sample_unref (sample);
      return GST_FLOW_OK;
    }

    JsonNode * root = json_parser_get_root (parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT (root)) {
      dc_send_string (dc, s);
      g_object_unref (parser);
      g_free (s);
      gst_buffer_unmap (buf, &map);
      gst_sample_unref (sample);
      return GST_FLOW_OK;
    }

    JsonObject * root_obj = json_node_get_object (root);

    // Extract optional thumbnail payload
    const gchar * thumb_b64 = NULL;
    if (json_object_has_member (root_obj, "buffer_base64"))
      thumb_b64 = json_object_get_string_member (root_obj, "buffer_base64");

    // Build meta object = original root object minus buffer_base64
    JsonObject * meta_only = json_object_new ();
    GList * members = json_object_get_members (root_obj);
    for (GList * l = members; l; l = l->next) {
      const gchar * k = (const gchar *) l->data;
      if (!k)
        continue;
      if (g_strcmp0 (k, "buffer_base64") == 0)
        continue;

      JsonNode * v = json_object_get_member (root_obj, k);
      if (v) {
        JsonNode * copy = json_node_copy (v);
        json_object_set_member (meta_only, k, copy);
      }
    }
    g_list_free (members);

    if (!json_object_has_member (meta_only, "text")) {
      g_printerr ("Missing text in meta!\n");
      goto clean;
    }

    // Send metadata envelope first (so receiver can display text immediately)
    {
      JsonObject * env = json_object_new ();
      json_object_set_string_member (env, "t", "meta");
      json_object_set_int_member (env, "sid", (gint) idx);
      json_object_set_int_member (env, "fid", (gint64) fid);

      JsonNode * meta_node = json_node_new (JSON_NODE_OBJECT);
      json_node_set_object (meta_node, meta_only);
      json_object_set_member (env, "meta", meta_node);

      dc_send_json_object (dc, env);
      json_object_unref (env);
    }

    // Then send thumbnail as base64 chunks (python-style)
    if (thumb_b64 && thumb_b64[0] != '\0') {
      // Init per-stream thumb tracking
      if (idx >= ud->appctx->dc_last_thumb_sent.size ())
        ud->appctx->dc_last_thumb_sent.resize (idx + 1, 0);

      // Rate limit (like THUMB_EVERY_N_FRAMES in python)
      guint every = ud->appctx->thumb_every_n_frames
        ? ud->appctx->thumb_every_n_frames
        : 1;

      if ((fid - ud->appctx->dc_last_thumb_sent[idx]) < (guint64) every) {
        // skip thumbnail
      } else {
        // Size guard (like MAX_B64 in python)
        gsize b64_len = strlen (thumb_b64);
        gsize max_b64 =
            ud->appctx->max_b64 ? ud->appctx->max_b64 : (gsize) 500000;
        if (b64_len <= 32 || b64_len > max_b64) {
          // skip too short / too big
          g_print ("Skip skip too short / too big! - b64_len - %ld, "
            "max_b64 - %ld\n",
            b64_len, max_b64);
        } else {
          ud->appctx->dc_last_thumb_sent[idx] = fid;
          dc_send_base64_chunks (dc, idx, fid, thumb_b64, 12000);
        }
      }
    }

  clean:
    json_object_unref (meta_only);
    g_object_unref (parser);
    g_free (s);
    gst_buffer_unmap (buf, &map);
  }

  gst_sample_unref (sample);
  return GST_FLOW_OK;
}

/**
 * Handles error events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to App context
 *
 */
void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  (void) bus;

  VLMAppContext * appctx = (VLMAppContext *) userdata;
  GError * error = NULL;
  gchar * debug = NULL;

  g_print ("\n\nReceived an error ...\n");

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (appctx->mloop);
}

/**
 * Handles warning events.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer Error Event Message.
 * @param userdata Pointer to Main Loop for Handling Error.
 *
 */
void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  (void) userdata;
  (void) bus;

  GError * error = NULL;
  gchar * debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

/**
 * Handles End-Of-Stream(eos) Event.
 *
 * @param bus Gstreamer bus for Mesaage passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to App context
 *
 */
void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  (void) bus;

  VLMAppContext * appctx = (VLMAppContext *) userdata;
  GstElement * element = GST_ELEMENT (GST_MESSAGE_SRC (message));
  const gchar * element_name = NULL;
  gboolean eos_all = TRUE;
  guint element_index;

  element_name = gst_element_get_name (element);

  if (!extract_index_from_name (element_name, &element_index)) {
    g_printerr ("Could not extract index from element name!\n");
    return;
  }

  g_mutex_lock (&appctx->mutex);
  appctx->eos_flags[element_index] = TRUE;
  g_mutex_unlock (&appctx->mutex);

  for (guint i = 0; i < appctx->num_streams; i++) {
    if (!appctx->eos_flags[i]) {
      eos_all = FALSE;
      break;
    }
  }

  if (eos_all) {
    g_print ("EOS recieved on all streams - qutting main loop\n");
    g_main_loop_quit (appctx->mloop);
  }
}

/**
 * Handles state change events for the pipeline
 *
 * @param bus Gstreamer bus for Message passing in Pipeline.
 * @param message Gstreamer eos Event Message.
 * @param userdata Pointer to Pipeline.
 *
 */
void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  (void) bus;

  GstElement * pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);

  g_print ("\nPipeline state changed from %s to %s:\n",
    gst_element_state_get_name (old),
    gst_element_state_get_name (new_st));
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 *
 */
static void
gst_app_context_free (VLMAppContext * appctx)
{
  // Release extra refs taken by gst_bin_get_by_name and data-channels
  for (guint i = 0; i < appctx->num_streams; i++) {
    if (i < appctx->data_channels.size () && appctx->data_channels[i]) {
      g_object_unref (appctx->data_channels[i]);
      appctx->data_channels[i] = NULL;
    }

    if (i < appctx->meta_sinks.size () && appctx->meta_sinks[i]) {
      gst_object_unref (appctx->meta_sinks[i]);
      appctx->meta_sinks[i] = NULL;
    }
    if (i < appctx->appsrces.size () && appctx->appsrces[i]) {
      gst_object_unref (appctx->appsrces[i]);
      appctx->appsrces[i] = NULL;
    }
    if (i < appctx->appsrces_jpeg.size () && appctx->appsrces_jpeg[i]) {
      gst_object_unref (appctx->appsrces_jpeg[i]);
      appctx->appsrces_jpeg[i] = NULL;
    }
    if (i < appctx->webrtcs.size () && appctx->webrtcs[i]) {
      gst_object_unref (appctx->webrtcs[i]);
      appctx->webrtcs[i] = NULL;
    }
  }

  for (guint i = 0; i < appctx->num_streams; i++) {
    if (appctx->pipelines[i]) {
      gst_element_set_state (appctx->pipelines[i], GST_STATE_NULL);
      gst_object_unref (appctx->pipelines[i]);
    }
  }

  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }
}

/**
 * Function to serialize a string in a format acceptable to be used as metadata:
 *
 * @param string Input string
 *
 */
std::string
parse_string_to_gst_structure_string (std::string & string)
{
  std::string ret_str;
  GstStructure *meta = NULL;
  gchar *string_cstr = NULL, *text = NULL;
  GValue list = G_VALUE_INIT;
  GValue value = G_VALUE_INIT, ts = G_VALUE_INIT;

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&value, GST_TYPE_STRUCTURE);
  g_value_init (&ts, G_TYPE_UINT);

  text = g_strdup (string.c_str ());

  meta = gst_structure_new ("Text",
    "id",         G_TYPE_UINT,   0,
    "contents",   G_TYPE_STRING, text,
    "confidence", G_TYPE_DOUBLE, 100.00,
    "color",      G_TYPE_UINT,   0xFF00FF,
    NULL);

  g_value_take_boxed (&value, meta);
  gst_value_list_append_value (&list, &value);

  string_cstr = gst_value_serialize (&list);
  if (!string_cstr) {
    g_printerr ("Failed to serialize VLM output!");
    goto cleanup;
  }

  ret_str = std::string (string_cstr);

cleanup:
  g_free (text);
  g_free (string_cstr);
  g_value_unset (&value);
  g_value_unset (&list);
  g_value_unset (&ts);

  return ret_str;
}

/**
 * Function to get and return the result of VLM inference:
 *
 * @param appsrc A pointer to the appsrc vlm/jpeg element
 * @param appsink_ml_meta_buf The buffer to be pushed downstream to appsrc_vlm
 *
 */
void
push_buffer_to_appsrc (GstElement * appsrc,  GstBuffer * appsink_ml_meta_buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  g_signal_emit_by_name (appsrc, "push-buffer", appsink_ml_meta_buf, &ret);

  if (ret != GST_FLOW_OK)
    g_printerr ("ERROR: Failed to emit push-buffer signal.\n");
}

/**
 * Push a stream index to queue if it's not already present
 *
 * @param appctx Application Context object
 * @param index Stream index
 *
 */
static bool
scheduler_push (VLMAppContext * appctx, guint index)
{
  std::queue<guint> tmp = appctx->stream_scheduler;

  while (!tmp.empty ()) {
    if (tmp.front () == index) {
      return false;
    }
    tmp.pop ();
  }

  appctx->stream_scheduler.push (index);
  return true;
}

/**
 * Initialize VLM and set inference callback
 *
 * @param appctx Application Context object
 *
 */
void
vlm_init (VLMAppContext * appctx)
{
  GenieVLMConfig cfg;

  for (guint x = 0; x < appctx->vlm_engines.size (); x++) {
    VlmInstanceConfig cf;

    if (appctx->vlm_engines[x] == "NPU0") {
      cf.engine = VlmEngine::NPU0;
      cf.nodes = {
        {.type = "ImageEncoder",
        .name = "image_encoder",
        .config_path = "qwen_veg_NPU_0.json"
        },
        {.type = "LutEncoder",
        .name = "lut_encoder",
        .config_path = "text-encoder.json"
        },
        {.type = "TextGenerator",
        .name = "text_generator",
        .config_path = "qwen-htp_NPU_0.json"
        },
      };
    } else if (appctx->vlm_engines[x] == "NPU1") {
      cf.engine = VlmEngine::NPU1;
      cf.nodes = {
        {.type = "ImageEncoder",
          .name = "image_encoder",
          .config_path = "qwen_veg_NPU_1.json"
        },
        {.type = "LutEncoder",
          .name = "lut_encoder",
          .config_path = "text-encoder.json"
        },
        {.type = "TextGenerator",
          .name = "text_generator",
          .config_path = "qwen-htp_NPU_1.json"
        },
      };
    } else if (appctx->vlm_engines[x] == "GPU") {
      cf.engine = VlmEngine::GPU;
    }

    cf.pipeline_config_path = "";
    cfg.instances.push_back (cf);
  }

  GenieLog_Level_t parsed_level;
  if (logger_level_from_string (appctx->logger_level, parsed_level)) {
    cfg.logging_enabled = true;
    cfg.log_level = parsed_level;
  }
  cfg.profiling_enabled = appctx->profile_enabled;

  cfg.lb_mode = GenieVLMConfig::LBMode::RoundRobin;
  cfg.max_queue_per_instance = 1;

  appctx->vlm = new GenieVLM (cfg, [] (uint64_t id, bool ok, const VlmResult & r,
      const std::string & err) {
    // callback runs on separate dispatcher thread
    (void) id;

    std::string result;
    VLMBufferBundle * vlm_buf_bundle = (VLMBufferBundle *) r.user_data;
    GstBuffer * appsink_ml_meta_buf = NULL;
    guint element_index;
    const gchar * element_name = NULL;

    g_mutex_lock (&vlm_buf_bundle->appctx->mutex);

    element_name = gst_element_get_name (vlm_buf_bundle->appsrc_vlm);

    if (!extract_index_from_name (element_name, &element_index)) {
      g_printerr ("Could not extract index from element name!\n");
      goto exit;
    }

    if (element_index >= vlm_buf_bundle->appctx->eos_flags.size ()) {
      g_printerr ("VLM callback: element_index out of range: %u\n",
        element_index);
      goto exit;
    }

    if (vlm_buf_bundle->appctx->eos_flags[element_index]) {
      g_print ("VLM callback: EOS reached.\n");
      goto exit;
    }

    if (!ok) {
      g_print ("VLM error - %s\n", err.c_str ());
      goto exit;
    }

    result = r.text;
    if (!result.empty () && (result[0] == '\n' || result[0] == ':')) {
      result.erase (0, 1);
    }

    if (result.empty () || result.size () < 5) {
      g_printerr ("ERROR: Failed to retrieve string from VLM.\n");

      if (vlm_buf_bundle->jpeg_buffer)
        gst_buffer_unref (vlm_buf_bundle->jpeg_buffer);

      if (vlm_buf_bundle->vlm_buffer)
        gst_buffer_unref (vlm_buf_bundle->vlm_buffer);

      goto exit;
    }

    result = parse_string_to_gst_structure_string (result);

    appsink_ml_meta_buf = gst_buffer_new_allocate (NULL, result.size (), NULL);

    if (appsink_ml_meta_buf == NULL) {
      g_printerr ("Could not allocate buffer for text\n");

      if (vlm_buf_bundle->jpeg_buffer)
        gst_buffer_unref (vlm_buf_bundle->jpeg_buffer);

      if (vlm_buf_bundle->vlm_buffer)
        gst_buffer_unref (vlm_buf_bundle->vlm_buffer);

      goto exit;
    }

    gst_buffer_fill (appsink_ml_meta_buf, 0, result.c_str (), result.size ());

    GST_BUFFER_PTS (appsink_ml_meta_buf) = gst_util_get_timestamp ();
    GST_BUFFER_DTS (appsink_ml_meta_buf) = GST_BUFFER_PTS (appsink_ml_meta_buf);

    // Push JPEG buffer
    push_buffer_to_appsrc (vlm_buf_bundle->appsrc_jpeg,
      vlm_buf_bundle->jpeg_buffer);

    if (vlm_buf_bundle->jpeg_buffer)
      gst_buffer_unref (vlm_buf_bundle->jpeg_buffer);

    if (vlm_buf_bundle->vlm_buffer)
      gst_buffer_unref (vlm_buf_bundle->vlm_buffer);

    // Push Meta buffer
    push_buffer_to_appsrc (vlm_buf_bundle->appsrc_vlm, appsink_ml_meta_buf);

  exit:
    if (appsink_ml_meta_buf)
      gst_buffer_unref (appsink_ml_meta_buf);

    g_mutex_unlock (&vlm_buf_bundle->appctx->mutex);

    g_slice_free (VLMBufferBundle, vlm_buf_bundle);
  });
}

/**
 * Denitialize VLM
 *
 * @param appctx Application Context object
 *
 */
void
vlm_deinit (VLMAppContext * appctx)
{
  if (appctx->vlm != NULL) {
    delete appctx->vlm;
    appctx->vlm = NULL;
  }
}

/**
 * Process queued VLM request, prepare buffers and metadata, submit job to VLM
 * engine
 *
 * @param appctx Application Context object
 * @param pipeline_idx Index of current pipeline
 *
 */
static void
process_vlm (VLMAppContext * appctx, guint pipeline_idx)
{
  GstMapInfo memmap = GST_MAP_INFO_INIT;
  GstProtectionMeta * pmeta = NULL;
  const gchar * question = NULL;
  VLMBufferBundle * vlm_buf_bundle = NULL;
  gint64 req_id = 0;

  if (appctx->buffer_bundles[pipeline_idx].vlm_buffer == NULL ||
      appctx->buffer_bundles[pipeline_idx].jpeg_buffer == NULL) {
    return;
  }

  if (appctx->stream_scheduler.empty ()) {
    if (appctx->buffer_bundles[pipeline_idx].vlm_buffer)
      gst_buffer_unref (appctx->buffer_bundles[pipeline_idx].vlm_buffer);

    if (appctx->buffer_bundles[pipeline_idx].jpeg_buffer)
      gst_buffer_unref (appctx->buffer_bundles[pipeline_idx].jpeg_buffer);

    appctx->buffer_bundles[pipeline_idx].vlm_buffer = NULL;
    appctx->buffer_bundles[pipeline_idx].jpeg_buffer = NULL;

    return;
  } else {
    guint front = appctx->stream_scheduler.front ();
    if (pipeline_idx != front) {

      if (appctx->buffer_bundles[pipeline_idx].vlm_buffer)
        gst_buffer_unref (appctx->buffer_bundles[pipeline_idx].vlm_buffer);

      if (appctx->buffer_bundles[pipeline_idx].jpeg_buffer)
        gst_buffer_unref (appctx->buffer_bundles[pipeline_idx].jpeg_buffer);

      appctx->buffer_bundles[pipeline_idx].vlm_buffer = NULL;
      appctx->buffer_bundles[pipeline_idx].jpeg_buffer = NULL;
      return;
    }
  }

  if (!gst_buffer_map (appctx->buffer_bundles[pipeline_idx].vlm_buffer, &memmap,
      GST_MAP_READ)) {

    g_printerr ("ERROR: failed to map buffer.\n");

    if (appctx->buffer_bundles[pipeline_idx].vlm_buffer)
      gst_buffer_unref (appctx->buffer_bundles[pipeline_idx].vlm_buffer);

    if (appctx->buffer_bundles[pipeline_idx].jpeg_buffer)
      gst_buffer_unref (appctx->buffer_bundles[pipeline_idx].jpeg_buffer);

    appctx->buffer_bundles[pipeline_idx].vlm_buffer = NULL;
    appctx->buffer_bundles[pipeline_idx].jpeg_buffer = NULL;

    return;
  }

  question = appctx->question_default.c_str ();

  // Get label info
  if ((pmeta = gst_buffer_get_protection_meta (
           appctx->buffer_bundles[pipeline_idx].vlm_buffer)) != NULL) {
    if (gst_structure_has_field (pmeta->info, "parent-label")) {
      const gchar * label =
          gst_structure_get_string (pmeta->info, "parent-label");

      if (g_strcmp0 (label, "person") == 0) {
        question = appctx->question_person.c_str ();
      } else if (g_strcmp0 (label, "car") == 0) {
        question = appctx->question_car.c_str ();
      } else {
        question = appctx->question_default.c_str ();
      }
    }
  }

  vlm_buf_bundle = g_slice_new0 (VLMBufferBundle);
  vlm_buf_bundle->appctx = appctx;
  vlm_buf_bundle->appsrc_vlm = appctx->buffer_bundles[pipeline_idx].appsrc_vlm;
  vlm_buf_bundle->appsrc_jpeg =
      appctx->buffer_bundles[pipeline_idx].appsrc_jpeg;
  vlm_buf_bundle->vlm_buffer = appctx->buffer_bundles[pipeline_idx].vlm_buffer;
  vlm_buf_bundle->jpeg_buffer =
      appctx->buffer_bundles[pipeline_idx].jpeg_buffer;

  req_id = appctx->vlm->submit (memmap.data, appctx->assistant_query, question,
    1.5, // temperature
    0.5, // top_p
    0.0, // seed
    0.0, // presence_penalty
    0.0, // frequency_penalty
    vlm_buf_bundle);
  if (req_id == 0) {

    gst_buffer_unmap (appctx->buffer_bundles[pipeline_idx].vlm_buffer, &memmap);

    if (appctx->buffer_bundles[pipeline_idx].vlm_buffer)
      gst_buffer_unref (appctx->buffer_bundles[pipeline_idx].vlm_buffer);

    if (appctx->buffer_bundles[pipeline_idx].jpeg_buffer)
      gst_buffer_unref (appctx->buffer_bundles[pipeline_idx].jpeg_buffer);

    g_slice_free (VLMBufferBundle, vlm_buf_bundle);
  } else {
    gst_buffer_unmap (appctx->buffer_bundles[pipeline_idx].vlm_buffer, &memmap);

    appctx->stream_scheduler.pop ();

    if (!appctx->scene_change_detection) {
      scheduler_push (appctx, pipeline_idx);
    }

    if (appctx->data_channels[pipeline_idx] != NULL)
      dc_send_status (appctx->data_channels[pipeline_idx], "Processing...");
  }

  appctx->buffer_bundles[pipeline_idx].vlm_buffer = NULL;
  appctx->buffer_bundles[pipeline_idx].jpeg_buffer = NULL;
}

/**
 * Release GstSample when processing is done:
 *
 * @param sample buffer to release
 *
 */
static void
gst_sample_release (GstSample * sample)
{
  gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
  gst_sample_set_buffer (sample, NULL);
#endif
}

/**
 * A callback function that is called when a new sample enters appsink
 *
 * @param appsink A pointer to the appsink element
 * @param user_data A pointer to the Application Context object
 *
 */
GstFlowReturn
appsink_vlm_callback (GstElement * appsink, gpointer user_data)
{
  GstSample * sample = NULL;
  GstBuffer * buffer = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  VLMAppContext * appctx = (VLMAppContext *) user_data;
  const gchar * appsink_name;
  guint pipeline_idx = 0;

  g_signal_emit_by_name (appsink, "pull-sample", &sample, &ret);
  if (ret != GST_FLOW_OK) {
    g_printerr ("ERROR: Cannot pull GstSample\n");
    return GST_FLOW_ERROR;
  }

  if (sample) {
    buffer = gst_sample_get_buffer (sample);
    if (buffer == NULL) {
      g_printerr ("ERROR: Cannot get buffer from sample\n");
    }

    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
      gst_sample_release (sample);
      return GST_FLOW_OK;
    }

    appsink_name = gst_element_get_name (appsink);

    if (!extract_index_from_name (appsink_name, &pipeline_idx)) {
      g_printerr ("ERROR: Could not get index of appsink\n");
      return GST_FLOW_OK;
    }

    g_mutex_lock (&appctx->mutex);

    if (appctx->buffer_bundles[pipeline_idx].vlm_buffer == NULL) {
      // Increase ref of the buffer and release the sample
      // Use the buffer for the next plugin
      gst_buffer_ref (buffer);

      appctx->buffer_bundles[pipeline_idx].appsrc_vlm =
        appctx->appsrces[pipeline_idx];
      appctx->buffer_bundles[pipeline_idx].vlm_buffer = buffer;
    }

    gst_sample_release (sample);

    if (appctx->buffer_bundles[pipeline_idx].jpeg_buffer != NULL) {
      // Call procesing
      process_vlm (appctx, pipeline_idx);
    }

    g_mutex_unlock (&appctx->mutex);
  }
  return GST_FLOW_OK;
}

/**
 * Updates downstream capsfilter with newly negotiated JPEG caps from appsink
 * pad
 *
 * @param pad Pad whose caps have changed
 * @param pspec GParamSpec for property change
 * @param user_data Pointer to capsfilter object that will be updated
 *
 */
void
on_appsink_jpeg_caps_notify (GObject * pad, GParamSpec * pspec,
  gpointer user_data)
{
  (void) pspec;

  GstElement * capsfilter = GST_ELEMENT (user_data);
  GstCaps * caps = NULL;

  g_object_get (pad, "caps", &caps, NULL);

  if (!caps)
    return;

  g_print ("Got negotiated caps from appsink: %" GST_PTR_FORMAT "\n", caps);
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);
}

/**
 * A callback function that is called when a new sample enters appsink
 *
 * @param appsink A pointer to the appsink element
 * @param user_data A pointer to the Application Context object
 *
 */

GstFlowReturn
appsink_jpeg_callback (GstElement * appsink, gpointer user_data)
{
  GstSample * sample = NULL;
  GstBuffer * buffer = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  VLMAppContext * appctx = (VLMAppContext *) user_data;
  guint pipeline_idx;
  const gchar * appsink_name;

  g_signal_emit_by_name (appsink, "pull-sample", &sample, &ret);
  if (ret != GST_FLOW_OK) {
    g_printerr ("ERROR: Cannot pull GstSample\n");
    return GST_FLOW_ERROR;
  }

  if (sample) {
    buffer = gst_sample_get_buffer (sample);
    if (buffer == NULL) {
      g_printerr ("ERROR: Cannot get buffer from sample\n");
    }

    appsink_name = gst_element_get_name (appsink);

    if (!extract_index_from_name (appsink_name, &pipeline_idx)) {
      g_printerr ("ERROR: Could not get index of appsink\n");
      return GST_FLOW_OK;
    }

    g_mutex_lock (&appctx->mutex);

    if (appctx->buffer_bundles[pipeline_idx].jpeg_buffer == NULL) {
      // Increase ref of the buffer and release the sample
      // Use the buffer for the next plugin
      gst_buffer_ref (buffer);

      appctx->buffer_bundles[pipeline_idx].appsrc_jpeg =
        appctx->appsrces_jpeg[pipeline_idx];
      appctx->buffer_bundles[pipeline_idx].jpeg_buffer = buffer;
    }

    gst_sample_release (sample);

    if (appctx->buffer_bundles[pipeline_idx].vlm_buffer != NULL) {
      // Call procesing
      process_vlm (appctx, pipeline_idx);
    }

    g_mutex_unlock (&appctx->mutex);
  }
  return GST_FLOW_OK;
}

/**
 * Checks all configured scene-detection streams for visual changes and
 * schedules processing
 *
 * @param appctx A pointer to the Application Context object
 *
 */
void
scene_change_detection (VLMAppContext * appctx)
{
  GstBuffer * buffer = NULL;

  for (auto & idx : appctx->sd_streams) {
    buffer = appctx->sd_buffers[idx];

    GstVideoMeta * vmeta = gst_buffer_get_video_meta (buffer);
    if (!vmeta) {
      g_printerr ("ERROR: Cannot get VideoMeta\n");
      continue;
    }
    gint width = (gint) vmeta->width;
    gint height = (gint) vmeta->height;
    gint stride = (gint) vmeta->stride[0];
    GstMapInfo map;

    if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
      g_printerr ("ERROR: Cannot map the buffer\n");
      continue;
    }

    gboolean changed = scene_det_process_gray8 (&appctx->sd[idx], map.data,
      stride, width, height);

    if (changed) {
      scheduler_push (appctx, idx);
      g_print ("Scene change detected for stream-%d, push to "
        "front of the scheduler\n",
        idx);

      if (appctx->data_channels[idx] != NULL)
        dc_send_status (appctx->data_channels[idx], "Scene Change...");
    }

    gst_buffer_unmap (buffer, &map);
  }
}

/**
 * A callback function that is called when a new sample enters appsink
 *
 * @param appsink A pointer to the appsink element
 * @param user_data A pointer to the Application Context object
 *
 */
GstFlowReturn
appsink_scene_callback (GstElement * appsink, gpointer user_data)
{
  GstSample * sample = NULL;
  GstBuffer * buffer = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  VLMAppContext * appctx = (VLMAppContext *) user_data;
  guint pipeline_idx;
  const gchar * appsink_name;

  g_signal_emit_by_name (appsink, "pull-sample", &sample, &ret);
  if (ret != GST_FLOW_OK) {
    g_printerr ("ERROR: Cannot pull GstSample\n");
    return GST_FLOW_ERROR;
  }

  if (sample) {
    buffer = gst_sample_get_buffer (sample);
    if (buffer == NULL) {
      g_printerr ("ERROR: Cannot get buffer from sample\n");
    }

    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
      gst_sample_release (sample);
      return GST_FLOW_OK;
    }

    appsink_name = gst_element_get_name (appsink);

    if (!extract_index_from_name (appsink_name, &pipeline_idx)) {
      g_printerr ("ERROR: Could not get index of appsink\n");
      return GST_FLOW_OK;
    }

    g_mutex_lock (&appctx->mutex);

    if (appctx->sd_buffers.count (pipeline_idx) == 0) {
      appctx->sd_buffers[pipeline_idx] = gst_buffer_ref (buffer);
    }

    if (appctx->sd_buffers.size () == appctx->sd_streams.size ()) {
      g_print ("Scene change: All pipelines have buffers\n");
      scene_change_detection (appctx);

      for (auto & kv : appctx->sd_buffers) {
        gst_buffer_unref (kv.second);
      }
      appctx->sd_buffers.clear ();
    }

    gst_sample_release (sample);

    g_mutex_unlock (&appctx->mutex);
  }
  return GST_FLOW_OK;
}

/**
 * Append formatted chunk to buffer safely
 *
 * @param dst Destination buffer
 * @param dst_sz Size of buffer in bytes
 * @param fmt Printf-style format
 *
 */
static void
appendf (gchar * dst, gsize dst_sz, const gchar * fmt, ...)
{
  va_list ap;
  gchar tmp[4096];

  va_start (ap, fmt);
  g_vsnprintf (tmp, sizeof (tmp), fmt, ap);
  va_end (ap);

  g_strlcat (dst, tmp, dst_sz);
}

/**
 * A function to create a VLM pipeline:
 *
 * @param appctx A pointer to the Application Context object
 * @param idx Stream index
 *
 */
static GstElement *
create_pipeline_vlm (VLMAppContext * appctx, guint idx)
{
  GstElement * pipeline = NULL;
  StreamConfig config = appctx->stream_configs[idx];
  std::string stream_type = config.type;
  std::string source_info = config.source_info;

  const gsize OUT_SZ = 1 << 16; /* 65536 */
  gchar * out = g_new0 (gchar, OUT_SZ);

  std::string dec_str =
    "v4l2h264dec "
      "capture-io-mode=dmabuf "
      "output-io-mode=dmabuf ! "
    "video/x-raw,format=NV12 ! ";

  std::string enc_str =
    "v4l2h264enc "
      "capture-io-mode=dmabuf "
      "output-io-mode=dmabuf "
      "extra-controls=\"controls,video_bitrate=2000000,video_bitrate_mode=0;\" ! ";

  if (stream_type == "file_SW" || stream_type == "rtsp_SW")
    dec_str =
        "avdec_h264 ! qtivtransform engine=ocv ! video/x-raw,format=NV12 ! ";

  /* -------- Source part (IF/ELSE) -------- */
  if (stream_type == "file" || stream_type == "file_SW") {
    appendf (out, OUT_SZ,
      "multifilesrc location=\"%s\" loop=true ! "
      "tsdemux ! video/x-h264,framerate=30/1 ! queue ! "
      "h264parse ! "
      "%s"
      "qtisync ! "
      "queue ! "
      "tee name=tee0-%u ",
      source_info.c_str(), dec_str.c_str(), idx
    );
  } else if (stream_type == "rtsp" || stream_type == "rtsp_SW") {
    appendf (out, OUT_SZ,
      "rtspsrc name=rtspsrc-%u latency=100 location=\"%s\" "
      "rtspsrc-%u. ! queue ! "
      "rtph264depay ! "
      "h264parse ! "
      "%s"
      "queue ! "
      "tee name=tee0-%u ",
      idx, source_info.c_str(), idx, dec_str.c_str(), idx
    );
  } else if (stream_type == "csi") {
    /* source_info: "primary" or "secondary" */
    const guint cam = source_info == "secondary" ? 1 : 0;

    appendf (out, OUT_SZ,
      "qtiqmmfsrc camera=%d ! "
      "video/x-raw,format=NV12,width=%d,height=%d,framerate=30/1 ! "
      "queue ! "
      "tee name=tee0-%u ",
      cam,
      CAMERA_PREVIEW_OUTPUT_WIDTH, CAMERA_PREVIEW_OUTPUT_HEIGHT,
      idx
    );
  } else {
    /* Unknown type */
    g_free (out);
    return NULL;
  }

  /* -------- source -> tee0 -> videorate -> tee1 -------- */
  appendf (out, OUT_SZ,
    "tee0-%u. ! queue ! "
    "videorate drop-only=true ! video/x-raw,framerate=1/%d ! queue ! "
    "queue ! "
    "tee name=tee1-%u ",
    idx,
    (guint) (appctx->process_interval_ms / 1000),
    idx
  );

  /* -------- video path to webrtcbin -------- */
  appendf (out, OUT_SZ,
    "tee0-%u. ! queue ! "
    "%s"
    "queue ! "
    "h264parse config-interval=-1 ! "
    "rtph264pay pt=96 config-interval=-1 ! "
    "application/x-rtp,media=video,payload=96 ! "
    "queue ! "
    "webrtcbin latency=100 name=webrtcbin-%u "
      "stun-server=stun://stun1.l.google.com:19302 "
      "bundle-policy=3 ",
    idx, enc_str.c_str(), idx
  );

  /* -------- VLM tensor branch to appsink_vlm --------
     NOTE: In your code ml_post_caps has tensor dimensions set via GstStructure.
     In parse_launch we can set only format/type easily; dimensions are tricky
     as string. Keep minimal caps same as you used (format/type). */
  appendf (out, OUT_SZ,
    "tee1-%u. ! "
    "qtimlvconverter image-disposition=centre ! "
    "neural-network/tensors,dimensions=<<1,3,%u,%u>>,type=FLOAT32 ! "
    "queue ! "
    "appsink name=appsink_vlm-%u "
      "sync=false "
      "emit-signals=true "
      "async=false "
      "enable-last-sample=false ",
    idx,
    MLVCONV_HEIGHT,
    MLVCONV_WIDTH,
    idx
  );

  /* -------- JPEG branch to appsink_jpeg -------- */
  appendf (out, OUT_SZ,
    "tee1-%u. ! queue ! "
    "jpegenc ! "
    "appsink name=appsink_jpeg-%u "
      "sync=false "
      "emit-signals=true "
      "async=false "
      "enable-last-sample=false ",
    idx,
    idx
  );

  if (appctx->scene_change_detection) {
    /* -------- Scene detection branch to appsink_scene -------- */
    appendf (out, OUT_SZ,
      "tee1-%u. ! queue ! "
      "qtivtransform ! video/x-raw,width=320,height=180,format=GRAY8 ! "
      "appsink name=appsink_scene-%u "
        "sync=false "
        "emit-signals=true "
        "async=false "
        "enable-last-sample=false ",
      idx,
      idx
    );
  }

  /* -------- appsrc_vlm + appsrc_caps -> qtimetamux_vlm -------- */
  appendf (out, OUT_SZ,
    "appsrc name=appsrc_vlm-%u ! "
    "text/x-raw,format=utf8 ! "
    "qtimetamux name=qtimetamux_vlm-%u ",
    idx, idx
  );

  /* -------- appsrc_jpeg + caps -> same qtimetamux_vlm -------- */
  appendf (out, OUT_SZ,
    "appsrc name=appsrc_jpeg-%u ! "
    "capsfilter name=appsrc_jpeg_caps-%u caps=image/jpeg ! "
    "qtimetamux_vlm-%u. ",
    idx, idx, idx
  );

  /* -------- metamux -> metaparser -> meta_sink -------- */
  appendf (out, OUT_SZ,
    "qtimetamux_vlm-%u. ! "
    "qtimlmetaparser module=json module-params=\"params,attach-frame=%s\" ! "
    "appsink name=meta_sink-%u "
      "sync=false "
      "emit-signals=true "
      "async=false "
      "enable-last-sample=false",
    idx,
    appctx->attach_jpeg ? "true" : "false",
    idx
  );

  g_print ("Pipeline %u (VLM):\n%s\n", idx, out);

  pipeline = gst_parse_launch (out, NULL);

  g_free (out);

  return pipeline;
}

/**
 * A function to create a detection pipeline:
 *
 * @param appctx A pointer to the Application Context object
 * @param idx Stream index
 *
 */
static GstElement *
create_pipeline_det (VLMAppContext * appctx, guint idx)
{
  GstElement * pipeline = NULL;
  StreamConfig config = appctx->stream_configs[idx];
  std::string stream_type = config.type;
  std::string source_info = config.source_info;

  const gsize OUT_SZ = 1 << 16; /* 65536 */
  gchar * out = g_new0 (gchar, OUT_SZ);

  std::string dec_str =
    "v4l2h264dec "
      "capture-io-mode=dmabuf "
      "output-io-mode=dmabuf ! "
    "video/x-raw,format=NV12 ! ";

  std::string enc_str =
    "v4l2h264enc "
      "capture-io-mode=dmabuf "
      "output-io-mode=dmabuf "
      "extra-controls=\"controls,video_bitrate=2000000,video_bitrate_mode=0;\" ! ";

  if (stream_type == "file_SW" || stream_type == "rtsp_SW") {
    dec_str = "avdec_h264 ! qtivtransform engine=ocv ! video/x-raw,format=NV12 ! ";
  }

  /* -------- Source part (IF/ELSE) -------- */
  if (stream_type == "file" || stream_type == "file_SW") {
    appendf (out, OUT_SZ,
      "multifilesrc location=\"%s\" loop=true ! "
      "tsdemux ! video/x-h264,framerate=30/1 ! queue ! "
      "h264parse ! "
      "%s"
      "qtisync ! "
      "queue ! "
      "tee name=tee0-%u ",
      source_info.c_str (), dec_str.c_str (), idx
    );
  } else if (stream_type == "rtsp" || stream_type == "rtsp_SW") {
    appendf (out, OUT_SZ,
      "rtspsrc name=rtspsrc-%u latency=100 location=\"%s\" "
      "rtspsrc-%u. ! queue ! "
      "rtph264depay ! "
      "h264parse ! "
      "%s"
      "queue ! "
      "tee name=tee0-%u ",
      idx, source_info.c_str (), idx, dec_str.c_str (), idx
    );
  } else if (stream_type == "csi") {
    /* source_info: "primary" or "secondary" */
    const guint cam = source_info == "secondary" ? 1 : 0;

    appendf (out, OUT_SZ,
      "qtiqmmfsrc camera=%d ! "
      "video/x-raw,format=NV12,width=%d,height=%d,framerate=30/1 ! "
      "queue ! "
      "tee name=tee0-%u ",
      cam,
      CAMERA_PREVIEW_OUTPUT_WIDTH, CAMERA_PREVIEW_OUTPUT_HEIGHT,
      idx
    );
  } else {
    /* Unknown type */
    g_free (out);
    return NULL;
  }

  /* -------- DET chain -------- */
  // Note: qtivtransform is added as WA because videorate is modifying the
  // duration of the buffer which breaks the qtivcomposer
  appendf (out, OUT_SZ,
    "tee0-%u. ! qtivtransform ! queue ! qtivcomposer name=qtivcomposer-%u "
    "tee0-%u. ! "
    "videorate drop-only=true ! video/x-raw,framerate=5/1 ! queue ! "
    "qtimlvconverter ! "
    "queue ! "
    "qtimltflite "
      "model=\"%s\" delegate=external "
      "external-delegate-path=\"libQnnTFLiteDelegate.so\" "
      "external-delegate-options=\"QNNExternalDelegate,backend_type=htp,"
        "htp_device_id=(string)%d,htp_performance_mode=(string)2;\" ! "
    "queue ! "
    "qtimlpostprocess "
      "bbox-stabilization=true results=5 module=yolov8 labels=\"%s\" "
      "settings=\"{\\\"confidence\\\": %.3f}\" ! "
    "video/x-raw,width=640,height=360 ! "
    "queue ! "
    "qtivcomposer-%u. ",
    idx,
    idx,
    idx,
    DETECTION_MODEL, idx % 2,
    DETECTION_LABELS, DEFAULT_THRESHOLD_VALUE,
    idx
  );

  /* -------- video path to webrtcbin -------- */
  appendf (out, OUT_SZ,
    "qtivcomposer-%u. ! video/x-raw,format=NV12 ! queue ! "
    "%s"
    "queue ! "
    "h264parse config-interval=-1 ! "
    "rtph264pay pt=96 config-interval=-1 ! "
    "application/x-rtp,media=video,payload=96 ! "
    "queue ! "
    "webrtcbin latency=100 name=webrtcbin-%u "
      "stun-server=stun://stun1.l.google.com:19302 "
      "bundle-policy=3 ",
    idx, enc_str.c_str (), idx
  );

  g_print ("Pipeline %u (DET):\n%s\n", idx, out);

  pipeline = gst_parse_launch (out, NULL);

  g_free (out);

  return pipeline;
}

/**
 * Initialize app context, parse configs, create and run pipelines, set up
 * WebRTC and enters main event loop
 *
 * @param argc Number of command-line arguments
 * @param argv Array of command-line arguments
 *
 */
gint
main (gint argc, gchar * argv[])
{
  GstBus * bus = NULL;
  GMainLoop * mloop = NULL;
  GstElement * pipeline = NULL;
  VLMAppContext appctx = {};
  guint intrpt_watch_id = 0;
  gchar pipeline_name_buffer[CHAR_BUFFER_SIZE];

  // Setting default values for VLMAppContext
  appctx.logger_level = "none";
  appctx.profile_enabled = FALSE;
  appctx.attach_jpeg = FALSE;
  appctx.process_interval_ms = 3000;
  appctx.bbox_threshold = 8;
  appctx.bbox_margin = 1.0;
  appctx.scene_change_detection = FALSE;
  appctx.scene_change_detection_th = 0.07;

  // Set Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  appctx.exit = FALSE;
  appctx.config_file = NULL;
  appctx.thumb_every_n_frames = 1;
  appctx.max_b64 = 1000000;

  for (int i = 1; i < argc; ++i) {
    if (!g_strcmp0 (argv[i], "-c") || !g_strcmp0 (argv[i], "--config")) {
      if (i + 1 < argc) {
        appctx.config_file = argv[++i];
      } else {
        g_printerr ("Missing argument after %s\n", argv[i]);
        return -1;
      }
    }
  }

  if (!appctx.config_file || appctx.config_file[0] == '\0') {
    g_printerr ("ERROR: Config file must be provided\n");
    return -1;
  }

  if (parse_json (appctx.config_file, &appctx) != 0) {
    gst_app_context_free (&appctx);
    return -EINVAL;
  }

  g_print ("Using config file: %s\n", appctx.config_file);

  appctx.eos_flags = std::vector<gboolean> (appctx.num_streams, FALSE);
  appctx.appsrces_jpeg.resize (appctx.num_streams);
  appctx.pipelines.resize (appctx.num_streams);
  appctx.appsrces.resize (appctx.num_streams);
  appctx.webrtcs.resize (appctx.num_streams);
  appctx.meta_sinks.resize (appctx.num_streams);
  appctx.data_channels.resize (appctx.num_streams);
  appctx.data_channels.assign (appctx.data_channels.size (), NULL);
  appctx.buffer_bundles.resize (appctx.num_streams);

  appctx.sd.resize (appctx.num_streams);
  for (guint x = 0; x < appctx.num_streams; x++) {
    memset (&appctx.sd[x], 0, sizeof (SceneDet));

    appctx.sd[x].th = appctx.scene_change_detection_th;
    appctx.sd[x].cooldown_frames = 1;
  }

  appctx.dc_frame_ids.resize (appctx.num_streams, 0);
  appctx.dc_last_thumb_sent.resize (appctx.num_streams, 0);

  // Init scheduler
  appctx.sd_streams.clear ();
  for (guint x = 0; x < appctx.num_streams; x++) {
    if (appctx.stream_configs[x].pipeline_type == "vlm") {
      scheduler_push (&appctx, x);
      appctx.sd_streams.push_back (x);
    }
  }

  // VLM Init
  vlm_init (&appctx);

  if (!file_exists (DETECTION_MODEL)) {
    g_printerr ("Invalid model file path: %s\n", DETECTION_MODEL);
    gst_app_context_free (&appctx);
    return -EINVAL;
  }

  if (!file_exists (DETECTION_LABELS)) {
    g_printerr ("Invalid labels file path: %s\n", DETECTION_LABELS);
    gst_app_context_free (&appctx);
    return -EINVAL;
  }

  g_print ("Running app with model: %s and labels: %s\n", DETECTION_MODEL,
    DETECTION_LABELS);

  // Initialize GST library.
  gst_init (&argc, &argv);

  while (TRUE) {
    for (guint i = 0; i < appctx.num_streams; i++) {
      const char * source_info = appctx.stream_configs[i].source_info.c_str ();
      gchar name[CHAR_BUFFER_SIZE];
      GstElement *elem = NULL, *elem2 = NULL;
      GstPad * sinkpad;

      if (appctx.stream_configs[i].type == "file" &&
          !file_exists (source_info)) {
        g_printerr ("Invalid file source path: %s\n", source_info);
        gst_app_context_free (&appctx);
        return -EINVAL;
      }

      // Create the pipeline that will form connection with other elements
      g_snprintf (pipeline_name_buffer, sizeof (pipeline_name_buffer),
        "pipeline-%u", i);

      if (appctx.stream_configs[i].pipeline_type == "vlm") {
        pipeline = create_pipeline_vlm (&appctx, i);
      } else if (appctx.stream_configs[i].pipeline_type == "det") {
        pipeline = create_pipeline_det (&appctx, i);
      }

      if (!pipeline) {
        g_printerr ("ERROR: failed to create pipeline.\n");
        gst_app_context_free (&appctx);
        return -EINVAL;
      } else
        g_print ("Created pipeline %s\n", pipeline_name_buffer);

      snprintf (name, 127, "webrtcbin-%u", i);
      appctx.webrtcs[i] = gst_bin_get_by_name (GST_BIN (pipeline), name);

      if (appctx.stream_configs[i].pipeline_type == "vlm") {
        // Add appsrc_vlm to vector of appsrc_vlm elements, for easier lookup
        // during stream
        snprintf (name, 127, "appsrc_vlm-%u", i);
        appctx.appsrces[i] = gst_bin_get_by_name (GST_BIN (pipeline), name);
        snprintf (name, 127, "appsrc_jpeg-%u", i);
        appctx.appsrces_jpeg[i] =
            gst_bin_get_by_name (GST_BIN (pipeline), name);
        snprintf (name, 127, "meta_sink-%u", i);
        appctx.meta_sinks[i] = gst_bin_get_by_name (GST_BIN (pipeline), name);

        MetaSinkUserData * msud = g_new0 (MetaSinkUserData, 1);
        msud->appctx = &appctx;
        msud->stream_idx = i;
        g_signal_connect_data (appctx.meta_sinks[i], "new-sample",
          G_CALLBACK (on_meta_new_sample), msud,
          (GClosureNotify) destroy_msud, (GConnectFlags) 0);

        snprintf (name, 127, "appsink_vlm-%u", i);
        elem = gst_bin_get_by_name (GST_BIN (pipeline), name);
        g_signal_connect (G_OBJECT (elem), "new-sample",
          G_CALLBACK (appsink_vlm_callback), &appctx);
        gst_object_unref (elem);

        snprintf (name, 127, "appsink_jpeg-%u", i);
        elem = gst_bin_get_by_name (GST_BIN (pipeline), name);
        g_signal_connect (G_OBJECT (elem), "new-sample",
G_CALLBACK (appsink_jpeg_callback), &appctx);
        // Keep elem referenced until we query its pad below.

        snprintf (name, 127, "appsrc_jpeg_caps-%u", i);
        elem2 = gst_bin_get_by_name (GST_BIN (pipeline), name);
        sinkpad = gst_element_get_static_pad (elem, "sink");
        g_signal_connect (sinkpad, "notify::caps",
          G_CALLBACK (on_appsink_jpeg_caps_notify), elem2);

        gst_object_unref (sinkpad);
        gst_object_unref (elem2);
        gst_object_unref (elem);

        if (appctx.scene_change_detection) {
          snprintf (name, 127, "appsink_scene-%u", i);
          elem = gst_bin_get_by_name (GST_BIN (pipeline), name);
          g_signal_connect (G_OBJECT (elem), "new-sample",
            G_CALLBACK (appsink_scene_callback), &appctx);
          gst_object_unref (elem);
        }
      }

      // Retrieve reference to the pipeline's bus.
      // Bus is message queue for getting callback from gstreamer pipeline
      if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
        g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
        gst_app_context_free (&appctx);
        return -EINVAL;
      }

      // Watch for messages on the pipeline's bus.
      gst_bus_add_signal_watch (bus);

      // Register respective callback function based on message
      g_signal_connect (bus, "message::state-changed",
        G_CALLBACK (state_changed_cb), pipeline);

      g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), &appctx);
      g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb),
        mloop);

      g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), &appctx);
      gst_object_unref (bus);
      g_print ("Callbacks set\n");

      appctx.pipelines[i] = pipeline;
    }

    // --- WebRTC signalling + negotiation (stream 0 by default) ---
    // Note: This app uses the same websocket signalling protocol as
    // webrtc_sendrecv.py. Start your signalling server (e.g. the python one)
    // and set env vars if needed:
    //   WEBRTC_SERVER=ws://127.0.0.1:8443 WEBRTC_ID=1 WEBRTC_PEER=0
    if (!webrtc_connect_signalling (&appctx)) {
      g_printerr ("[webrtc] WARNING: signalling connection failed; "
        "WebRTC will not negotiate.");
    }

    for (guint i = 0; i < appctx.num_streams; i++) {
      if (!appctx.webrtcs[i])
        continue;

      g_signal_connect (appctx.webrtcs[i], "on-negotiation-needed",
        G_CALLBACK (on_negotiation_needed), &appctx);
      g_signal_connect (appctx.webrtcs[i], "on-ice-candidate",
        G_CALLBACK (on_webrtc_ice_candidate), &appctx);
    }

    // Initialize main loop.
    if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
      g_printerr ("ERROR: Failed to create Main loop!\n");
      gst_app_context_free (&appctx);
      return -EINVAL;
    }

    appctx.mloop = mloop;

    g_mutex_init (&appctx.mutex);

    // Register function for handling interrupt signals with the main loop.
    intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &appctx);

    for (guint i = 0; i < appctx.num_streams; i++) {
      // On successful transition to PAUSED state, state_changed_cb is called.
      // state_changed_cb callback is used to send pipeline to play state.
      g_print ("Set pipeline to PAUSED state ...\n");
      switch (gst_element_set_state (appctx.pipelines[i], GST_STATE_READY)) {
      case GST_STATE_CHANGE_FAILURE:
        g_printerr ("ERROR: Failed to transition to PAUSED state!\n");

        g_mutex_lock (&appctx.mutex);
        appctx.exit = TRUE;
        g_mutex_unlock (&appctx.mutex);

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
    }

    // Wait till pipeline encounters an error or EOS
    g_print ("g_main_loop_run\n");
    g_main_loop_run (mloop);
    g_print ("g_main_loop_run ends\n");

  error:
    // Remove the interrupt signal handler
    if (intrpt_watch_id)
      g_source_remove (intrpt_watch_id);

    for (guint i = 0; i < appctx.num_streams; i++) {
      g_print ("Set pipeline to NULL state ...\n");
      if (!gst_element_set_state (appctx.pipelines[i], GST_STATE_NULL))
        g_printerr ("WARNING: Failed to set pipeline to NULL state\n");
    }

    webrtc_disconnect_signalling (&appctx);

    g_print ("Destroy pipeline\n");
    gst_app_context_free (&appctx);

    g_mutex_lock (&appctx.mutex);
    if (appctx.exit) {
      g_mutex_unlock (&appctx.mutex);
      break;
    }
    g_mutex_unlock (&appctx.mutex);
  }

  // VLM Init
  vlm_deinit (&appctx);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
