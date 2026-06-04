/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * GStreamer Application:
 * GStreamer Application for 360-Degree Dual-Camera Pre-Buffering and Live Recording
 *
 * Description:
 * This application demonstrates a dual-camera (360-degree) pre-buffering use case
 * using a logical camera (camera ID 0) that maps to two physical cameras (CAM1 and CAM2).
 * Video frames are pre-buffered before recording starts, ensuring that the final video
 * includes content from a few seconds before the recording trigger.
 *
 * Features:
 *   -- Dual-camera support via logical camera (camera=0)
 *   -- Per-stream physical camera routing via logical-stream-type pad property
 *   -- Pre-buffer frames from CAM1 (and CAM2 in IPE Bypass mode) using appsink
 *   -- FD streams (CAM1 and CAM2) run continuously through buffering and recording
 *   -- Push pre-buffered frames to appsrc pipeline for encoding
 *   -- Smooth transition from pre-buffered content to live recording
 *
 * RDI Tap-Out Mode (11 streams):
 *   CAM1: STR0(RAW10 4096x3072 ring buffer), STR1(YUV 640x480 FD continuous),
 *         STR2(YUV 1920x1080 live rec), STR3(YUV 640x480 live view),
 *         STR4(JPEG 4096x3072 snapshot), STR5(RAW10 4096x3072 MFNR snapshot)
 *   CAM2: STR6(YUV 640x480 FD continuous), STR7(YUV 1920x1080 live rec),
 *         STR8(YUV 640x480 live view), STR9(JPEG 4096x3072 snapshot),
 *         STR10(RAW10 4096x3072 MFNR snapshot)
 *
 * IPE Bypass Tap-Out Mode (12 streams):
 *   CAM1: STR0(YUV 1920x1080 ring buffer), STR1(YUV 640x480 FD continuous),
 *         STR2(YUV 1920x1080 live rec), STR3(YUV 640x480 live view),
 *         STR4(JPEG 4096x3072 snapshot), STR5(RAW10 4096x3072 MFNR snapshot)
 *   CAM2: STR6(YUV 1920x1080 ring buffer), STR7(YUV 640x480 FD continuous),
 *         STR8(YUV 1920x1080 live rec), STR9(YUV 640x480 live view),
 *         STR10(JPEG 4096x3072 snapshot), STR11(RAW10 4096x3072 MFNR snapshot)
 *
 * Usage:
 * gst-camera-360-prebuffered-data-app [OPTIONS]
 * Example (IPE Bypass):
 *   gst-camera-360-prebuffered-data-app -c 0 -d 30 -r 30 -t 2
 * Example (RDI):
 *   gst-camera-360-prebuffered-data-app -c 0 -d 30 -r 30 -t 1
 *
 * Options:
 * -c, --camera-id=id              Camera ID (logical camera, default: 0)
 * -d, --delay=delay               Delay before recording starts (seconds, default: 30)
 * -r, --record-duration=duration  Record duration after recording starts (seconds, default: 30)
 * -q, --queue-size=size           Max buffer queue size (default: 300)
 * -t, --tap-out=mode              Tap out mode: 1 - RDI, 2 - IPE Bypass (default: 2)
 * -w, --width=width               RAW stream/snapshot width: RDI ring buffer + RAW snapshots
 *                                   both cameras (RDI mode only, default: 4096)
 * -h, --height=height             RAW stream/snapshot height: RDI ring buffer + RAW snapshots
 *                                   both cameras (RDI mode only, default: 3072)
 * -j, --snapshot-jpeg-width=width JPEG snapshot width for both cameras (default: 4096)
 * -k, --snapshot-jpeg-height=height JPEG snapshot height for both cameras (default: 3072)
 * -x, --rdi-output-width=width    RDI reprocess output width (default: 1920)
 * -z, --rdi-output-height=height  RDI reprocess output height (default: 1080)
 * -e, --enable-snapshot-streams   Enable snapshot streams
 * -n, --num-snapshots=count       Number of snapshots to capture (default: 1)
 * -y, --snapshot-type=type        Snapshot type: 0 - video, 1 - still (default: 0)
 * -m, --noise-reduction-mode=mode Noise reduction mode: 0-off, 1-fast, 2-high_quality (default: 0)
 * -b, --standby-duration=seconds  CAM2 standby duration at start of buffering in seconds
 *                                   (0=disabled, default: 0)
 *     --standby-camera-id=id      Physical camera ID to put in standby (default: 2)
 *     --reproc-camera-id=id       Physical camera ID for qticamimgreproc in RDI mode (default: 2)
 *     --livestream-start=seconds  Start live view streams during buffering at this second
 *                                   (0=disabled, default: 0)
 *     --livestream-stop=seconds   Stop live view streams during buffering at this second (default: 0)
 *     --master-camera-id=id       Physical camera ID to configure as master (-1=disabled, default: -1)
 *
 * *******************************************************************************
 * Pipeline for Pre-buffering and Recording (IPE Bypass mode):
 * Main Pipeline:
 *   qtiqmmfsrc(cam=0) -> capsfilter -> appsink  (CAM1 ring buffer, STR0)
 *   qtiqmmfsrc(cam=0) -> capsfilter -> encoder -> h264parse -> mp4mux -> filesink (CAM1 FD, STR1)
 *   qtiqmmfsrc(cam=0) -> capsfilter -> [dummy/encoder] (CAM1 1080P, STR2)
 *   qtiqmmfsrc(cam=0) -> capsfilter -> [dummy/encoder] (CAM1 480P, STR3)
 *   qtiqmmfsrc(cam=0) -> capsfilter -> appsink  (CAM2 ring buffer, STR6)
 *   qtiqmmfsrc(cam=0) -> capsfilter -> encoder -> h264parse -> mp4mux -> filesink (CAM2 FD, STR7)
 *   qtiqmmfsrc(cam=0) -> capsfilter -> [dummy/encoder] (CAM2 1080P, STR8)
 *   qtiqmmfsrc(cam=0) -> capsfilter -> [dummy/encoder] (CAM2 480P, STR9)
 * CAM1 Appsrc Pipeline:
 *   appsrc -> queue -> encoder -> h264parse -> mp4mux -> filesink
 * CAM2 Appsrc Pipeline (IPE Bypass only):
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
#include <time.h>
#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

/* Default parameters */
#define MAX_QUEUE_SIZE            300
#define DELAY_TO_START_RECORDING  30
#define RECORD_DURATION           30

/* Fixed stream resolutions per spec */
#define RDI_RING_BUFFER_WIDTH     4096
#define RDI_RING_BUFFER_HEIGHT    3072
#define IPE_RING_BUFFER_WIDTH     1920
#define IPE_RING_BUFFER_HEIGHT    1080
#define FD_STREAM_WIDTH           640
#define FD_STREAM_HEIGHT          480
#define REC_1080P_WIDTH           1920
#define REC_1080P_HEIGHT          1080
#define REC_480P_WIDTH            640
#define REC_480P_HEIGHT           480
#define SNAPSHOT_JPEG_WIDTH       4096
#define SNAPSHOT_JPEG_HEIGHT      3072
#define SNAPSHOT_RAW_WIDTH        4096
#define SNAPSHOT_RAW_HEIGHT       3072
#define RDI_OUTPUT_WIDTH_DEFAULT  1920
#define RDI_OUTPUT_HEIGHT_DEFAULT 1080

/* Logical camera physical camera indices */
#define CAM1_LOGICAL_STREAM_TYPE  0
#define CAM2_LOGICAL_STREAM_TYPE  1

#define CAMERA_SESSION_TAG \
  "org.codeaurora.qcamera3.sessionParameters.DynamicTapOut"

#define CAMERA_MASTER_CAM_TAG \
  "org.codeaurora.qcamera3.sessionParameters.AppMasterCameraId"

typedef struct _GstAppContext GstAppContext;
typedef struct _GstStreamInf GstStreamInf;
typedef struct _ProcessBuffersCtx ProcessBuffersCtx;

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

/* Stream information */
struct _GstStreamInf {
  GstElement *capsfilter;
  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;
  GstElement *appsink;
  GstPad     *qmmf_pad;
  GstCaps    *qmmf_caps;
  gint        width;
  gint        height;
  gint        logical_stream_type;  /* 0 = CAM1, 1 = CAM2 */
  gchar       output_path[256];     /* output file path for encoder streams */
  gboolean    is_dummy;
  gboolean    is_encoder;
  gboolean    is_jpeg_snapshot;
  gboolean    is_raw_snapshot;
};

/* Context passed to process_queued_buffers callback */
struct _ProcessBuffersCtx {
  GstAppContext *appctx;
  gint           cam_id;  /* 0 = CAM1, 1 = CAM2 */
};

/* Application context */
struct _GstAppContext {
  /* Main pipeline (camera source) */
  GstElement *main_pipeline;

  /* CAM1 appsrc pipeline (pre-buffered data encoding) */
  GstElement *appsrc_pipeline;
  GstElement *appsrc;
  GstElement *h264parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;
  GstElement *queue;
  GstElement *camimgreproc;  /* RDI mode only */
  GstElement *capsfilter;    /* RDI mode only */

  /* CAM2 appsrc pipeline (IPE Bypass mode only) */
  GstElement *appsrc_pipeline_cam2;
  GstElement *appsrc_cam2;
  GstElement *h264parse_cam2;
  GstElement *mp4mux_cam2;
  GstElement *encoder_cam2;
  GstElement *filesink_cam2;
  GstElement *queue_cam2;

  /* Main loop */
  GMainLoop *mloop;

  /* CAM1 ring buffer queue */
  GQueue   *buffers_queue;
  gboolean  switch_to_live;
  guint     process_src_id;

  /* CAM2 ring buffer queue (IPE Bypass mode only) */
  GQueue   *buffers_queue_cam2;
  gboolean  switch_to_live_cam2;
  guint     process_src_id_cam2;

  /* Camera ID (logical camera) */
  guint camera_id;

  /* Timing parameters */
  guint delay_to_start_recording;
  guint record_duration;
  guint queue_size;

  /* Tap-out mode */
  GstDynamicTapOut mode;

  /* Stream management */
  GList   *streams_list;
  gint     stream_cnt;
  GMutex   lock;
  gboolean exit;

  /* Synchronization */
  GCond        eos_signal;
  GCond        live_pts_signal;
  GstClockTime first_live_pts;

  /* Recording timing (PTS-based) */
  GstClockTime recording_start_pts;
  GstClockTime recording_end_pts;
  GstClockTime recording_mid_pts;
  gboolean     recording_ended;
  gboolean     mid_snapshot_taken;

  /* Pre-buffering timing (PTS-based) */
  GstClockTime prebuffer_start_pts;
  GstClockTime prebuffer_end_pts;
  GstClockTime prebuffer_mid_pts;
  gboolean     prebuffer_ended;
  gboolean     prebuffer_mid_snapshot_taken;

  /* Encoder name */
  gchar *encoder_name;

  /* Snapshot configuration */
  gint     snapshot_type;
  gint     noise_reduction_mode;
  gint     num_snapshots;
  gboolean enable_snapshot_streams;
  GPtrArray *meta_capture;

  /* RDI output resolution (for qticamimgreproc) */
  guint rdi_output_width;
  guint rdi_output_height;

  /* RAW stream/snapshot resolution (RDI ring buffer + RAW snapshots, both cameras) */
  gint  raw_width;            /* default: RDI_RING_BUFFER_WIDTH  = 4096 */
  gint  raw_height;           /* default: RDI_RING_BUFFER_HEIGHT = 3072 */

  /* JPEG snapshot resolution (both cameras, both modes) */
  gint  jpeg_snapshot_width;  /* default: SNAPSHOT_JPEG_WIDTH  = 4096 */
  gint  jpeg_snapshot_height; /* default: SNAPSHOT_JPEG_HEIGHT = 3072 */

  /* CAM2 standby configuration */
  guint standby_duration;    /* seconds CAM2 stays in standby at start of buffering (0=disabled) */
  gint  standby_camera_id;   /* physical camera ID to put in standby (default: 2 from CamX log) */
  gint  reproc_camera_id;    /* physical camera ID for qticamimgreproc reprocessing in RDI mode (default: 2) */

  guint livestream_start;    /* seconds into buffering to start live view (0=disabled) */
  guint livestream_stop;     /* seconds into buffering to stop live view */

  /* Master camera configuration */
  gint master_camera_id;     /* -1 = not set (disabled), >=0 = physical camera ID to set as master */

  /* Use case function pointer */
  void (*usecase_fn)(GstAppContext *appctx);
};

/* Forward declarations */
void release_stream(GstAppContext *appctx, GstStreamInf *stream);

/* ============================================================
 * Helper: clear a buffer queue safely
 * ============================================================ */
static void
clear_queue (GQueue *queue, GMutex *lock)
{
  if (!queue)
    return;

  g_mutex_lock (lock);
  while (!g_queue_is_empty (queue)) {
    GstBuffer *buf = (GstBuffer *) g_queue_pop_head (queue);
    if (buf)
      gst_buffer_unref (buf);
  }
  g_mutex_unlock (lock);
}

static void
clear_buffers_queue (GstAppContext *appctx)
{
  if (!appctx)
    return;
  clear_queue (appctx->buffers_queue, &appctx->lock);
  g_print ("[INFO] Cleared CAM1 buffer queue\n");
}

static void
clear_buffers_queue_cam2 (GstAppContext *appctx)
{
  if (!appctx)
    return;
  clear_queue (appctx->buffers_queue_cam2, &appctx->lock);
  g_print ("[INFO] Cleared CAM2 buffer queue\n");
}

/* ============================================================
 * Exit cleanup
 * ============================================================ */
