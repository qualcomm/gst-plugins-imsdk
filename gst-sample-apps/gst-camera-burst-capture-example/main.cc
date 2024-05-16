/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
 * Application:
 * GStreamer capture in burst example
 *
 * Description:
 * This app connects the camera with appsink element,
 * once an appsink callback is connected to the new-sample signal,
 * it saves buffer to device storage based on the capture type.
 * capture format can be BAYER/RAW/JPEG.
 * main stream format can be avc/wayland.
 *
 * Usage:
 * gst-camera-burst-capture-example --help
 *
 */

#include <glib-unix.h>
#include <gst/gst.h>
#include <glib.h>
#include <stdarg.h>

#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

#define DEFAULT_OUTPUT_WIDTH 3840
#define DEFAULT_OUTPUT_HEIGHT 2160

#define DEFAULT_OUTPUT_PREVIEW GST_PREVIEW_OUTPUT_DISPLAY
#define DEFAULT_FORMAT_CAPTURE GST_CAPTURE_FORMAT_JPEG

#define N_SNAPSHOTS 5
#define N_STILLS 7
#define TIMEOUT_S 10

#define FILE_MP4 "/opt/mux.mp4"

enum _GstPreviewOutput
{
  GST_PREVIEW_OUTPUT_AVC,
  GST_PREVIEW_OUTPUT_DISPLAY,
};
typedef enum _GstPreviewOutput GstPreviewOutput;

enum _GstCaptureFormat
{
  GST_CAPTURE_FORMAT_JPEG,
  GST_CAPTURE_FORMAT_RAW,
  GST_CAPTURE_FORMAT_BAYER,
};
typedef enum _GstCaptureFormat GstCaptureFormat;

typedef struct _GstAppContext GstAppContext;
struct _GstAppContext {
  GMainLoop *loop;
  GstElement *pipeline, *qmmfsrc;
  ::camera::CameraMetadata *meta;
  const char *file_ext;
  gboolean quit_requested;
  GMutex mutex;
  GCond cond_quit;
  guint pending;
};

#define GST_APP_SUMMARY \
  "This application captures 5 burst snapshots\n"\
  "delayed by a 10s timer then quits the app\n"\
  "in file path starting with /opt/frame_ \n"\
  "preview is shown either on display or avc\n"\
  "capture is either in jpeg, raw or bayer\n" \
  "\nCommand:\n" \
  "For Display Stream and jpeg capture \n" \
  "  gst-camera-burst-capture-example -w 1280 -h 720 -p 1 -c 0 \n" \
  "For Encode Stream and jpeg capture \n" \
  "  gst-camera-burst-capture-example -w 1280 -h 720 -p 0 -c 0 \n" \
  "\nOutput:\n" \
  "  Upon execution, application will generates output as preview OR " \
  "encoded mp4 file."

static gint width = DEFAULT_OUTPUT_WIDTH;
static gint height = DEFAULT_OUTPUT_HEIGHT;

