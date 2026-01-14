/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
 * GStreamer Application:
 * GStreamer Application for Demonstrating Pre-Buffering and Live Recording
 *
 * Description:
 * This application demonstrates a use case where video frames are pre-buffered
 * before recording starts, ensuring that the final video includes content from
 * a few seconds before the recording trigger.
 *
 * Features:
 *   -- Pre-buffer frames from camera using appsink
 *   -- Push pre-buffered frames to appsrc pipeline for encoding
 *   -- Smooth transition from pre-buffered content to live recording
 *
 * Usage:
 * gst-camera-prebuffered-data-app [OPTIONS]
 * Example:
 * gst-camera-prebuffered-data-app -c 0 -w 1920 -h 1080 -d 30 -r 30
 *
 * Options:
 * -c, --camera-id=id                    Camera ID
 * -h, --height=height                   Frame height
 * -w, --width=width                     Frame width
 * -d, --delay=delay                     Delay before recording starts (seconds)
 * -r, --record-duration=duration        Record duration after recording starts (seconds)
 * -q, --queue-size=size                 Max buffer queue size
 * -t, --tap-out=mode                    Tap out mode: 0 - Normal, 1 - RDI, 2 - IPE By Pass
 * -j, --snapshot-jpeg-width=width       Snapshot JPEG width
 * -k, --snapshot-jpeg-height=height     Snapshot JPEG height
 * -o, --raw-snapshot-width=width        Raw snapshot width
 * -s, --raw-snapshot-height=height      Raw snapshot height
 * -e, --enable-snapshot-streams         Enable snapshot streams
 * -n, --num-snapshots=count             Number of snapshots to capture
 * -y, --snapshot-type=type              Snapshot type: 0 - video,  1 - still
 * -m, --noise-reduction-mode=mode       Noise reduction mode: 0 - off,  1 - fast, 2 - high_quality
 * -x, --rdi-output-width=width          RDI output width (for reprocessing)
 * -z, --rdi-output-height=height        RDI output height (for reprocessing)
 *
 * Help:
 * gst-camera-prebuffered-data-app --help
 *
 * *******************************************************************************
 * Pipeline for Pre-buffering and Recording:
 * Main Pipeline:
 *   qtiqmmfsrc -> capsfilter -> appsink (for prebuffering)
 *   qtiqmmfsrc -> capsfilter -> encoder -> h264parse -> mp4mux -> filesink (for live data)
 * Appsrc Pipeline:
 *   appsrc -> queue -> encoder -> h264parse -> mp4mux -> filesink
 * *******************************************************************************
 */

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/base/gstdataqueue.h>
#include <pthread.h>
#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

#define MAX_QUEUE_SIZE           300
#define OUTPUT_WIDTH             1920
#define OUTPUT_HEIGHT            1080
#define DELAY_TO_START_RECORDING 30
#define RECORD_DURATION          30
#define JPEG_SNAPHOT_WIDTH       1920
#define JPEG_SNAPHOT_HEIGHT      1080
#define RAW_SNAPHOT_WIDTH        1920
#define RAW_SNAPHOT_HEIGHT       1080

#define CAMERA_SESSION_TAG "org.codeaurora.qcamera3.sessionParameters.DynamicTapOut"

typedef struct _GstAppContext GstAppContext;
typedef struct _GstStreamInf GstStreamInf;

typedef enum {
  GST_TAPOUT_NORMAL,
  GST_TAPOUT_RDI,
  GST_TAPOUT_IPEBYPASS
} GstDynamicTapOut;

typedef enum {
  GST_STREAM_TYPE_ENCODER_BUFFERING,
  GST_STREAM_TYPE_DUMMY_ENCODER,
  GST_STREAM_TYPE_APPSINK,
  GST_STREAM_TYPE_JPEG,
  GST_STREAM_TYPE_RAW
} GstStreamInfo;

// Stream information
struct _GstStreamInf {
  GstElement *capsfilter;
  GstElement *waylandsink;
  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;
  GstElement *appsink;
  GstPad *qmmf_pad;
  GstCaps *qmmf_caps;
  gint width;
  gint height;
  gboolean is_dummy;
  gboolean is_encoder;
  gboolean is_jpeg_snapshot;
  gboolean is_raw_snapshot;
};

// Contains app context information
struct _GstAppContext {
  // Pointer to the main pipeline
  GstElement *main_pipeline;

  // Pointer to the appsrc pipeline and components
  GstElement *appsrc_pipeline;
  GstElement *appsrc;
  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;
  GstElement *queue;
  GstElement *camimgreproc;
  GstElement *capsfilter;
  // Pointer to the mainloop
  GMainLoop *mloop;
  // Queue to store pre buffered data
  GQueue *buffers_queue;
  // Camera ID
  guint camera_id;
  // Height
  guint height;
  //Width
  guint width;
  // Wait for time before recording starts
  guint delay_to_start_recording;
  // Live record duration
  guint record_duration;
  // Max queue size
  guint queue_size;
  // Buffering mode
  GstDynamicTapOut mode;
  // List with all streams
  GList *streams_list;
  // Stream count
  gint stream_cnt;
  // Stream count
  GMutex lock;
  // Exit thread flag
  gboolean exit;
  // EOS signal
  GCond eos_signal;
  // First live frame PTS
  GstClockTime first_live_pts;
  // Switch to live stream
  gboolean switch_to_live;
  // Live PTS arrived signal
  GCond live_pts_signal;
  // Source ID
  guint process_src_id;
  // Encoder Name
  gchar *encoder_name;
  // Snapshot Streams configs
  gint jpeg_snapshot_width;
  gint jpeg_snapshot_height;
  gint raw_snapshot_width;
  gint raw_snapshot_height;
  gint snapshot_type;
  gint noise_reduction_mode;
  gint num_snapshots;
  gboolean enable_snapshot_streams;
  // Metadata to capture image
  GPtrArray *meta_capture;
  // RDI output resolution
  guint rdi_output_width;
  guint rdi_output_height;
  // Selected usecase
  void (*usecase_fn) (GstAppContext * appctx);
};

// Forward Declaration
void release_stream (GstAppContext * appctx, GstStreamInf * stream);

static void
exit_cleanup(GstAppContext * appctx)
{
  g_print ("[INFO] Exit requested during prebuffering delay\n");
  g_print ("[INFO] Transitioning main pipeline to NULL state\n");
  gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->main_pipeline, NULL, NULL,
      GST_CLOCK_TIME_NONE);

  g_print ("[INFO] Transitioning appsrc pipeline to NULL state\n");
  gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->appsrc_pipeline, NULL, NULL,
      GST_CLOCK_TIME_NONE);
}

static void
gst_camera_metadata_release (gpointer data)
{
  ::camera::CameraMetadata *meta = (::camera::CameraMetadata*) data;
  delete meta;
}

static gboolean
trigger_snapshot (GstAppContext * appctx)
{
  gboolean success = FALSE;
  GstElement *qtiqmmfsrc = nullptr;

  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return FALSE;
  }

  g_print ("[INFO] Triggering snapshot capture (mode: %s, count: %u)...\n",
      appctx->snapshot_type == 0 ? "VIDEO" : "STILL", appctx->num_snapshots);

  // Emit capture-image signal
  g_signal_emit_by_name (qtiqmmfsrc, "capture-image",
      appctx->snapshot_type, appctx->num_snapshots,
      appctx->meta_capture, &success);

  if (success)
    g_print ("[INFO] Snapshot capture triggered successfully\n");
  else
    g_printerr ("[ERROR] Failed to trigger snapshot capture\n");

  return success;
}

static gboolean
capture_prepare_metadata (GstAppContext * appctx)
{
  ::camera::CameraMetadata *meta = nullptr;
  ::camera::CameraMetadata *metadata = nullptr;
  guchar afmode = 0;
  guchar noisemode = 0;
  GstElement *qtiqmmfsrc = nullptr;

  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return FALSE;
  }

  /* Get high quality metadata, which will be used for submitting capture-image. */
  g_object_get (G_OBJECT (qtiqmmfsrc), "image-metadata", &meta, NULL);
  if (!meta) {
    g_printerr ("failed to get image metadata\n");
    goto cleanupset;
  }

  /* Remove last metadata saved in gmetas. */
  if (appctx->meta_capture->len > 0)
    g_ptr_array_remove_range (appctx->meta_capture, 0, appctx->meta_capture->len);

  /*
   * Capture burst of images with metadata.
   * Modify a copy of the capture metadata and add it to the meta array.
   */
  metadata = new ::camera::CameraMetadata (*meta);

  /* Set OFF focus mode and ensure noise mode is not high quality. */
  afmode = ANDROID_CONTROL_AF_MODE_OFF;
  metadata->update (ANDROID_CONTROL_AF_MODE, &afmode, 1);

  switch (appctx->noise_reduction_mode) {
    case 0:
      noisemode = ANDROID_NOISE_REDUCTION_MODE_OFF;
      break;

    case 1:
      noisemode = ANDROID_NOISE_REDUCTION_MODE_FAST;
      break;

    case 2:
      noisemode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
      break;

    default:
      break;
  }

  metadata->update (ANDROID_NOISE_REDUCTION_MODE, &noisemode, 1);

  g_ptr_array_add (appctx->meta_capture, (gpointer) metadata);

  if (qtiqmmfsrc)
    gst_object_unref (qtiqmmfsrc);

  return TRUE;