static void
exit_cleanup (GstAppContext *appctx)
{
  g_print ("[INFO] Exit requested during prebuffering delay\n");
  g_print ("[INFO] Transitioning main pipeline to NULL state\n");
  gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->main_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  g_print ("[INFO] Transitioning CAM1 appsrc pipeline to NULL state\n");
  gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->appsrc_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (appctx->appsrc_pipeline_cam2) {
    g_print ("[INFO] Transitioning CAM2 appsrc pipeline to NULL state\n");
    gst_element_set_state (appctx->appsrc_pipeline_cam2, GST_STATE_NULL);
    gst_element_get_state (appctx->appsrc_pipeline_cam2, NULL, NULL,
        GST_CLOCK_TIME_NONE);
  }
}

/* ============================================================
 * Snapshot helpers
 * ============================================================ */
static void
gst_camera_metadata_release (gpointer data)
{
  ::camera::CameraMetadata *meta = (::camera::CameraMetadata *) data;
  delete meta;
}

static gboolean
trigger_snapshot (GstAppContext *appctx)
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

  g_signal_emit_by_name (qtiqmmfsrc, "capture-image",
      appctx->snapshot_type, appctx->num_snapshots,
      appctx->meta_capture, &success);

  if (success)
    g_print ("[INFO] Snapshot capture triggered successfully\n");
  else
    g_printerr ("[ERROR] Failed to trigger snapshot capture\n");

  gst_object_unref (qtiqmmfsrc);
  return FALSE;
}

static gboolean
capture_prepare_metadata (GstAppContext *appctx)
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

  g_object_get (G_OBJECT (qtiqmmfsrc), "video-metadata", &meta, NULL);
  if (!meta) {
    g_printerr ("failed to get image metadata\n");
    goto cleanupset;
  }

  if (appctx->meta_capture->len > 0)
    g_ptr_array_remove_range (appctx->meta_capture, 0, appctx->meta_capture->len);

  metadata = new ::camera::CameraMetadata (*meta);

  afmode = ANDROID_CONTROL_AF_MODE_OFF;
  metadata->update (ANDROID_CONTROL_AF_MODE, &afmode, 1);

  switch (appctx->noise_reduction_mode) {
    case 0:  noisemode = ANDROID_NOISE_REDUCTION_MODE_OFF;          break;
    case 1:  noisemode = ANDROID_NOISE_REDUCTION_MODE_FAST;         break;
    case 2:  noisemode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY; break;
    default: break;
  }

  metadata->update (ANDROID_NOISE_REDUCTION_MODE, &noisemode, 1);
  g_object_set (G_OBJECT (qtiqmmfsrc), "video-metadata", metadata, NULL);
  g_ptr_array_add (appctx->meta_capture, (gpointer) metadata);

  if (qtiqmmfsrc)
    gst_object_unref (qtiqmmfsrc);
  return TRUE;

cleanupset:
  if (qtiqmmfsrc) gst_object_unref (qtiqmmfsrc);
  if (meta)       delete meta;
  if (metadata)   delete metadata;
  return FALSE;
}

/* ============================================================
 * Caps creation helpers
 * ============================================================ */
static GstCaps *
create_stream_caps (gint width, gint height)
{
  GstCaps *caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width",  G_TYPE_INT,    width,
      "height", G_TYPE_INT,    height,
      "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  gst_caps_set_features (caps, 0, gst_caps_features_new ("memory:GBM", NULL));
  return caps;
}

static GstCaps *
create_bayer_caps (gint width, gint height)
{
  return gst_caps_new_simple ("video/x-bayer",
      "format", G_TYPE_STRING, "rggb",
      "bpp",    G_TYPE_STRING, "10",
      "width",  G_TYPE_INT,    width,
      "height", G_TYPE_INT,    height,
      "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
}

static GstCaps *
create_jpeg_snapshot_caps (gint width, gint height)
{
  return gst_caps_new_simple ("image/jpeg",
      "width",  G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
}

/* ============================================================
 * Encoder detection
 * ============================================================ */
static gchar *
get_encoder_name (void)
{
  if (gst_element_factory_find ("qtic2venc")) {
    g_print ("[INFO] Using qtic2venc encoder plugin\n");
    return (gchar *) "qtic2venc";
  } else if (gst_element_factory_find ("omxh264enc")) {
    g_print ("[INFO] Using omxh264enc encoder plugin\n");
    return (gchar *) "omxh264enc";
  } else {
    g_printerr ("[ERROR] No suitable encoder plugin found\n");
    return NULL;
  }
}

/* ============================================================
 * Pad probes
 * ============================================================ */

/* Captures PTS of first live frame from the recording stream */
GstPadProbeReturn
live_frame_probe (GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
  GstAppContext *ctx = (GstAppContext *) user_data;
  if (GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_BUFFER) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
    if (buffer && ctx->first_live_pts == GST_CLOCK_TIME_NONE) {
      ctx->first_live_pts = GST_BUFFER_PTS (buffer);
      g_cond_signal (&ctx->live_pts_signal);
      g_print ("[INFO] First live frame PTS: %" GST_TIME_FORMAT "\n",
          GST_TIME_ARGS (ctx->first_live_pts));
      return GST_PAD_PROBE_REMOVE;
    }
  }
  return GST_PAD_PROBE_OK;
}

/*
 * prebuffer_delay_control_probe_360:
 * Attached to the CAM1 FD stream (STR1) capsfilter src pad.
 * Tracks frame PTS to know when the pre-buffering delay has elapsed.
 * Unlike the original app, this probe does NOT stop the FD stream when
 * pre-buffering ends — it simply removes itself (FD stream continues).
 */
static GstPadProbeReturn
prebuffer_delay_control_probe_360 (GstPad *pad, GstPadProbeInfo *info,
    gpointer user_data)
{
  GstAppContext *ctx = (GstAppContext *) user_data;

  if (!(GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_BUFFER))
    return GST_PAD_PROBE_OK;

  GstBuffer    *buffer     = GST_PAD_PROBE_INFO_BUFFER (info);
  GstClockTime  buffer_pts = GST_BUFFER_PTS (buffer);

  /* Initialize prebuffer timing on first valid frame */
  if (ctx->prebuffer_start_pts == GST_CLOCK_TIME_NONE &&
      GST_CLOCK_TIME_IS_VALID (buffer_pts)) {
    g_mutex_lock (&ctx->lock);
    ctx->prebuffer_start_pts = buffer_pts;
    ctx->prebuffer_end_pts   = buffer_pts +
        (ctx->delay_to_start_recording * GST_SECOND);
    ctx->prebuffer_mid_pts   = buffer_pts +
        ((ctx->delay_to_start_recording * GST_SECOND) / 2);
    ctx->prebuffer_ended              = FALSE;
    ctx->prebuffer_mid_snapshot_taken = FALSE;
    g_mutex_unlock (&ctx->lock);

    g_print ("[INFO] Initialized prebuffer timing from first frame PTS: %"
        GST_TIME_FORMAT "\n", GST_TIME_ARGS (buffer_pts));
  }

  /* Mid-delay snapshot trigger */
  if (ctx->enable_snapshot_streams && !ctx->prebuffer_mid_snapshot_taken &&
      GST_CLOCK_TIME_IS_VALID (ctx->prebuffer_mid_pts) &&
      GST_CLOCK_TIME_IS_VALID (buffer_pts) &&
      buffer_pts >= ctx->prebuffer_mid_pts) {
    ctx->prebuffer_mid_snapshot_taken = TRUE;
    g_timeout_add (1, (GSourceFunc) trigger_snapshot, ctx);
  }

  /* Check if pre-buffering delay has elapsed */
  if (GST_CLOCK_TIME_IS_VALID (ctx->prebuffer_end_pts) &&
      GST_CLOCK_TIME_IS_VALID (buffer_pts) &&
      buffer_pts >= ctx->prebuffer_end_pts) {

    /* First time: signal worker thread to link recording streams */
    if (!ctx->prebuffer_ended) {
      ctx->prebuffer_ended = TRUE;
      g_mutex_lock (&ctx->lock);
      g_cond_signal (&ctx->eos_signal);
      g_mutex_unlock (&ctx->lock);
      return GST_PAD_PROBE_OK;
    }

    /* Check if prebuffer_end_pts has been updated to first_live_pts */
    g_mutex_lock (&ctx->lock);
    gboolean pts_updated = (ctx->first_live_pts != GST_CLOCK_TIME_NONE &&
                            ctx->prebuffer_end_pts == ctx->first_live_pts);
    g_mutex_unlock (&ctx->lock);

    if (pts_updated) {
      g_print ("[INFO] Pre-buffering ended, removing timing probe from FD stream\n");
      /* FD stream continues — just remove this probe */
      return GST_PAD_PROBE_REMOVE;
    }
  }

  return GST_PAD_PROBE_OK;
}

/* Controls exact recording duration based on frame PTS */
static GstPadProbeReturn
duration_control_probe (GstPad *pad, GstPadProbeInfo *info, gpointer user_data)
{
  GstAppContext *ctx = (GstAppContext *) user_data;

  if (!(GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_BUFFER))
    return GST_PAD_PROBE_OK;

  GstBuffer    *buffer     = GST_PAD_PROBE_INFO_BUFFER (info);
  GstClockTime  buffer_pts = GST_BUFFER_PTS (buffer);

  /* Mid-recording snapshot trigger */
  if (ctx->enable_snapshot_streams && !ctx->mid_snapshot_taken &&
      GST_CLOCK_TIME_IS_VALID (ctx->recording_mid_pts) &&
      GST_CLOCK_TIME_IS_VALID (buffer_pts) &&
      buffer_pts >= ctx->recording_mid_pts) {
    ctx->mid_snapshot_taken = TRUE;
    g_timeout_add (1, (GSourceFunc) trigger_snapshot, ctx);
  }

  /* Stop when recording duration is reached */
  if (GST_CLOCK_TIME_IS_VALID (ctx->recording_end_pts) &&
      GST_CLOCK_TIME_IS_VALID (buffer_pts) &&
      buffer_pts >= ctx->recording_end_pts) {

    GstElement *encoder = gst_pad_get_parent_element (pad);
    if (encoder) {
      gst_element_send_event (encoder, gst_event_new_eos ());
      gst_object_unref (encoder);
    }

    g_mutex_lock (&ctx->lock);
    if (!ctx->recording_ended) {
      ctx->recording_ended = TRUE;
      g_cond_signal (&ctx->eos_signal);
    }
    g_mutex_unlock (&ctx->lock);

    return GST_PAD_PROBE_DROP;
  }

  return GST_PAD_PROBE_OK;
}

/* ============================================================
 * Appsink callbacks (one per camera)
 * ============================================================ */
GstFlowReturn
on_new_sample_cam1 (GstAppSink *appsink, gpointer user_data)
{
  GstAppContext *ctx = (GstAppContext *) user_data;
  GstSample     *sample = gst_app_sink_pull_sample (appsink);

  if (!sample)
    return GST_FLOW_ERROR;

  GstBuffer *buffer = gst_sample_get_buffer (sample);
  if (!buffer) {
    gst_sample_unref (sample);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&ctx->lock);

  if (g_queue_get_length (ctx->buffers_queue) >= ctx->queue_size) {
    GstBuffer *old = (GstBuffer *) g_queue_pop_head (ctx->buffers_queue);
    if (old) gst_buffer_unref (old);
  }

  if (!ctx->switch_to_live) {
    gst_buffer_ref (buffer);
    g_queue_push_tail (ctx->buffers_queue, buffer);
  }

  g_mutex_unlock (&ctx->lock);
  gst_sample_unref (sample);
  return GST_FLOW_OK;
}

GstFlowReturn
on_new_sample_cam2 (GstAppSink *appsink, gpointer user_data)
{
  GstAppContext *ctx = (GstAppContext *) user_data;
  GstSample     *sample = gst_app_sink_pull_sample (appsink);

  if (!sample)
    return GST_FLOW_ERROR;

  GstBuffer *buffer = gst_sample_get_buffer (sample);
  if (!buffer) {
    gst_sample_unref (sample);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&ctx->lock);

  if (g_queue_get_length (ctx->buffers_queue_cam2) >= ctx->queue_size) {
    GstBuffer *old = (GstBuffer *) g_queue_pop_head (ctx->buffers_queue_cam2);
    if (old) gst_buffer_unref (old);
  }

  if (!ctx->switch_to_live_cam2) {
    gst_buffer_ref (buffer);
    g_queue_push_tail (ctx->buffers_queue_cam2, buffer);
  }

  g_mutex_unlock (&ctx->lock);
  gst_sample_unref (sample);
  return GST_FLOW_OK;
}

/* ============================================================
 * Utility
 * ============================================================ */
static gboolean
check_for_exit (GstAppContext *appctx)
{
  g_mutex_lock (&appctx->lock);
  gboolean e = appctx->exit;
  g_mutex_unlock (&appctx->lock);
  return e;
}

static gboolean
wait_for_eos (GstAppContext *appctx)
{
  g_mutex_lock (&appctx->lock);
  gint64   wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (5000000);
  gboolean signalled = g_cond_wait_until (&appctx->eos_signal,
      &appctx->lock, wait_time);
  if (!signalled)
    g_print ("[ERROR] Timeout waiting for EOS\n");
  g_mutex_unlock (&appctx->lock);
  return signalled;
}

static void
release_all_streams (GstAppContext *appctx)
{
  GList *list = NULL;
  for (list = appctx->streams_list; list != NULL; list = list->next)
    release_stream (appctx, (GstStreamInf *) list->data);
}

/* ============================================================
 * Signal handlers / bus callbacks
 * ============================================================ */
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
  if (appctx->appsrc_pipeline_cam2)
    gst_element_set_state (appctx->appsrc_pipeline_cam2, GST_STATE_NULL);

  if (appctx->buffers_queue)
    clear_buffers_queue (appctx);
  if (appctx->buffers_queue_cam2)
    clear_buffers_queue_cam2 (appctx);

  g_cond_signal (&appctx->eos_signal);

  if (appctx->mloop && g_main_loop_is_running (appctx->mloop))
    g_main_loop_quit (appctx->mloop);

  g_print ("[INFO] Interrupt handling complete\n");
  return TRUE;
}

