/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
* Application:
* GStreamer Switch cameras in Playing state
*
* Description:
* This application uses the two cameras of the device and switch them
* using different pipelines with appsink. The switching is done every 5 seconds.
* Additional pipeline with appsrc using the camera buffers and send them
* to next plugins.
*
* Usage:
* gst-camera-switch-appsrc-example
*
* Help:
* gst-camera-switch-appsrc-example --help
*
*/

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/gstimagepool.h>

#define OUTPUT_WIDTH 1280
#define OUTPUT_HEIGHT 720
#define DEFAULT_POOL_MIN_BUFFERS 2
#define DEFAULT_POOL_MAX_BUFFERS 5
#define CAMERA_SWITCH_DELAY 5

// Function will be named gst_cam_switch_qdata_quark()
static G_DEFINE_QUARK (QtiCamswitchQuark, gst_cam_switch_qdata);

typedef struct _GstCameraSwitchCtx GstCameraSwitchCtx;

// Contains app context information
struct _GstCameraSwitchCtx
{
  GstElement *pipeline_cam0;
  GstElement *pipeline_cam1;
  GstElement *pipeline_main;
  GMainLoop *mloop;

  GstElement *qtiqmmfsrc_0;
  GstElement *qtiqmmfsrc_1;
  GstElement *capsfilter_0;
  GstElement *capsfilter_1;
  GstElement *appsink_0;
  GstElement *appsink_1;

  GstElement *appsrc;
  GstElement *waylandsink;
  GstElement *h265parse;
  GstElement *mp4mux;
  GstElement *encoder;
  GstElement *filesink;
  GstElement *queue;

  gboolean is_camera0;
  GMutex lock;
  gboolean exit;
  gboolean use_display;
  guint  camera0;
  guint  camera1;
  guint  width;
  guint  height;
  guint  switch_delay;

  GstDataQueue *buffers_queue;
  GstCaps *pool_caps;
  GstBufferPool *pool;
  gboolean pipeline_stopping;
  guint camera_buffer_cnt;
  GstClockTime last_camera_timestamp;
};

static void
gst_sample_release (GstSample * sample)
{
    gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
    gst_sample_set_buffer (sample, NULL);
#endif
}

static gboolean
create_image_pool (GstCameraSwitchCtx *cameraswitchctx)
{
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  // Create caps
  cameraswitchctx->pool_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, cameraswitchctx->width,
      "height", G_TYPE_INT, cameraswitchctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (cameraswitchctx->pool_caps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  if (!gst_video_info_from_caps (&info, cameraswitchctx->pool_caps)) {
    gst_printerr ("Invalid caps %" GST_PTR_FORMAT, cameraswitchctx->pool_caps);
    return FALSE;
  }

  cameraswitchctx->pool =
      gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
  if (!cameraswitchctx->pool) {
    gst_printerr ("Failed to ccreate a new pool!");
    return FALSE;
  }

  config = gst_buffer_pool_get_config (cameraswitchctx->pool);
  gst_buffer_pool_config_set_params (
      config, cameraswitchctx->pool_caps, info.size,
      DEFAULT_POOL_MIN_BUFFERS, DEFAULT_POOL_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (cameraswitchctx->pool, config)) {
    gst_printerr ("Failed to set pool configuration!");
    g_object_unref (cameraswitchctx->pool);
    cameraswitchctx->pool = NULL;
    return FALSE;
  }

  g_object_unref (allocator);
  gst_buffer_pool_set_active (cameraswitchctx->pool, TRUE);

  return TRUE;
}

static void
destroy_image_pool (GstCameraSwitchCtx *cameraswitchctx)
{
  if (cameraswitchctx->pool_caps) {
    gst_caps_unref (cameraswitchctx->pool_caps);
    cameraswitchctx->pool_caps = NULL;
  }

  if (cameraswitchctx->pool) {
    gst_buffer_pool_set_active (cameraswitchctx->pool, FALSE);
    g_object_unref (cameraswitchctx->pool);
    cameraswitchctx->pool = NULL;
  }
}

// Hangles interrupt signals like Ctrl+C etc.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstCameraSwitchCtx *cameraswitchctx = (GstCameraSwitchCtx *) userdata;
  guint idx = 0;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (
      cameraswitchctx->pipeline_main, &state, &pending, GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (cameraswitchctx->pipeline_main,
        gst_event_new_eos ());
    return TRUE;
  }
  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (cameraswitchctx->pipeline_main,
        gst_event_new_eos ());
  } else {
    g_main_loop_quit (cameraswitchctx->mloop);
  }

  g_mutex_lock (&cameraswitchctx->lock);
  cameraswitchctx->pipeline_stopping = TRUE;
  cameraswitchctx->exit = TRUE;
  g_mutex_unlock (&cameraswitchctx->lock);

  return TRUE;
}