cleanupset:
  if (qtiqmmfsrc)
    gst_object_unref (qtiqmmfsrc);
  if (meta)
    delete meta;
  if (metadata)
    delete metadata;

  return FALSE;
}

static GstCaps *
create_stream_caps (gint width, gint height)
{
  GstCaps *filter_caps;

  // configure stream caps
  filter_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1, NULL);

  gst_caps_set_features (filter_caps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  return filter_caps;
}

static GstCaps *
create_bayer_caps (gint width, gint height)
{
  GstCaps *filter_caps;

  // Configure bayer capture caps
  filter_caps = gst_caps_new_simple ("video/x-bayer",
      "format", G_TYPE_STRING, "rggb",
      "bpp", G_TYPE_STRING, "10",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  return filter_caps;
}

static GstCaps *
create_jpeg_snapshot_caps (gint width, gint height)
{
  GstCaps *filter_caps;

  // Configure image capture caps
  filter_caps = gst_caps_new_simple ("image/jpeg",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  return filter_caps;
}

static gchar *
get_encoder_name ()
{
  if (gst_element_factory_find ("qtic2venc")) {
    g_print ("[INFO] Using qtic2venc encoder plugin\n");
    return "qtic2venc";
  } else if (gst_element_factory_find ("omxh264enc")) {
    g_print ("[INFO] Using omxh264enc encoder plugin\n");
    return "omxh264enc";
  } else {
    g_printerr ("[ERROR] No suitable encoder plugin found (qtic2venc or omxh264enc)\n");
    return NULL;
  }
}

static void
clear_buffers_queue (GstAppContext *appctx)
{
  if (!appctx || !appctx->buffers_queue)
    return;

  g_mutex_lock (&appctx->lock);

  while (!g_queue_is_empty (appctx->buffers_queue)) {
    GstBuffer *buffer = (GstBuffer *) g_queue_pop_head (appctx->buffers_queue);
    if (buffer)
      gst_buffer_unref (buffer);
  }

  g_mutex_unlock (&appctx->lock);

  g_print ("[INFO] Cleared buffer queue\n");
}

GstPadProbeReturn
live_frame_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstAppContext *ctx = (GstAppContext *) user_data;
  if (GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_BUFFER) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
    if (buffer && ctx->first_live_pts == GST_CLOCK_TIME_NONE) {
      ctx->first_live_pts = GST_BUFFER_PTS (buffer);
      g_cond_signal(&ctx->live_pts_signal);
      g_print ("[INFO] First live frame PTS: %" GST_TIME_FORMAT "\n",
          GST_TIME_ARGS (ctx->first_live_pts));
      return GST_PAD_PROBE_REMOVE;
    }
  }
  return GST_PAD_PROBE_OK;
}

GstFlowReturn
on_new_sample (GstAppSink *appsink, gpointer user_data)
{
  GstAppContext *ctx = (GstAppContext *) user_data;
  GstSample *sample = gst_app_sink_pull_sample (appsink);

  if (!sample)
    return GST_FLOW_ERROR;

  GstBuffer *buffer = gst_sample_get_buffer (sample);
  if (!buffer) {
    gst_sample_unref (sample);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&ctx->lock);

  if (g_queue_get_length (ctx->buffers_queue) >= ctx->queue_size) {
    GstBuffer *item = (GstBuffer *) g_queue_pop_head (ctx->buffers_queue);
    if (item)
      gst_buffer_unref (item);
  }

  if (!ctx->switch_to_live) {
    gst_buffer_ref (buffer);
    g_queue_push_tail (ctx->buffers_queue, buffer);
  }

  g_mutex_unlock (&ctx->lock);
  gst_sample_unref (sample);

  return GST_FLOW_OK;
}

static gboolean
check_for_exit (GstAppContext * appctx)
{
  g_mutex_lock (&appctx->lock);
  if (appctx->exit) {
    g_mutex_unlock (&appctx->lock);
    return TRUE;
  }
  g_mutex_unlock (&appctx->lock);
  return FALSE;
}

// Wait for end of streaming
static gboolean
wait_for_eos (GstAppContext * appctx)
{
  g_mutex_lock (&appctx->lock);
  gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (5000000);
  gboolean timeout = g_cond_wait_until (&appctx->eos_signal,
      &appctx->lock, wait_time);
  if (!timeout) {
    g_print ("[ERROR] Timeout on wait for eos\n");
    g_mutex_unlock (&appctx->lock);
    return FALSE;
  }
  g_mutex_unlock (&appctx->lock);
  return TRUE;
}

// Release all streams in the list
static void
release_all_streams (GstAppContext * appctx)
{
  GList *list = NULL;
  for (list = appctx->streams_list; list != NULL; list = list->next) {
    GstStreamInf *stream = (GstStreamInf *) list->data;
    release_stream (appctx, stream);
  }
}

// Handles interrupt signals like Ctrl+C etc.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;

  g_print ("\n[INFO] Received interrupt signal . . .\n");

  g_mutex_lock (&appctx->lock);
  if (appctx->exit) {
    g_mutex_unlock (&appctx->lock);
    return TRUE;
  }
  appctx->exit = TRUE;
  g_mutex_unlock (&appctx->lock);

  if (appctx->main_pipeline)
    gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);
  if (appctx->appsrc_pipeline)
    gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);

  // Clear any queued buffers
  if (appctx->buffers_queue) {
    g_print ("[INFO] Clearing buffer queue\n");
    clear_buffers_queue (appctx);
  }

  // Signal any waiting threads
  g_print ("[INFO] Signaling EOS condition to waiting threads\n");
  g_cond_signal (&appctx->eos_signal);

  if (appctx->mloop && g_main_loop_is_running (appctx->mloop)) {
    g_print ("[INFO] Quitting main loop\n");
    g_main_loop_quit (appctx->mloop);
  }

  g_print ("[INFO] Interrupt handling complete\n");
  return TRUE;
}

// Handles state change transisions
static void
state_changed_cb (GstBus *bus, GstMessage *message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the provided pipeline
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);

  const gchar *pipeline_name = gst_object_get_name (GST_OBJECT (pipeline));

  g_print ("\n[INFO] Pipeline '%s' state changed from %s to %s, pending: %s\n",
      pipeline_name,
      gst_element_state_get_name (old),
      gst_element_state_get_name (new_st),
      gst_element_state_get_name (pending));
}

// Handle warnings
static void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

// Handle errors
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop *) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (mloop);
}

// Error callback function
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  g_print ("\n[INFO] Received End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  g_mutex_lock (&appctx->lock);
  g_cond_signal (&appctx->eos_signal);
  g_mutex_unlock (&appctx->lock);

  if (check_for_exit (appctx))
    g_main_loop_quit (appctx->mloop);

}