static void
state_changed_cb (GstBus *bus, GstMessage *message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);
  g_print ("\n[INFO] Pipeline '%s' state changed from %s to %s, pending: %s\n",
      gst_object_get_name (GST_OBJECT (pipeline)),
      gst_element_state_get_name (old),
      gst_element_state_get_name (new_st),
      gst_element_state_get_name (pending));
}

static void
warning_cb (GstBus *bus, GstMessage *message, gpointer userdata)
{
  GError *error = NULL;
  gchar  *debug = NULL;
  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);
  g_free (debug);
  g_error_free (error);
}

static void
error_cb (GstBus *bus, GstMessage *message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop *) userdata;
  GError    *error = NULL;
  gchar     *debug = NULL;
  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);
  g_free (debug);
  g_error_free (error);
  g_main_loop_quit (mloop);
}

static void
eos_cb (GstBus *bus, GstMessage *message, gpointer userdata)
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

/* ============================================================
 * Stream element creation / release helpers
 * ============================================================ */

static void
set_encoder_props (GstElement *encoder, const gchar *encoder_name)
{
  g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);
  if (g_strcmp0 (encoder_name, "qtic2venc") == 0) {
    g_object_set (G_OBJECT (encoder), "control-rate", 3, NULL); /* VBR-CFR */
  } else {
    g_object_set (G_OBJECT (encoder), "periodicity-idr", 1, NULL);
    g_object_set (G_OBJECT (encoder), "interval-intraframes", 29, NULL);
    g_object_set (G_OBJECT (encoder), "control-rate", 2, NULL);
  }
}

static void
set_mp4mux_robust_props (GstElement *mp4mux)
{
  g_object_set (G_OBJECT (mp4mux),
      "reserved-moov-update-period", (guint64) 1000000,
      "reserved-bytes-per-sec",      (guint)   10000,
      "reserved-max-duration",       (guint64) 8000000000ULL,
      NULL);
}

static gboolean
create_snapshot_stream (GstAppContext *appctx, GstStreamInf *stream,
    GstElement *qtiqmmfsrc)
{
  gchar       temp_str[100];
  gboolean    ret = FALSE;
  const gchar *src_pad_name = NULL;

  if (!appctx || !stream || !qtiqmmfsrc || !stream->qmmf_caps || !stream->qmmf_pad) {
    g_printerr ("[ERROR] Snapshot: invalid arguments\n");
    return FALSE;
  }

  g_snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  g_snprintf (temp_str, sizeof (temp_str), "snapshot_sink_%d", appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("multifilesink", temp_str);

  if (!stream->capsfilter || !stream->filesink) {
    if (stream->capsfilter) gst_object_unref (stream->capsfilter);
    if (stream->filesink)   gst_object_unref (stream->filesink);
    g_printerr ("[ERROR] Snapshot elements could not be created\n");
    return FALSE;
  }

  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  /* Build output path */
  if (stream->output_path[0] != '\0') {
    g_strlcpy (temp_str, stream->output_path, sizeof (temp_str));
  } else if (stream->is_jpeg_snapshot) {
    g_snprintf (temp_str, sizeof (temp_str),
        "/data/360_snapshot_s%u-%%05d.jpg", appctx->stream_cnt);
  } else {
    g_snprintf (temp_str, sizeof (temp_str),
        "/data/360_snapshot_s%u-%%05d.raw", appctx->stream_cnt);
  }

  g_object_set (G_OBJECT (stream->filesink),
      "location",           temp_str,
      "post-messages",      FALSE,
      "enable-last-sample", FALSE,
      "max-files",          10,
      "async",              FALSE,
      NULL);

  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  if (!gst_element_sync_state_with_parent (stream->capsfilter) ||
      !gst_element_sync_state_with_parent (stream->filesink)) {
    g_printerr ("[ERROR] Snapshot: failed to sync state with parent\n");
    goto cleanup;
  }

  src_pad_name = gst_pad_get_name (stream->qmmf_pad);
  if (!src_pad_name) {
    g_printerr ("[ERROR] Snapshot: source pad name is NULL\n");
    goto cleanup;
  }

  ret = gst_element_link_pads_full (qtiqmmfsrc, src_pad_name,
      stream->capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Snapshot: link qmmfsrc->capsfilter failed\n");
    goto cleanup;
  }

  if (!gst_element_link_many (stream->capsfilter, stream->filesink, NULL)) {
    g_printerr ("[ERROR] Snapshot: link capsfilter->multifilesink failed\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  if (stream->capsfilter) gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  if (stream->filesink)   gst_element_set_state (stream->filesink,   GST_STATE_NULL);
  if (GST_IS_BIN (appctx->main_pipeline))
    gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
        stream->capsfilter, stream->filesink, NULL);
  stream->capsfilter = NULL;
  stream->filesink   = NULL;
  return FALSE;
}

static void
release_snapshot_stream (GstAppContext *appctx, GstStreamInf *stream)
{
  GstElement *qtiqmmfsrc = NULL;

  if (!appctx || !stream) return;

  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");

  g_print ("[INFO] Unlinking elements for snapshot stream...\n");
  if (qtiqmmfsrc && stream->capsfilter)
    gst_element_unlink (qtiqmmfsrc, stream->capsfilter);
  if (stream->capsfilter && stream->filesink)
    gst_element_unlink (stream->capsfilter, stream->filesink);

  if (stream->capsfilter) {
    gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
    gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  }
  if (stream->filesink) {
    gst_element_set_state (stream->filesink, GST_STATE_NULL);
    gst_element_get_state (stream->filesink, NULL, NULL, GST_CLOCK_TIME_NONE);
  }

  if (GST_IS_BIN (appctx->main_pipeline) &&
      (stream->capsfilter || stream->filesink))
    gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
        stream->capsfilter, stream->filesink, NULL);

  stream->capsfilter = NULL;
  stream->filesink   = NULL;

  if (qtiqmmfsrc) gst_object_unref (qtiqmmfsrc);
}

static gboolean
create_encoder_stream (GstAppContext *appctx, GstStreamInf *stream,
    GstElement *qtiqmmfsrc)
{
  static guint output_cnt = 0;
  gchar    temp_str[100];
  gboolean ret = FALSE;

  g_snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  g_snprintf (temp_str, sizeof (temp_str), "encoder_%d", appctx->stream_cnt);
  stream->encoder = gst_element_factory_make (appctx->encoder_name, temp_str);

  g_snprintf (temp_str, sizeof (temp_str), "h264parse_%d", appctx->stream_cnt);
  stream->h264parse = gst_element_factory_make ("h264parse", temp_str);

  g_snprintf (temp_str, sizeof (temp_str), "mp4mux_%d", appctx->stream_cnt);
  stream->mp4mux = gst_element_factory_make ("mp4mux", temp_str);

  g_snprintf (temp_str, sizeof (temp_str), "filesink_%d", appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("filesink", temp_str);

  if (!stream->capsfilter || !stream->encoder || !stream->h264parse ||
      !stream->mp4mux || !stream->filesink) {
    if (stream->capsfilter) gst_object_unref (stream->capsfilter);
    if (stream->encoder)    gst_object_unref (stream->encoder);
    if (stream->h264parse)  gst_object_unref (stream->h264parse);
    if (stream->mp4mux)     gst_object_unref (stream->mp4mux);
    if (stream->filesink)   gst_object_unref (stream->filesink);
    g_printerr ("[ERROR] Encoder stream elements could not be created\n");
    return FALSE;
  }

  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);
  set_encoder_props (stream->encoder, appctx->encoder_name);
  set_mp4mux_robust_props (stream->mp4mux);

  /* Output file path */
  if (stream->output_path[0] != '\0') {
    g_object_set (G_OBJECT (stream->filesink), "location", stream->output_path, NULL);
  } else {
    g_snprintf (temp_str, sizeof (temp_str), "/data/360_video_%u.mp4", output_cnt++);
    g_object_set (G_OBJECT (stream->filesink), "location", temp_str, NULL);
  }

  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->encoder);
  gst_element_sync_state_with_parent (stream->h264parse);
  gst_element_sync_state_with_parent (stream->mp4mux);
  gst_element_sync_state_with_parent (stream->filesink);

  ret = gst_element_link_pads_full (qtiqmmfsrc,
      gst_pad_get_name (stream->qmmf_pad), stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Encoder stream: link qmmfsrc->capsfilter failed\n");
    goto cleanup;
  }

  if (!gst_element_link_many (stream->capsfilter, stream->encoder,
          stream->h264parse, stream->mp4mux, stream->filesink, NULL)) {
    g_printerr ("[ERROR] Encoder stream: link chain failed\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->encoder,    GST_STATE_NULL);
  gst_element_set_state (stream->h264parse,  GST_STATE_NULL);
  gst_element_set_state (stream->mp4mux,     GST_STATE_NULL);
  gst_element_set_state (stream->filesink,   GST_STATE_NULL);
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);
  return FALSE;
}

static void
release_encoder_stream (GstAppContext *appctx, GstStreamInf *stream)
{
  GstState    state = GST_STATE_VOID_PENDING;
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");

  g_print ("[INFO] Unlinking elements for encoder stream...\n");
  if (qtiqmmfsrc)
    gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter, NULL);

  gst_element_get_state (appctx->main_pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
  if (state == GST_STATE_PLAYING && stream->encoder)
    gst_element_send_event (stream->encoder, gst_event_new_eos ());

  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->encoder,    GST_STATE_NULL);
  gst_element_get_state (stream->encoder,    NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->h264parse,  GST_STATE_NULL);
  gst_element_get_state (stream->h264parse,  NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->mp4mux,     GST_STATE_NULL);
  gst_element_get_state (stream->mp4mux,     NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->filesink,   GST_STATE_NULL);
  gst_element_get_state (stream->filesink,   NULL, NULL, GST_CLOCK_TIME_NONE);

  if (qtiqmmfsrc)
    gst_element_unlink_many (stream->capsfilter, stream->encoder,
        stream->h264parse, stream->mp4mux, stream->filesink, NULL);

  g_print ("[INFO] Unlinked successfully for encoder stream\n");

  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->encoder, stream->h264parse,
      stream->mp4mux, stream->filesink, NULL);

  stream->capsfilter = NULL;
  stream->encoder    = NULL;
  stream->h264parse  = NULL;
  stream->mp4mux     = NULL;
  stream->filesink   = NULL;

  if (qtiqmmfsrc) gst_object_unref (qtiqmmfsrc);
}

static gboolean
create_appsink_stream (GstAppContext *appctx, GstStreamInf *stream,
    GstElement *qtiqmmfsrc)
{
  gchar    temp_str[100];
  gboolean ret = FALSE;

  g_snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  g_snprintf (temp_str, sizeof (temp_str), "appsink_%d", appctx->stream_cnt);
  stream->appsink = gst_element_factory_make ("appsink", temp_str);

  if (!stream->capsfilter || !stream->appsink) {
    if (stream->capsfilter) gst_object_unref (stream->capsfilter);
    if (stream->appsink)    gst_object_unref (stream->appsink);
    g_printerr ("[ERROR] Appsink stream elements could not be created\n");
    return FALSE;
  }

  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);
  gst_app_sink_set_emit_signals (GST_APP_SINK (stream->appsink), TRUE);

  /* Connect the correct callback based on which camera this stream belongs to */
  if (stream->logical_stream_type == CAM1_LOGICAL_STREAM_TYPE)
    g_signal_connect (stream->appsink, "new-sample",
        G_CALLBACK (on_new_sample_cam1), appctx);
  else
    g_signal_connect (stream->appsink, "new-sample",
        G_CALLBACK (on_new_sample_cam2), appctx);

  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->appsink, NULL);

  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->appsink);

  ret = gst_element_link_pads_full (qtiqmmfsrc,
      gst_pad_get_name (stream->qmmf_pad), stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Appsink stream: link qmmfsrc->capsfilter failed\n");
    goto cleanup;
  }

  if (!gst_element_link_many (stream->capsfilter, stream->appsink, NULL)) {
    g_printerr ("[ERROR] Appsink stream: link capsfilter->appsink failed\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->appsink,    GST_STATE_NULL);
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->appsink, NULL);
  return FALSE;
}

static void
release_appsink_stream (GstAppContext *appctx, GstStreamInf *stream)
{
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");

  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] qmmfsrc not found in pipeline\n");
    return;
  }

  g_print ("[INFO] Unlinking elements for appsink stream...\n");
  gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter, stream->appsink, NULL);
  g_print ("[INFO] Unlinked successfully for appsink stream\n");

  gst_element_set_locked_state (stream->capsfilter, TRUE);
  gst_element_set_locked_state (stream->appsink,    TRUE);

  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->appsink,    GST_STATE_NULL);
  gst_element_get_state (stream->appsink,    NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->appsink, NULL);

  stream->capsfilter = NULL;
  stream->appsink    = NULL;

  gst_object_unref (qtiqmmfsrc);
}

static gboolean
create_dummy_stream (GstAppContext *appctx, GstStreamInf *stream,
    GstElement *qtiqmmfsrc)
{
  gchar    temp_str[100];
  gboolean ret = FALSE;

  g_snprintf (temp_str, sizeof (temp_str), "capsfilter_%d", appctx->stream_cnt);
  stream->capsfilter = gst_element_factory_make ("capsfilter", temp_str);

  g_snprintf (temp_str, sizeof (temp_str), "fakesink_%d", appctx->stream_cnt);
  stream->filesink = gst_element_factory_make ("fakesink", temp_str);

  if (!stream->capsfilter || !stream->filesink) {
    if (stream->capsfilter) gst_object_unref (stream->capsfilter);
    if (stream->filesink)   gst_object_unref (stream->filesink);
    g_printerr ("[ERROR] Dummy stream elements could not be created\n");
    return FALSE;
  }

  g_object_set (G_OBJECT (stream->capsfilter), "caps", stream->qmmf_caps, NULL);

  gst_bin_add_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  gst_element_sync_state_with_parent (stream->capsfilter);
  gst_element_sync_state_with_parent (stream->filesink);

  ret = gst_element_link_pads_full (qtiqmmfsrc,
      gst_pad_get_name (stream->qmmf_pad), stream->capsfilter, NULL,
      GST_PAD_LINK_CHECK_DEFAULT);
  if (!ret) {
    g_printerr ("[ERROR] Dummy stream: link qmmfsrc->capsfilter failed\n");
    goto cleanup;
  }

  if (!gst_element_link_many (stream->capsfilter, stream->filesink, NULL)) {
    g_printerr ("[ERROR] Dummy stream: link capsfilter->fakesink failed\n");
    goto cleanup;
  }

  return TRUE;

cleanup:
  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_set_state (stream->filesink,   GST_STATE_NULL);
  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);
  return FALSE;
}