// Handles state change transisions
static void
state_changed_cb_cam (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);
  g_print ("\nCAM Pipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new_st),
      gst_element_state_get_name (pending));
}

// Handles state change transisions
static void
state_changed_cb_main(GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);
  g_print ("\nMAIN Pipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new_st),
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
  GMainLoop *mloop = (GMainLoop*) userdata;
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
  GMainLoop *mloop = (GMainLoop*) userdata;
  static guint eoscnt = 0;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

static gboolean
wait_for_state_change (GstElement * pipeline) {
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  g_print ("Pipeline is PREROLLING ...\n");

  ret = gst_element_get_state (pipeline,
      NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Pipeline failed to PREROLL!\n");
    return FALSE;
  }
  return TRUE;
}

void
switch_camera (GstCameraSwitchCtx *cameraswitchctx) {
  GstElement *qmmf = NULL;
  GstElement *qmmf_current = NULL;
  GstElement *capsfilter = NULL;
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;

  g_mutex_lock (&cameraswitchctx->lock);
  if (cameraswitchctx->exit) {
    g_mutex_unlock (&cameraswitchctx->lock);
    return;
  }
  g_mutex_unlock (&cameraswitchctx->lock);

  g_print ("\n\nSwitch_camera...\n");
  if (cameraswitchctx->is_camera0) {
    // Send EOS
    gst_element_send_event (cameraswitchctx->pipeline_cam0,
        gst_event_new_eos ());

    g_mutex_lock (&cameraswitchctx->lock);
    cameraswitchctx->pipeline_stopping = TRUE;
    gst_data_queue_set_flushing (cameraswitchctx->buffers_queue, TRUE);
    g_mutex_unlock (&cameraswitchctx->lock);

    g_print ("Stopping pipeline_cam0\n");
    if (GST_STATE_CHANGE_ASYNC ==
        gst_element_set_state (cameraswitchctx->pipeline_cam0,
            GST_STATE_NULL)) {
      wait_for_state_change (cameraswitchctx->pipeline_cam0);
    }
    g_print ("Stopped pipeline_cam0\n");

    // Reset last camera timestamp since the camera will start from zero
    g_mutex_lock (&cameraswitchctx->lock);
    cameraswitchctx->last_camera_timestamp = 0;
    g_mutex_unlock (&cameraswitchctx->lock);

    g_print ("Start pipeline_cam1\n");
    if (GST_STATE_CHANGE_ASYNC ==
        gst_element_set_state (cameraswitchctx->pipeline_cam1,
            GST_STATE_PLAYING)) {
      wait_for_state_change (cameraswitchctx->pipeline_cam1);
    }
  } else {
    // Send EOS
    gst_element_send_event (cameraswitchctx->pipeline_cam1,
        gst_event_new_eos ());

    g_mutex_lock (&cameraswitchctx->lock);
    cameraswitchctx->pipeline_stopping = TRUE;
    gst_data_queue_set_flushing (cameraswitchctx->buffers_queue, TRUE);
    g_mutex_unlock (&cameraswitchctx->lock);

    g_print ("Stopping pipeline_cam1\n");
    if (GST_STATE_CHANGE_ASYNC ==
        gst_element_set_state (cameraswitchctx->pipeline_cam1,
            GST_STATE_NULL)) {
      wait_for_state_change (cameraswitchctx->pipeline_cam1);
    }
    g_print ("Stopped pipeline_cam1\n");

    // Reset last camera timestamp since the camera will start from zero
    g_mutex_lock (&cameraswitchctx->lock);
    cameraswitchctx->last_camera_timestamp = 0;
    g_mutex_unlock (&cameraswitchctx->lock);

    g_print ("Start pipeline_cam0\n");
    if (GST_STATE_CHANGE_ASYNC ==
        gst_element_set_state (cameraswitchctx->pipeline_cam0,
            GST_STATE_PLAYING)) {
      wait_for_state_change (cameraswitchctx->pipeline_cam0);
    }
  }

  cameraswitchctx->is_camera0 = !cameraswitchctx->is_camera0;
}

static void
worker_task_func (gpointer userdata)
{
  GstCameraSwitchCtx *cameraswitchctx = (GstCameraSwitchCtx *) userdata;

  sleep (cameraswitchctx->switch_delay);
  switch_camera (cameraswitchctx);

  return;
}

static void
buffer_release_notify (GstCameraSwitchCtx *cameraswitchctx)
{
  g_mutex_lock (&cameraswitchctx->lock);
  cameraswitchctx->camera_buffer_cnt--;
  if (cameraswitchctx->camera_buffer_cnt == 0 &&
      cameraswitchctx->pipeline_stopping) {

    cameraswitchctx->pipeline_stopping = FALSE;
    gst_data_queue_set_flushing (cameraswitchctx->buffers_queue, FALSE);
    g_print ("All buffers from camera are returned\n");
  }
  g_mutex_unlock (&cameraswitchctx->lock);
}

static void
buffers_task_func (gpointer userdata)
{
  GstCameraSwitchCtx *cameraswitchctx = (GstCameraSwitchCtx *) userdata;
  GstBuffer *buffer = NULL;
  static GstClockTime local_timestamp = 0;
  static GstClockTime duration = 0;

  g_mutex_lock (&cameraswitchctx->lock);

  if (cameraswitchctx->pipeline_stopping &&
      gst_data_queue_is_empty (cameraswitchctx->buffers_queue)) {

    // Acquiring blank buffers from the pool and push them to the appsrc
    // until camera is stopped

    g_mutex_unlock (&cameraswitchctx->lock);
    usleep (duration / 1000);
    g_mutex_lock (&cameraswitchctx->lock);

    // Do not send dummy buffer if all buffers are returned during sleep
    if (!cameraswitchctx->pipeline_stopping) {
      g_mutex_unlock (&cameraswitchctx->lock);
      return;
    }

    if (gst_buffer_pool_acquire_buffer (cameraswitchctx->pool, &buffer, NULL)
        != GST_FLOW_OK) {
      gst_printerr ("Failed to acquire output video buffer!\n");
      g_mutex_unlock (&cameraswitchctx->lock);
      return;
    }

    // Set time stamp of the outpput buffer
    // Increase the timestamp with 1ns to prevent visible gap in the
    // recorded video
    local_timestamp += 1;
    GST_BUFFER_DURATION (buffer) = duration;
    GST_BUFFER_PTS (buffer) = local_timestamp;

    g_print ("Push blank buffer\n");
  } else {

    // This is the normal operation when the camera is streaming
    // It takes the buffers from the buffers queue and push them to the appsrc

    GstDataQueueItem *item = NULL;
    g_mutex_unlock (&cameraswitchctx->lock);
    if (!gst_data_queue_pop (cameraswitchctx->buffers_queue, &item)) {
      g_print ("buffers_queue flushing\n");
      return;
    }
    buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);
    g_mutex_lock (&cameraswitchctx->lock);

    // Get first timestamp
    if (local_timestamp == 0) {
      local_timestamp = GST_BUFFER_PTS (buffer);
    } else if (cameraswitchctx->last_camera_timestamp == 0) {
      local_timestamp += GST_BUFFER_DURATION (buffer);
    } else {
      local_timestamp +=
          GST_BUFFER_PTS (buffer) - cameraswitchctx->last_camera_timestamp;
    }

    // Save last camera timestamp
    cameraswitchctx->last_camera_timestamp = GST_BUFFER_PTS (buffer);
    duration = GST_BUFFER_DURATION (buffer);
    GST_BUFFER_PTS (buffer) = local_timestamp;

    cameraswitchctx->camera_buffer_cnt++;
    // Set a notification function to signal when the buffer is no longer used.
    gst_mini_object_set_qdata (
        GST_MINI_OBJECT (buffer), gst_cam_switch_qdata_quark (),
        cameraswitchctx, (GDestroyNotify) buffer_release_notify
    );
  }

  if (!cameraswitchctx->exit) {
    // Push buffer to appsrc
    GstFlowReturn ret =
        gst_app_src_push_buffer (GST_APP_SRC (cameraswitchctx->appsrc), buffer);
    if (ret != GST_FLOW_OK) {
      g_printerr ("ERROR: gst_app_src_push_buffer!\n");
    }
  } else {
    g_print ("EOS, release buffer\n");
    gst_buffer_unref (buffer);
  }

  g_mutex_unlock (&cameraswitchctx->lock);

  return;
}