static gboolean
create_snapshot_stream (GstAppContext * appctx, GstStreamInf * stream,
    GstElement * qtiqmmfsrc)
{
  gchar temp_str[100];
  gboolean ret = FALSE;
  const gchar *src_pad_name = NULL;

  /* Validate inputs early */
  if (appctx == NULL || stream == NULL || qtiqmmfsrc == NULL) {
    g_printerr ("[ERROR] Snapshot: invalid arguments (appctx/stream/src)\n");
    return FALSE;
  }
  if (stream->qmmf_caps == NULL) {
    g_printerr ("[ERROR] Snapshot: qmmf_caps is NULL\n");
    return FALSE;
  }
  if (stream->qmmf_pad == NULL) {
    g_printerr ("[ERROR] Snapshot: qmmf_pad is NULL\n");
    return FALSE;
  }

  /* create elements */
  /* Clear buffer before use (defensive) */
  temp_str[0] = '\0';
  g_snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  /* Clear buffer before next use */
  temp_str[0] = '\0';
  g_snprintf (temp_str, sizeof (temp_str), "snapshot_sink_%d",
      appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("multifilesink", temp_str);

  if (!stream->capsfilter || !stream->filesink) {
    if (stream->capsfilter)
      gst_object_unref (stream->capsfilter);
    if (stream->filesink)
      gst_object_unref (stream->filesink);
    g_printerr ("[ERROR] Snapshot elements could not be created\n");
    return FALSE;
  }

  /* set properties */
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  temp_str[0] = '\0';

  if (stream->is_jpeg_snapshot)
    g_snprintf(temp_str, sizeof(temp_str),
              "/data/snapshot_s%u-%%05d.jpg", appctx->stream_cnt);
  else
    g_snprintf(temp_str, sizeof(temp_str),
              "/data/snapshot_s%u-%%05d.raw", appctx->stream_cnt);

  g_object_set (G_OBJECT (stream->filesink),
      "location", temp_str,
      "post-messages", FALSE,
      "enable-last-sample", FALSE,
      "max-files", 10,
      "async", FALSE,
      NULL);

  /* add to bin */
  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  /* sync states with parent */
  if (!gst_element_sync_state_with_parent (stream->capsfilter)) {
    g_printerr ("[ERROR] Snapshot: capsfilter failed to sync state with parent\n");
    goto cleanup;
  }
  if (!gst_element_sync_state_with_parent (stream->filesink)) {
    g_printerr ("[ERROR] Snapshot: filesink failed to sync state with parent\n");
    goto cleanup;
  }

  /* link qmmfsrc -> capsfilter using explicit source pad name */
  src_pad_name = gst_pad_get_name (stream->qmmf_pad);
  if (src_pad_name == NULL) {
    g_printerr ("[ERROR] Snapshot: source pad name is NULL\n");
    goto cleanup;
  }

  ret = gst_element_link_pads_full (qtiqmmfsrc,
      src_pad_name,
      stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Snapshot: link qmmfsrc->capsfilter failed\n");
    goto cleanup;
  }

  /* capsfilter -> multifilesink */
  if (!gst_element_link_many (stream->capsfilter, stream->filesink, NULL)) {
    g_printerr ("[ERROR] Snapshot: link capsfilter->multifilesink failed\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  /* put elements to NULL and remove from bin */
  if (stream->capsfilter)
    gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  if (stream->filesink)
    gst_element_set_state (stream->filesink, GST_STATE_NULL);

  if (GST_IS_BIN (appctx->main_pipeline))
    gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
        stream->capsfilter, stream->filesink, NULL);

  /* Avoid dangling references after removal */
  stream->capsfilter = NULL;
  stream->filesink = NULL;

  return FALSE;
}

static void
release_snapshot_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  GstElement *qtiqmmfsrc = NULL;

  if (appctx == NULL || stream == NULL) {
    g_printerr ("[ERROR] Snapshot: invalid arguments (appctx/stream)\n");
    return;
  }

  /* Get qtiqmmfsrc instance */
  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (qtiqmmfsrc == NULL)
    g_printerr ("[ERROR] Snapshot: 'qmmf' element not found in bin\n");

  g_print ("[INFO] Unlinking elements for snapshot stream...\n");

  /* Unlink qmmf -> capsfilter if both exist */
  if (qtiqmmfsrc && stream->capsfilter)
    gst_element_unlink (qtiqmmfsrc, stream->capsfilter);

  /* Unlink capsfilter -> filesink if both exist */
  if (stream->capsfilter && stream->filesink)
    gst_element_unlink (stream->capsfilter, stream->filesink);

  g_print ("[INFO] Unlinked successfully for snapshot stream\n");

  /* Set elements to NULL state (if they exist) */
  if (stream->capsfilter) {
    gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
    gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  }

  if (stream->filesink) {
    gst_element_set_state (stream->filesink, GST_STATE_NULL);
    gst_element_get_state (stream->filesink, NULL, NULL, GST_CLOCK_TIME_NONE);
  }

  /* Remove the elements from the main_pipeline */
  if (GST_IS_BIN (appctx->main_pipeline)) {
    if (stream->capsfilter || stream->filesink) {
      gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
          stream->capsfilter, stream->filesink, NULL);
    }
  }

  /* Clear pointers after removal */
  stream->capsfilter = NULL;
  stream->filesink = NULL;

  if (qtiqmmfsrc)
    gst_object_unref (qtiqmmfsrc);
}

static gboolean
create_encoder_stream (GstAppContext * appctx, GstStreamInf * stream,
    GstElement * qtiqmmfsrc)
{
  static guint output_cnt = 0;
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "encoder_%d", appctx->stream_cnt);
  stream->encoder = gst_element_factory_make(appctx->encoder_name, temp_str);

  snprintf (temp_str, sizeof (temp_str), "filesink_%d", appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("filesink", temp_str);

  snprintf (temp_str, sizeof (temp_str), "h264parse_%d", appctx->stream_cnt);
  stream->h264parse = gst_element_factory_make ("h264parse", temp_str);

  snprintf (temp_str, sizeof (temp_str), "mp4mux_%d", appctx->stream_cnt);
  stream->mp4mux = gst_element_factory_make ("mp4mux", temp_str);

  if (!stream->capsfilter || !stream->encoder || !stream->filesink ||
      !stream->h264parse || !stream->mp4mux) {
    if (stream->capsfilter)
      gst_object_unref (stream->capsfilter);
    if (stream->encoder)
      gst_object_unref (stream->encoder);
    if (stream->filesink)
      gst_object_unref (stream->filesink);
    if (stream->h264parse)
      gst_object_unref (stream->h264parse);
    if (stream->mp4mux)
      gst_object_unref (stream->mp4mux);
    g_printerr ("One element could not be created of found. Exiting.\n");
    return FALSE;
  }
  // Set caps the the caps filter
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (stream->encoder), "target-bitrate", 6000000, NULL);
  if (g_strcmp0 (appctx->encoder_name, "qtic2venc") == 0)
    g_object_set (G_OBJECT (stream->encoder), "control-rate", 3, NULL); // VBR-CFR
  else {
    g_object_set (G_OBJECT (stream->encoder), "periodicity-idr", 1, NULL);
    g_object_set (G_OBJECT (stream->encoder), "interval-intraframes", 29, NULL);
    g_object_set (G_OBJECT (stream->encoder), "control-rate", 2, NULL);
  }

  // Set mp4mux in robust mode
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-moov-update-period",
      1000000, NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-bytes-per-sec", 10000,
      NULL);
  g_object_set (G_OBJECT (stream->mp4mux), "reserved-max-duration", 8000000000,
      NULL);

  snprintf (temp_str, sizeof (temp_str), "/data/video_live_data_%d.mp4",
      output_cnt++);
  g_object_set (G_OBJECT (stream->filesink), "location", temp_str, NULL);

  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  // Sync the elements state to the curtent main_pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->encoder);
  gst_element_sync_state_with_parent (stream->h264parse);
  gst_element_sync_state_with_parent (stream->mp4mux);
  gst_element_sync_state_with_parent (stream->filesink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (qtiqmmfsrc,
      gst_pad_get_name (stream->qmmf_pad), stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Link cannot be done!\n");
    goto cleanup;
  }
  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->encoder,
          stream->h264parse, stream->mp4mux, stream->filesink, NULL)) {
    g_printerr ("[ERROR] Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->encoder, GST_STATE_NULL);
  gst_element_set_state (stream->h264parse, GST_STATE_NULL);
  gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);

  // Remove the elements from the main_pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  return FALSE;
}

static void
release_encoder_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  GstState state = GST_STATE_VOID_PENDING;
  GstElement *qtiqmmfsrc = NULL;

  // Get qtiqmmfsrc instance
  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");

  // Unlink the elements of this stream
  g_print ("[INFO] Unlinking elements for encoder stream...\n");
  gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter, NULL);

  gst_element_get_state (appctx->main_pipeline, &state, NULL,
      GST_CLOCK_TIME_NONE);
  if (state == GST_STATE_PLAYING)
    gst_element_send_event (stream->encoder, gst_event_new_eos ());

  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->encoder, GST_STATE_NULL);
  gst_element_get_state (stream->encoder, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->h264parse, GST_STATE_NULL);
  gst_element_get_state (stream->h264parse, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->mp4mux, GST_STATE_NULL);
  gst_element_get_state (stream->mp4mux, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);
  gst_element_get_state (stream->filesink, NULL, NULL, GST_CLOCK_TIME_NONE);

  // Unlink the elements of this stream
  gst_element_unlink_many (stream->capsfilter, stream->encoder,
      stream->h264parse, stream->mp4mux, stream->filesink, NULL);
  g_print ("[INFO] Unlinked successfully for encoder stream \n");

  // Remove the elements from the main_pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  stream->capsfilter = NULL;
  stream->encoder = NULL;
  stream->h264parse = NULL;
  stream->mp4mux = NULL;
  stream->filesink = NULL;

  gst_object_unref (qtiqmmfsrc);
}