static void
release_dummy_stream (GstAppContext *appctx, GstStreamInf *stream)
{
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");

  g_print ("[INFO] Unlinking elements for dummy stream...\n");
  if (qtiqmmfsrc)
    gst_element_unlink_many (qtiqmmfsrc, stream->capsfilter, stream->filesink, NULL);
  g_print ("[INFO] Unlinked successfully for dummy stream\n");

  gst_element_set_state (stream->capsfilter, GST_STATE_NULL);
  gst_element_get_state (stream->capsfilter, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_element_set_state (stream->filesink,   GST_STATE_NULL);
  gst_element_get_state (stream->filesink,   NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_bin_remove_many (GST_BIN (appctx->main_pipeline),
      stream->capsfilter, stream->filesink, NULL);

  stream->capsfilter = NULL;
  stream->filesink   = NULL;

  if (qtiqmmfsrc) gst_object_unref (qtiqmmfsrc);
}

/* ============================================================
 * link_stream / unlink_stream
 * ============================================================ */
static void
link_stream (GstAppContext *appctx, GstStreamInf *stream)
{
  gboolean    ret = FALSE;
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");

  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return;
  }

  gst_pad_set_active (stream->qmmf_pad, TRUE);
  g_print ("[INFO] Pad name - %s\n", gst_pad_get_name (stream->qmmf_pad));

  if (stream->is_encoder)
    ret = create_encoder_stream (appctx, stream, qtiqmmfsrc);
  else
    ret = create_appsink_stream (appctx, stream, qtiqmmfsrc);

  if (!ret) {
    g_printerr ("[ERROR] failed to create stream\n");
    gst_object_unref (qtiqmmfsrc);
    return;
  }

  appctx->stream_cnt++;
  gst_object_unref (qtiqmmfsrc);
}

static void
unlink_stream (GstAppContext *appctx, GstStreamInf *stream)
{
  if (stream->qmmf_pad)
    gst_pad_set_active (stream->qmmf_pad, FALSE);

  if (stream->is_dummy) {
    release_dummy_stream (appctx, stream);
    stream->is_dummy = FALSE;
  } else if (stream->is_encoder) {
    release_encoder_stream (appctx, stream);
  } else if (stream->is_jpeg_snapshot || stream->is_raw_snapshot) {
    release_snapshot_stream (appctx, stream);
  } else {
    release_appsink_stream (appctx, stream);
  }

  g_print ("\n");
}

/* ============================================================
 * Session metadata configuration (DynamicTapOut)
 * ============================================================ */
static gboolean
configure_metadata (GstAppContext *appctx)
{
  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    return FALSE;
  }

  ::camera::CameraMetadata  session_meta (128, 128);
  ::camera::CameraMetadata *static_meta = nullptr;
  uint32_t tag;

  const std::shared_ptr<::camera::VendorTagDescriptor> vtags =
      ::camera::VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (!vtags) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    gst_object_unref (qtiqmmfsrc);
    return FALSE;
  }

  g_object_get (G_OBJECT (qtiqmmfsrc), "static-metadata", &static_meta, NULL);
  if (!static_meta) {
    g_printerr ("[WARN] Failed to retrieve static metadata\n");
    gst_object_unref (qtiqmmfsrc);
    return FALSE;
  }

  gint ret = static_meta->getTagFromName (CAMERA_SESSION_TAG, vtags.get (), &tag);
  if (ret != 0) {
    g_printerr ("[WARN] DynamicTapOut vendor tag not found\n");
    gst_object_unref (qtiqmmfsrc);
    return FALSE;
  }

  int32_t mode_val = static_cast<int32_t> (appctx->mode);
  session_meta.update (tag, &mode_val, 1);

  /* Set AppMasterCameraId session parameter if specified */
  if (appctx->master_camera_id >= 0) {
    uint32_t master_tag = 0;
    gint master_ret = static_meta->getTagFromName (CAMERA_MASTER_CAM_TAG,
        vtags.get (), &master_tag);
    if (master_ret != 0) {
      g_printerr ("[WARN] AppMasterCameraId vendor tag not found\n");
    } else {
      int32_t master_cam_val = static_cast<int32_t> (appctx->master_camera_id);
      session_meta.update (master_tag, &master_cam_val, 1);
      g_print ("[INFO] Session metadata (AppMasterCameraId=%d) updated\n",
          master_cam_val);
    }
  }

  g_object_set (G_OBJECT (qtiqmmfsrc), "session-metadata", &session_meta, NULL);
  g_print ("[INFO] Session metadata (DynamicTapOut=%d) updated successfully\n",
      mode_val);

  gst_object_unref (qtiqmmfsrc);
  return TRUE;
}

/* ============================================================
 * create_stream — main stream factory
 * logical_stream_type: 0 = CAM1, 1 = CAM2
 * output_path: output file path for encoder streams (NULL = auto-generate)
 * ============================================================ */
static GstStreamInf *
create_stream (GstAppContext *appctx, GstStreamInfo type,
               gint w, gint h,
               gint logical_stream_type,
               const gchar *output_path)
{
  gboolean      ret    = FALSE;
  GstStreamInf *stream = g_new0 (GstStreamInf, 1);
  gint          pad_type;
  GstPadTemplate *qtiqmmfsrc_template;

  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] Failed to retrieve qtiqmmfsrc element\n");
    g_free (stream);
    return NULL;
  }

  stream->is_dummy          = FALSE;
  stream->is_encoder        = FALSE;
  stream->is_raw_snapshot   = FALSE;
  stream->is_jpeg_snapshot  = FALSE;
  stream->logical_stream_type = logical_stream_type;
  stream->output_path[0]    = '\0';

  if (output_path)
    g_strlcpy (stream->output_path, output_path, sizeof (stream->output_path));

  switch (type) {
    case GST_STREAM_TYPE_DUMMY_ENCODER:
      stream->is_dummy   = TRUE;
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

  stream->width  = w;
  stream->height = h;

  /* Build caps */
  stream->qmmf_caps = create_stream_caps (w, h);
  switch (type) {
    case GST_STREAM_TYPE_APPSINK:
      if (appctx->mode == GST_TAPOUT_RDI) {
        gst_caps_unref (stream->qmmf_caps);
        stream->qmmf_caps = create_bayer_caps (w, h);
      }
      break;
    case GST_STREAM_TYPE_JPEG:
      gst_caps_unref (stream->qmmf_caps);
      stream->qmmf_caps = create_jpeg_snapshot_caps (w, h);
      break;
    case GST_STREAM_TYPE_RAW:
      gst_caps_unref (stream->qmmf_caps);
      stream->qmmf_caps = create_bayer_caps (w, h);
      break;
    default:
      break;
  }

  /* Request pad from qtiqmmfsrc */
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);

  if (type == GST_STREAM_TYPE_JPEG || type == GST_STREAM_TYPE_RAW) {
    qtiqmmfsrc_template =
        gst_element_class_get_pad_template (klass, "image_%u");
    stream->qmmf_pad =
        gst_element_request_pad (qtiqmmfsrc, qtiqmmfsrc_template, "image_%u", NULL);
  } else {
    qtiqmmfsrc_template =
        gst_element_class_get_pad_template (klass, "video_%u");
    stream->qmmf_pad =
        gst_element_request_pad (qtiqmmfsrc, qtiqmmfsrc_template, "video_%u", NULL);
  }

  if (!stream->qmmf_pad) {
    g_printerr ("[ERROR] pad cannot be retrieved from qtiqmmfsrc!\n");
    goto cleanup;
  }

  g_print ("[INFO] Pad received - %s (cam%d)\n",
      gst_pad_get_name (stream->qmmf_pad),
      logical_stream_type + 1);

  /* Set logical-stream-type on the pad to route to correct physical camera */
  g_object_set (G_OBJECT (stream->qmmf_pad),
      "logical-stream-type", logical_stream_type, NULL);

  /* Set pad type (preview=1, video=0) for video pads */
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

  if (type != GST_STREAM_TYPE_JPEG && type != GST_STREAM_TYPE_RAW)
    g_object_set (G_OBJECT (stream->qmmf_pad), "type", pad_type, NULL);

  /* Create the stream elements */
  if (stream->is_dummy) {
    ret = create_dummy_stream (appctx, stream, qtiqmmfsrc);
  } else if (stream->is_encoder) {
    ret = create_encoder_stream (appctx, stream, qtiqmmfsrc);
  } else if (stream->is_jpeg_snapshot || stream->is_raw_snapshot) {
    ret = create_snapshot_stream (appctx, stream, qtiqmmfsrc);
  } else {
    /* Appsink: allocate extra buffers to match queue size */
    g_object_set (G_OBJECT (stream->qmmf_pad),
        "extra-buffers", (guint) appctx->queue_size, NULL);
    if (appctx->mode == GST_TAPOUT_RDI)
    g_object_set (G_OBJECT (stream->qmmf_pad), "attach-cam-meta", TRUE, NULL);
    ret = create_appsink_stream (appctx, stream, qtiqmmfsrc);
  }

  if (!ret) {
    g_printerr ("[ERROR] failed to create stream\n");
    goto cleanup;
  }

  appctx->streams_list = g_list_append (appctx->streams_list, stream);
  appctx->stream_cnt++;

  gst_object_unref (qtiqmmfsrc);
  return stream;

cleanup:
  if (stream->qmmf_pad) {
    gst_pad_set_active (stream->qmmf_pad, FALSE);
    gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);
  }
  gst_object_unref (qtiqmmfsrc);
  gst_caps_unref (stream->qmmf_caps);
  g_free (stream);
  return NULL;
}

/* ============================================================
 * release_stream
 * ============================================================ */
void
release_stream (GstAppContext *appctx, GstStreamInf *stream)
{
  unlink_stream (appctx, stream);

  GstElement *qtiqmmfsrc =
      gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] Failed to retrieve qtiqmmfsrc in release_stream\n");
    gst_caps_unref (stream->qmmf_caps);
    appctx->streams_list = g_list_remove (appctx->streams_list, stream);
    g_free (stream);
    return;
  }

  gst_element_release_request_pad (qtiqmmfsrc, stream->qmmf_pad);
  gst_object_unref (qtiqmmfsrc);
  gst_caps_unref (stream->qmmf_caps);

  appctx->streams_list = g_list_remove (appctx->streams_list, stream);
  g_free (stream);
  g_print ("\n");
}

/* ============================================================
 * Pipeline state helpers
 * ============================================================ */
static gboolean
wait_for_state_change (GstElement *pipeline)
{
  g_return_val_if_fail (pipeline != NULL, FALSE);

  const gchar *name = gst_object_get_name (GST_OBJECT (pipeline));
  g_print ("[INFO] Pipeline '%s' is PREROLLING ...\n", name);

  GstStateChangeReturn ret =
      gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("[ERROR] Pipeline '%s' failed to PREROLL!\n", name);
    return FALSE;
  }
  return TRUE;
}

/* ============================================================
 * Buffer pushing (appsrc drain)
 * ============================================================ */
static gboolean
process_queued_buffers (gpointer user_data)
{
  ProcessBuffersCtx *ctx    = (ProcessBuffersCtx *) user_data;
  GstAppContext     *appctx = ctx->appctx;
  gint               cam_id = ctx->cam_id;

  if (check_for_exit (appctx)) {
    g_print ("[INFO] Exit requested, stopping buffer processing (cam%d)\n",
        cam_id + 1);
    g_free (ctx);
    return FALSE;
  }

  /* Select the correct appsrc and queue */
  const gchar *appsrc_name = (cam_id == 0) ? "appsrc" : "appsrc_cam2";
  GstElement  *appsrc_elem =
      gst_bin_get_by_name (GST_BIN (
          (cam_id == 0) ? appctx->appsrc_pipeline
                        : appctx->appsrc_pipeline_cam2),
          appsrc_name);

  if (!appsrc_elem) {
    g_printerr ("[ERROR] Failed to retrieve appsrc element (cam%d)\n", cam_id + 1);
    g_free (ctx);
    return FALSE;
  }

  GstAppSrc *src   = GST_APP_SRC (appsrc_elem);
  GQueue    *queue = (cam_id == 0) ? appctx->buffers_queue
                                   : appctx->buffers_queue_cam2;

  /* Check if queue is empty */
  g_mutex_lock (&appctx->lock);
  gboolean empty = g_queue_is_empty (queue);
  g_mutex_unlock (&appctx->lock);

  if (empty) {
    gst_app_src_end_of_stream (src);
    g_print ("[INFO] CAM%d buffer queue empty, sending EOS\n", cam_id + 1);
    gst_object_unref (appsrc_elem);
    g_free (ctx);
    return FALSE;
  }

  /* Pop one buffer */
  g_mutex_lock (&appctx->lock);
  GstBuffer *buffer = GST_BUFFER (g_queue_pop_head (queue));
  g_mutex_unlock (&appctx->lock);

  /* Discard frames that overlap with live recording */
  if (GST_CLOCK_TIME_IS_VALID (appctx->first_live_pts) &&
      GST_BUFFER_PTS (buffer) >= appctx->first_live_pts) {
    g_print ("[INFO] CAM%d: discarding buffer after live PTS reached\n",
        cam_id + 1);
    gst_buffer_unref (buffer);
  } else {
    gst_app_src_push_buffer (src, buffer);
  }

  gst_object_unref (appsrc_elem);
  return TRUE;
}

