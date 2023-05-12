/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* GStreamer AppSrc from file using GBM Buffers
*
* Description:
*
* This application reads raw YUV frames from a file into GBM Buffers
* and uses GstAppSrc to supply buffers downstream for display and filesink.
* The output is saved in MP4
*
* Usage:
* gst-gbm-appsrc
* gst-gbm-appsrc --help (for options)
*
* Sample Gstreamer pipeline to generate a testfile:
*
* gst-launch-1.0 -e qtiqmmfsrc name=camsrc ! \
* video/x-raw\(memory:GBM\),format=NV12,width=1920,height=1080, framerate=24/1 ! \
* tee name=split ! queue ! filesink location=/data/testfile.yuv sync=true async=false \
* split. ! queue ! waylandsink fullscreen=true sync=true
*
*/

#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/gstimagepool.h>
#include <gst/video/video.h>
#include <stdio.h>

#define DEFAULT_MIN_BUFFERS 2
#define DEFAULT_MAX_BUFFERS 5
#define FRAMES_PER_PUSH 1

#define DEFAULT_SOURCE_WIDTH 1920
#define DEFAULT_SOURCE_HEIGHT 1080
#define DEFAULT_FRAMERATE 24
#define DEFAULT_OUTPUT_PATH "/data/output.mp4"
#define DEFAULT_SOURCE_PATH "/data/testfile.yuv"

#define LOOP_VIDEO TRUE


static G_DEFINE_QUARK (GbmAppsrcQuark, gst_gbm_qdata);

/* Structure to contain all our information, so we can pass it to callbacks
 * separated into a structure for our source data and app data. */

struct _SourceData {
  FILE *fileptr;
  glong filelen;
  gsize frame_size;
  guint64 current_frame;
  GstVideoInfo *video_info;
  gchar *source_path;
  gchar *output_path;
  gint padding_right;
  gint padding_bottom;
  gint loop;
};

typedef struct _SourceData SourceData;

struct _AppData {
  GstElement *pipeline, *app_source;
  guint sourceid;        /* To control the GSource */
  GMainLoop *main_loop;  /* GLib's Main Loop */
  GstBufferPool *pool;
  SourceData *src;
};

typedef struct _AppData AppData;

// Example of a fuction to call upon buffer release.
static void
buffer_release_notify (AppData *data)
{
  // Do anything here with the pointer
  GST_INFO ("Buffer was released!");
}

static inline void
gst_app_src_deactivate_buffer_pool (GstBufferPool *pool)
{
  if (!gst_buffer_pool_set_active (pool, FALSE))
    GST_ERROR_OBJECT (pool, "Unable to deactivate GstBufferPool for app source");
}

static glong
get_file_length (FILE *fileptr)
{
  glong filelen;

  fseek (fileptr, 0, SEEK_END);
  filelen = ftell (fileptr);
  rewind (fileptr);

  return filelen;
}

static GstBufferPool*
gst_app_create_pool (GstCaps *caps, guint frame_size)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;

  // Create new buffer pool using GBM buffers
  pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);

  if (NULL == pool) {
    GST_ERROR ("Failed to create GstImageBufferPool.");
    return NULL;
  }

  // Configure the buffer pool params
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, frame_size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (GST_IS_IMAGE_BUFFER_POOL (pool)) {
    // Returns an allocator with fd-backed memory
    GstAllocator *allocator = gst_fd_allocator_new();
    // Set allocator to configure according to default parameters provided by the pool
    gst_buffer_pool_config_set_allocator(config, allocator, NULL);
    g_object_unref(allocator);
    gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (pool, "Failed to set configuration for buffer pool.");
    g_object_unref (pool);
    pool = NULL;
  }
  return pool;
}

static gboolean
gst_app_gbm_pool_init(GstCaps *caps, AppData *data)
{
  GstBufferPool *pool = NULL;
  pool = gst_app_create_pool (caps, data->src->frame_size);

  if (!pool) {
    GST_ERROR ("Failed to initialize GBM-backed buffer pool.");
    return FALSE;
  }

  data->pool = pool;
  return TRUE;
}


/*
* This method is called by the idle GSource in the mainloop, to feed data into appsrc.
* The idle handler is added to the mainloop when appsrc requests
* us to start sending data (need-data signal)
* and is removed when appsrc has enough data (enough-data signal).
*/