static gboolean
create_appsink_stream (GstAppContext * appctx, GstStreamInf * stream,
    GstElement * qtiqmmfsrc)
{
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "appsink_%d", appctx->stream_cnt);
  stream->appsink = gst_element_factory_make ("appsink", temp_str);

  // Check if all elements are created successfully
  if (!stream->capsfilter || !stream->appsink) {
    if (stream->capsfilter)
      gst_object_unref (stream->capsfilter);
    if (stream->appsink)
      gst_object_unref (stream->appsink);
    g_printerr ("[ERROR] One element could not be created of found. Exiting.\n");
    return FALSE;
  }
  // Set caps the the caps filter
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);
  gst_app_sink_set_emit_signals (GST_APP_SINK (stream->appsink), TRUE);
  g_signal_connect (stream->appsink, "new-sample", G_CALLBACK (on_new_sample),
      appctx);

  // Add the elements to the pipeline
  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->appsink, NULL);

  // Sync the elements state to the curtent pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->appsink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (qtiqmmfsrc,
      gst_pad_get_name (stream->qmmf_pad), stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Error: Link cannot be done!\n");
    goto cleanup;
  }
  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->appsink, NULL)) {
    g_printerr ("[ERROR] Error: Link cannot be done!\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->appsink, GST_STATE_NULL);

  // Remove the elements from the pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->appsink, NULL);

  return FALSE;
}

static void
release_appsink_stream (GstAppContext *appctx, GstStreamInf *stream)
{
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name(GST_BIN(appctx->main_pipeline), "qmmf");

  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] qmmfsrc not found in pipeline\n");
    return;
  }

  g_print("[INFO] Unlinking elements for appsink stream...\n");
  gst_element_unlink_many(qtiqmmfsrc, stream->capsfilter, stream->appsink, NULL);
  g_print("[INFO] Unlinked successfully for appsink stream\n");

  // Lock state to prevent parent forcing PLAYING
  gst_element_set_locked_state(stream->capsfilter, TRUE);
  gst_element_set_locked_state(stream->appsink, TRUE);

  gst_element_set_state(stream->capsfilter, GST_STATE_NULL);
  gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state(stream->appsink, GST_STATE_NULL);
  gst_element_get_state (stream->appsink, NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_bin_remove_many(GST_BIN(appctx->main_pipeline),
                      stream->capsfilter, stream->appsink, NULL);

  stream->capsfilter = NULL;
  stream->appsink = NULL;

  gst_object_unref(qtiqmmfsrc);
}

static gboolean
create_dummy_stream (GstAppContext * appctx, GstStreamInf * stream,
    GstElement * qtiqmmfsrc)
{
  gchar temp_str[100];
  gboolean ret = FALSE;

  // Create the elements
  snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  snprintf (temp_str, sizeof (temp_str), "filesink_%d", appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("fakesink", temp_str);

  // Check if all elements are created successfully
  if (!stream->capsfilter || !stream->filesink) {
    if (stream->capsfilter)
      gst_object_unref (stream->capsfilter);
    if (stream->filesink)
      gst_object_unref (stream->filesink);
    g_printerr ("[ERROR] One element could not be created of found. Exiting.\n");
    return FALSE;
  }
  // Set caps the the caps filter
  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  // Add the elements to the main_pipeline
  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  // Sync the elements state to the curtent main_pipeline state
  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->filesink);

  // Link qmmfsrc with capsfilter
  ret = gst_element_link_pads_full (qtiqmmfsrc,
      gst_pad_get_name (stream->qmmf_pad), stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Link cannot be done!\n");
    goto cleanup;
  }
  // Link the elements
  if (!gst_element_link_many (stream->capsfilter, stream->filesink, NULL)) {
    g_printerr ("[ERROR] Link cannot be done!\n");
    goto cleanup;
  }
  return TRUE;

cleanup:
  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);

  // Remove the elements from the main_pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  return FALSE;
}

static void
release_dummy_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");

  // Unlink the elements of this stream
  g_print ("[INFO] Unlinking elements for dummy stream...\n");
  gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter,
      stream->filesink, NULL);
  g_print ("[INFO] Unlinked successfully for dummy stream \n");

  // Set NULL state to the unlinked elemets
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->filesink, GST_STATE_NULL);
  gst_element_get_state (stream->filesink, NULL, NULL, GST_CLOCK_TIME_NONE);

  // Remove the elements from the main_pipeline
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  stream->capsfilter = NULL;
  stream->filesink = NULL;

  gst_object_unref (qtiqmmfsrc);
}

static void
link_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  gboolean ret = FALSE;

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return;
  }
  // Activation the pad
  gst_pad_set_active (stream->qmmf_pad, TRUE);
  g_print ("[INFO] Pad name - %s\n", gst_pad_get_name (stream->qmmf_pad));

  if (stream->is_encoder)
    ret = create_encoder_stream (appctx, stream, qtiqmmfsrc);
  else
    ret = create_appsink_stream (appctx, stream, qtiqmmfsrc);
  if (!ret) {
    g_printerr ("[ERROR] failed to create steam\n");
    gst_object_unref (qtiqmmfsrc);
    return;
  }

  appctx->stream_cnt++;
  gst_object_unref (qtiqmmfsrc);

  return;
}

static void
unlink_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  /* Unlink all elements for this stream */
  if (stream->is_dummy) {
    release_dummy_stream (appctx, stream);
    stream->is_dummy = FALSE;

  } else if (stream->is_encoder)
    release_encoder_stream (appctx, stream);
  else if (stream->is_jpeg_snapshot || stream->is_raw_snapshot)
    release_snapshot_stream (appctx, stream);
  else
    release_appsink_stream (appctx, stream);

  /* Deactivate the pad */
  if (stream->qmmf_pad)
    gst_pad_set_active (stream->qmmf_pad, FALSE);

  g_print ("\n");
}

static gboolean
configure_metadata (GstAppContext *appctx)
{
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return FALSE;
  }

  ::camera::CameraMetadata session_meta(128, 128);
  ::camera::CameraMetadata *static_meta = nullptr;
  uint32_t tag;
  const std::shared_ptr<::camera::VendorTagDescriptor> vtags =
      ::camera::VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (vtags.get() == NULL) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    return -1;
  }

  // Get static and session metadata from qtiqmmfsrc
  g_object_get(G_OBJECT(qtiqmmfsrc),
               "static-metadata", &static_meta,
               NULL);

  if (!static_meta) {
    g_printerr("[WARN] Failed to retrieve metadata objects \n");
    gst_object_unref(qtiqmmfsrc);
    return FALSE;
  }

  // Find the vendor tag for CAMERA_SESSION_TAG
  gint ret = static_meta->getTagFromName(CAMERA_SESSION_TAG, vtags.get(), &tag);
  if (ret != 0) {
    g_printerr("[WARN] Vendor tag not found \n");
    gst_object_unref(qtiqmmfsrc);
    return FALSE;
  }

  // Update session metadata with mode value
  int32_t mode_val = static_cast<int32_t>(appctx->mode);
  session_meta.update(tag, &mode_val, 1);

  // Apply updated session metadata back to qtiqmmfsrc
  g_object_set(G_OBJECT(qtiqmmfsrc), "session-metadata", &session_meta, NULL);
  g_print("[INFO] Session metadata updated successfully \n");
  gst_object_unref(qtiqmmfsrc);

  return TRUE;
}

