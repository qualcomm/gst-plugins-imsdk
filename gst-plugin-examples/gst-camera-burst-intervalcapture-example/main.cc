/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <glib-unix.h>
#include <gst/gst.h>
#include <glib.h>
#include <stdarg.h>
#include <camera/CameraMetadata.h>
#include <camera/VendorTagDescriptor.h>

#ifndef CAMERA_METADATA_1_0_NS
namespace camera = android;
#else
namespace camera = android::hardware::camera::common::V1_0::helper;
#endif

#define DEFAULT_OUTPUT_WIDTH 3840
#define DEFAULT_OUTPUT_HEIGHT 2160

#define DEFAULT_WAYLAND_WIDTH 960
#define DEFAULT_WAYLAND_HEIGHT 720

#define DEFAULT_BURST_ROUND 1

#define DEFAULT_OUTPUT_PREVIEW  GST_PREVIEW_OUTPUT_DISPLAY
#define DEFAULT_FORMAT_CAPTURE  GST_CAPTURE_FORMAT_JPEG
#define DEFAULT_REQUIRE_CAPTURE GST_CAPTURE_5PIC_IN_1SEC

#define TIMEOUT_S 10

#define FILE_MP4 "/data/mux.mp4"

typedef struct _GstAppContext GstAppContext;
struct _GstAppContext {
  GMainLoop *loop;
  GstElement *pipeline, *qmmfsrc;
  const char *file_ext;
  const char *file_ext_2nd;
  gboolean quit_requested;
  GMutex mutex;
  GCond cond_quit;
  guint pending;
  GstPad *video_pad;
};

/*** Data Structure ***/
enum {
  CAM_OPMODE_NONE,
  CAM_OPMODE_FRAMESELECTION,
  CAM_OPMODE_FASTSWITCH,
};

typedef enum {
  GST_PREVIEW_OUTPUT_AVC,
  GST_PREVIEW_OUTPUT_DISPLAY,
} GstPreviewOutput;

typedef enum {
  GST_CAPTURE_FORMAT_JPEG,
  GST_CAPTURE_FORMAT_YUV,
  GST_CAPTURE_FORMAT_BAYER,
  GST_CAPTURE_FORMAT_JPEG_PLUS_BAYER,
} GstCaptureFormat;

typedef enum {
  GST_CAPTURE_5PIC_IN_1SEC,
  GST_CAPTURE_10PIC_IN_1SEC,
  GST_CAPTURE_15PIC_IN_3SEC,
  GST_CAPTURE_30PIC_IN_3SEC,
} GstCaptureRequire;

#define GST_APP_SUMMARY \
  "This application is running preview for 10s, \n"\
  "then begins to capture burst snapshots. After \n"\
  "capturing, it will run preview for another 10s \n"\
  "and exit. Time interval between burst snapshots \n"\
  "is 100ms or 200ms. Application captures one of \n"\
  "following burst count 5/10/15/30 images then \n"\
  "quits the app in file path starting with \n"\
  "/data/frame_. Preview is shown either on \n"\
  "display or avc. Capture is either in JPEG, \n"\
  "YUV or bayer.\n"

static gint width = DEFAULT_OUTPUT_WIDTH;
static gint height = DEFAULT_OUTPUT_HEIGHT;

static gint width_preview = DEFAULT_WAYLAND_WIDTH;
static gint height_preview = DEFAULT_WAYLAND_HEIGHT;

static guint burst_round = DEFAULT_BURST_ROUND;

static GstPreviewOutput preview_output   = DEFAULT_OUTPUT_PREVIEW;
static GstCaptureFormat capture_format   = DEFAULT_FORMAT_CAPTURE;
static GstCaptureRequire capture_require = DEFAULT_REQUIRE_CAPTURE;

// Configure input parameters
static GOptionEntry entries[] = {
  { "width", 'w', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &width,
    "width",
    "image width of stream"
  },
  { "height", 'h', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &height,
    "height",
    "image height of stream"
  },
  { "width_preview", 'a', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &width_preview,
    "width_preview",
    "preview width of stream"
  },
  { "height_preview", 'b', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &height_preview,
    "height_preview",
    "preview height of stream"
  },
  { "burst_round", 'd', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &burst_round,
    "burst_round",
    "rounds of burst snapshot"
  },
  { "output_preview", 'p', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &preview_output,
    "output preview"
    "(default: 1 - Display)",
    "preview output type:"
    "\n\t0 - AVC"
    "\n\t1 - Display\n"
  },
  { "capture_format", 'c', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &capture_format,
    "capture format"
    "(default: 0 - JPEG)",
    "capture format type:"
    "\n\t0 - JPEG"
    "\n\t1 - YUV"
    "\n\t2 - BAYER"
    "\n\t3 - JPEG+BAYER\n"
  },
  { "capture_require", 'r', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &capture_require,
    "capture require"
    "(default: 0 - 5pics in 1sec)",
    "capture interval capture require:"
    "\n\t0 - 5pics  in 1sec"
    "\n\t1 - 10pics in 1sec"
    "\n\t2 - 15pics in 3sec"
    "\n\t3 - 30pics in 3sec\n"
  },
  { NULL }
};