static void
start_pushing_buffers (GstAppContext *appctx, gint cam_id)
{
  ProcessBuffersCtx *ctx = g_new0 (ProcessBuffersCtx, 1);
  ctx->appctx = appctx;
  ctx->cam_id = cam_id;

  g_print ("[INFO] Starting to push CAM%d queued buffers to appsrc pipeline\n",
      cam_id + 1);

  guint src_id = g_timeout_add (10, process_queued_buffers, ctx);

  if (cam_id == 0)
    appctx->process_src_id = src_id;
  else
    appctx->process_src_id_cam2 = src_id;
}

static void
set_cam2_standby (GstAppContext *appctx, gboolean enable)
{
  GstElement *qtiqmmfsrc = NULL;
  ::camera::CameraMetadata *meta = nullptr;
  uint32_t tag = 0;

  qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->main_pipeline), "qmmf");
  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] set_cam2_standby: Failed to retrieve qtiqmmfsrc\n");
    return;
  }

  /* Get current video metadata from qtiqmmfsrc */
  g_object_get (G_OBJECT (qtiqmmfsrc), "video-metadata", &meta, NULL);
  if (!meta) {
    g_printerr ("[ERROR] set_cam2_standby: Failed to get video metadata\n");
    gst_object_unref (qtiqmmfsrc);
    return;
  }

  /* Look up the SensorStandByCameraId vendor tag */
  const std::shared_ptr<::camera::VendorTagDescriptor> vtags =
      ::camera::VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (!vtags) {
    g_printerr ("[ERROR] set_cam2_standby: Failed to get vendor tag descriptor\n");
    delete meta;
    gst_object_unref (qtiqmmfsrc);
    return;
  }

  /* Set SensorStandByFlag — required by CamX to process the standby request */
  uint32_t flag_tag = 0;
  int flag_ret = meta->getTagFromName (
      "org.codeaurora.qcamera3.sensorwriteinput.SensorStandByFlag",
      vtags.get (), &flag_tag);
  if (flag_ret == 0 && flag_tag != 0) {
    guint8 flag_val = enable ? 1 : 0;
    meta->update (flag_tag, &flag_val, 1);
  } else {
    g_printerr ("[WARN] set_cam2_standby: SensorStandByFlag tag not found\n");
  }

  /* Set SensorStandByCameraId — tells CamX which physical sensor to standby */
  int ret = meta->getTagFromName (
      "org.codeaurora.qcamera3.sensorwriteinput.SensorStandByCameraId",
      vtags.get (), &tag);

  if (ret != 0 || tag == 0) {
    g_printerr ("[WARN] set_cam2_standby: SensorStandByCameraId tag not found\n");
    delete meta;
    gst_object_unref (qtiqmmfsrc);
    return;
  }

  int32_t cam_id = enable ? (int32_t) appctx->standby_camera_id : -1;
  meta->update (tag, &cam_id, 1);

  g_print ("[INFO] CAM2 (physical id=%d) standby: %s (SensorStandByFlag=%d, SensorStandByCameraId=%d)\n",
      appctx->standby_camera_id, enable ? "ON" : "OFF",
      enable ? 1 : 0, (int) cam_id);

  /* Apply — triggers SetCameraParam() in QMMF SDK */
  g_object_set (G_OBJECT (qtiqmmfsrc), "video-metadata", meta, NULL);

  delete meta;
  gst_object_unref (qtiqmmfsrc);
}

static gboolean
wakeup_cam2_callback (gpointer user_data)
{
  GstAppContext *appctx = (GstAppContext *) user_data;

  if (check_for_exit (appctx)) {
    g_print ("[INFO] App exiting, skipping CAM2 wake-up\n");
    return FALSE;  /* one-shot, remove timer */
  }

  g_print ("[INFO] CAM2 standby duration elapsed, waking up CAM2\n");
  set_cam2_standby (appctx, FALSE);  /* SensorStandByCameraId = -1 */

  return FALSE;
}

typedef struct {
  GstAppContext *appctx;
  GstStreamInf  *str3_cam1;   /* CAM1 480P live view stream */
  GstStreamInf  *str_cam2;    /* CAM2 480P live view stream (str8 RDI / str9 IPE) */
} LivestreamCtx;

static gboolean
stop_livestream_callback (gpointer user_data)
{
  LivestreamCtx *ctx = (LivestreamCtx *) user_data;

  if (check_for_exit (ctx->appctx)) {
    g_free (ctx);
    return FALSE;
  }

  g_print ("[INFO] Stopping live view streams during buffering\n");

  unlink_stream (ctx->appctx, ctx->str3_cam1);
  unlink_stream (ctx->appctx, ctx->str_cam2);

  /* Restore original recording output paths for the recording phase */
  g_strlcpy (ctx->str3_cam1->output_path,
      "/data/360_rec_cam1_480p.mp4",
      sizeof (ctx->str3_cam1->output_path));
  g_strlcpy (ctx->str_cam2->output_path,
      "/data/360_rec_cam2_480p.mp4",
      sizeof (ctx->str_cam2->output_path));

  g_free (ctx);
  return FALSE;  /* one-shot */
}

static gboolean
start_livestream_callback (gpointer user_data)
{
  LivestreamCtx *ctx = (LivestreamCtx *) user_data;

  if (check_for_exit (ctx->appctx)) {
    g_free (ctx);
    return FALSE;
  }

  g_print ("[INFO] Starting live view streams during buffering\n");

  /* Set livestream output file names before linking */
  g_strlcpy (ctx->str3_cam1->output_path,
      "/data/360_livestream_buffering_cam1.mp4",
      sizeof (ctx->str3_cam1->output_path));
  g_strlcpy (ctx->str_cam2->output_path,
      "/data/360_livestream_buffering_cam2.mp4",
      sizeof (ctx->str_cam2->output_path));

  link_stream (ctx->appctx, ctx->str3_cam1);
  link_stream (ctx->appctx, ctx->str_cam2);

  /* Schedule stop timer for (livestream_stop - livestream_start) seconds */
  guint duration = ctx->appctx->livestream_stop - ctx->appctx->livestream_start;
  g_timeout_add_seconds (duration, stop_livestream_callback, ctx);

  return FALSE;  /* one-shot */
}

/* ============================================================
 * Main use case: 360-degree dual-camera pre-buffering
 * ============================================================ */