static GstStreamInf *
create_stream (GstAppContext *appctx, GstStreamInfo type, gint w, gint h)
{
  gboolean ret = FALSE;
  GstStreamInf *stream = g_new0 (GstStreamInf, 1);
  gchar temp_str[100] = {0};
  gint pad_type;
  GstPadTemplate *qtiqmmfsrc_template;

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return NULL;
  }

  stream->is_dummy = FALSE;
  stream->is_encoder = FALSE;
  stream->is_raw_snapshot = FALSE;
  stream->is_jpeg_snapshot = FALSE;

  switch (type) {
    case GST_STREAM_TYPE_DUMMY_ENCODER:
      stream->is_dummy = TRUE;
      stream->is_encoder = TRUE;
      break;

    case GST_STREAM_TYPE_ENCODER_BUFFERING:
      stream->is_encoder = TRUE;
      break;

    case GST_STREAM_TYPE_JPEG:
      stream->is_jpeg_snapshot = TRUE;
      break;

    case GST_STREAM_TYPE_RAW:
      stream->is_raw_snapshot = TRUE;
      break;

    default:
      break;
  }

  stream->width = w;
  stream->height = h;

  /* Default caps */
  stream->qmmf_caps = create_stream_caps (w, h);
  switch (type) {
    case GST_STREAM_TYPE_APPSINK:
      if (appctx->mode == GST_TAPOUT_RDI)
        stream->qmmf_caps = create_bayer_caps (w, h);
      break;

    case GST_STREAM_TYPE_JPEG:
      stream->qmmf_caps = create_jpeg_snapshot_caps (w, h);
      break;

    case GST_STREAM_TYPE_RAW:
      stream->qmmf_caps = create_bayer_caps (w, h);
        break;

    default:
      break;
  }

  // Get qmmfsrc Element class
  GstElementClass *qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);

  // Request a pad from qmmfsrc
  if (type == GST_STREAM_TYPE_JPEG || type == GST_STREAM_TYPE_RAW) {
    qtiqmmfsrc_template =
        gst_element_class_get_pad_template (qtiqmmfsrc_klass, "image_%u");
    stream->qmmf_pad =
        gst_element_request_pad (qtiqmmfsrc, qtiqmmfsrc_template, "image_%u", NULL);
  } else {
    qtiqmmfsrc_template =
        gst_element_class_get_pad_template (qtiqmmfsrc_klass, "video_%u");
    stream->qmmf_pad =
        gst_element_request_pad (qtiqmmfsrc, qtiqmmfsrc_template, "video_%u", NULL);
  }

  if (!stream->qmmf_pad) {
    g_printerr ("[ERROR] pad cannot be retrieved from qmmfsrc!\n");
    goto cleanup;
  }

  g_print ("[INFO] Pad received - %s\n", gst_pad_get_name (stream->qmmf_pad));


  pad_type = 1; /* default: preview */
  switch (type) {
    case GST_STREAM_TYPE_DUMMY_ENCODER:
      pad_type = 0; /* video */
      break;
    case GST_STREAM_TYPE_ENCODER_BUFFERING:
      pad_type = 1; /* preview */
      break;
    default:
      break;
  }

  /* Apply pad type where relevant */
  if (stream->qmmf_pad && type != GST_STREAM_TYPE_JPEG && type != GST_STREAM_TYPE_RAW)
    g_object_set (G_OBJECT (stream->qmmf_pad), "type", pad_type, NULL);

  if (stream->is_dummy)
    ret = create_dummy_stream (appctx, stream, qtiqmmfsrc);
  else if (stream->is_encoder)
    ret = create_encoder_stream (appctx, stream, qtiqmmfsrc);
  else if (stream->is_jpeg_snapshot || stream->is_raw_snapshot)
    ret = create_snapshot_stream (appctx, stream, qtiqmmfsrc);
  else {
    if (stream->qmmf_pad)
      /* set extra buffer for camera stream to match queue size */
      g_object_set (G_OBJECT (stream->qmmf_pad), "extra-buffers",
          (guint) appctx->queue_size, NULL);
      g_object_set (G_OBJECT (stream->qmmf_pad), "attach-cam-meta", TRUE, NULL);
    ret = create_appsink_stream (appctx, stream, qtiqmmfsrc);
  }

  if (!ret) {
    g_printerr ("[ERROR] failed to create stream\n");
    goto cleanup;
  }

  // Add the stream to the list
  appctx->streams_list = g_list_append (appctx->streams_list, stream);
  appctx->stream_cnt++;

  gst_object_unref (qtiqmmfsrc);
  return stream;

cleanup:
  if (stream->qmmf_pad) {
    // Release the unlinked pad
    gst_pad_set_active (stream->qmmf_pad, FALSE);
    gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);
  }

  gst_object_unref (qtiqmmfsrc);
  gst_caps_unref (stream->qmmf_caps);
  g_free (stream);

  return NULL;
}

void
release_stream (GstAppContext * appctx, GstStreamInf * stream)
{
  // Unlink all elements for that stream
  unlink_stream (appctx, stream);

  // Get qtiqmmfsrc instance
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] Failed to retrieve qtiqmmfsrc element in release_stream\n");
    gst_caps_unref (stream->qmmf_caps);
    appctx->streams_list = g_list_remove (appctx->streams_list, stream);
    g_free (stream);
    return;
  }

  // Release the unlinked pad
  gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);

  gst_object_unref (qtiqmmfsrc);
  gst_caps_unref (stream->qmmf_caps);

  // Remove the stream from the list
  appctx->streams_list = g_list_remove (appctx->streams_list, stream);

  g_free (stream);

  g_print ("\n");
}

// In case of ASYNC state change it will properly wait for state change
static gboolean
wait_for_state_change (GstElement *pipeline)
{
  g_return_val_if_fail (pipeline != NULL, FALSE);

  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  const gchar *pipeline_name = gst_object_get_name (GST_OBJECT (pipeline));

  g_print ("[INFO] Pipeline '%s' is PREROLLING ...\n", pipeline_name);

  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("[ERROR] Pipeline '%s' failed to PREROLL!\n", pipeline_name);
    return FALSE;
  }

  return TRUE;
}

/*
 * process_queued_buffers:
 * @appctx: (in): Application context containing appsrc pipeline and buffer queue.
 *
 * This function processes buffers queued for prebuffering and pushes them
 * into the `appsrc` element of the pipeline.
 */
static gboolean
process_queued_buffers (gpointer user_data)
{
  GstAppContext *appctx = NULL;
  GstElement *appsrc = NULL;
  GstAppSrc *src = NULL;
  GstBuffer *buffer = NULL;
  gboolean empty = FALSE;

  appctx = static_cast<GstAppContext *> (user_data);

  if (check_for_exit (appctx)) {
    g_print ("[INFO] Exit requested, stopping buffer processing\n");
    return FALSE;
  }

  appsrc = gst_bin_get_by_name (GST_BIN (appctx->appsrc_pipeline), "appsrc");
  if (!appsrc) {
    g_printerr ("[ERROR] Failed to retrieve appsrc element\n");
    return FALSE;
  }

  src = GST_APP_SRC (appsrc);

  // Check if queue is empty
  g_mutex_lock (&appctx->lock);
  empty = g_queue_is_empty (appctx->buffers_queue);
  g_mutex_unlock (&appctx->lock);

  if (empty) {
    gst_app_src_end_of_stream (src);
    g_print ("[INFO] Buffer queue empty, sending EOS and stopping\n");
    g_print ("[INFO] Procesing of queued buffers are done.\n");
    gst_object_unref (appsrc);
    return FALSE;
  }

  // Pop buffer from queue under lock
  g_mutex_lock (&appctx->lock);
  buffer = GST_BUFFER (g_queue_pop_head (appctx->buffers_queue));
  g_mutex_unlock (&appctx->lock);

  // Validate PTS and push or discard
  if (GST_CLOCK_TIME_IS_VALID (appctx->first_live_pts) &&
      GST_BUFFER_PTS (buffer) >= appctx->first_live_pts) {
    g_print ("[INFO] Discarding buffer after live PTS reached\n");
    gst_buffer_unref (buffer);
  } else {
    gst_app_src_push_buffer (src, buffer);
  }

  gst_object_unref (appsrc);

  return TRUE;
}

static gboolean
start_pushing_buffers (gpointer user_data)
{
  GstAppContext *appctx = static_cast<GstAppContext *> (user_data);

  g_print ("[INFO] Starting to push queued buffers to appsrc pipeline\n");
  appctx->process_src_id = g_timeout_add(16, process_queued_buffers, appctx);

  return FALSE;
}

static void
interruptible_sleep (GstAppContext *appctx, guint seconds)
{
  const guint step_ms = 100;
  guint elapsed_ms = 0;
  guint target_ms = seconds * 1000;

  while (elapsed_ms < target_ms) {
    if (check_for_exit (appctx))
      break;

    g_usleep (step_ms * 1000);
    elapsed_ms += step_ms;
  }
}

/**
 * prebuffering_usecase:
 * @appctx: (in): Application context containing pipelines and stream info.
 *
 * Implements a pre-buffering use case for video recording with smooth
 * transition from prebuffered frames to live recording.
 */