static void
request_stop (GstAppContext * ctx)
{
  GstState state, pending;
  GstStateChangeReturn status;

  status = gst_element_get_state (ctx->pipeline, &state, &pending,
      GST_CLOCK_TIME_NONE);
  if (status != GST_STATE_CHANGE_SUCCESS) {
    g_printerr ("failed to get state\n");
    g_main_loop_quit (ctx->loop);
  }

  if (state == GST_STATE_PLAYING) {
    g_print ("stream playing - sending eof ...\n");
    gst_element_send_event (ctx->pipeline, gst_event_new_eos ());
  } else {
    g_print ("stream was not playing - ending loop\n");
    g_main_loop_quit (ctx->loop);
  }
}

// Recieves a list of pointers to variable containing pointer to gst element
// and unrefs the gst element if needed
static void
cleanup_many_gst (void * first_elem, ...)
{
  va_list args;
  void **p_gst_obj = (void **)first_elem;

  va_start (args, first_elem);
  while (p_gst_obj) {
    if (*p_gst_obj)
      gst_object_unref (*p_gst_obj);
    p_gst_obj = va_arg(args, void **);
  }
  va_end(args);
}

static void
gst_camera_metadata_release (gpointer data)
{
  ::camera::CameraMetadata *meta = (::camera::CameraMetadata*) data;
  delete meta;
}

static gboolean
capture_get_imgtype (gint * imgtype)
{
  GValue capture_mode_val = G_VALUE_INIT;
  gboolean success = FALSE;
  GType capture_mode_ty = g_type_from_name ("GstImageCaptureMode");
  if (!capture_mode_ty) {
    g_printerr ("can't get GstImageCaptureMode type\n");
    return FALSE;
  }

  g_value_init (&capture_mode_val, capture_mode_ty);
  success = gst_value_deserialize (&capture_mode_val, "still");
  if (!success) {
    g_value_unset (&capture_mode_val);
    g_printerr ("can't deserialize 'still' for GstImageCaptureMode enum\n");
    return FALSE;
  }

  // We need to get the enum integer value because
  // the actual capture-image function requires int
  *imgtype = g_value_get_enum (&capture_mode_val);
  g_value_unset (&capture_mode_val);

  return TRUE;
}

static gboolean
capture_set_compensation (GstAppContext *ctx, GPtrArray * gmetas,
    gint n_snapshots)
{
  ::camera::CameraMetadata *smeta = nullptr, *meta = nullptr;

  // Get high quality metadata, which will be used for submitting capture-image.
  g_object_get (G_OBJECT (ctx->qmmfsrc), "image-metadata", &meta, NULL);
  if (!meta) {
    g_printerr ("failed to get image metadata\n");
    goto cleanupset;
  }

  // Get static metadata, which will be used for submitting capture-image.
  g_object_get (G_OBJECT (ctx->qmmfsrc), "static-metadata", &smeta, NULL);
  if (!smeta) {
    g_printerr ("failed to get static metadata\n");
    goto cleanupset;
  }

  gmetas = g_ptr_array_new_full (0, gst_camera_metadata_release);
  if (!gmetas) {
    g_printerr ("failed to create metas array\n");
    goto cleanupset;
  }

  // Capture burst of images with AE bracketing.
  if (smeta->exists(ANDROID_CONTROL_AE_COMPENSATION_RANGE)) {
    camera_metadata_entry entry = {};
    gint32 compensation = 0;

    entry = smeta->find(ANDROID_CONTROL_AE_COMPENSATION_RANGE);

    compensation = (entry.data.i32[0] + entry.data.i32[1]) / 2;

    g_print ("\nCapturing images with bracketing %d\n", compensation);

    // Modify a copy of the capture metadata and add it to the meta array.
    for (guint i_meta = 0; i_meta < n_snapshots; i_meta++) {
      ::camera::CameraMetadata *metadata = new ::camera::CameraMetadata(*meta);

      // Set OFF focus mode
      guchar afmode = ANDROID_CONTROL_AF_MODE_OFF;
      metadata->update(ANDROID_CONTROL_AF_MODE, &afmode, 1);
      metadata->update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &compensation,
          1);

      g_ptr_array_add (gmetas, (gpointer) metadata);
    }
  } else {
    g_printerr ("WARN: EV Compensation not supported!\n"
        "Using default meta\n");
    g_ptr_array_add (gmetas, (gpointer) meta);
    meta = nullptr;
  }

  return TRUE;