static void
prebuffering_usecase_360 (GstAppContext *appctx)
{
  /* Stream handles */
  GstStreamInf *str0_cam1 = NULL;  /* ring buffer (appsink) */
  GstStreamInf *str1_cam1 = NULL;  /* FD 480P (continuous encoder) */
  GstStreamInf *str2_cam1 = NULL;  /* 1080P dummy -> recording encoder */
  GstStreamInf *str3_cam1 = NULL;  /* 480P  dummy -> recording encoder */
  GstStreamInf *str4_cam1 = NULL;  /* JPEG snapshot (optional) */
  GstStreamInf *str5_cam1 = NULL;  /* RAW  snapshot (optional) */

  GstStreamInf *str6_cam2 = NULL;  /* ring buffer (appsink, IPE only) or FD (RDI) */
  GstStreamInf *str7_cam2 = NULL;  /* FD 480P (IPE) or 1080P dummy (RDI) */
  GstStreamInf *str8_cam2 = NULL;  /* 1080P dummy -> recording encoder */
  GstStreamInf *str9_cam2 = NULL;  /* 480P  dummy -> recording encoder (IPE) or JPEG (RDI) */
  GstStreamInf *str10_cam2 = NULL; /* JPEG snapshot (IPE) or RAW snapshot (RDI) */
  GstStreamInf *str11_cam2 = NULL; /* RAW  snapshot (IPE only) */

  gboolean is_rdi = (appctx->mode == GST_TAPOUT_RDI);

  /* ----------------------------------------------------------
   * Phase 1: Create all streams
   * ---------------------------------------------------------- */

  if (is_rdi) {
    /* === RDI mode: 11 streams === */

    /* STR0: CAM1 RAW10 ring buffer */
    g_print ("[INFO] Creating STR0: CAM1 RAW ring buffer (%dx%d)\n",
        appctx->raw_width, appctx->raw_height);
    str0_cam1 = create_stream (appctx, GST_STREAM_TYPE_APPSINK,
        appctx->raw_width, appctx->raw_height,
        CAM1_LOGICAL_STREAM_TYPE, NULL);
    if (!str0_cam1) {
      g_printerr ("Failed to create STR0\n");
      return;
    }

    /* STR1: CAM1 FD 480P (continuous) */
    g_print ("[INFO] Creating STR1: CAM1 FD 480P (continuous)\n");
    str1_cam1 = create_stream (appctx, GST_STREAM_TYPE_ENCODER_BUFFERING,
        FD_STREAM_WIDTH, FD_STREAM_HEIGHT,
        CAM1_LOGICAL_STREAM_TYPE, "/data/360_fd_cam1.mp4");
    if (!str1_cam1) {
      g_printerr ("Failed to create STR1\n");
      release_stream (appctx, str0_cam1);
      return;
    }

    /* STR2: CAM1 1080P dummy -> recording */
    g_print ("[INFO] Creating STR2: CAM1 1080P recording (dummy)\n");
    str2_cam1 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER,
        REC_1080P_WIDTH, REC_1080P_HEIGHT,
        CAM1_LOGICAL_STREAM_TYPE, "/data/360_rec_cam1_1080p.mp4");
    if (!str2_cam1) {
      g_printerr ("Failed to create STR2\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      return;
    }

    /* STR3: CAM1 480P dummy -> recording */
    g_print ("[INFO] Creating STR3: CAM1 480P live view (dummy)\n");
    str3_cam1 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER,
        REC_480P_WIDTH, REC_480P_HEIGHT,
        CAM1_LOGICAL_STREAM_TYPE, "/data/360_rec_cam1_480p.mp4");
    if (!str3_cam1) {
      g_printerr ("Failed to create STR3\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      release_stream (appctx, str2_cam1);
      return;
    }

    /* Optional CAM1 snapshot streams — created before CAM2 streams
     * so that CamX stream order is: CAM1[0-5] then CAM2[6-10] */
    if (appctx->enable_snapshot_streams) {
      appctx->meta_capture =
          g_ptr_array_new_full (0, gst_camera_metadata_release);

      g_print ("[INFO] Creating STR4: CAM1 JPEG snapshot (%dx%d)\n",
          appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height);
      str4_cam1 = create_stream (appctx, GST_STREAM_TYPE_JPEG,
          appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height,
          CAM1_LOGICAL_STREAM_TYPE,
          "/data/360_snapshot_cam1-%05d.jpg");
      if (!str4_cam1) {
        g_printerr ("Failed to create STR4\n");
        return;
      }

      g_print ("[INFO] Creating STR5: CAM1 RAW MFNR snapshot (%dx%d)\n",
          appctx->raw_width, appctx->raw_height);
      str5_cam1 = create_stream (appctx, GST_STREAM_TYPE_RAW,
          appctx->raw_width, appctx->raw_height,
          CAM1_LOGICAL_STREAM_TYPE,
          "/data/360_snapshot_cam1-%05d.raw");
      if (!str5_cam1) {
        g_printerr ("Failed to create STR5\n");
        return;
      }
    }

    /* STR6: CAM2 FD 480P (continuous, RDI has no CAM2 ring buffer) */
    g_print ("[INFO] Creating STR6: CAM2 FD 480P (continuous)\n");
    str6_cam2 = create_stream (appctx, GST_STREAM_TYPE_ENCODER_BUFFERING,
        FD_STREAM_WIDTH, FD_STREAM_HEIGHT,
        CAM2_LOGICAL_STREAM_TYPE, "/data/360_fd_cam2.mp4");
    if (!str6_cam2) {
      g_printerr ("Failed to create STR6\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      release_stream (appctx, str2_cam1);
      release_stream (appctx, str3_cam1);
      return;
    }

    /* STR7: CAM2 1080P dummy -> recording */
    g_print ("[INFO] Creating STR7: CAM2 1080P recording (dummy)\n");
    str7_cam2 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER,
        REC_1080P_WIDTH, REC_1080P_HEIGHT,
        CAM2_LOGICAL_STREAM_TYPE, "/data/360_rec_cam2_1080p.mp4");
    if (!str7_cam2) {
      g_printerr ("Failed to create STR7\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      release_stream (appctx, str2_cam1);
      release_stream (appctx, str3_cam1);
      release_stream (appctx, str6_cam2);
      return;
    }

    /* STR8: CAM2 480P dummy -> recording */
    g_print ("[INFO] Creating STR8: CAM2 480P live view (dummy)\n");
    str8_cam2 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER,
        REC_480P_WIDTH, REC_480P_HEIGHT,
        CAM2_LOGICAL_STREAM_TYPE, "/data/360_rec_cam2_480p.mp4");
    if (!str8_cam2) {
      g_printerr ("Failed to create STR8\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      release_stream (appctx, str2_cam1);
      release_stream (appctx, str3_cam1);
      release_stream (appctx, str6_cam2);
      release_stream (appctx, str7_cam2);
      return;
    }

    /* Optional CAM2 snapshot streams */
    if (appctx->enable_snapshot_streams) {
      g_print ("[INFO] Creating STR9: CAM2 JPEG snapshot (%dx%d)\n",
          appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height);
      str9_cam2 = create_stream (appctx, GST_STREAM_TYPE_JPEG,
          appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height,
          CAM2_LOGICAL_STREAM_TYPE,
          "/data/360_snapshot_cam2-%05d.jpg");
      if (!str9_cam2) {
        g_printerr ("Failed to create STR9\n");
        return;
      }

      g_print ("[INFO] Creating STR10: CAM2 RAW MFNR snapshot (%dx%d)\n",
          appctx->raw_width, appctx->raw_height);
      str10_cam2 = create_stream (appctx, GST_STREAM_TYPE_RAW,
          appctx->raw_width, appctx->raw_height,
          CAM2_LOGICAL_STREAM_TYPE,
          "/data/360_snapshot_cam2-%05d.raw");
      if (!str10_cam2) {
        g_printerr ("Failed to create STR10\n");
        return;
      }
    }

  } else {
    /* === IPE Bypass mode: 12 streams === */

    /* STR0: CAM1 YUV 1080P ring buffer */
    g_print ("[INFO] Creating STR0: CAM1 YUV ring buffer (%dx%d)\n",
        IPE_RING_BUFFER_WIDTH, IPE_RING_BUFFER_HEIGHT);
    str0_cam1 = create_stream (appctx, GST_STREAM_TYPE_APPSINK,
        IPE_RING_BUFFER_WIDTH, IPE_RING_BUFFER_HEIGHT,
        CAM1_LOGICAL_STREAM_TYPE, NULL);
    if (!str0_cam1) { g_printerr ("Failed to create STR0\n"); return; }

    /* STR1: CAM1 FD 480P (continuous) */
    g_print ("[INFO] Creating STR1: CAM1 FD 480P (continuous)\n");
    str1_cam1 = create_stream (appctx, GST_STREAM_TYPE_ENCODER_BUFFERING,
        FD_STREAM_WIDTH, FD_STREAM_HEIGHT,
        CAM1_LOGICAL_STREAM_TYPE, "/data/360_fd_cam1.mp4");
    if (!str1_cam1) {
      g_printerr ("Failed to create STR1\n");
      release_stream (appctx, str0_cam1);
      return;
    }

    /* STR2: CAM1 1080P dummy -> recording */
    g_print ("[INFO] Creating STR2: CAM1 1080P recording (dummy)\n");
    str2_cam1 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER,
        REC_1080P_WIDTH, REC_1080P_HEIGHT,
        CAM1_LOGICAL_STREAM_TYPE, "/data/360_rec_cam1_1080p.mp4");
    if (!str2_cam1) {
      g_printerr ("Failed to create STR2\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      return;
    }

    /* STR3: CAM1 480P dummy -> recording */
    g_print ("[INFO] Creating STR3: CAM1 480P live view (dummy)\n");
    str3_cam1 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER,
        REC_480P_WIDTH, REC_480P_HEIGHT,
        CAM1_LOGICAL_STREAM_TYPE, "/data/360_rec_cam1_480p.mp4");
    if (!str3_cam1) {
      g_printerr ("Failed to create STR3\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      release_stream (appctx, str2_cam1);
      return;
    }

    /* Optional CAM1 snapshot streams — created before CAM2 streams
     * so that CamX stream order is: CAM1[0-5] then CAM2[6-11] */
    if (appctx->enable_snapshot_streams) {
      appctx->meta_capture =
          g_ptr_array_new_full (0, gst_camera_metadata_release);

      g_print ("[INFO] Creating STR4: CAM1 JPEG snapshot (%dx%d)\n",
          appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height);
      str4_cam1 = create_stream (appctx, GST_STREAM_TYPE_JPEG,
          appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height,
          CAM1_LOGICAL_STREAM_TYPE,
          "/data/360_snapshot_cam1-%05d.jpg");
      if (!str4_cam1) {
        g_printerr ("Failed to create STR4\n");
        return;
      }

      g_print ("[INFO] Creating STR5: CAM1 RAW MFNR snapshot (%dx%d)\n",
          appctx->raw_width, appctx->raw_height);
      str5_cam1 = create_stream (appctx, GST_STREAM_TYPE_RAW,
          appctx->raw_width, appctx->raw_height,
          CAM1_LOGICAL_STREAM_TYPE,
          "/data/360_snapshot_cam1-%05d.raw");
      if (!str5_cam1) {
        g_printerr ("Failed to create STR5\n");
        return;
      }
    }

    /* STR6: CAM2 YUV 1080P ring buffer */
    g_print ("[INFO] Creating STR6: CAM2 YUV ring buffer (%dx%d)\n",
        IPE_RING_BUFFER_WIDTH, IPE_RING_BUFFER_HEIGHT);
    str6_cam2 = create_stream (appctx, GST_STREAM_TYPE_APPSINK,
        IPE_RING_BUFFER_WIDTH, IPE_RING_BUFFER_HEIGHT,
        CAM2_LOGICAL_STREAM_TYPE, NULL);
    if (!str6_cam2) {
      g_printerr ("Failed to create STR6\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      release_stream (appctx, str2_cam1);
      release_stream (appctx, str3_cam1);
      return;
    }

    /* STR7: CAM2 FD 480P (continuous) */
    g_print ("[INFO] Creating STR7: CAM2 FD 480P (continuous)\n");
    str7_cam2 = create_stream (appctx, GST_STREAM_TYPE_ENCODER_BUFFERING,
        FD_STREAM_WIDTH, FD_STREAM_HEIGHT,
        CAM2_LOGICAL_STREAM_TYPE, "/data/360_fd_cam2.mp4");
    if (!str7_cam2) {
      g_printerr ("Failed to create STR7\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      release_stream (appctx, str2_cam1);
      release_stream (appctx, str3_cam1);
      release_stream (appctx, str6_cam2);
      return;
    }

    /* STR8: CAM2 1080P dummy -> recording */
    g_print ("[INFO] Creating STR8: CAM2 1080P recording (dummy)\n");
    str8_cam2 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER,
        REC_1080P_WIDTH, REC_1080P_HEIGHT,
        CAM2_LOGICAL_STREAM_TYPE, "/data/360_rec_cam2_1080p.mp4");
    if (!str8_cam2) {
      g_printerr ("Failed to create STR8\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      release_stream (appctx, str2_cam1);
      release_stream (appctx, str3_cam1);
      release_stream (appctx, str6_cam2);
      release_stream (appctx, str7_cam2);
      return;
    }

    /* STR9: CAM2 480P dummy -> recording */
    g_print ("[INFO] Creating STR9: CAM2 480P live view (dummy)\n");
    str9_cam2 = create_stream (appctx, GST_STREAM_TYPE_DUMMY_ENCODER,
        REC_480P_WIDTH, REC_480P_HEIGHT,
        CAM2_LOGICAL_STREAM_TYPE, "/data/360_rec_cam2_480p.mp4");
    if (!str9_cam2) {
      g_printerr ("Failed to create STR9\n");
      release_stream (appctx, str0_cam1);
      release_stream (appctx, str1_cam1);
      release_stream (appctx, str2_cam1);
      release_stream (appctx, str3_cam1);
      release_stream (appctx, str6_cam2);
      release_stream (appctx, str7_cam2);
      release_stream (appctx, str8_cam2);
      return;
    }

    /* Optional CAM2 snapshot streams */
    if (appctx->enable_snapshot_streams) {
      g_print ("[INFO] Creating STR10: CAM2 JPEG snapshot (%dx%d)\n",
          appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height);
      str10_cam2 = create_stream (appctx, GST_STREAM_TYPE_JPEG,
          appctx->jpeg_snapshot_width, appctx->jpeg_snapshot_height,
          CAM2_LOGICAL_STREAM_TYPE,
          "/data/360_snapshot_cam2-%05d.jpg");
      if (!str10_cam2) {
        g_printerr ("Failed to create STR10\n");
        return;
      }

      g_print ("[INFO] Creating STR11: CAM2 RAW MFNR snapshot (%dx%d)\n",
          appctx->raw_width, appctx->raw_height);
      str11_cam2 = create_stream (appctx, GST_STREAM_TYPE_RAW,
          appctx->raw_width, appctx->raw_height,
          CAM2_LOGICAL_STREAM_TYPE,
          "/data/360_snapshot_cam2-%05d.raw");
      if (!str11_cam2) {
        g_printerr ("Failed to create STR11\n");
        return;
      }
    }
  }

  /* ----------------------------------------------------------
   * Phase 2: Attach prebuffer timing probe to CAM1 FD stream (STR1)
   * ---------------------------------------------------------- */
  if (str1_cam1->capsfilter) {
    GstPad *src_pad =
        gst_element_get_static_pad (str1_cam1->capsfilter, "src");
    if (src_pad) {
      gst_pad_add_probe (src_pad, GST_PAD_PROBE_TYPE_BUFFER,
          prebuffer_delay_control_probe_360, appctx, NULL);
      gst_object_unref (src_pad);
    }
  }

  /* ----------------------------------------------------------
   * Phase 3: Attach live_frame_probe to CAM1 1080P dummy (STR2) qmmf pad
   * ---------------------------------------------------------- */
  gst_pad_add_probe (str2_cam1->qmmf_pad, GST_PAD_PROBE_TYPE_BUFFER,
      live_frame_probe, appctx, NULL);

  /* ----------------------------------------------------------
   * Phase 4: PAUSED → configure metadata → unlink dummies → PLAYING
   * ---------------------------------------------------------- */
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->main_pipeline, GST_STATE_PAUSED))
    wait_for_state_change (appctx->main_pipeline);

  if (!configure_metadata (appctx))
    g_printerr ("[WARN] Failed to configure camera session params\n");

  g_print ("[INFO] Unlinking dummy recording streams before PLAYING\n");
  unlink_stream (appctx, str2_cam1);
  unlink_stream (appctx, str3_cam1);
  if (is_rdi) {
    unlink_stream (appctx, str7_cam2);
    unlink_stream (appctx, str8_cam2);
  } else {
    unlink_stream (appctx, str8_cam2);
    unlink_stream (appctx, str9_cam2);
  }

  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (appctx->main_pipeline, GST_STATE_PLAYING))
    wait_for_state_change (appctx->main_pipeline);

  /* Start both appsrc pipelines */
  gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_PLAYING);
  if (!is_rdi && appctx->appsrc_pipeline_cam2)
    gst_element_set_state (appctx->appsrc_pipeline_cam2, GST_STATE_PLAYING);

  if (appctx->standby_duration > 0) {
    g_print ("[INFO] Putting CAM2 (physical id=%d) in standby for %u seconds\n",
        appctx->standby_camera_id, appctx->standby_duration);
    set_cam2_standby (appctx, TRUE);
    g_timeout_add_seconds (appctx->standby_duration, wakeup_cam2_callback, appctx);
  }

  if (appctx->livestream_start > 0 &&
      appctx->livestream_stop > appctx->livestream_start &&
      appctx->livestream_stop <= appctx->delay_to_start_recording) {

    LivestreamCtx *ls_ctx = g_new0 (LivestreamCtx, 1);
    ls_ctx->appctx    = appctx;
    ls_ctx->str3_cam1 = str3_cam1;
    /* RDI: CAM2 480P live view = str8_cam2; IPE: str9_cam2 */
    ls_ctx->str_cam2  = is_rdi ? str8_cam2 : str9_cam2;

    g_print ("[INFO] Scheduling live view during buffering: t=%us to t=%us\n",
        appctx->livestream_start, appctx->livestream_stop);

    g_timeout_add_seconds (appctx->livestream_start,
        start_livestream_callback, ls_ctx);
  }

  /* ----------------------------------------------------------
   * Phase 5: Pre-buffering — wait for delay to elapse
   * ---------------------------------------------------------- */
  g_print ("[INFO] Pre-buffering in progress...\n");

  if (appctx->enable_snapshot_streams) {
    if (!capture_prepare_metadata (appctx)) {
      g_printerr ("[ERROR] Failed to prepare capture metadata\n");
      g_ptr_array_free (appctx->meta_capture, TRUE);
      return;
    }
  }

  g_print ("[INFO] Waiting %u seconds before switching to live recording...\n",
      appctx->delay_to_start_recording);

  g_mutex_lock (&appctx->lock);
  while (!appctx->prebuffer_ended && !appctx->exit) {
    gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (1000000);
    g_cond_wait_until (&appctx->eos_signal, &appctx->lock, wait_time);
  }
  g_mutex_unlock (&appctx->lock);

  if (check_for_exit (appctx)) {
    exit_cleanup (appctx);
    return;
  }

  /* ----------------------------------------------------------
   * Phase 6: Link recording streams
   * ---------------------------------------------------------- */
  g_print ("[INFO] Linking live recording streams\n");
  link_stream (appctx, str2_cam1);
  link_stream (appctx, str3_cam1);
  if (is_rdi) {
    link_stream (appctx, str7_cam2);
    link_stream (appctx, str8_cam2);
  } else {
    link_stream (appctx, str8_cam2);
    link_stream (appctx, str9_cam2);
  }

  /* ----------------------------------------------------------
   * Phase 7: Wait for first live frame PTS
   * ---------------------------------------------------------- */
  g_mutex_lock (&appctx->lock);
  while (appctx->first_live_pts == GST_CLOCK_TIME_NONE && !appctx->exit)
    g_cond_wait (&appctx->live_pts_signal, &appctx->lock);

  if (GST_CLOCK_TIME_IS_VALID (appctx->first_live_pts))
    appctx->prebuffer_end_pts = appctx->first_live_pts;

  g_mutex_unlock (&appctx->lock);

  /* ----------------------------------------------------------
   * Phase 8: Set recording duration and add duration probes
   * ---------------------------------------------------------- */
  appctx->recording_start_pts = appctx->first_live_pts;
  appctx->recording_end_pts   = appctx->recording_start_pts +
                                 (appctx->record_duration * GST_SECOND);
  appctx->recording_mid_pts   = appctx->recording_start_pts +
                                 ((appctx->record_duration * GST_SECOND) / 2);
  appctx->recording_ended     = FALSE;
  appctx->mid_snapshot_taken  = FALSE;

  g_print ("[INFO] Live recording: %" GST_TIME_FORMAT " → %" GST_TIME_FORMAT
           " (%u seconds)\n",
           GST_TIME_ARGS (appctx->recording_start_pts),
           GST_TIME_ARGS (appctx->recording_end_pts),
           appctx->record_duration);

  /* Add duration_control_probe to all 4 recording streams */
  GstStreamInf *rec_streams[4];
  rec_streams[0] = str2_cam1;
  rec_streams[1] = str3_cam1;
  if (is_rdi) {
    rec_streams[2] = str7_cam2;
    rec_streams[3] = str8_cam2;
  } else {
    rec_streams[2] = str8_cam2;
    rec_streams[3] = str9_cam2;
  }

  for (gint i = 0; i < 4; i++) {
    if (rec_streams[i] && rec_streams[i]->capsfilter) {
      GstPad *src_pad =
          gst_element_get_static_pad (rec_streams[i]->capsfilter, "src");
      if (src_pad) {
        gst_pad_add_probe (src_pad, GST_PAD_PROBE_TYPE_BUFFER,
            duration_control_probe, appctx, NULL);
        gst_object_unref (src_pad);
      }
    }
  }

  /* ----------------------------------------------------------
   * Phase 9: Switch to live — stop filling queues, start draining
   * ---------------------------------------------------------- */
  appctx->switch_to_live      = TRUE;
  appctx->switch_to_live_cam2 = TRUE;

  start_pushing_buffers (appctx, 0);  /* CAM1 */
  if (!is_rdi)
    start_pushing_buffers (appctx, 1);  /* CAM2 (IPE only) */

  /* Release ring buffer appsink streams */
  release_stream (appctx, str0_cam1);
  if (!is_rdi)
    release_stream (appctx, str6_cam2);

  g_print ("[INFO] Live recording started for %u seconds\n",
      appctx->record_duration);

  /* ----------------------------------------------------------
   * Phase 10: Wait for recording to complete
   * ---------------------------------------------------------- */
  g_print ("[INFO] Waiting for recording to complete...\n");
  g_mutex_lock (&appctx->lock);
  while (!appctx->recording_ended && !appctx->exit) {
    gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (1000000);
    g_cond_wait_until (&appctx->eos_signal, &appctx->lock, wait_time);
  }
  g_mutex_unlock (&appctx->lock);

  g_print ("[INFO] Recording completed at exactly %u seconds\n",
      appctx->record_duration);

  /* ----------------------------------------------------------
   * Phase 11: Cleanup
   * ---------------------------------------------------------- */
  clear_buffers_queue (appctx);
  if (!is_rdi)
    clear_buffers_queue_cam2 (appctx);

  g_print ("[INFO] Sending EOS to main pipeline\n");
  gst_element_send_event (appctx->main_pipeline, gst_event_new_eos ());
  wait_for_eos (appctx);

  g_print ("[INFO] Transitioning main pipeline to NULL\n");
  gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->main_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  g_print ("[INFO] Transitioning CAM1 appsrc pipeline to NULL\n");
  gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);
  gst_element_get_state (appctx->appsrc_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (!is_rdi && appctx->appsrc_pipeline_cam2) {
    g_print ("[INFO] Transitioning CAM2 appsrc pipeline to NULL\n");
    gst_element_set_state (appctx->appsrc_pipeline_cam2, GST_STATE_NULL);
    gst_element_get_state (appctx->appsrc_pipeline_cam2, NULL, NULL,
        GST_CLOCK_TIME_NONE);
  }

  /* Release recording streams */
  release_stream (appctx, str2_cam1);
  release_stream (appctx, str3_cam1);
  if (is_rdi) {
    release_stream (appctx, str7_cam2);
    release_stream (appctx, str8_cam2);
  } else {
    release_stream (appctx, str8_cam2);
    release_stream (appctx, str9_cam2);
  }

  /* Release FD streams (ran continuously) */
  release_stream (appctx, str1_cam1);
  if (is_rdi)
    release_stream (appctx, str6_cam2);
  else
    release_stream (appctx, str7_cam2);

  /* Release snapshot streams */
  if (appctx->enable_snapshot_streams) {
    if (str4_cam1)  release_stream (appctx, str4_cam1);
    if (str5_cam1)  release_stream (appctx, str5_cam1);
    if (str10_cam2) release_stream (appctx, str10_cam2);
    if (is_rdi) {
      /* RDI: str9_cam2 = JPEG, str10_cam2 = RAW */
      if (str9_cam2) release_stream (appctx, str9_cam2);
    } else {
      /* IPE: str11_cam2 = RAW */
      if (str11_cam2) release_stream (appctx, str11_cam2);
    }
  }

  g_print ("[INFO] Cleanup complete\n");
}