static gboolean
push_data (AppData *data)
{
  GST_INFO_OBJECT(data->app_source,
      "'push-data' called to push source buffers to appsrc..");

  GstBuffer *buffer;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo map;
  SourceData *src = data->src;
  guint8 *raw;
  gboolean read_success = FALSE;

  // Check that pool is initialized, and everything is ready
  if (!data->pool) {
    GST_ERROR_OBJECT (data->app_source,
        "GstBufferPool has not been initialized for app source.");
    return FALSE;
  }

  // Pool must be activated before acquiring a buffer from it
  if (!gst_buffer_pool_is_active (data->pool)) {
    if (!gst_buffer_pool_set_active (data->pool, TRUE)) {
      GST_ERROR_OBJECT (data->pool, "Unable to activate GstBufferPool");
      return FALSE;
    }
    GST_INFO_OBJECT (data->pool, "Pool is already active..");
  }

  // Acquire the buffer, check for success
  ret = gst_buffer_pool_acquire_buffer (data->pool, &buffer, NULL);

  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (data->app_source,"Failed to acquire buffer from bufferpool.");
    gst_app_src_deactivate_buffer_pool (data->pool);
    gst_buffer_unref (buffer);
    return (FALSE);
  }

  // Notify and call buffer_release when buffer ref count drops to zero
  gst_mini_object_set_qdata (
      GST_MINI_OBJECT (buffer), gst_gbm_qdata_quark (),
      data, (GDestroyNotify) buffer_release_notify
  );

  // Attach Buffer timestamps
  GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (GST_SECOND,
      src->current_frame, src->video_info->fps_n);

  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (GST_SECOND,
      FRAMES_PER_PUSH, src->video_info->fps_n);

  // Check if we've reached the end of the file and loop back if LOOP is enabled.
  if (src->loop && (ftell (src->fileptr) == src->filelen))
      rewind (src->fileptr);

  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  raw = (gint8 *) map.data;
  read_success = (fread (map.data, src->frame_size, FRAMES_PER_PUSH, src->fileptr)
      == FRAMES_PER_PUSH);

  gst_buffer_unmap (buffer, &map);
  GST_DEBUG ("Status of file read into buffer: %d\n", read_success);

  if (!read_success) {

    if (ftell (src->fileptr) == src->filelen) {
      GST_LOG_OBJECT (data->app_source, "Reached EOF, closing source..");
    } else {
      GST_ERROR_OBJECT(data->app_source, "Failed to read from source..");
    }

    gst_buffer_unref (buffer);
    gst_element_send_event (data->pipeline, gst_event_new_eos ());

  }

  GST_LOG_OBJECT(data->app_source, "Succesfully read frame into buffer..");

  src->current_frame++;

  /* Push the buffer into the appsrc */
  g_signal_emit_by_name (data->app_source, "push-buffer", buffer, &ret);

  /* Free the buffer and return TRUE*/
  gst_buffer_unref (buffer);
  return (TRUE);
}

/* This signal callback triggers when appsrc needs data. Here, we add an idle handler
 * to the mainloop to start pushing data into the appsrc */
static void
start_feed (GstElement *source, guint size, AppData *data)
{
  GST_INFO_OBJECT (source, "GstAppSrc has signaled 'need-data'...");
  data->sourceid = g_idle_add_full (0, (GSourceFunc) push_data, data, NULL);

  if (data->sourceid == G_SOURCE_REMOVE || data->sourceid == FALSE) {
    g_printerr ("Unable to attach push_data as idle source!");
  }

  GST_INFO_OBJECT (source, "Feeding data to GstAppSrc, \
      new event source attached to GMainContext with id: %u", data->sourceid);
}


/* This callback triggers when appsrc has enough data and we can stop sending.
 * We remove the idle handler from the mainloop */
static void
stop_feed (GstElement *source, AppData *data)
{
  GST_INFO_OBJECT (source, "GstAppSrc has signaled 'enough-data'...");

  if (data->sourceid != 0) {
    g_print ("Stop feeding\n");
    g_source_remove (data->sourceid);
    GST_INFO_OBJECT (source, "Stopping data feed to GstAppSrc,\
        event source removed GMainContext with id: %u", data->sourceid);

    data->sourceid = 0;
  }
}

/* This function is called when an error message is posted on the bus */
static void
error_cb (GstBus *bus, GstMessage *msg, AppData *data)
{
  GError *err;
  gchar *debug_info;

  /* Print error details on the screen */
  gst_message_parse_error (msg, &err, &debug_info);
  g_printerr ("Error received from element %s: %s\n",
      GST_OBJECT_NAME (msg->src), err->message);

  g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
  g_clear_error (&err);
  g_free (debug_info);

  fclose (data->src->fileptr);
  gst_app_src_deactivate_buffer_pool (data->pool);

  g_main_loop_quit (data->main_loop);
}