cleanupset:
  if (meta)
    delete meta;
  if (smeta)
    delete smeta;

  return FALSE;
}

static gpointer
capture_thread (gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;
  GPtrArray *gmetas = NULL;
  gint imgtype;
  gboolean success = FALSE, error = TRUE;
  guint i_snap = 0;
  guint i_round = 0;
  gint64 end_time;

  gfloat s_timeout_backup = (gfloat)TIMEOUT_S;
  gfloat s_timeout = (gfloat)TIMEOUT_S;
  gint n_snapshots = 0;

  switch (capture_require) {
    case 0 :
      n_snapshots = 5;
      s_timeout = 0.2;
      break;
    case 1 :
      n_snapshots = 10;
      s_timeout = 0.1;
      break;
    case 2 :
      n_snapshots = 15;
      s_timeout = 0.2;
      break;
    case 3 :
      n_snapshots = 30;
      s_timeout = 0.1;
      break;
    default:
      g_printerr ("\n invalid capture_require \n");
      break;
  }

  // Get imgtype.
  success = capture_get_imgtype(&imgtype);
  if (!success) {
    g_printerr ("capture_get_imgtype() fail ...\n");
    goto cleanup;
  }

  // Set compensation.
  success = capture_set_compensation (ctx, gmetas, n_snapshots);
  if (!success) {
    g_printerr ("capture_set_compensation() fail ...\n");
    goto cleanup;
  }

  for (i_round = 0; i_round < burst_round; ++i_round) {
    for (i_snap = 0; i_snap < n_snapshots; ++i_snap) {
      if (0 == i_snap) {
        s_timeout_backup = s_timeout;
        s_timeout = (gfloat)TIMEOUT_S;
      } else {
        s_timeout = s_timeout_backup;
      }
      end_time = g_get_monotonic_time ()
          + (gint64)(s_timeout * G_TIME_SPAN_SECOND);
      g_mutex_lock (&ctx->mutex);
      g_print ("delaying next request for %f seconds...\n", s_timeout);

      while (!ctx->quit_requested) {
        if (!g_cond_wait_until (&ctx->cond_quit, &ctx->mutex, end_time)) {
          g_print ("Waiting is over...\n");
          break;
        }
      }

      if (ctx->quit_requested) {
        error = FALSE;
        g_mutex_unlock (&ctx->mutex);
        goto cleanup;
      }

      if (0 == i_snap) {
        g_print ("Pause preview stream for NZSL Burst...\n");
        // Deactivation the preview pad
        gst_pad_set_active (ctx->video_pad, FALSE);

        g_print ("requesting %d snapshot...\n", n_snapshots);
      }

      g_signal_emit_by_name (ctx->qmmfsrc, "capture-image", imgtype, 1,
          gmetas, &success);
      if (!success) {
        g_mutex_unlock (&ctx->mutex);
        g_printerr ("failed to send capture request\n");
        goto cleanup;
      }

      g_print ("snapshot request %d send\n", i_snap);

      if (n_snapshots - 1 == i_snap) {
        g_print ("Resume preview stream for NZSL Burst...\n");
        // Activation the preview pad
        gst_pad_set_active (ctx->video_pad, TRUE);
      }

      ctx->pending +=
          ((GST_CAPTURE_FORMAT_JPEG_PLUS_BAYER == capture_format) ? 2 : 1);
      g_mutex_unlock (&ctx->mutex);
    }

    g_print ("snapshot requests send...\n");
    g_mutex_lock (&ctx->mutex);

    while (ctx->pending && !ctx->quit_requested)
      g_cond_wait (&ctx->cond_quit, &ctx->mutex);

    g_mutex_unlock (&ctx->mutex);
  
    if ((i_round + 1 < burst_round) && (i_snap > 0)) {
      g_print ("cancelling capture\n");
      g_signal_emit_by_name (G_OBJECT (ctx->qmmfsrc),
          "cancel-capture", &success);
      if (!success) {
        g_printerr ("cancel capture failed\n");
        error = TRUE;
      }
    }
  }

  error = FALSE;

cleanup:
  // If we have send any capture requests, emit cancel-capture
  if (i_snap > 0) {
    g_print ("cancelling capture\n");
    g_signal_emit_by_name (G_OBJECT (ctx->qmmfsrc), "cancel-capture", &success);
    if (!success) {
      g_printerr ("cancel capture failed\n");
      error = TRUE;
    }

    // Run TIMEOUT_S seconds after capturing
    end_time = g_get_monotonic_time ()
        + (gint64)(TIMEOUT_S * G_TIME_SPAN_SECOND);
    g_mutex_lock (&ctx->mutex);
    g_print ("After request, running for %d seconds...\n", TIMEOUT_S);
    while (!ctx->quit_requested) {
      if (!g_cond_wait_until (&ctx->cond_quit, &ctx->mutex, end_time)) {
        break;
      }
    }
    g_mutex_unlock (&ctx->mutex);
  }

  if (gmetas)
    g_ptr_array_unref (gmetas);

  if (!error)
    request_stop (ctx);
  else
    g_main_loop_quit (ctx->loop);

  return NULL;
}