static gpointer
memset_all_buffers (gpointer userdata)
{
  GstCameraSwitchCtx *cameraswitchctx = (GstCameraSwitchCtx *) userdata;

  GstBuffer *buff[DEFAULT_POOL_MAX_BUFFERS];
  // Acquire and memset all buffers in the pool
  for (guint i = 0; i < DEFAULT_POOL_MAX_BUFFERS; i++) {
    GstMapInfo mapinfo;
    if (gst_buffer_pool_acquire_buffer (cameraswitchctx->pool, &buff[i], NULL)
        != GST_FLOW_OK) {
      gst_printerr ("Failed to create output video buffer!");
      return NULL;
    }

    if (!gst_buffer_map (buff[i], &mapinfo, GST_MAP_READWRITE)) {
      gst_printerr ("ERROR: Failed to map the buffer!");
      return NULL;
    }

    // memset only chroma plane with black
    memset (mapinfo.data +
        (mapinfo.size - mapinfo.size / 3), 0x80, mapinfo.size / 3);

    gst_buffer_unmap (buff[i], &mapinfo);
  }

  // Free all buffers in the pool
  for (guint i = 0; i < DEFAULT_POOL_MAX_BUFFERS; i++) {
    gst_buffer_unref (buff[i]);
  }

  return NULL;
}