static GstPreviewOutput preview_output = DEFAULT_OUTPUT_PREVIEW;
static GstCaptureFormat capture_format = DEFAULT_FORMAT_CAPTURE;

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
  { "output_preview", 'p', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &preview_output,
    "output preview",
    "preview output type:"
    "\n\t0 - AVC"
    "\n\t1 - Display\n"
  },
  { "capture_format", 'c', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT,
    &capture_format,
    "capture format",
    "capture format type:"
    "\n\t0 - JPEG"
    "\n\t1 - RAW"
    "\n\t2 - BAYER\n"
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

static gpointer
capture_thread (gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;
  GPtrArray *gmetas = NULL;
  gint imgtype;
  gint n_images = N_STILLS;
  gboolean success = FALSE, signal, error = TRUE;
  guint i_snap = 0, i_meta;
  gint64 end_time;
  GType capture_mode_ty;
  GValue capture_mode_val = G_VALUE_INIT;
  ::camera::CameraMetadata *smeta = nullptr, *meta = nullptr;

  // This line will only work if qtiqmmfsrc is loaded
  capture_mode_ty = g_type_from_name ("GstImageCaptureMode");
  if (!capture_mode_ty) {
    g_printerr ("can't get GstImageCaptureMode type\n");
    goto end;
  }

  g_value_init (&capture_mode_val, capture_mode_ty);
  success = gst_value_deserialize (&capture_mode_val, "still");
  if (!success) {
    g_value_unset (&capture_mode_val);
    g_printerr ("can't deserialize 'still' for GstImageCaptureMode enum\n");
    goto end;
  }

  // We need to get the enum integer value because
  // the actual capture-image function requires int
  imgtype = g_value_get_enum (&capture_mode_val);
  g_value_unset (&capture_mode_val);

  // Get high quality metadata, which will be used for submitting capture-image.
  g_object_get (G_OBJECT (ctx->qmmfsrc), "image-metadata", &meta, NULL);
  if (!meta) {
    g_printerr ("failed to get image metadata\n");
    goto end;
  }

  // Get static metadata, which will be used for submitting capture-image.
  g_object_get (G_OBJECT (ctx->qmmfsrc), "static-metadata", &smeta, NULL);
  if (!smeta) {
    g_printerr ("failed to get static metadata\n");
    goto end;
  }

  gmetas = g_ptr_array_new_full (0, gst_camera_metadata_release);
  if (!gmetas) {
    g_printerr ("failed to create metas array\n");
    goto end;
  }

  // Capture burst of images with AE bracketing.
  if (smeta->exists(ANDROID_CONTROL_AE_COMPENSATION_RANGE)) {
    camera_metadata_entry entry = {};
    gint32 idx = 0, compensation = 0, step = 0;

    entry = smeta->find(ANDROID_CONTROL_AE_COMPENSATION_RANGE);

    compensation = entry.data.i32[1];
    step = (entry.data.i32[0] - entry.data.i32[1]) / (N_STILLS  - 1);

    g_print ("\nCapturing images with bracketing from %d to %d step %d\n",
        entry.data.i32[1], entry.data.i32[0], step);

    // Modify a copy of the capture metadata and add it to the meta array.
    for (i_meta = 0; i_meta < N_STILLS ; i_meta++) {
      ::camera::CameraMetadata *metadata = new ::camera::CameraMetadata(*meta);

      metadata->update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &compensation,
          1);
      compensation += step;

      g_ptr_array_add (gmetas, (gpointer) metadata);
    }
  } else {
    g_printerr ("WARN: EV Compensation not supported!\n"
        "Using default meta\n");
    g_ptr_array_add (gmetas, (gpointer) meta);
    meta = nullptr;
  }

  g_print ("requesting %d snapshot...\n", N_SNAPSHOTS);

  for (; i_snap < N_SNAPSHOTS; ++i_snap) {
    end_time = g_get_monotonic_time () + TIMEOUT_S * G_TIME_SPAN_SECOND;
    g_mutex_lock (&ctx->mutex);
    g_print ("delaying next request for %d seconds...\n", TIMEOUT_S);

    for (signal = TRUE; !ctx->quit_requested && signal;) {
      signal = g_cond_wait_until (&ctx->cond_quit, &ctx->mutex, end_time);
    }

    if (ctx->quit_requested) {
      error = FALSE;
      g_mutex_unlock (&ctx->mutex);
      goto end;
    }

    g_signal_emit_by_name (ctx->qmmfsrc, "capture-image", imgtype, n_images,
        gmetas, &success);
    if (!success) {
      g_mutex_unlock (&ctx->mutex);
      g_printerr ("failed to send capture request\n");
      goto end;
    }

    g_print ("snapshot request %d send\n", i_snap);
    ctx->pending += N_STILLS;
    g_mutex_unlock (&ctx->mutex);
  }

  g_print ("snapshot requests send...\n");
  g_mutex_lock (&ctx->mutex);

  while (ctx->pending && !ctx->quit_requested) {
    g_cond_wait (&ctx->cond_quit, &ctx->mutex);
  }
  g_mutex_unlock (&ctx->mutex);
  error = FALSE;

end:
  // If we have send any capture requests
  // emit cancel-capture
  if (i_snap > 0) {
    g_print ("cancelling capture\n");
    g_signal_emit_by_name (G_OBJECT (ctx->qmmfsrc), "cancel-capture", &success);
    if (!success) {
      g_printerr ("cancel capture failed\n");
      error = TRUE;
    }
  }

  if (meta)
    delete meta;

  if (smeta)
    delete smeta;

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
  ctx->pending -= 1;
  if (!ctx->pending)
    g_cond_signal (&ctx->cond_quit);
  g_mutex_unlock (&ctx->mutex);

  // Extract the original camera timestamp from GstBuffer OFFSET_END field
  timestamp = GST_BUFFER_OFFSET_END (buffer);
  g_print ("Camera timestamp: %" G_GUINT64_FORMAT "\n", timestamp);

  filename = g_strdup_printf ("/opt/frame_%" G_GUINT64_FORMAT "%s", timestamp,
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

  success = gst_element_link_pads (qtiqmmfsrc, "image_1", filter_caps_elem, NULL);
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

static gboolean
link_avc_output (GstCaps * stream_caps, GstElement * pipeline,
    GstElement * qtiqmmfsrc)
{
  GstElement *filesink, *encoder, *h264parse, *mp4mux,
      *filter_caps_elem;
  gboolean success;

  filter_caps_elem = gst_element_factory_make ("capsfilter", "capsfilter-0");
  filesink = gst_element_factory_make ("filesink", "filesink-0");
  // Create encoder element and set the properties
  encoder = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
  g_object_set (G_OBJECT (encoder), "capture-io-mode", 5, NULL);
  g_object_set (G_OBJECT (encoder), "output-io-mode", 5, NULL);

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

  // Set filesink location
  g_object_set (G_OBJECT (filesink), "location", FILE_MP4, NULL);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (pipeline), filter_caps_elem, encoder,
      h264parse, mp4mux, filesink, NULL);

  g_print ("Linking camera video pad ...\n");

  success = gst_element_link_pads (qtiqmmfsrc, "video_0", filter_caps_elem, NULL);
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
    GstElement * qtiqmmfsrc)
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

  success = gst_element_link_pads (qtiqmmfsrc, "video_0", filter_caps_elem, NULL);
  if (!success) {
    g_printerr ("failed to link camera.video_0 to nv12 filter\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem, waylandsink, NULL);
    return FALSE;
  }

  g_print ("Linking elements...\n");

  // Linking the stream
  success = gst_element_link_many (filter_caps_elem, waylandsink, NULL);
  if (!success) {
    g_printerr ("failed to link waylandsink\n");
    gst_bin_remove_many (GST_BIN (pipeline), filter_caps_elem, waylandsink, NULL);
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
  GstCaps *stream_caps = NULL, *capture_caps = NULL;
  GstElement *pipeline = NULL, *qtiqmmfsrc;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstAppContext app_ctx = {0};
  GMainLoop *loop = NULL;
  GstStateChangeReturn change_ret;
  GThread *mthread = NULL;

  g_cond_init (&app_ctx.cond_quit);
  g_mutex_init (&app_ctx.mutex);

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

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
    case GST_CAPTURE_FORMAT_RAW:
      capture_caps = create_raw_caps (width, height);
      app_ctx.file_ext = ".raw";
      break;
    case GST_CAPTURE_FORMAT_BAYER:
    {
      GValue value = G_VALUE_INIT;
      gint sensor_width, sensor_height;

      // Retrieve sensor width and height form active-sensor-size property
      g_value_init (&value, GST_TYPE_ARRAY);

      g_object_get_property (G_OBJECT (qtiqmmfsrc), "active-sensor-size", &value);
      if (4 != gst_value_array_get_size (&value)) {
        g_printerr ("ERROR: Expected 4 values for active sensor size, Recieved %d",
            gst_value_array_get_size (&value));
        g_value_unset (&value);
        goto cleanup;
      }

      sensor_width  = g_value_get_int (gst_value_array_get_value (&value, 2));
      sensor_height = g_value_get_int (gst_value_array_get_value (&value, 3));

      g_value_unset (&value);

      g_print ("\nbayer, using sensor width: %d and height %d\n",
          sensor_width, sensor_height);

      capture_caps = create_bayer_caps (sensor_width, sensor_height);
      app_ctx.file_ext = ".bayer";
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

  success = link_capture_output (capture_caps, pipeline, qtiqmmfsrc, &app_ctx);
  if (!success) {
    g_printerr ("failed to link capture stream\n");
    goto cleanup;
  }

  // Create the stream caps with the input camera resolution
  stream_caps = create_stream_caps (width, height);
  if (!stream_caps) {
    g_printerr ("failed to create prevew caps\n");
    goto cleanup;
  }

  switch (preview_output) {
    case GST_PREVIEW_OUTPUT_AVC:
      success = link_avc_output (stream_caps, pipeline, qtiqmmfsrc);
      break;
    case GST_PREVIEW_OUTPUT_DISPLAY:
      success = link_wayland_output (stream_caps, pipeline, qtiqmmfsrc);
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

  app_ctx.loop = loop;
  app_ctx.pipeline = pipeline;
  app_ctx.qmmfsrc = qtiqmmfsrc;

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

  if (pipeline)
    gst_object_unref (pipeline);

  g_cond_clear (&app_ctx.cond_quit);
  g_mutex_clear (&app_ctx.mutex);

  return res;
}