// Handle interrupt by CTRL+C
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;

  g_mutex_lock (&ctx->mutex);
  ctx->quit_requested = TRUE;
  g_cond_signal (&ctx->cond_quit);
  g_mutex_unlock (&ctx->mutex);

  return TRUE;
}

// Handle warning events
// Currently only prints the warning message
static void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

// Handle error events
// Currently only prints the error message
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (ctx->loop);
}

// Handles state change transisions
// Currently only prints the state change and only for our pipeline element
static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (ctx->pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new_st),
      gst_element_state_get_name (pending));
}

// Handle eos events
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (ctx->pipeline))
    return;

  g_print ("eos recieved - qutting main loop\n");
  g_main_loop_quit (ctx->loop);
}

static GstCaps *
create_stream_caps (gint width, gint height)
{
  GstCaps *filter_caps;

  // Configure the stream caps
  filter_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filter_caps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  return filter_caps;
}

static GstCaps *
create_raw_caps (gint width, gint height)
{
  GstCaps *filter_caps;

  // Configure raw capture caps
  filter_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV21",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);

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
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);

  return filter_caps;
}

static GstCaps *
create_jpeg_caps (gint width, gint height)
{
  GstCaps *filter_caps;

  // Configure jpeg still caps
  filter_caps = gst_caps_new_simple ("image/jpeg",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);

  return filter_caps;
}

static void
gst_sample_release (GstSample * sample)
{
    gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
    gst_sample_set_buffer (sample, NULL);
#endif
}