static void
prebuffering_usecase (GstAppContext *appctx)
{
  GstStreamInf *stream_inf_1;
  GstStreamInf *stream_inf_2;
  GstStreamInf *stream_inf_3;
  GstStreamInf *stream_inf_4;
  GstStreamInf *stream_inf_5;
  GstStreamInf *stream_inf_6;

  if (appctx->mode == GST_TAPOUT_RDI) {
    g_print ("[INFO] Creating appsink RDI stream (%dx%d)\n", appctx->width, appctx->height);
    stream_inf_1 = create_stream (appctx, GST_STREAM_TYPE_APPSINK, appctx->width, appctx->height);
    if (!stream_inf_1) {
      g_printerr ("Failed to create appsink stream\n");
      return;
    }
  } else {
    g_print ("[INFO] Creating appsink YUV stream (1920x1080)\n");
    stream_inf_1 = create_stream (appctx, GST_STREAM_TYPE_APPSINK, 1920, 1080);
    if (!stream_inf_1) {
      g_printerr ("Failed to create appsink stream\n");
      return;
    }
  }

  g_print ("[INFO] Creating live encoder stream(buffering) (640x480)\n");
  stream_inf_2 = create_stream (appctx, GST_STREAM_TYPE_ENCODER_BUFFERING, 640, 480);
  if (!stream_inf_2) {
    g_printerr ("Failed to create live stream\n");
    release_stream (appctx, stream_inf_1);
    return;
  }

  g_print ("[INFO] Creating live encoder stream(recording) (1920x1080)\n");
  stream_inf_3 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER, 1920, 1080);
  if (!stream_inf_3) {
    g_printerr ("Failed to create live stream\n");
    release_stream (appctx, stream_inf_1);
    release_stream (appctx, stream_inf_2);
    return;
  }

  gst_pad_add_probe (stream_inf_3->qmmf_pad, GST_PAD_PROBE_TYPE_BUFFER,
      live_frame_probe, appctx, NULL);

  g_print ("[INFO] Creating live encoder stream(recording) (640x480)\n");
  stream_inf_4 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER, 640, 480);
  if (!stream_inf_4) {
    g_printerr ("Failed to create live stream\n");
    release_stream (appctx, stream_inf_1);
    release_stream (appctx, stream_inf_2);
    release_stream (appctx, stream_inf_3);
    return;
  }

  if (appctx->enable_snapshot_streams) {
    appctx->meta_capture = g_ptr_array_new_full (0, gst_camera_metadata_release);
    if (!appctx->meta_capture) {
       g_printerr ("ERROR: failed to create metadata for capture.\n");
       return;
    }
    g_print ("[INFO] Creating JPEG stream(SnapShot) (%dx%d)\n",
        appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height);
    stream_inf_5 = create_stream (appctx, GST_STREAM_TYPE_JPEG,
        appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height);
    if (!stream_inf_5) {
      g_printerr ("Failed to create JPEG stream(SnapShot)\n");
      release_stream (appctx, stream_inf_1);
      release_stream (appctx, stream_inf_2);
      release_stream (appctx, stream_inf_3);
      release_stream (appctx, stream_inf_4);
      return;
    }

    g_print ("[INFO] Creating RAW stream(SnapShot) (%dx%d)\n",
        appctx->raw_snapshot_width, appctx->raw_snapshot_height);
    stream_inf_6 = create_stream (appctx, GST_STREAM_TYPE_RAW,
        appctx->raw_snapshot_width, appctx->raw_snapshot_height);
    if (!stream_inf_6) {
      g_printerr ("Failed to create Raw stream(SnapShot)\n");
      release_stream (appctx, stream_inf_1);
      release_stream (appctx, stream_inf_2);
      release_stream (appctx, stream_inf_3);
      release_stream (appctx, stream_inf_4);
      release_stream (appctx, stream_inf_5);
      return;
    }
  }

  // Transition main pipeline to PAUSED for caps negotiation
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->main_pipeline, GST_STATE_PAUSED))
    wait_for_state_change (appctx->main_pipeline);

  if (!configure_metadata (appctx))
    g_printerr ("[WARN] Failed to configure camera session params \n");

  g_print ("[INFO] Unlinking live stream before switching pipeline "
      "to PLAYING\n");
  unlink_stream (appctx, stream_inf_3);
  unlink_stream (appctx, stream_inf_4);

  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->main_pipeline, GST_STATE_PLAYING))
    wait_for_state_change (appctx->main_pipeline);

  gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_PLAYING);

  // Wait before switching to live
  g_print ("[INFO] Prebuffering of data is going on ...\n");

  if (appctx->enable_snapshot_streams) {
    if (!capture_prepare_metadata (appctx)) {
      g_printerr ("[ERROR] Failed to prepare capture metadata\n");
      g_ptr_array_free (appctx->meta_capture, TRUE);
      return;
    }
  }

  g_print ("[INFO] Waiting %u seconds before switching to live recording...\n",
      appctx->delay_to_start_recording);

  interruptible_sleep (appctx, appctx->delay_to_start_recording/2);
  if (check_for_exit (appctx)) {
    exit_cleanup(appctx);
    return;
  }

  if (appctx->enable_snapshot_streams)
    if (!trigger_snapshot (appctx))
      g_printerr ("[WARN] Failed to Trigger Snapshot \n");


  interruptible_sleep (appctx, appctx->delay_to_start_recording/2);
  if (check_for_exit (appctx)) {
    exit_cleanup(appctx);
    return;
  }

  g_print ("[INFO] Linking live stream back to pipeline\n");
  link_stream (appctx, stream_inf_3);
  link_stream (appctx, stream_inf_4);

  g_mutex_lock (&appctx->lock);

  while (appctx->first_live_pts == GST_CLOCK_TIME_NONE && !appctx->exit)
    g_cond_wait (&appctx->live_pts_signal, &appctx->lock);

  g_mutex_unlock (&appctx->lock);

  appctx->switch_to_live = TRUE;

  // Start pushing buffers
  start_pushing_buffers (appctx);

  // Unlink appsink stream (prebuffered) after switching to live
  unlink_stream (appctx, stream_inf_1);
  unlink_stream (appctx, stream_inf_2);

  // Record for specified duration
  g_print ("[INFO] Live recording started for %u seconds\n",
      appctx->record_duration);

  interruptible_sleep (appctx, appctx->record_duration/2);
  if (check_for_exit (appctx)) {
    exit_cleanup(appctx);
    return;
  }

  if (appctx->enable_snapshot_streams) {
    if (!trigger_snapshot (appctx))
      g_printerr ("[WARN] Failed to Trigger Snapshot \n");
  }

  interruptible_sleep (appctx, appctx->record_duration/2);
  if (check_for_exit (appctx)) {
    exit_cleanup(appctx);
    return;
  }

  clear_buffers_queue (appctx);

  link_stream (appctx, stream_inf_1);
  link_stream (appctx, stream_inf_2);

  // Send EOS to allow proper flushing
  g_print ("[INFO] Sending EOS event to main pipeline\n");
  gst_element_send_event (appctx->main_pipeline, gst_event_new_eos ());

  // Wait for EOS message on bus
  wait_for_eos (appctx);

  // Transition pipelines to NULL state
  g_print ("[INFO] Transitioning main pipeline to NULL state\n");
  gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->main_pipeline, NULL, NULL,
      GST_CLOCK_TIME_NONE);

  g_print ("[INFO] Transitioning appsrc pipeline to NULL state\n");
  gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->appsrc_pipeline, NULL, NULL,
      GST_CLOCK_TIME_NONE);

  // Release streams and pads
  release_stream (appctx, stream_inf_1);
  release_stream (appctx, stream_inf_2);
  release_stream (appctx, stream_inf_3);
  release_stream (appctx, stream_inf_4);
  if (appctx->enable_snapshot_streams) {
    release_stream (appctx, stream_inf_5);
    release_stream (appctx, stream_inf_6);
  }

  g_print ("[INFO] Cleanup complete\n");
}