// Handle interrupt by CTRL+C
static gboolean
handle_interrupt_signal (AppData *data)
{
  GstState state, pending;
  GstFlowReturn ret;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (
      data->pipeline, &state, &pending, (GstClockTime) 3000000000)) {
    gst_printerr ("ERROR: get current state!\n");
    g_signal_emit_by_name (data->app_source, "end-of-stream", &ret);
    return TRUE;
  }

  if (state == GST_STATE_PLAYING) {
    g_signal_emit_by_name (data->app_source, "end-of-stream", &ret);
  } else {
    fclose (data->src->fileptr);
    gst_app_src_deactivate_buffer_pool (data->pool);
    g_main_loop_quit (data->main_loop);
  }

  return TRUE;
}

// Handle end of stream event
static void
eos_cb (GstBus * bus, GstMessage * message, AppData *data)
{
  GMainLoop *mloop = data->main_loop;
  static guint eoscnt = 0;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  fclose (data->src->fileptr);
  gst_app_src_deactivate_buffer_pool (data->pool);
  g_main_loop_quit (mloop);
}

gint
main(int argc, char *argv[])
{
  AppData data = {0};
  GOptionContext *ctx = NULL;
  SourceData src;
  GstElement *app_queue, *waylandsink, *tee, *encoder, *h264parse, *mp4mux, *filesink;
  GstCaps *video_caps;
  GstVideoInfo *video_info;
  GstVideoAlignment *align_info;
  GstBus *bus;
  gchar temp_str[1000];
  guint intrpt_watch_id = 0;
  gint width, height, frame_rate;

  // Configure Defaults
  width = DEFAULT_SOURCE_WIDTH;
  height = DEFAULT_SOURCE_HEIGHT;
  frame_rate = DEFAULT_FRAMERATE;
  src.source_path = DEFAULT_SOURCE_PATH;
  src.output_path = DEFAULT_OUTPUT_PATH;
  src.padding_right = 128;
  src.padding_bottom = 456;
  src.loop = LOOP_VIDEO;

  // CLI
  GOptionEntry entries[] = {
    { "width", 'w', 0, G_OPTION_ARG_INT, &width,
        "source width, default is 1920)" },
    { "height", 'h', 0, G_OPTION_ARG_INT, &height,
        "source height, default is 1080" },
    { "padding-right", 'r', 0, G_OPTION_ARG_INT, &src.padding_right,
        "alignment padding on the right, default is 128 (for 1920x1080)" },
    { "padding-bottom", 'b', 0, G_OPTION_ARG_INT, &src.padding_bottom,
        "alignment padding on the bottom, default is 456 (for 1920x1080)" },
    { "framerate", 'f', 0, G_OPTION_ARG_INT, &frame_rate,
        "source framereate, default is 24 fps" },
    { "source", 's', 0, G_OPTION_ARG_FILENAME, &src.source_path,
        "Source file name (expects raw YUV frames as single file)" },
    { "output", 'o', 0, G_OPTION_ARG_FILENAME, &src.output_path,
        "Output file name (including extension)" },
    { "loop", 'l', 0, G_OPTION_ARG_INT, &src.loop,
        "Loop the video (1), play a single time (0) (1 By default)" },
    { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new("Sample application, showcasing \
          how to use GstAppSrc to produce GBM-back buffers." )) != NULL) {

    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    return -EFAULT;
  }

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  memset (&data, 0, sizeof (data));

  align_info = malloc (sizeof (GstVideoAlignment));

  snprintf (temp_str, sizeof (temp_str),
    "video/x-raw(memory:GBM), format=NV12,"
    "width=(int)%d, height=(int)%d, framerate=%d/1",
    width, height, frame_rate);

  video_caps = gst_caps_from_string (temp_str);
  video_info = gst_video_info_new();

  if (!gst_video_info_from_caps (video_info, video_caps)) {
    printf ("Unable to convert GstCaps into GstVideoCaps");
    return -1;
  }

  // Initialize GstVideoAlignment for but is hardcoded for 1920x1080 with 512 alignment.
  // Improvement: enable dynamic alignment
  gst_video_alignment_reset (align_info);
  align_info->padding_right = src.padding_right;
  align_info->padding_bottom = src.padding_bottom;

  // Initialize Source Data
  src.fileptr = fopen(src.source_path, "rb");
  src.filelen = get_file_length (src.fileptr);

  gst_video_info_align (video_info, align_info);

  src.frame_size = GST_VIDEO_INFO_SIZE (video_info);
  src.video_info = video_info;
  src.current_frame = 0;
  data.src = &src;

  g_print ("Frame size is %d!", src.frame_size);

  free(align_info);

  data.sourceid = 0;
  data.pool = gst_app_create_pool (video_caps, src.frame_size);

  /* Create the elements */
  data.app_source = gst_element_factory_make ("appsrc", "app_src");
  app_queue = gst_element_factory_make ("queue", "app_queue");
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  tee = gst_element_factory_make ("tee", "tee");
  h264parse = gst_element_factory_make ("h264parse", "h264parse");
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
  filesink = gst_element_factory_make ("filesink", "filesink");

#ifdef CODEC2_ENCODE
  encoder = gst_element_factory_make ("qtic2venc", "qtic2venc");
#else
  encoder = gst_element_factory_make ("omxh264enc", "omxh264enc");
#endif

  /* Create the empty pipeline */
  data.pipeline = gst_pipeline_new ("app-pipeline");

  if (!data.pipeline || !data.app_source ||  !app_queue || !waylandsink
      || !encoder || !h264parse || !mp4mux || !tee || !filesink ) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Configure appsrc */
  g_object_set (G_OBJECT (data.app_source), "caps", video_caps, "format", GST_FORMAT_TIME, NULL);
  g_object_set (G_OBJECT (data.app_source), "block", TRUE);
  GST_INFO("Size of frame: %zu", src.frame_size);

  // Five frames worth
  g_object_set (G_OBJECT (data.app_source), "max-bytes", src.frame_size*DEFAULT_MAX_BUFFERS);
  g_object_set (G_OBJECT (data.app_source), "min-percent",  40);

  g_signal_connect (data.app_source, "need-data", G_CALLBACK (start_feed), &data);
  g_signal_connect (data.app_source, "enough-data", G_CALLBACK (stop_feed), &data);
  gst_caps_unref (video_caps);

  g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);
  g_object_set (G_OBJECT (waylandsink), "max-lateness", -1, NULL);

  g_object_set (G_OBJECT (filesink), "location", src.output_path, NULL);
  g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);