static GstFlowReturn
new_sample (GstElement * element, gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GError *error = NULL;
  GstMapInfo memmap;
  gchar *filename = NULL;
  guint64 timestamp = 0;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (element, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!\n");
    return GST_FLOW_ERROR;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &memmap, GST_MAP_READ)) {
    g_printerr ("ERROR: Failed to map the pulled buffer!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&ctx->mutex);
  if (--(ctx->pending) == 0)
    g_cond_signal (&ctx->cond_quit);
  g_mutex_unlock (&ctx->mutex);

  // Extract the original camera timestamp from GstBuffer OFFSET_END field
  timestamp = GST_BUFFER_OFFSET_END (buffer);
  g_print ("Camera timestamp: %" G_GUINT64_FORMAT "\n", timestamp);

  filename = g_strdup_printf ("/data/frame_%" G_GUINT64_FORMAT "%s", timestamp,
      ctx->file_ext);

  if (!g_file_set_contents (filename, (const gchar*) memmap.data, memmap.size,
      &error)) {
    g_printerr ("ERROR: Writing to %s failed: %s\n", filename, error->message);
    g_clear_error (&error);
  } else {
    g_print ("Buffer written to file system: %s\n", filename);
  }

  g_free (filename);
  gst_buffer_unmap (buffer, &memmap);
  gst_sample_release (sample);

  return GST_FLOW_OK;
}

// Used for JPEG+BAYER
static GstFlowReturn
new_sample_2nd (GstElement * element, gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GError *error = NULL;
  GstMapInfo memmap;
  gchar *filename = NULL;
  guint64 timestamp = 0;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (element, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!\n");
    return GST_FLOW_ERROR;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &memmap, GST_MAP_READ)) {
    g_printerr ("ERROR: Failed to map the pulled buffer!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&ctx->mutex);
  if (--(ctx->pending) == 0)
    g_cond_signal (&ctx->cond_quit);
  g_mutex_unlock (&ctx->mutex);

  // Extract the original camera timestamp from GstBuffer OFFSET_END field
  timestamp = GST_BUFFER_OFFSET_END (buffer);
  g_print ("Camera timestamp: %" G_GUINT64_FORMAT "\n", timestamp);

  filename = g_strdup_printf ("/data/frame_%" G_GUINT64_FORMAT "%s", timestamp,
      ctx->file_ext_2nd);

  if (!g_file_set_contents (filename, (const gchar*) memmap.data, memmap.size,
      &error)) {
    g_printerr ("ERROR: Writing to %s failed: %s\n", filename, error->message);
    g_clear_error (&error);
  } else {
    g_print ("Buffer written to file system: %s\n", filename);
  }

  g_free (filename);
  gst_buffer_unmap (buffer, &memmap);
  gst_sample_release (sample);

  return GST_FLOW_OK;
}

static gboolean
link_capture_output (GstCaps * stream_caps, GstElement * pipeline,
    GstElement * qtiqmmfsrc, GstAppContext * smpl_ctx)
{
  GstElement *filter_caps_elem, *appsink;
  gboolean success;

  appsink = gst_element_factory_make ("appsink", "appsink-1");
  filter_caps_elem = gst_element_factory_make ("capsfilter", "capsfilter-1");
  if (!appsink || !filter_caps_elem) {
    g_printerr ("failed to create elements for capture stream \n.");
    cleanup_many_gst (&filter_caps_elem, &appsink, NULL);
    return FALSE;
  }

  g_object_set (G_OBJECT (filter_caps_elem), "caps", stream_caps, NULL);

  g_object_set (G_OBJECT (appsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (appsink), "emit-signals", TRUE, NULL);
  g_object_set (G_OBJECT (appsink), "async", FALSE, NULL);
  g_object_set (G_OBJECT (appsink), "enable-last-sample", FALSE, NULL);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (pipeline), filter_caps_elem, appsink, NULL);

  g_print ("Linking camera capture pad ...\n");

  success = gst_element_link_pads (qtiqmmfsrc, "image_1",
      filter_caps_elem, NULL);
  if (!success) {
    g_printerr ("failed to link camera.image_1 to capture filter\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem, appsink, NULL);
    return FALSE;
  }

  // Linking the stream
  success = gst_element_link_many (filter_caps_elem, appsink, NULL);
  if (!success) {
    g_printerr ("failed to link multifilesink\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem, appsink, NULL);
    return FALSE;
  }

  g_print ("All elements are linked successfully\n");

  g_signal_connect (G_OBJECT (appsink), "new-sample", G_CALLBACK (new_sample),
      smpl_ctx);

  return TRUE;
}

// Used for JPEG+BAYER
static gboolean
link_2nd_capture_output (GstCaps * stream_caps, GstElement * pipeline,
    GstElement * qtiqmmfsrc, GstAppContext * smpl_ctx)
{
  GstElement *filter_caps_elem, *appsink;
  gboolean success;

  appsink = gst_element_factory_make ("appsink", "appsink-2");
  filter_caps_elem = gst_element_factory_make ("capsfilter", "capsfilter-2");
  if (!appsink || !filter_caps_elem) {
    g_printerr ("failed to create elements for capture stream \n.");
    cleanup_many_gst (&filter_caps_elem, &appsink, NULL);
    return FALSE;
  }

  g_object_set (G_OBJECT (filter_caps_elem), "caps", stream_caps, NULL);

  g_object_set (G_OBJECT (appsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (appsink), "emit-signals", TRUE, NULL);
  g_object_set (G_OBJECT (appsink), "async", FALSE, NULL);
  g_object_set (G_OBJECT (appsink), "enable-last-sample", FALSE, NULL);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (pipeline), filter_caps_elem, appsink, NULL);

  g_print ("Linking camera capture pad ...\n");

  success = gst_element_link_pads (qtiqmmfsrc, "image_2",
      filter_caps_elem, NULL);
  if (!success) {
    g_printerr ("failed to link camera.image_2 to capture filter\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem, appsink, NULL);
    return FALSE;
  }

  // Linking the stream
  success = gst_element_link_many (filter_caps_elem, appsink, NULL);
  if (!success) {
    g_printerr ("failed to link multifilesink\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem, appsink, NULL);
    return FALSE;
  }

  g_print ("All elements are linked successfully\n");

  g_signal_connect (G_OBJECT (appsink), "new-sample",
      G_CALLBACK (new_sample_2nd), smpl_ctx);

  return TRUE;
}

static gboolean
link_avc_output (GstCaps * stream_caps, GstElement * pipeline,
    GstElement * qtiqmmfsrc, GstPad * video_pad)
{
  GstElement *filesink, *encoder, *h264parse, *mp4mux,
      *filter_caps_elem;
  gboolean success;

  filter_caps_elem = gst_element_factory_make ("capsfilter", "capsfilter-0");
  filesink = gst_element_factory_make ("filesink", "filesink-0");
#ifdef CODEC2_ENCODE
  encoder = gst_element_factory_make ("qtic2venc", "qtic2venc-0");
#else
  encoder = gst_element_factory_make ("omxh264enc", "omxh264enc-0");
#endif
  h264parse = gst_element_factory_make ("h264parse", "h264parse-0");
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux-0");
  if (!filesink || !encoder || !h264parse || !mp4mux
      || !filter_caps_elem) {
    g_printerr ("failed to create elements in video stream\n.");
    cleanup_many_gst (&filesink, &encoder, &h264parse, &mp4mux,
        &filter_caps_elem, NULL);
    return FALSE;
  }

  g_object_set (G_OBJECT (filter_caps_elem), "caps", stream_caps, NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);

#ifndef CODEC2_ENCODE
  // OMX encoder specific props
  g_object_set (G_OBJECT (encoder), "periodicity-idr", 1, NULL);
  g_object_set (G_OBJECT (encoder), "interval-intraframes", 29, NULL);
  g_object_set (G_OBJECT (encoder), "control-rate", 2, NULL);
#endif

  // Set filesink location
  g_object_set (G_OBJECT (filesink), "location", FILE_MP4, NULL);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (pipeline), filter_caps_elem, encoder,
      h264parse, mp4mux, filesink, NULL);

  g_print ("Linking camera video pad ...\n");

  success = gst_element_link_pads (qtiqmmfsrc,
      gst_pad_get_name (video_pad), filter_caps_elem, NULL);
  if (!success) {
    g_printerr ("failed to link camera.video_0 to nv12 filter\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem, encoder,
        h264parse, mp4mux, filesink, NULL);
    return FALSE;
  }

  g_print ("Linking elements...\n");

  // Linking the stream
  success = gst_element_link_many (filter_caps_elem,
      encoder, h264parse, mp4mux, filesink, NULL);
  if (!success) {
    g_printerr ("failed to link rest of avc elements\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem, encoder,
        h264parse, mp4mux, filesink, NULL);
    return FALSE;
  }

  g_print ("All elements are linked successfully\n");

  return TRUE;
}

static gboolean
link_wayland_output (GstCaps * stream_caps, GstElement * pipeline,
    GstElement * qtiqmmfsrc, GstPad * video_pad)
{
  GstElement *waylandsink, *filter_caps_elem;
  gboolean success;
  gint x, y;

  filter_caps_elem = gst_element_factory_make ("capsfilter", "capsfilter-0");
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink-0");
  if (!waylandsink || !filter_caps_elem) {
    g_printerr ("failed to create elements for wayland preview\n");
    cleanup_many_gst (&waylandsink, &filter_caps_elem, NULL);
    return FALSE;
  }

  g_object_set (G_OBJECT (filter_caps_elem), "caps", stream_caps, NULL);

  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (pipeline), filter_caps_elem, waylandsink, NULL);

  g_print ("Linking camera video pad ...\n");

  success = gst_element_link_pads (qtiqmmfsrc,
      gst_pad_get_name (video_pad), filter_caps_elem, NULL);
  if (!success) {
    g_printerr ("failed to link camera.video_0 to nv12 filter\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem,
        waylandsink, NULL);
    return FALSE;
  }

  g_print ("Linking elements...\n");

  // Linking the stream
  success = gst_element_link_many (filter_caps_elem, waylandsink, NULL);
  if (!success) {
    g_printerr ("failed to link waylandsink\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem,
        waylandsink, NULL);
    return FALSE;
  }

  g_print ("All elements are linked successfully\n");

  return TRUE;
}

int
main (int argc, char * * argv)
{
  GOptionContext *ctx;
  GError *gerr = NULL;
  gboolean success = FALSE;
  gint res = -1;
  GstCaps *stream_caps = NULL, *capture_caps = NULL, *capture_caps_2nd = NULL;
  GstElementClass *qtiqmmfsrc_klass = NULL;
  GstPadTemplate *qtiqmmfsrc_template = NULL;
  GstPad *video_pad = NULL;
  GstElement *pipeline = NULL, *qtiqmmfsrc;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstAppContext app_ctx = {0};
  GMainLoop *loop = NULL;
  GstStateChangeReturn change_ret;
  GThread *mthread = NULL;

  g_cond_init (&app_ctx.cond_quit);
  g_mutex_init (&app_ctx.mutex);

  ctx = g_option_context_new (NULL);
  if (!ctx) {
    g_printerr ("failed to create options context.\n");
    return -1;
  }

  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_set_summary (ctx, GST_APP_SUMMARY);
  success = g_option_context_parse (ctx, &argc, &argv, &gerr);
  g_option_context_free (ctx);
  if (!success && !gerr) {
    g_printerr ("failed to parse arguments - unrecovarable error.\n");
    return -1;
  } else if (!success && gerr) {
    g_printerr ("failed to parse command line options: %s!\n",
      GST_STR_NULL (gerr->message));
    g_error_free (gerr);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline
  pipeline = gst_pipeline_new ("gst-test-app");
  if (!pipeline) {
    g_printerr ("failed to create pipeline.\n");
    return -1;
  }

  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qmmf-src");
  if (!qtiqmmfsrc) {
    g_printerr ("failed to create qtiqmmfsrc element.\n");
    goto cleanup;
  }

  // Configure op-mode for BURSTNZSL, same as CAM_OPMODE_FASTSWITCH
  g_object_set (G_OBJECT (qtiqmmfsrc), "op-mode", CAM_OPMODE_FASTSWITCH, NULL);

  success = gst_bin_add (GST_BIN (pipeline), qtiqmmfsrc);
  if (!success) {
    g_printerr ("failed to add qtiqmmfsrc to pipeline.\n");
    gst_object_unref (qtiqmmfsrc);
    goto cleanup;
  }

  // Transition qmmfsrc to READY state so if BAYER we can query
  // sensor size
  change_ret = gst_element_set_state (pipeline, GST_STATE_READY);
  switch (change_ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to READY state!\n");
      goto cleanup;
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change READY was successful\n");
      break;
    default:
      g_printerr ("set_state READY: unknwon return value %x\n", change_ret);
      goto cleanup;
  }

  // Create the capture caps
  switch (capture_format) {
    case GST_CAPTURE_FORMAT_JPEG:
      capture_caps = create_jpeg_caps (width, height);
      app_ctx.file_ext = ".jpg";
      break;
    case GST_CAPTURE_FORMAT_YUV:
      capture_caps = create_raw_caps (width, height);
      app_ctx.file_ext = ".yuv";
      break;
    case GST_CAPTURE_FORMAT_BAYER:
    {
      GValue value = G_VALUE_INIT;
      gint sensor_width, sensor_height;

      // Retrieve sensor width and height form active-sensor-size property
      g_value_init (&value, GST_TYPE_ARRAY);

      g_object_get_property (G_OBJECT (qtiqmmfsrc),
          "active-sensor-size", &value);
      if (4 != gst_value_array_get_size (&value)) {
        g_printerr ("ERROR: Expected 4 values for active sensor size,"
            " Recieved %d", gst_value_array_get_size (&value));
        g_value_unset (&value);
        goto cleanup;
      }

      sensor_width  = g_value_get_int (gst_value_array_get_value (&value, 2));
      sensor_height = g_value_get_int (gst_value_array_get_value (&value, 3));

      g_value_unset (&value);

      g_print ("bayer, using sensor width: %d and height %d\n",
          sensor_width, sensor_height);
      capture_caps = create_bayer_caps (sensor_width, sensor_height);
      app_ctx.file_ext = ".bayer";
      break;
    }
    case GST_CAPTURE_FORMAT_JPEG_PLUS_BAYER:
    {
      // JPEG
      capture_caps = create_jpeg_caps (width, height);
      app_ctx.file_ext = ".jpg";

      // BAYER
      GValue value = G_VALUE_INIT;
      gint sensor_width, sensor_height;

      // Retrieve sensor width and height form active-sensor-size property
      g_value_init (&value, GST_TYPE_ARRAY);

      g_object_get_property (G_OBJECT (qtiqmmfsrc),
          "active-sensor-size", &value);
      if (4 != gst_value_array_get_size (&value)) {
        g_printerr ("ERROR: Expected 4 values for active sensor size,"
            " Recieved %d", gst_value_array_get_size (&value));
        g_value_unset (&value);
        goto cleanup;
      }

      sensor_width  = g_value_get_int (gst_value_array_get_value (&value, 2));
      sensor_height = g_value_get_int (gst_value_array_get_value (&value, 3));

      g_value_unset (&value);

      g_print ("bayer, using sensor width: %d and height %d\n",
          sensor_width, sensor_height);
      capture_caps_2nd = create_bayer_caps (sensor_width, sensor_height);
      app_ctx.file_ext_2nd = ".bayer";
      break;
    }
    default:
      g_printerr ("unknown option for capture format\n");
      goto cleanup;
  }

  if (!capture_caps) {
    g_printerr ("failed to create capture caps\n");
    goto cleanup;
  }
  if ((GST_CAPTURE_FORMAT_JPEG_PLUS_BAYER == capture_format)
      && (!capture_caps_2nd)) {
    g_printerr ("failed to create second capture caps for JPEG + RAW\n");
    goto cleanup;
  }

  success = link_capture_output (capture_caps, pipeline, qtiqmmfsrc, &app_ctx);
  if (!success) {
    g_printerr ("failed to link capture stream\n");
    goto cleanup;
  }

  if ((GST_CAPTURE_FORMAT_JPEG_PLUS_BAYER == capture_format)) {
    success = link_2nd_capture_output (capture_caps_2nd, pipeline,
        qtiqmmfsrc, &app_ctx);
    if (!success) {
      g_printerr ("failed to link second capture stream\n");
      goto cleanup;
    }
  }

  // Create the stream caps with the input camera resolution
  stream_caps = create_stream_caps (width_preview,
      height_preview);
  if (!stream_caps) {
    g_printerr ("failed to create preview caps\n");
    goto cleanup;
  }

  // Get qmmfsrc Element class
  qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (qtiqmmfsrc);

  // Get qmmfsrc pad template
  qtiqmmfsrc_template =
      gst_element_class_get_pad_template (qtiqmmfsrc_klass, "video_%u");

  // Request a pad from qmmfsrc
  video_pad = gst_element_request_pad (qtiqmmfsrc, qtiqmmfsrc_template,
      "video_%u", NULL);
  if (!video_pad) {
    g_printerr ("Error: pad cannot be retrieved from qmmfsrc!\n");
    goto cleanup;
  }
  g_print ("Pad received - %s\n",  gst_pad_get_name (video_pad));

  // Set properties of elements
  g_object_set (G_OBJECT (video_pad), "type", 1, NULL);

  switch (preview_output) {
    case GST_PREVIEW_OUTPUT_AVC:
      success = link_avc_output (stream_caps, pipeline, qtiqmmfsrc, video_pad);
      break;
    case GST_PREVIEW_OUTPUT_DISPLAY:
      success =
          link_wayland_output (stream_caps, pipeline, qtiqmmfsrc, video_pad);
      break;
    default:
      g_printerr ("unknown option for preview output\n");
      goto cleanup;
  }

  if (!success) {
    g_printerr ("failed to link video stream\n");
    goto cleanup;
  }

  loop = g_main_loop_new (NULL, FALSE);
  if (!loop) {
    g_printerr ("failed to create main loop\n");
    goto cleanup;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  if (!bus) {
    g_printerr ("failed to get pipeline bus.\n");
    goto cleanup;
  }

  app_ctx.loop      = loop;
  app_ctx.pipeline  = pipeline;
  app_ctx.qmmfsrc   = qtiqmmfsrc;
  app_ctx.video_pad = video_pad;

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), &app_ctx);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), &app_ctx);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), &app_ctx);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), &app_ctx);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &app_ctx);

  change_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  switch (change_ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PLAYING state!\n");
      goto cleanup;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      change_ret = gst_element_get_state (pipeline, NULL, NULL,
          GST_CLOCK_TIME_NONE);
      switch (change_ret) {
        case GST_STATE_CHANGE_FAILURE:
          g_printerr ("ERROR: Failed async transition to PLAYING state!\n");
          goto cleanup;
        case GST_STATE_CHANGE_NO_PREROLL:
          g_print ("NO_PREROLL returned from async state change to PLAYING\n");
          break;
        case GST_STATE_CHANGE_ASYNC:
          g_printerr ("ERROR: ASYNC transition to PLAYING returned ASYNC!\n");
          goto cleanup;
        case GST_STATE_CHANGE_SUCCESS:
          g_print ("Pipeline async state change to PLAYING was successful\n");
          break;
        default:
          g_printerr ("get_state: unhandled PLAYING async %x\n", change_ret);
          goto cleanup;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change to PLAYING was successful\n");
      break;
    default:
      g_printerr ("set_state PLAYING: unhandled return %x\n", change_ret);
      goto cleanup;
  }

  mthread = g_thread_new ("CaptureThread", capture_thread, &app_ctx);
  if (!mthread) {
    g_printerr ("failed to create thread\n");
    goto cleanup;
  }

  // Main loop is used to call our callbacks for various gstreamer events
  // on the bus - we block here until g_main_loop_quit is called
  // Currently that happens in eos_cb
  g_print ("g_main_loop_run\n");
  g_main_loop_run (loop);
  g_print ("g_main_loop_run ends\n");

  change_ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  switch (change_ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to NULL state!\n");
      goto cleanup;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_printerr ("ERROR: Setting state to NULL can't return NO_PREROLL.\n");
      goto cleanup;
    case GST_STATE_CHANGE_ASYNC:
      g_printerr ("ERROR: Setting state to NULL can't be ASYNC.\n");
      goto cleanup;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change to NULL was successful\n");
      break;
    default:
      g_printerr ("set_state to NULL: unhandled return %x\n", change_ret);
      goto cleanup;
  }

  res = 0;

cleanup:
  if (mthread)
    g_thread_join (mthread);

  if (bus) {
    gst_bus_remove_signal_watch (bus);
    gst_object_unref (bus);
  }

  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  if (loop)
    g_main_loop_unref (loop);

  if (stream_caps)
    gst_caps_unref (stream_caps);
  if (capture_caps)
    gst_caps_unref (capture_caps);
  if (capture_caps_2nd)
    gst_caps_unref (capture_caps_2nd);

  if (video_pad) {
    // Release the unlinked pad
    gst_element_release_request_pad (qtiqmmfsrc, video_pad);
  }

  if (pipeline)
    gst_object_unref (pipeline);

  g_cond_clear (&app_ctx.cond_quit);
  g_mutex_clear (&app_ctx.mutex);

  return res;
}