static void *
thread_fn (gpointer user_data)
{
  GstAppContext *appctx = (GstAppContext *) user_data;

  // Execute the selected use case
  appctx->usecase_fn (appctx);

  // Quit the main loop only if we are not already exiting and the loop is running
  if (!check_for_exit (appctx)
      && appctx->mloop
      && g_main_loop_is_running (appctx->mloop))
    g_main_loop_quit (appctx->mloop);

  return NULL;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstCaps *filtercaps;
  GstCaps *caps;
  GstPad *sinkpad;
  GstElement *pipeline = NULL;
  GstElement *qtiqmmfsrc = NULL;
  GstElement *appsrc = NULL;
  GstElement *queue = NULL;
  GstElement *h264parse = NULL;
  GstElement *mp4mux = NULL;
  GstElement *encoder = NULL;
  GstElement *filesink = NULL;
  GstElement *multifilesink = NULL;
  GstElement *camimgreproc = NULL;
  GstElement *capsfilter = NULL;
  gboolean ret = FALSE;
  GstAppContext *appctx = g_new0 (GstAppContext, 1);
  g_mutex_init (&appctx->lock);
  g_cond_init (&appctx->eos_signal);
  g_cond_init (&appctx->live_pts_signal);
  appctx->stream_cnt = 0;
  appctx->camera_id = 2;
  appctx->height = OUTPUT_HEIGHT;
  appctx->width = OUTPUT_WIDTH;
  appctx->delay_to_start_recording = DELAY_TO_START_RECORDING;
  appctx->queue_size = MAX_QUEUE_SIZE;
  appctx->mode = GST_TAPOUT_NORMAL;
  appctx->usecase_fn = prebuffering_usecase;
  appctx->first_live_pts = GST_CLOCK_TIME_NONE;
  appctx->switch_to_live = FALSE;
  appctx->record_duration = RECORD_DURATION;
  appctx->jpeg_snapshot_width = JPEG_SNAPHOT_WIDTH;
  appctx->jpeg_snapshot_height = JPEG_SNAPHOT_HEIGHT;
  appctx->raw_snapshot_width = RAW_SNAPHOT_WIDTH;
  appctx->raw_snapshot_height = RAW_SNAPHOT_HEIGHT;
  appctx->enable_snapshot_streams = FALSE;
  appctx->meta_capture = NULL;
  appctx->snapshot_type = 0;
  appctx->noise_reduction_mode = 0;
  appctx->num_snapshots = 1;
  appctx->rdi_output_width = 1920;
  appctx->rdi_output_height = 1080;

  GOptionEntry entries[] = {
    {
      "camera-id", 'c', 0, G_OPTION_ARG_INT, &appctx->camera_id,
      "Camera ID", "id"
    },
    {
      "height", 'h', 0, G_OPTION_ARG_INT, &appctx->height,
      "Frame height", "height"
    },
    {
      "width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
      "Frame width", "width"
    },
    {
      "delay", 'd', 0, G_OPTION_ARG_INT, &appctx->delay_to_start_recording,
      "Delay before recording starts (seconds)", "delay"
    },
    {
      "record-duration", 'r', 0, G_OPTION_ARG_INT, &appctx->record_duration,
      "Record duration after recording starts (seconds)", "duration"
    },
    {
      "queue-size", 'q', 0, G_OPTION_ARG_INT, &appctx->queue_size,
      "Max buffer queue size", "size"
    },
    {
      "tap-out", 't', 0, G_OPTION_ARG_INT, &appctx->mode,
      "Tap out mode: 0 - Normal, 1 - RDI, 2 - IPE By Pass", "mode"
    },
    {
      "snapshot-jpeg-width", 'j', 0, G_OPTION_ARG_INT, &appctx->jpeg_snapshot_width,
      "Snapshot JPEG width", "width"
    },
    {
      "snapshot-jpeg-height", 'k', 0, G_OPTION_ARG_INT, &appctx->jpeg_snapshot_height,
      "Snapshot JPEG height", "height"
    },
    {
      "raw-snapshot-width", 'o', 0, G_OPTION_ARG_INT, &appctx->raw_snapshot_width,
      "Raw snapshot width", "width"
    },
    {
      "raw-snapshot-height", 's', 0, G_OPTION_ARG_INT, &appctx->raw_snapshot_height,
      "Raw snapshot height", "height"
    },
    {
      "enable-snapshot-streams", 'e', 0, G_OPTION_ARG_NONE, &appctx->enable_snapshot_streams,
      "Enable snapshot streams", NULL
    },
    {
      "num-snapshots", 'n', 0, G_OPTION_ARG_INT, &appctx->num_snapshots,
      "Number of snapshots to capture", "count"
    },
    {
      "snapshot-type", 'y', 0, G_OPTION_ARG_INT, &appctx->snapshot_type,
      "Snapshot type: 0 - video,  1 - still", "type"
    },
    {
      "noise-reduction-mode", 'm', 0, G_OPTION_ARG_INT, &appctx->noise_reduction_mode,
      "Noise reduction mode: 0 - off,  1 - fast, 2 - high_quality", "mode"
    },
    {
      "rdi-output-width", 'x', 0, G_OPTION_ARG_INT, &appctx->rdi_output_width,
      "RDI output width (for reprocessing)", "width"
    },
    {
      "rdi-output-height", 'z', 0, G_OPTION_ARG_INT, &appctx->rdi_output_height,
      "RDI output height (for reprocessing)", "height"
    },
    { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("Pre-Buffered data and recording ")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("[ERROR] Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("[ERROR] Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("[ERROR] Failed to create options context!\n");
    return -EFAULT;
  }

  if (appctx->enable_snapshot_streams) {
      if (appctx->jpeg_snapshot_width <= 0 || appctx->jpeg_snapshot_height <= 0) {
          g_printerr ("Invalid JPEG snapshot size: %dx%d",
                  appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height);
          return -EINVAL;
      }
      if (appctx->raw_snapshot_width <= 0 || appctx->raw_snapshot_height <= 0) {
          g_printerr ("Invalid RAW snapshot size: %dx%d",
                  appctx->raw_snapshot_width, appctx->raw_snapshot_height);
          return -EINVAL;
      }
  }

  if (appctx->mode != GST_TAPOUT_NORMAL
      && appctx->mode != GST_TAPOUT_RDI &&
      appctx->mode != GST_TAPOUT_IPEBYPASS) {
    g_printerr ("[ERROR] Invalid buffer mode: %d\n",appctx->mode);
    return -EFAULT;
  }

  if (appctx->width == 0 || appctx->height == 0) {
    g_printerr ("[ERROR] Invalid width and height  %dx%d\n",appctx->width, appctx->height);
    return -EFAULT;
  }

  if (appctx->delay_to_start_recording == 0)
    g_printerr("[WARN] Delay to start recording is 0 prebuffering will be ineffective\n");

  if (appctx->queue_size == 0) {
    g_printerr("[ERROR] Queue size cannot be 0\n");
    return -EFAULT;
  }

  g_print ("[INFO] Parsed Options:\n");
  g_print ("[INFO] Camera ID: %u\n", appctx->camera_id);
  g_print ("[INFO] Height: %u\n", appctx->height);
  g_print ("[INFO] Width: %u\n", appctx->width);
  g_print ("[INFO] Delay to Start Recording: %u seconds\n",
      appctx->delay_to_start_recording);
  g_print ("[INFO] Record Duration: %u seconds\n", appctx->record_duration);
  g_print ("[INFO] Queue Size: %u\n", appctx->queue_size);
  g_print ("[INFO] Tap out mode: %d\n",appctx->mode);
  g_print ("[INFO] Snapshot JPEG Width: %d\n", appctx->jpeg_snapshot_width);
  g_print ("[INFO] Snapshot JPEG Height: %d\n", appctx->jpeg_snapshot_height);
  g_print ("[INFO] Raw Snapshot Width: %d\n", appctx->raw_snapshot_width);
  g_print ("[INFO] Raw Snapshot Height: %d\n", appctx->raw_snapshot_height);
  g_print ("[INFO] Enable Snapshot Streams: %s\n",
      appctx->enable_snapshot_streams ? "Yes" : "No");
  g_print ("[INFO] SnapShot Count: %d\n", appctx->num_snapshots);
  g_print ("[INFO] SnapShot Type: %d\n", appctx->snapshot_type);
  g_print ("[INFO] NR Mode: %d\n", appctx->noise_reduction_mode);
  g_print ("[INFO] RDI Output Width: %d\n", appctx->rdi_output_width);
  g_print ("[INFO] RDI Output Height: %d\n", appctx->rdi_output_height);

  // Initialize GST library.
  gst_init (&argc, &argv);

  appctx->encoder_name = get_encoder_name();
  if (!appctx->encoder_name)
    return -EFAULT;

  pipeline = gst_pipeline_new ("gst-main-pipeline");
  appctx->main_pipeline = pipeline;

  // Create qmmfsrc element
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");

  if (!qtiqmmfsrc) {
    g_printerr("[ERROR] Failed to create qtiqmmfsrc element\n");
    gst_object_unref(appctx->main_pipeline);
    g_main_loop_unref(mloop);
    return -EFAULT;
  }
  // Set qmmfsrc properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "qmmf", NULL);

  // Set the Camera ID
  g_object_set (G_OBJECT (qtiqmmfsrc), "camera", appctx->camera_id, NULL);

  // Add qmmfsrc to the main_pipeline
  gst_bin_add (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    gst_bin_remove (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);
    gst_object_unref (appctx->main_pipeline);
    g_printerr ("[ERROR] Failed to create Main loop!\n");
    return -1;
  }

  appctx->mloop = mloop;

  pipeline = gst_pipeline_new ("gst-appsrc-pipeline");
  appsrc = gst_element_factory_make ("appsrc", "appsrc");
  queue =  gst_element_factory_make ("queue", "queue");
  if (appctx->mode == GST_TAPOUT_RDI) {
    camimgreproc = gst_element_factory_make ("qticamimgreproc", "camimgreproc");
    capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  }
  encoder = gst_element_factory_make (appctx->encoder_name, "encoder");
  filesink = gst_element_factory_make ("filesink", "filesink");
  h264parse = gst_element_factory_make ("h264parse", "h264parse");
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");

  // Check if all elements are created successfully
  if (appctx->mode == GST_TAPOUT_RDI) {
    if (!pipeline || !appsrc || !queue || !camimgreproc || !capsfilter || !encoder
            || !filesink || !h264parse || !mp4mux) {
      g_printerr ("[ERROR] One element could not be created or found. Exiting.\n");
      return -1;
    }
  } else {
    if (!pipeline || !appsrc || !queue || !encoder || !filesink || !h264parse || !mp4mux) {
      g_printerr ("[ERROR] One element could not be created of found. Exiting.\n");
      return -1;
    }
  }

  //Set properties
  g_object_set (G_OBJECT (h264parse), "name", "h264parse", NULL);
  g_object_set (G_OBJECT (mp4mux), "name", "mp4mux", NULL);

    // Set encoder properties
  g_object_set (G_OBJECT (encoder), "name", "encoder", NULL);
  g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);

  if (g_strcmp0(appctx->encoder_name, "qtic2venc") == 0)
    g_object_set (G_OBJECT (encoder), "control-rate", 3, NULL);   // VBR-CFR
  else {
    g_object_set (G_OBJECT (encoder), "periodicity-idr", 1, NULL);
    g_object_set (G_OBJECT (encoder), "interval-intraframes", 29, NULL);
    g_object_set (G_OBJECT (encoder), "control-rate", 2, NULL);
  }

  g_object_set (G_OBJECT (filesink), "name", "filesink", NULL);
  g_object_set (G_OBJECT (filesink), "location", "/data/video_prebuffered_data.mp4",
      NULL);
  g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);

  // Set appsrc properties
  if (appctx->mode == GST_TAPOUT_RDI) {
    filtercaps = gst_caps_new_simple ("video/x-bayer",
        "format", G_TYPE_STRING, "rggb",
        "bpp", G_TYPE_STRING, "10",
        "width", G_TYPE_INT, appctx->width,
        "height", G_TYPE_INT, appctx->height,
        "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  } else {
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, appctx->width,
        "height", G_TYPE_INT, appctx->height,
        "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
    gst_caps_set_features (filtercaps, 0,
        gst_caps_features_new ("memory:GBM", NULL));
  }

  g_object_set (G_OBJECT (appsrc), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  g_object_set (G_OBJECT (appsrc), "stream-type", 0,    // GST_APP_STREAM_TYPE_STREAM
      "format", GST_FORMAT_TIME, "is-live", TRUE,
      NULL);

  //setting caps on capsfilter
  if (appctx->mode == GST_TAPOUT_RDI) {
    caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->rdi_output_width,
      "height", G_TYPE_INT, appctx->rdi_output_height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
    gst_caps_set_features (caps, 0,
        gst_caps_features_new ("memory:GBM", NULL));
    g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
    gst_caps_unref (caps);
  }

  // Assign elements to context
  appctx->appsrc_pipeline = pipeline;
  appctx->appsrc = appsrc;
  appctx->queue = queue;
  if (appctx->mode == GST_TAPOUT_RDI) {
    appctx->camimgreproc = camimgreproc;
    appctx->capsfilter = capsfilter;
  }
  appctx->h264parse = h264parse;
  appctx->mp4mux = mp4mux;
  appctx->encoder = encoder;
  appctx->filesink = filesink;

  // Add elements to the pipeline
  if (appctx->mode == GST_TAPOUT_RDI)
    gst_bin_add_many (GST_BIN (appctx->appsrc_pipeline),
        appsrc, queue, camimgreproc, capsfilter, encoder, h264parse, mp4mux, filesink, NULL);
  else
    gst_bin_add_many (GST_BIN (appctx->appsrc_pipeline),
        appsrc, queue, encoder, h264parse, mp4mux, filesink, NULL);

  if (appctx->mode == GST_TAPOUT_RDI) {
    sinkpad = gst_element_request_pad_simple(camimgreproc, "sink_%u");
    if (!sinkpad) {
      g_printerr ("[ERROR] Failed to get sink pad from reprocess element\n");
      return -1;
    }
    //setting the prop
    g_object_set (G_OBJECT (sinkpad),
                 "camera-id", appctx->camera_id,
                 NULL);

    gst_object_unref (sinkpad);
  }

  if (appctx->mode == GST_TAPOUT_RDI) {
    if (!gst_element_link_many (appsrc, queue, camimgreproc, capsfilter, encoder,
            h264parse, mp4mux, filesink, NULL)) {
      g_printerr ("[ERROR] Link cannot be done!\n");
      return -1;
    }
  } else {
    if (!gst_element_link_many (appsrc, queue, encoder,
            h264parse, mp4mux, filesink, NULL)) {
      g_printerr ("[ERROR] Link cannot be done!\n");
      return -1;
    }
  }

  // Retrieve reference to the main_pipeline's bus.
  if ((bus = gst_pipeline_get_bus (
       GST_PIPELINE (appctx->main_pipeline))) == NULL) {
    gst_bin_remove (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);
    gst_object_unref (appctx->main_pipeline);
    g_printerr ("[ERROR] Failed to retrieve main_pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }
  // Watch for messages on the main_pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->main_pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), appctx);
  gst_object_unref (bus);

  // Retrieve reference to the main_pipeline's bus.
  if ((bus = gst_pipeline_get_bus (
       GST_PIPELINE (appctx->appsrc_pipeline))) == NULL) {
    gst_object_unref (appctx->appsrc_pipeline);
    g_printerr ("[ERROR] Failed to retrieve appsrc_pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }

  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->appsrc_pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  appctx->buffers_queue = g_queue_new ();

  // Run thread which perform link and unlink of streams
  pthread_t thread;
  pthread_create (&thread, NULL, &thread_fn, appctx);

  // Run main loop.
  g_print ("[INFO] g_main_loop_run\n");
  g_main_loop_run (mloop);

  if (appctx->process_src_id) {
    GSource *src = g_main_context_find_source_by_id(NULL, appctx->process_src_id);
    if (src && !g_source_is_destroyed(src)) {
      g_source_remove(appctx->process_src_id);
      g_print("[INFO] Removed buffer pushing source\n");
    }
    appctx->process_src_id = 0;
  }

  pthread_join(thread, NULL);
  g_print ("[INFO] g_main_loop_run ends\n");

  g_print ("[INFO] Setting main_pipeline to NULL state ...\n");
  if (appctx->main_pipeline)
    gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);

  if (appctx->appsrc_pipeline)
    gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);

  // Release any remaining streams
  if (appctx->streams_list != NULL)
    release_all_streams (appctx);

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  // Remove qmmfsrc from the main_pipeline
  if (appctx->main_pipeline && qtiqmmfsrc)
    gst_bin_remove (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);

  // Free the streams list
  if (appctx->streams_list != NULL) {
    g_list_free (appctx->streams_list);
    appctx->streams_list = NULL;
  }

 // Clear buffer queue
  if (appctx->buffers_queue) {
    clear_buffers_queue (appctx);
    g_queue_free (appctx->buffers_queue);
  }

  g_mutex_clear (&appctx->lock);
  g_cond_clear (&appctx->eos_signal);
  g_cond_clear(&appctx->live_pts_signal);

  // Cleanup pipelines
  if (appctx->appsrc_pipeline)
    gst_object_unref (appctx->appsrc_pipeline);

  if (appctx->main_pipeline)
    gst_object_unref (appctx->main_pipeline);

  g_free (appctx);

  gst_deinit ();

  g_print ("[INFO] main: Exit\n");

  return 0;
}