/* ============================================================
 * Worker thread
 * ============================================================ */
static void *
thread_fn (gpointer user_data)
{
  GstAppContext *appctx = (GstAppContext *) user_data;

  appctx->usecase_fn (appctx);

  if (!check_for_exit (appctx) &&
      appctx->mloop &&
      g_main_loop_is_running (appctx->mloop))
    g_main_loop_quit (appctx->mloop);

  return NULL;
}

/* ============================================================
 * main()
 * ============================================================ */
gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx          = NULL;
  GMainLoop      *mloop        = NULL;
  GstBus         *bus          = NULL;
  guint           intrpt_watch_id = 0;
  GstCaps        *filtercaps   = NULL;
  GstCaps        *caps         = NULL;
  GstPad         *sinkpad      = NULL;
  GstElement     *pipeline     = NULL;
  GstElement     *qtiqmmfsrc   = NULL;

  /* CAM1 appsrc pipeline elements */
  GstElement *appsrc       = NULL;
  GstElement *queue        = NULL;
  GstElement *encoder      = NULL;
  GstElement *filesink     = NULL;
  GstElement *h264parse    = NULL;
  GstElement *mp4mux       = NULL;
  GstElement *camimgreproc = NULL;
  GstElement *capsfilter   = NULL;

  /* CAM2 appsrc pipeline elements */
  GstElement *pipeline_cam2  = NULL;
  GstElement *appsrc_cam2    = NULL;
  GstElement *queue_cam2     = NULL;
  GstElement *encoder_cam2   = NULL;
  GstElement *filesink_cam2  = NULL;
  GstElement *h264parse_cam2 = NULL;
  GstElement *mp4mux_cam2    = NULL;

  GstAppContext *appctx = g_new0 (GstAppContext, 1);
  g_mutex_init (&appctx->lock);
  g_cond_init  (&appctx->eos_signal);
  g_cond_init  (&appctx->live_pts_signal);

  /* Defaults */
  appctx->stream_cnt                = 0;
  appctx->camera_id                 = 0;  /* logical camera */
  appctx->delay_to_start_recording  = DELAY_TO_START_RECORDING;
  appctx->queue_size                = MAX_QUEUE_SIZE;
  appctx->mode                      = GST_TAPOUT_IPEBYPASS;
  appctx->usecase_fn                = prebuffering_usecase_360;
  appctx->first_live_pts            = GST_CLOCK_TIME_NONE;
  appctx->switch_to_live            = FALSE;
  appctx->switch_to_live_cam2       = FALSE;
  appctx->record_duration           = RECORD_DURATION;
  appctx->recording_start_pts       = GST_CLOCK_TIME_NONE;
  appctx->recording_end_pts         = GST_CLOCK_TIME_NONE;
  appctx->recording_mid_pts         = GST_CLOCK_TIME_NONE;
  appctx->recording_ended           = FALSE;
  appctx->mid_snapshot_taken        = FALSE;
  appctx->prebuffer_start_pts       = GST_CLOCK_TIME_NONE;
  appctx->prebuffer_end_pts         = GST_CLOCK_TIME_NONE;
  appctx->prebuffer_mid_pts         = GST_CLOCK_TIME_NONE;
  appctx->prebuffer_ended           = FALSE;
  appctx->prebuffer_mid_snapshot_taken = FALSE;
  appctx->enable_snapshot_streams   = FALSE;
  appctx->meta_capture              = NULL;
  appctx->snapshot_type             = 0;
  appctx->noise_reduction_mode      = 0;
  appctx->num_snapshots             = 1;
  appctx->rdi_output_width          = RDI_OUTPUT_WIDTH_DEFAULT;
  appctx->rdi_output_height         = RDI_OUTPUT_HEIGHT_DEFAULT;
  appctx->raw_width                 = RDI_RING_BUFFER_WIDTH;   /* 4096 */
  appctx->raw_height                = RDI_RING_BUFFER_HEIGHT;  /* 3072 */
  appctx->jpeg_snapshot_width       = SNAPSHOT_JPEG_WIDTH;     /* 4096 */
  appctx->jpeg_snapshot_height      = SNAPSHOT_JPEG_HEIGHT;    /* 3072 */
  appctx->standby_duration          = 0;   /* disabled by default */
  appctx->standby_camera_id         = 2;   /* physical CAM2 ID from CamX log */
  appctx->reproc_camera_id          = 2;   /* physical CAM2 ID for qticamimgreproc */
  appctx->livestream_start          = 0;   /* disabled by default */
  appctx->livestream_stop           = 0;
  appctx->master_camera_id          = -1;  /* disabled by default */

  GOptionEntry entries[] = {
    {
      "camera-id", 'c', 0, G_OPTION_ARG_INT, &appctx->camera_id,
      "Camera ID (logical camera, default: 0)", "id"
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
      "Tap out mode: 1 - RDI, 2 - IPE By Pass", "mode"
    },
    {
      "enable-snapshot-streams", 'e', 0, G_OPTION_ARG_NONE,
      &appctx->enable_snapshot_streams, "Enable snapshot streams", NULL
    },
    {
      "num-snapshots", 'n', 0, G_OPTION_ARG_INT, &appctx->num_snapshots,
      "Number of snapshots to capture", "count"
    },
    {
      "snapshot-type", 'y', 0, G_OPTION_ARG_INT, &appctx->snapshot_type,
      "Snapshot type: 0 - video, 1 - still", "type"
    },
    {
      "noise-reduction-mode", 'm', 0, G_OPTION_ARG_INT,
      &appctx->noise_reduction_mode,
      "Noise reduction mode: 0-off, 1-fast, 2-high_quality", "mode"
    },
    {
      "width", 'w', 0, G_OPTION_ARG_INT, &appctx->raw_width,
      "RAW width: RDI ring buffer + RAW snapshots, both cameras"
      " (RDI mode, default: 4096)", "width"
    },
    {
      "height", 'h', 0, G_OPTION_ARG_INT, &appctx->raw_height,
      "RAW height: RDI ring buffer + RAW snapshots, both cameras"
      " (RDI mode, default: 3072)", "height"
    },
    {
      "snapshot-jpeg-width", 'j', 0, G_OPTION_ARG_INT, &appctx->jpeg_snapshot_width,
      "JPEG snapshot width for both cameras (default: 4096)", "width"
    },
    {
      "snapshot-jpeg-height", 'k', 0, G_OPTION_ARG_INT, &appctx->jpeg_snapshot_height,
      "JPEG snapshot height for both cameras (default: 3072)", "height"
    },
    {
      "rdi-output-width", 'x', 0, G_OPTION_ARG_INT, &appctx->rdi_output_width,
      "RDI output width (for reprocessing)", "width"
    },
    {
      "rdi-output-height", 'z', 0, G_OPTION_ARG_INT, &appctx->rdi_output_height,
      "RDI output height (for reprocessing)", "height"
    },
    {
      "standby-duration", 'b', 0, G_OPTION_ARG_INT, &appctx->standby_duration,
      "CAM2 standby duration at start of buffering in seconds (0=disabled)", "seconds"
    },
    {
      "standby-camera-id", 0, 0, G_OPTION_ARG_INT, &appctx->standby_camera_id,
      "Physical camera ID for standby (default: 2)", "id"
    },
    {
      "reproc-camera-id", 0, 0, G_OPTION_ARG_INT, &appctx->reproc_camera_id,
      "Physical camera ID for qticamimgreproc reprocessing in RDI mode (default: 2)", "id"
    },
    {
      "livestream-start", 0, 0, G_OPTION_ARG_INT, &appctx->livestream_start,
      "Start live view (STR3/STR8) during buffering at this second (0=disabled)", "seconds"
    },
    {
      "livestream-stop", 0, 0, G_OPTION_ARG_INT, &appctx->livestream_stop,
      "Stop live view (STR3/STR8) during buffering at this second", "seconds"
    },
    {
      "master-camera-id", 0, 0, G_OPTION_ARG_INT, &appctx->master_camera_id,
      "Physical camera ID to configure as master (-1 = disabled, default: -1)", "id"
    },
    { NULL }
  };

  /* Parse command line */
  if ((ctx = g_option_context_new ("360-degree dual-camera pre-buffering")) != NULL) {
    gboolean  success = FALSE;
    GError   *error   = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());
    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success) {
      if (error) {
        g_printerr ("[ERROR] Failed to parse options: %s\n",
            GST_STR_NULL (error->message));
        g_clear_error (&error);
      } else {
        g_printerr ("[ERROR] Initializing: Unknown error!\n");
      }
      g_free (appctx);
      return -EFAULT;
    }
  } else {
    g_printerr ("[ERROR] Failed to create options context!\n");
    g_free (appctx);
    return -EFAULT;
  }

  /* Validate options */
  if (appctx->mode != GST_TAPOUT_RDI && appctx->mode != GST_TAPOUT_IPEBYPASS) {
    g_printerr ("[ERROR] Invalid tap-out mode: %d (use 1=RDI or 2=IPEBypass)\n",
        appctx->mode);
    g_free (appctx);
    return -EFAULT;
  }

  if (appctx->delay_to_start_recording == 0)
    g_printerr ("[WARN] Delay is 0 — pre-buffering will be ineffective\n");

  if (appctx->queue_size == 0) {
    g_printerr ("[ERROR] Queue size cannot be 0\n");
    g_free (appctx);
    return -EFAULT;
  }

  g_print ("[INFO] Parsed Options:\n");
  g_print ("[INFO]   Camera ID (logical): %u\n", appctx->camera_id);
  g_print ("[INFO]   Tap-out mode: %s\n",
      appctx->mode == GST_TAPOUT_RDI ? "RDI (11 streams)" : "IPE Bypass (12 streams)");
  g_print ("[INFO]   Delay: %u seconds\n", appctx->delay_to_start_recording);
  g_print ("[INFO]   Record Duration: %u seconds\n", appctx->record_duration);
  g_print ("[INFO]   Queue Size: %u\n", appctx->queue_size);
  g_print ("[INFO]   Snapshots: %s\n",
      appctx->enable_snapshot_streams ? "enabled" : "disabled");
  if (appctx->mode == GST_TAPOUT_RDI)
    g_print ("[INFO]   RDI Output: %ux%u\n",
        appctx->rdi_output_width, appctx->rdi_output_height);

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  appctx->encoder_name = get_encoder_name ();
  if (!appctx->encoder_name) {
    g_free (appctx);
    return -EFAULT;
  }

  /* --------------------------------------------------------
   * Build main pipeline (camera source)
   * -------------------------------------------------------- */
  pipeline = gst_pipeline_new ("gst-360-main-pipeline");
  appctx->main_pipeline = pipeline;

  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  if (!qtiqmmfsrc) {
    g_printerr ("[ERROR] Failed to create qtiqmmfsrc element\n");
    gst_object_unref (appctx->main_pipeline);
    g_free (appctx);
    return -EFAULT;
  }

  g_object_set (G_OBJECT (qtiqmmfsrc), "name",   "qmmf",             NULL);
  g_object_set (G_OBJECT (qtiqmmfsrc), "camera", appctx->camera_id,  NULL);
  gst_bin_add (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);

  /* Initialize main loop */
  mloop = g_main_loop_new (NULL, FALSE);
  if (!mloop) {
    gst_bin_remove (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);
    gst_object_unref (appctx->main_pipeline);
    g_printerr ("[ERROR] Failed to create main loop!\n");
    g_free (appctx);
    return -1;
  }
  appctx->mloop = mloop;

  /* --------------------------------------------------------
   * Build CAM1 appsrc pipeline
   * -------------------------------------------------------- */
  pipeline  = gst_pipeline_new ("gst-360-appsrc-cam1-pipeline");
  appsrc    = gst_element_factory_make ("appsrc",  "appsrc");
  queue     = gst_element_factory_make ("queue",   "queue");
  encoder   = gst_element_factory_make (appctx->encoder_name, "encoder");
  filesink  = gst_element_factory_make ("filesink", "filesink");
  h264parse = gst_element_factory_make ("h264parse", "h264parse");
  mp4mux    = gst_element_factory_make ("mp4mux",   "mp4mux");

  if (appctx->mode == GST_TAPOUT_RDI) {
    camimgreproc = gst_element_factory_make ("qticamimgreproc", "camimgreproc");
    capsfilter   = gst_element_factory_make ("capsfilter",      "capsfilter");
    if (!pipeline || !appsrc || !queue || !camimgreproc || !capsfilter ||
        !encoder || !filesink || !h264parse || !mp4mux) {
      g_printerr ("[ERROR] CAM1 appsrc pipeline elements could not be created\n");
      return -1;
    }
  } else {
    if (!pipeline || !appsrc || !queue || !encoder || !filesink ||
        !h264parse || !mp4mux) {
      g_printerr ("[ERROR] CAM1 appsrc pipeline elements could not be created\n");
      return -1;
    }
  }

  g_object_set (G_OBJECT (h264parse), "name", "h264parse", NULL);
  g_object_set (G_OBJECT (mp4mux),    "name", "mp4mux",    NULL);
  g_object_set (G_OBJECT (encoder),   "name", "encoder",   NULL);
  g_object_set (G_OBJECT (filesink),  "name", "filesink",  NULL);
  set_encoder_props (encoder, appctx->encoder_name);
  g_object_set (G_OBJECT (filesink),
      "location", "/data/360_prebuffered_cam1.mp4",
      "enable-last-sample", FALSE, NULL);

  if (appctx->mode == GST_TAPOUT_RDI) {
    filtercaps = gst_caps_new_simple ("video/x-bayer",
        "format", G_TYPE_STRING, "rggb", "bpp", G_TYPE_STRING, "10",
        "width", G_TYPE_INT, appctx->raw_width,
        "height", G_TYPE_INT, appctx->raw_height,
        "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  } else {
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, IPE_RING_BUFFER_WIDTH,
        "height", G_TYPE_INT, IPE_RING_BUFFER_HEIGHT,
        "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
    gst_caps_set_features (filtercaps, 0,
        gst_caps_features_new ("memory:GBM", NULL));
  }
  g_object_set (G_OBJECT (appsrc), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);
  g_object_set (G_OBJECT (appsrc), "stream-type", 0,
      "format", GST_FORMAT_TIME, "is-live", TRUE, NULL);

  if (appctx->mode == GST_TAPOUT_RDI) {
    caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, appctx->rdi_output_width,
        "height", G_TYPE_INT, appctx->rdi_output_height,
        "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
    gst_caps_set_features (caps, 0, gst_caps_features_new ("memory:GBM", NULL));
    g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
    gst_caps_unref (caps);
  }

  appctx->appsrc_pipeline = pipeline;
  appctx->appsrc          = appsrc;
  appctx->queue           = queue;
  appctx->encoder         = encoder;
  appctx->filesink        = filesink;
  appctx->h264parse       = h264parse;
  appctx->mp4mux          = mp4mux;
  if (appctx->mode == GST_TAPOUT_RDI) {
    appctx->camimgreproc = camimgreproc;
    appctx->capsfilter   = capsfilter;
  }

  if (appctx->mode == GST_TAPOUT_RDI)
    gst_bin_add_many (GST_BIN (appctx->appsrc_pipeline),
        appsrc, queue, camimgreproc, capsfilter, encoder, h264parse, mp4mux,
        filesink, NULL);
  else
    gst_bin_add_many (GST_BIN (appctx->appsrc_pipeline),
        appsrc, queue, encoder, h264parse, mp4mux, filesink, NULL);

  if (appctx->mode == GST_TAPOUT_RDI) {
    sinkpad = gst_element_request_pad_simple (camimgreproc, "sink_%u");
    if (!sinkpad) {
      g_printerr ("[ERROR] Failed to get sink pad from camimgreproc\n");
      return -1;
    }
    g_object_set (G_OBJECT (sinkpad), "camera-id", appctx->reproc_camera_id, NULL);
    gst_object_unref (sinkpad);
    if (!gst_element_link_many (appsrc, queue, camimgreproc, capsfilter,
            encoder, h264parse, mp4mux, filesink, NULL)) {
      g_printerr ("[ERROR] CAM1 appsrc pipeline link failed (RDI)\n");
      return -1;
    }
  } else {
    if (!gst_element_link_many (appsrc, queue, encoder, h264parse, mp4mux,
            filesink, NULL)) {
      g_printerr ("[ERROR] CAM1 appsrc pipeline link failed\n");
      return -1;
    }
  }

  /* --------------------------------------------------------
   * Build CAM2 appsrc pipeline (IPE Bypass mode only)
   * -------------------------------------------------------- */
  if (appctx->mode == GST_TAPOUT_IPEBYPASS) {
    pipeline_cam2  = gst_pipeline_new ("gst-360-appsrc-cam2-pipeline");
    appsrc_cam2    = gst_element_factory_make ("appsrc",   "appsrc_cam2");
    queue_cam2     = gst_element_factory_make ("queue",    "queue_cam2");
    encoder_cam2   = gst_element_factory_make (appctx->encoder_name, "encoder_cam2");
    filesink_cam2  = gst_element_factory_make ("filesink", "filesink_cam2");
    h264parse_cam2 = gst_element_factory_make ("h264parse","h264parse_cam2");
    mp4mux_cam2    = gst_element_factory_make ("mp4mux",   "mp4mux_cam2");

    if (!pipeline_cam2 || !appsrc_cam2 || !queue_cam2 || !encoder_cam2 ||
        !filesink_cam2 || !h264parse_cam2 || !mp4mux_cam2) {
      g_printerr ("[ERROR] CAM2 appsrc pipeline elements could not be created\n");
      return -1;
    }

    set_encoder_props (encoder_cam2, appctx->encoder_name);
    g_object_set (G_OBJECT (filesink_cam2),
        "location", "/data/360_prebuffered_cam2.mp4",
        "enable-last-sample", FALSE, NULL);

    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, "NV12",
        "width", G_TYPE_INT, IPE_RING_BUFFER_WIDTH,
        "height", G_TYPE_INT, IPE_RING_BUFFER_HEIGHT,
        "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
    gst_caps_set_features (filtercaps, 0,
        gst_caps_features_new ("memory:GBM", NULL));
    g_object_set (G_OBJECT (appsrc_cam2), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);
    g_object_set (G_OBJECT (appsrc_cam2), "stream-type", 0,
        "format", GST_FORMAT_TIME, "is-live", TRUE, NULL);

    appctx->appsrc_pipeline_cam2 = pipeline_cam2;
    appctx->appsrc_cam2          = appsrc_cam2;
    appctx->queue_cam2           = queue_cam2;
    appctx->encoder_cam2         = encoder_cam2;
    appctx->filesink_cam2        = filesink_cam2;
    appctx->h264parse_cam2       = h264parse_cam2;
    appctx->mp4mux_cam2          = mp4mux_cam2;

    gst_bin_add_many (GST_BIN (appctx->appsrc_pipeline_cam2),
        appsrc_cam2, queue_cam2, encoder_cam2, h264parse_cam2, mp4mux_cam2,
        filesink_cam2, NULL);

    if (!gst_element_link_many (appsrc_cam2, queue_cam2, encoder_cam2,
            h264parse_cam2, mp4mux_cam2, filesink_cam2, NULL)) {
      g_printerr ("[ERROR] CAM2 appsrc pipeline link failed\n");
      return -1;
    }
  }

  /* --------------------------------------------------------
   * Set up bus watchers for main pipeline
   * -------------------------------------------------------- */
  bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->main_pipeline));
  if (!bus) {
    g_printerr ("[ERROR] Failed to retrieve main pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->main_pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error",   G_CALLBACK (error_cb),   mloop);
  g_signal_connect (bus, "message::eos",     G_CALLBACK (eos_cb),     appctx);
  gst_object_unref (bus);

  /* Bus watcher for CAM1 appsrc pipeline */
  bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->appsrc_pipeline));
  if (!bus) {
    g_printerr ("[ERROR] Failed to retrieve CAM1 appsrc pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->appsrc_pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error",   G_CALLBACK (error_cb),   mloop);
  gst_object_unref (bus);

  /* Bus watcher for CAM2 appsrc pipeline (IPE only) */
  if (appctx->appsrc_pipeline_cam2) {
    bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->appsrc_pipeline_cam2));
    if (!bus) {
      g_printerr ("[ERROR] Failed to retrieve CAM2 appsrc pipeline bus!\n");
      g_main_loop_unref (mloop);
      return -1;
    }
    gst_bus_add_signal_watch (bus);
    g_signal_connect (bus, "message::state-changed",
        G_CALLBACK (state_changed_cb), appctx->appsrc_pipeline_cam2);
    g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
    g_signal_connect (bus, "message::error",   G_CALLBACK (error_cb),   mloop);
    gst_object_unref (bus);
  }

  /* Register interrupt handler */
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  /* Create buffer queues */
  appctx->buffers_queue = g_queue_new ();
  if (appctx->mode == GST_TAPOUT_IPEBYPASS)
    appctx->buffers_queue_cam2 = g_queue_new ();

  /* Spawn worker thread */
  pthread_t thread;
  pthread_create (&thread, NULL, &thread_fn, appctx);

  /* Run main loop */
  g_print ("[INFO] g_main_loop_run\n");
  g_main_loop_run (mloop);

  /* Remove pending buffer sources */
  if (appctx->process_src_id) {
    GSource *src = g_main_context_find_source_by_id (NULL,
        appctx->process_src_id);
    if (src && !g_source_is_destroyed (src))
      g_source_remove (appctx->process_src_id);
    appctx->process_src_id = 0;
  }
  if (appctx->process_src_id_cam2) {
    GSource *src = g_main_context_find_source_by_id (NULL,
        appctx->process_src_id_cam2);
    if (src && !g_source_is_destroyed (src))
      g_source_remove (appctx->process_src_id_cam2);
    appctx->process_src_id_cam2 = 0;
  }

  pthread_join (thread, NULL);
  g_print ("[INFO] g_main_loop_run ends\n");

  /* Final pipeline cleanup */
  if (appctx->main_pipeline)
    gst_element_set_state (appctx->main_pipeline, GST_STATE_NULL);
  if (appctx->appsrc_pipeline)
    gst_element_set_state (appctx->appsrc_pipeline, GST_STATE_NULL);
  if (appctx->appsrc_pipeline_cam2)
    gst_element_set_state (appctx->appsrc_pipeline_cam2, GST_STATE_NULL);

  if (appctx->streams_list != NULL)
    release_all_streams (appctx);

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  if (appctx->main_pipeline && qtiqmmfsrc)
    gst_bin_remove (GST_BIN (appctx->main_pipeline), qtiqmmfsrc);

  if (appctx->streams_list != NULL) {
    g_list_free (appctx->streams_list);
    appctx->streams_list = NULL;
  }

  if (appctx->buffers_queue) {
    clear_buffers_queue (appctx);
    g_queue_free (appctx->buffers_queue);
  }
  if (appctx->buffers_queue_cam2) {
    clear_buffers_queue_cam2 (appctx);
    g_queue_free (appctx->buffers_queue_cam2);
  }

  if (appctx->meta_capture)
    g_ptr_array_free (appctx->meta_capture, TRUE);

  g_mutex_clear (&appctx->lock);
  g_cond_clear  (&appctx->eos_signal);
  g_cond_clear  (&appctx->live_pts_signal);

  if (appctx->appsrc_pipeline_cam2)
    gst_object_unref (appctx->appsrc_pipeline_cam2);
  if (appctx->appsrc_pipeline)
    gst_object_unref (appctx->appsrc_pipeline);
  if (appctx->main_pipeline)
    gst_object_unref (appctx->main_pipeline);

  g_free (appctx);
  gst_deinit ();

  g_print ("[INFO] main: Exit\n");
  return 0;
}