static void
gst_free_queue_item (gpointer data)
{
  GstDataQueueItem *item = (GstDataQueueItem *) data;
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static GstFlowReturn
new_sample_cam (GstElement * sink, gpointer userdata)
{
  GstCameraSwitchCtx *cameraswitchctx = (GstCameraSwitchCtx *) userdata;

  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!\n");
    return GST_FLOW_ERROR;
  }

  // Release on EOS or on stopping
  g_mutex_lock (&cameraswitchctx->lock);
  if (cameraswitchctx->exit || cameraswitchctx->pipeline_stopping) {
    gst_sample_release (sample);
    g_mutex_unlock (&cameraswitchctx->lock);
    return GST_FLOW_OK;
  }
  g_mutex_unlock (&cameraswitchctx->lock);

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  // Increase ref of the bufffer and release the sample
  // Use the buffer for the next plugin
  gst_buffer_ref (buffer);
  gst_sample_release (sample);

  // Push the sample in the queue
  GstDataQueueItem *item = NULL;
  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->visible = TRUE;
  item->destroy = gst_free_queue_item;
  if (!gst_data_queue_push (cameraswitchctx->buffers_queue, item)) {
    g_printerr ("ERROR: Cannot push data to the queue!\n");
    item->destroy (item);
  }

  return GST_FLOW_OK;
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
                  guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstCaps *filtercaps;
  GstElement *pipeline = NULL;
  GstElement *qtiqmmfsrc = NULL;
  GstElement *capsfilter = NULL;
  GstElement *waylandsink = NULL;
  GstElement *encoder = NULL;
  GstElement *filesink = NULL;
  GstElement *h265parse = NULL;
  GstElement *mp4mux = NULL;
  GstElement *appsink = NULL;
  GstElement *appsrc = NULL;
  GstElement *queue = NULL;

  gboolean ret = FALSE;
  GstStateChangeReturn state_ret = GST_STATE_CHANGE_FAILURE;
  GstCameraSwitchCtx cameraswitchctx = {};
  cameraswitchctx.exit = FALSE;
  cameraswitchctx.pipeline_stopping = FALSE;
  cameraswitchctx.use_display = FALSE;
  cameraswitchctx.camera0 = 0;
  cameraswitchctx.camera1 = 1;
  cameraswitchctx.width = OUTPUT_WIDTH;
  cameraswitchctx.height = OUTPUT_HEIGHT;
  cameraswitchctx.is_camera0 = TRUE;
  cameraswitchctx.switch_delay = CAMERA_SWITCH_DELAY;
  cameraswitchctx.camera_buffer_cnt = 0;
  cameraswitchctx.last_camera_timestamp = 0;
  g_mutex_init (&cameraswitchctx.lock);

  // Initialize GST library.
  gst_init (&argc, &argv);

  GOptionEntry entries[] = {
      { "display", 'd', 0, G_OPTION_ARG_NONE,
        &cameraswitchctx.use_display,
        "Enable display",
        "Parameter for enable display output"
      },
      { "camera0", 'm', 0, G_OPTION_ARG_INT,
        &cameraswitchctx.camera0,
        "ID of camera0",
        NULL,
      },
      { "camera1", 's', 0, G_OPTION_ARG_INT,
        &cameraswitchctx.camera1,
        "ID of camera1",
        NULL,
      },
      { "width", 'w', OUTPUT_WIDTH, G_OPTION_ARG_INT,
        &cameraswitchctx.width,
        "Output width",
        NULL,
      },
      { "height", 'h', OUTPUT_HEIGHT, G_OPTION_ARG_INT,
        &cameraswitchctx.height,
        "Output height",
        NULL,
      },
      { "delay", 'l', CAMERA_SWITCH_DELAY, G_OPTION_ARG_INT,
        &cameraswitchctx.switch_delay,
        "Camera switch delay",
        NULL,
      },
      { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("DESCRIPTION")) != NULL) {
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

  g_print("Using camera0 id = %d and camera1 id = %d\n",
          cameraswitchctx.camera0,
          cameraswitchctx.camera1);

  // -------------Create Camera 0 pipeline-------------
  pipeline = gst_pipeline_new ("gst-camera0");
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc_0");
  capsfilter   = gst_element_factory_make ("capsfilter", "capsfilter");
  appsink      = gst_element_factory_make ("appsink", "appsink_0");

  // Check if all elements are created successfully
  if (!pipeline || !qtiqmmfsrc || !capsfilter || !appsink) {
    g_printerr ("One element could not be created of found. Exiting.\n");
    return -1;
  }

  // Set qmmfsrc 0 properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "qmmf_0", NULL);
  g_object_set (G_OBJECT (qtiqmmfsrc), "camera", cameraswitchctx.camera0, NULL);

  // Set capsfilter properties
  g_object_set (G_OBJECT (capsfilter), "name", "capsfilter", NULL);

  // Set appsink properties
  g_object_set (G_OBJECT (appsink), "name", "appsink_0", NULL);
  g_object_set (G_OBJECT (appsink), "emit-signals", 1, NULL);

  // Set caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, cameraswitchctx.width,
      "height", G_TYPE_INT, cameraswitchctx.height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  cameraswitchctx.pipeline_cam0 = pipeline;
  cameraswitchctx.qtiqmmfsrc_0 = qtiqmmfsrc;
  cameraswitchctx.capsfilter_0 = capsfilter;
  cameraswitchctx.appsink_0 = appsink;

  // Add elements to the pipeline
  gst_bin_add_many (GST_BIN (cameraswitchctx.pipeline_cam0), qtiqmmfsrc,
      capsfilter, appsink, NULL);

  // -------------Create Camera 1 pipeline-------------
  pipeline = gst_pipeline_new ("gst-camera1");
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc_1");
  capsfilter   = gst_element_factory_make ("capsfilter", "capsfilter");
  appsink      = gst_element_factory_make ("appsink", "appsink_1");

  // Check if all elements are created successfully
  if (!pipeline || !qtiqmmfsrc || !capsfilter || !appsink) {
    g_printerr ("One element could not be created of found. Exiting.\n");
    return -1;
  }

  // Set qmmfsrc 1 properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "qmmf_1", NULL);
  g_object_set (G_OBJECT (qtiqmmfsrc), "camera", cameraswitchctx.camera1, NULL);

  // Set capsfilter properties
  g_object_set (G_OBJECT (capsfilter), "name", "capsfilter", NULL);

  // Set appsink properties
  g_object_set (G_OBJECT (appsink), "name", "appsink_1", NULL);
  g_object_set (G_OBJECT (appsink), "emit-signals", 1, NULL);

  // Set caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, cameraswitchctx.width,
      "height", G_TYPE_INT, cameraswitchctx.height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  cameraswitchctx.pipeline_cam1 = pipeline;
  cameraswitchctx.qtiqmmfsrc_1 = qtiqmmfsrc;
  cameraswitchctx.capsfilter_1 = capsfilter;
  cameraswitchctx.appsink_1 = appsink;

  // Add elements to the pipeline
  gst_bin_add_many (GST_BIN (cameraswitchctx.pipeline_cam1), qtiqmmfsrc,
      capsfilter, appsink, NULL);

  // -------------Create Main pipeline-------------
  pipeline = gst_pipeline_new ("gst-main");
  appsrc = gst_element_factory_make ("appsrc", "appsrc");
  queue = gst_element_factory_make ("queue", "queue");

  if (!pipeline || !appsrc || !queue) {
    g_printerr ("One element could not be created of found. Exiting.\n");
    return -1;
  }

  if (cameraswitchctx.use_display) {
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    // Check if all elements are created successfully
    if (!waylandsink) {
      g_printerr ("waylandsink could not be created of found. Exiting.\n");
      return -1;
    }
  } else {
#ifdef CODEC2_ENCODE
    encoder      = gst_element_factory_make ("qtic2venc", "qtic2venc");
#else
    encoder      = gst_element_factory_make ("omxh264enc", "omxh264enc");
#endif
    filesink        = gst_element_factory_make ("filesink", "filesink");
    h265parse       = gst_element_factory_make ("h265parse", "h265parse");
    mp4mux          = gst_element_factory_make ("mp4mux", "mp4mux");

    // Check if all elements are created successfully
    if (!encoder || !filesink || !h265parse || !mp4mux) {
      g_printerr ("Encoder elements could not be created of found. Exiting.\n");
      return -1;
    }
  }

  // Set properties
  if (cameraswitchctx.use_display) {
    // Set waylandsink properties
    g_object_set (G_OBJECT (waylandsink), "name", "waylandsink", NULL);
    g_object_set (G_OBJECT (waylandsink), "x", 0, NULL);
    g_object_set (G_OBJECT (waylandsink), "y", 0, NULL);
    g_object_set (G_OBJECT (waylandsink), "width", 600, NULL);
    g_object_set (G_OBJECT (waylandsink), "height", 400, NULL);
    g_object_set (G_OBJECT (waylandsink), "async", true, NULL);
    g_object_set (G_OBJECT (waylandsink), "sync", false, NULL);
    g_object_set (G_OBJECT (waylandsink), "enable-last-sample", false, NULL);
  } else {
    g_object_set (G_OBJECT (h265parse), "name", "h265parse", NULL);
    g_object_set (G_OBJECT (mp4mux), "name", "mp4mux", NULL);

    // Set encoder properties
    g_object_set (G_OBJECT (encoder), "name", "encoder", NULL);
    g_object_set (G_OBJECT (encoder), "target-bitrate", 6000000, NULL);

#ifdef CODEC2_ENCODE
    // Codec2 encoder specific props
    g_object_set (G_OBJECT (encoder), "control-rate", 3, NULL); // VBR-CFR
#else
    // OMX encoder specific props
    g_object_set (G_OBJECT (encoder), "periodicity-idr", 1, NULL);
    g_object_set (G_OBJECT (encoder), "interval-intraframes", 29, NULL);
    g_object_set (G_OBJECT (encoder), "control-rate", 2, NULL);
#endif

    g_object_set (G_OBJECT (filesink), "name", "filesink", NULL);
    g_object_set (G_OBJECT (filesink), "location", "/data/mux.mp4", NULL);
    g_object_set (G_OBJECT (filesink), "enable-last-sample", false, NULL);
  }

  // Set appsrc properties
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, cameraswitchctx.width,
      "height", G_TYPE_INT, cameraswitchctx.height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (appsrc), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  g_object_set (G_OBJECT (appsrc),
      "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
      "format", GST_FORMAT_TIME,
      "is-live", TRUE,
      NULL);

  cameraswitchctx.pipeline_main = pipeline;
  cameraswitchctx.appsrc = appsrc;
  cameraswitchctx.queue = queue;

  if (cameraswitchctx.use_display) {
    cameraswitchctx.waylandsink = waylandsink;
  } else {
    cameraswitchctx.h265parse = h265parse;
    cameraswitchctx.mp4mux = mp4mux;
    cameraswitchctx.encoder = encoder;
    cameraswitchctx.filesink = filesink;
  }

  if (cameraswitchctx.use_display) {
    // Add elements to the pipeline
    gst_bin_add_many (GST_BIN (cameraswitchctx.pipeline_main), appsrc,
        queue, waylandsink, NULL);
  } else {
      // Add elements to the pipeline
      gst_bin_add_many (GST_BIN (cameraswitchctx.pipeline_main), appsrc,
          queue, encoder, h265parse, mp4mux, filesink, NULL);
  }

  // -------------Link Camera 0 pipeline-------------
  if (!gst_element_link_many (cameraswitchctx.qtiqmmfsrc_0,
      cameraswitchctx.capsfilter_0, cameraswitchctx.appsink_0, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    return -1;
  }

  // -------------Link Camera 1 pipeline-------------
  if (!gst_element_link_many (cameraswitchctx.qtiqmmfsrc_1,
      cameraswitchctx.capsfilter_1, cameraswitchctx.appsink_1, NULL)) {
    g_printerr ("Error: Link cannot be done!\n");
    return -1;
  }

  // -------------Link Main pipeline-------------
  if (cameraswitchctx.use_display) {
    // Link the elements
    if (!gst_element_link_many (appsrc, queue, waylandsink, NULL)) {
      g_printerr ("Error: Link cannot be done!\n");
      return -1;
    }
  } else {
    // Link the elements
    if (!gst_element_link_many (appsrc, queue, encoder,
          h265parse, mp4mux, filesink, NULL)) {
      g_printerr ("Error: Link cannot be done!\n");
      return -1;
    }
  }

  // Create image pool
  // Used for sending buffers during camera stop in order to
  // return all camera buffer and stop correctly
  if (!create_image_pool (&cameraswitchctx)) {
    gst_printerr ("ERROR: Image pool is not created!");
    return -1;
  }
  g_print ("Image pool is created successfully\n");

  // Start thread to memset all buffer in the pool
  // to remove the green frames
  GThread *memsetthread = g_thread_new (
      "MemsetThread", memset_all_buffers, &cameraswitchctx);

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    return -1;
  }
  cameraswitchctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (
      GST_PIPELINE (cameraswitchctx.pipeline_cam0))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }
  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb_cam), cameraswitchctx.pipeline_cam0);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  gst_object_unref (bus);

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (
      GST_PIPELINE (cameraswitchctx.pipeline_cam1))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }
  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb_cam), cameraswitchctx.pipeline_cam1);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  gst_object_unref (bus);

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (
      GST_PIPELINE (cameraswitchctx.pipeline_main))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }
  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb_main), cameraswitchctx.pipeline_main);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  {
    GstElement *element = gst_bin_get_by_name (
        GST_BIN (cameraswitchctx.pipeline_cam0), "appsink_0");
    g_signal_connect (element, "new-sample",
        G_CALLBACK (new_sample_cam), &cameraswitchctx);

    element = gst_bin_get_by_name (
        GST_BIN (cameraswitchctx.pipeline_cam1), "appsink_1");
    g_signal_connect (element, "new-sample",
        G_CALLBACK (new_sample_cam), &cameraswitchctx);
  }

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &cameraswitchctx);

  // Create buffers queue
  cameraswitchctx.buffers_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, mloop);
  gst_data_queue_set_flushing (cameraswitchctx.buffers_queue, FALSE);

  // Create camera switch task
  GRecMutex workerlock;
  g_rec_mutex_init (&workerlock);
  GstTask *workertask =
      gst_task_new (worker_task_func, &cameraswitchctx, NULL);
  gst_task_set_lock (workertask, &workerlock);

  // Create buffer queue task
  GRecMutex bufferslock;
  g_rec_mutex_init (&bufferslock);
  GstTask *bufferstask =
      gst_task_new (buffers_task_func, &cameraswitchctx, NULL);
  gst_task_set_lock (bufferstask, &bufferslock);

  // Start buffer queue task
  gst_task_start (bufferstask);

  g_print ("Set cam0 pipeline to GST_STATE_PLAYING state\n");
  gst_element_set_state (cameraswitchctx.pipeline_cam0, GST_STATE_PLAYING);

  g_print ("Set main pipeline to GST_STATE_PLAYING state\n");
  gst_element_set_state (cameraswitchctx.pipeline_main, GST_STATE_PLAYING);

  // Start camera switch task
  gst_task_start (workertask);

  // Run main loop.
  g_print ("run main loop\n");
  g_main_loop_run (mloop);
  g_print ("main loop ends\n");

  // Disable buffers queue
  gst_data_queue_set_flushing (cameraswitchctx.buffers_queue, TRUE);

  // Stop tasks
  gst_task_stop (workertask);
  gst_task_stop (bufferstask);

  // Make sure task is not running.
  g_rec_mutex_lock (&workerlock);
  g_rec_mutex_unlock (&workerlock);

  // Make sure task is not running.
  g_rec_mutex_lock (&bufferslock);
  g_rec_mutex_unlock (&bufferslock);

  gst_task_join (workertask);
  gst_task_join (bufferstask);
  g_rec_mutex_clear (&workerlock);
  g_rec_mutex_clear (&bufferslock);
  gst_object_unref (workertask);
  gst_object_unref (bufferstask);

  g_thread_join (memsetthread);

  g_print ("Setting MAIN pipeline to NULL state ...\n");
  if (GST_STATE_CHANGE_ASYNC ==
      gst_element_set_state (cameraswitchctx.pipeline_main, GST_STATE_NULL)) {
    wait_for_state_change (cameraswitchctx.pipeline_main);
  }

  g_print ("Setting Camera pipeline to NULL state ...\n");
  if (cameraswitchctx.is_camera0) {
    gst_element_send_event (cameraswitchctx.pipeline_cam0,
        gst_event_new_eos ());
    if (GST_STATE_CHANGE_ASYNC ==
        gst_element_set_state (cameraswitchctx.pipeline_cam0, GST_STATE_NULL)) {
      wait_for_state_change (cameraswitchctx.pipeline_cam0);
    }
  } else {
    gst_element_send_event (cameraswitchctx.pipeline_cam1,
        gst_event_new_eos ());
    if (GST_STATE_CHANGE_ASYNC ==
        gst_element_set_state (cameraswitchctx.pipeline_cam1, GST_STATE_NULL)) {
      wait_for_state_change (cameraswitchctx.pipeline_cam1);
    }
  }

  // Remove elements from the pipeline
  gst_bin_remove_many (GST_BIN (cameraswitchctx.pipeline_cam0),
      cameraswitchctx.qtiqmmfsrc_0, cameraswitchctx.capsfilter_0,
      cameraswitchctx.appsink_0, NULL);

  gst_bin_remove_many (GST_BIN (cameraswitchctx.pipeline_cam1),
        cameraswitchctx.qtiqmmfsrc_1, cameraswitchctx.capsfilter_1,
        cameraswitchctx.appsink_1, NULL);

  if (cameraswitchctx.use_display) {
    gst_bin_remove_many (GST_BIN (cameraswitchctx.pipeline_main),
      cameraswitchctx.appsrc, cameraswitchctx.queue,
      cameraswitchctx.waylandsink, NULL);
  } else {
    gst_bin_remove_many (GST_BIN (cameraswitchctx.pipeline_main),
      cameraswitchctx.appsrc, cameraswitchctx.queue,
      cameraswitchctx.encoder, cameraswitchctx.h265parse,
      cameraswitchctx.mp4mux, cameraswitchctx.filesink, NULL);
  }

  // Destroy image pool
  destroy_image_pool (&cameraswitchctx);

  // Unref all pipelines
  gst_object_unref (cameraswitchctx.pipeline_cam0);
  gst_object_unref (cameraswitchctx.pipeline_cam1);
  gst_object_unref (cameraswitchctx.pipeline_main);

  g_mutex_clear (&cameraswitchctx.lock);
  gst_data_queue_flush (cameraswitchctx.buffers_queue);
  gst_object_unref (GST_OBJECT_CAST(cameraswitchctx.buffers_queue));

  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

  gst_deinit ();

  g_print ("main: Exit\n");

  return 0;
}