#ifndef CODEC2_ENCODE
  // OMX encoder specific props
  g_object_set (G_OBJECT (encoder), "periodicity-idr", 1, NULL);
  g_object_set (G_OBJECT (encoder), "interval-intraframes", 29, NULL);
  g_object_set (G_OBJECT (encoder), "control-rate", 2, NULL);
#endif

  gst_bin_add_many(GST_BIN(data.pipeline), data.app_source, app_queue, tee,
      encoder, h264parse, mp4mux, filesink, waylandsink, NULL);

  if (!gst_element_link_many (data.app_source, app_queue, tee, encoder,
      h264parse, mp4mux, filesink, NULL)) {

      g_printerr ("Elements could not be linked.\n");
      gst_object_unref (data.pipeline);
      g_printerr("Elements in filesink stream could not be linked.");
    return -1;
  }

  if (!gst_element_link_many (tee, waylandsink, NULL)) {
      g_printerr ("Elements could not be linked.\n");
      gst_object_unref (data.pipeline);
      g_printerr("Elements in waylandsink stream could not be linked.");
    return -1;
  }


  /* Instruct the bus to emit signals for each received message,
      and connect to the interesting signals */

  bus = gst_element_get_bus (data.pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb, &data);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), &data);
  gst_object_unref (bus);

  /* Start playing the pipeline */
  GST_INFO("Starting the pipline: Set to Playing..");
  gst_element_set_state (data.pipeline, GST_STATE_PLAYING);

  /* Create a GLib Main Loop and set it to run */
  data.main_loop = g_main_loop_new (NULL, FALSE);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) handle_interrupt_signal, &data);

  g_main_loop_run (data.main_loop);

  GST_INFO("Exited the main loop..");

  /* Free resources */
  gst_element_set_state (data.pipeline, GST_STATE_NULL);

  g_source_remove (intrpt_watch_id);
  gst_object_unref (data.pipeline);
  gst_object_unref (data.pool);
  g_main_loop_unref (data.main_loop);
  gst_deinit ();

  return 0;
}
