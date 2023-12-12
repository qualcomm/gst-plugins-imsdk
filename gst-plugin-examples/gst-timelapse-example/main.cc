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
* Gstreamer application for timelapse
*
* Description:
* Capture images with low framerate for timelapse
*
* Usage:
* gst-timelapse-example -c <interval> -i <hostip>
*
* Help:
* gst-timelapse-example --help
*/

#include <stdio.h>

#include <gst/gst.h>
#include <glib-unix.h>
#include <camera/CameraMetadata.h>
#include <camera/VendorTagDescriptor.h>

#ifndef CAMERA_METADATA_1_0_NS
namespace camera = android;
#else
namespace camera = android::hardware::camera::common::V1_0::helper;
#endif // CAMERA_METADATA_1_0_NS

#define PIPELINE_MAIN "qtiqmmfsrc name=camsrc camsrc.video_0 ! " \
    "video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=30/1 ! " \
    "fakesink " \
    "camsrc.image_1 ! video/x-raw(memory:GBM),format=NV12," \
    "width=3840,height=2160,framerate=0/1,max-framerate=30/1 ! " \
    "tee name=tee_4k ! qtic2venc ! video/x-h264,framerate=30/1 ! queue ! " \
    "h264parse ! mp4mux ! " \
    "filesink location=/data/output/Timelapse_mux_4k.mp4 async=false " \
    "tee_4k. ! qtijpegenc ! multifilesink " \
    "location=/data/output/Timelapse_mux_4k_%d.jpg max-files=1 async=false " \
    "camsrc.image_2 ! video/x-raw(memory:GBM),format=NV12," \
    "width=1280,height=720,framerate=0/1,max-framerate=30/1 ! " \
    "tee name=tee_720p ! qtic2venc ! queue ! " \
    "h264parse ! mp4mux ! " \
    "filesink location=/data/output/Timelapse_mux_720p.mp4 async=false " \
    "tee_720p. ! qtic2venc ! queue ! " \
    "h264parse config-interval=-1 ! rtph264pay pt=96 ! " \
    "udpsink name=udpsink host=127.0.0.1 port=8554 async=false " \
    "tee_720p. ! appsink name=appsink emit-signals=true async=false " \
    "tee_720p. ! waylandsink sync=false async=false " \
    "x=0 y=0 width=840 height=480 " \
    "tee_720p. ! waylandsink sync=false async=false " \
    "x=0 y=480 width=480 height=480 " \
    "camsrc.image_3 ! " \
    "video/x-bayer,format=rggb,bpp=(string)10,width=4096,height=3072 ! " \
    "multifilesink location=/data/output/Timelapse_%d.raw max-files=1 async=false"

#define PIPELINE_SNAPSHOT "appsrc name=appsrc is-live=true ! " \
    "video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=1/1 ! " \
    "tee name=apsrctee ! queue ! qtijpegenc ! image/jpeg,framerate=1/1 ! queue ! " \
    "multifilesink async=false " \
    "location=/data/output/Timelapse_First_Snapshot_1280_720_%d.jpg " \
    "apsrctee. ! qtivtransform engine=fcv ! " \
    "video/x-raw(memory:GBM),format=NV12,width=400,height=224,framerate=1/1 ! " \
    "queue ! qtijpegenc ! queue ! " \
    "multifilesink async=false " \
    "location=/data/output/Timelapse_First_Snapshot_400_224_%d.jpg"

typedef struct _GstAppContext GstAppContext;

// Macro defination
#define DEFAULT_CAPTURE_INTERVAL 1
#define DEFAULT_CAPTURE_DELAY 333
#define DEFAULT_NUMBER_JPEG 1
#define DEFAULT_HOST_IP "127.0.0.1"

// Function declaration
static GstAppContext* appcontext_create ();
static void appcontext_clean (GstAppContext* appctx);
static gboolean get_metadata (GstAppContext* appctx);
static void wait_for_async (GstElement* pipe);
static GstFlowReturn new_sample_callback (GstElement* appsink, gpointer userdata);
static gboolean signals_add (GstAppContext* appctx);
static gboolean interrupt_handler (gpointer userdata);
static gboolean capture_func (gpointer userdata);
static gboolean streams_set_state (GstAppContext* appctx, GstState state);

/*** Data Structure ***/
struct _GstAppContext {
  // Pipeline main
  GstElement* pipeline_main;

  // Pipeline for snapshot
  GstElement* pipeline_snapshot;

  // Main loop
  GMainLoop* mloop;

  // Flag of exit
  gboolean exit;

  // Metadata to capture image
  GPtrArray *meta_capture;

  // Number of Jpeg
  guint num_jpeg;

  // Global mutex
  GMutex lock;
};

/*** Function ***/
static GstAppContext*
appcontext_create ()
{
  GstAppContext* appctx = g_new0 (GstAppContext, 1);
  GError* error = NULL;
  appctx->pipeline_main = NULL;
  appctx->pipeline_snapshot = NULL;
  appctx->mloop = NULL;
  appctx->exit = FALSE;
  appctx->meta_capture = NULL;
  appctx->num_jpeg = DEFAULT_NUMBER_JPEG;

  // Create pipelines
  appctx->pipeline_main = gst_parse_launch (PIPELINE_MAIN, &error);
  if (!appctx->pipeline_main && error != NULL) {
    g_printerr ("ERROR: failed to create pipeline_main, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);

    return NULL;
  } else if (!appctx->pipeline_main && error == NULL) {
    g_printerr ("ERROR: failed to create pipeline_main, unknown error!\n");

    return NULL;
  } else if (appctx->pipeline_main && error != NULL) {
    g_printerr ("ERROR: pipeline created with error: %s\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);

    return NULL;
  }

  appctx->pipeline_snapshot = gst_parse_launch (PIPELINE_SNAPSHOT, &error);
  if (!appctx->pipeline_snapshot && error != NULL) {
    g_printerr ("ERROR: failed to create pipeline_snapshot, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);

    return NULL;
  } else if (!appctx->pipeline_snapshot && error == NULL) {
    g_printerr ("ERROR: failed to create pipeline_snapshot, unknown error!\n");

    return NULL;
  } else if (appctx->pipeline_snapshot && error != NULL) {
    g_printerr ("ERROR: pipeline created with error: %s\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);

    return NULL;
  }

  // Init main loop
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: failed to create main loop.\n");
    return NULL;
  }

  // Get metadata for capture images
  appctx->meta_capture = g_ptr_array_new ();
  if (!appctx->meta_capture) {
    g_printerr ("ERROR: failed to create metadata for capture.\n");
    return NULL;
  }

  g_mutex_init (&appctx->lock);

  return appctx;
}

static void
appcontext_clean (GstAppContext* appctx)
{
  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (appctx->pipeline_snapshot != NULL) {
    gst_object_unref (appctx->pipeline_snapshot);
    appctx->pipeline_snapshot = NULL;
  }

  if (appctx->pipeline_main != NULL) {
    gst_object_unref (appctx->pipeline_main);
    appctx->pipeline_main = NULL;
  }

  if (appctx->meta_capture != NULL) {
    g_ptr_array_free (appctx->meta_capture, TRUE);
    appctx->meta_capture = NULL;
  }

  g_mutex_clear (&appctx->lock);

  g_free (appctx);
  appctx = NULL;
}

// Wait state change from ASYNC to PLAYING
static void
wait_for_async (GstElement* pipe)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  ret = gst_element_get_state (pipe, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("ERROR: failed to preroll.\n");
  } else if (ret == GST_STATE_CHANGE_SUCCESS) {
    g_print ("Preroll done.\n");
  }
}

// Get metadata for capture image
static gboolean
get_metadata (GstAppContext* appctx)
{
  ::camera::CameraMetadata* meta = NULL;
  GstElement* camera = NULL;

  camera = gst_bin_get_by_name (GST_BIN (appctx->pipeline_main), "camsrc");

  if (!camera)
    g_printerr ("ERROR: failed to get camera element.\n");

  // Get metadata for capture image
  g_object_get (G_OBJECT (camera), "image-metadata", &meta, NULL);
  if (!meta) {
    g_printerr ("ERROR: Failed to get image-metadata.\n");
    return FALSE;
  }

  g_ptr_array_add (appctx->meta_capture, (gpointer) meta);

  gst_object_unref (camera);

  return TRUE;
}

// Callback to handle state change, just print state
static void
state_change_callback (GstBus* bus, GstMessage* message, gpointer userdata)
{
  GstElement* pipe = GST_ELEMENT (userdata);
  GstState oldstate, newstate, pendingstate;

  // Only handle state change message from pipeline
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipe))
    return;

  gst_message_parse_state_changed (message,
      &oldstate, &newstate, &pendingstate);
  g_print ("\nPipeline state changed from %s to %s, pending:%s\n",
      gst_element_state_get_name (oldstate),
      gst_element_state_get_name (newstate),
      gst_element_state_get_name (pendingstate));
}

// Callback to handle warning
static void
warning_callback (GstBus* bus, GstMessage* message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

// Callback to handle error
static void
error_callback (GstBus* bus, GstMessage* message, gpointer userdata)
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

// Callback to handle eos
static void
eos_callback (GstBus* bus, GstMessage* message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\n\nReceived End-of-Stream from '%s' ...\n\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

static void
sample_release (GstSample* sample)
{
  gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
  gst_sample_set_buffer (sample, NULL);
#endif // GST_VERSION
}

// Callback for appsink to push data to appsrc
static GstFlowReturn
new_sample_callback (GstElement* appsink, gpointer userdata)
{
  GstSample* sample = NULL;
  GstBuffer* buffer = NULL;
  GstBuffer* copybuffer = NULL;
  GstAppContext* appctx = (GstAppContext*) userdata;
  GstElement* appsrc = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GstState state = GST_STATE_NULL;
  guint num = 0;

  g_mutex_lock (&appctx->lock);
  num = appctx->num_jpeg;
  g_mutex_unlock (&appctx->lock);

  if (num <= 0) {
    g_signal_emit_by_name (appsink, "pull-sample", &sample);
    sample_release (sample);

    // Close pipeline_snapshot
    if (appctx->pipeline_snapshot) {
      g_print ("send eos to pipeline_snapshot.\n");
      gst_element_send_event (appctx->pipeline_snapshot, gst_event_new_eos ());

      g_print ("Set pipeline_snapshot to NULL.\n");
      gst_element_set_state (appctx->pipeline_snapshot, GST_STATE_NULL);
      gst_object_unref (appctx->pipeline_snapshot);
      appctx->pipeline_snapshot = NULL;
    }

    g_print ("pull-sample, just return.\n");

    return GST_FLOW_OK;
  }

  // Emit available sample and retrieve it
  g_signal_emit_by_name (appsink, "pull-sample", &sample);
  g_print ("pull-sample.\n");
  if (!sample) {
    g_printerr ("ERROR: Failed to pull sample.\n");
    sample_release (sample);
    return GST_FLOW_ERROR;
  }

  buffer = gst_sample_get_buffer (sample);
  if (!buffer) {
    g_printerr ("ERROR: Failed to get buffer from sample.\n");
    sample_release (sample);
    return GST_FLOW_ERROR;
  }

  appsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline_snapshot), "appsrc");

  /*
    Select target buffer to push as preview. But first frame is black due to
    the wakeup of camera, so the first frame will be replaced with the one
    (pts >= DEFAULT_CAPTURE_DELAY) to skip wakeup.
  */
  if (GST_TIME_AS_MSECONDS (buffer->pts) <= 0) {
    g_print ("FirstJpeg Capture timestamp: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (buffer->pts));
    copybuffer = gst_buffer_copy (buffer);
    sample_release (sample);

    g_signal_emit_by_name (appsrc, "push-buffer", copybuffer, &ret);
    g_print ("push-buffer.\n");
    if (ret != GST_FLOW_OK) {
      g_printerr ("ERROR: Failed to emit push-buffer signal.\n");
      return GST_FLOW_ERROR;
    }
  } else if (GST_TIME_AS_MSECONDS (buffer->pts) >= DEFAULT_CAPTURE_DELAY) {
    g_print ("FirstJpeg Capture timestamp: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (buffer->pts));
    copybuffer = gst_buffer_copy (buffer);
    sample_release (sample);

    g_mutex_lock (&appctx->lock);
    --(appctx->num_jpeg);
    g_mutex_unlock (&appctx->lock);

    g_signal_emit_by_name (appsrc, "push-buffer", copybuffer, &ret);
    if (ret != GST_FLOW_OK) {
      g_printerr ("ERROR: Failed to emit push-buffer signal.\n");
      return GST_FLOW_ERROR;
    }
    g_print ("push-buffer.\n");
  } else {
    g_print ("Drop Capture timestamp: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (buffer->pts));
    sample_release (sample);
  }

  // Clear and exit
  gst_object_unref (appsrc);

  return GST_FLOW_OK;
}

// Retrieve bus and add signals
static gboolean
signals_add (GstAppContext* appctx)
{
  GstBus* bus_main = NULL;
  GstElement* appsink = NULL;
  GstElement* appsrc = NULL;

  bus_main = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline_main));
  if (!bus_main) {
    g_printerr ("ERROR: failed to retrieve bus from pipeline.\n");
    return FALSE;
  }

  // Add signal for bus_main
  gst_bus_add_signal_watch (bus_main);
  g_signal_connect (bus_main, "message::state-changed",
      G_CALLBACK (state_change_callback), appctx->pipeline_main);
  g_signal_connect (bus_main, "message::warning",
      G_CALLBACK (warning_callback), NULL);
  g_signal_connect (bus_main, "message::error",
      G_CALLBACK (error_callback), appctx->mloop);
  g_signal_connect (bus_main, "message::eos",
      G_CALLBACK (eos_callback), appctx->mloop);
  gst_object_unref (bus_main);

  // Add signal (new-sample) for appsink
  appsink = gst_bin_get_by_name (GST_BIN (appctx->pipeline_main), "appsink");
  g_signal_connect (appsink, "new-sample",
      G_CALLBACK (new_sample_callback), appctx);
  gst_object_unref (appsink);

  return TRUE;
}

// Interrupt handler when press Ctrl+C
static gboolean
interrupt_handler (gpointer userdata)
{
  GstAppContext* appctx = (GstAppContext*) userdata;
  GstElement* camera = NULL;
  gboolean success = FALSE;
  GstState state = GST_STATE_NULL;

  // set exit to true
  appctx->exit = TRUE;

  // send cancel-capture signal
  camera = gst_bin_get_by_name (GST_BIN (appctx->pipeline_main), "camsrc");
  g_signal_emit_by_name (G_OBJECT (camera), "cancel-capture", &success);
  g_print ("cancel-capture.\n");

  if (!success)
    g_printerr ("ERROR: Failed to emit cancel-capture signal.\n");

  g_print ("\n\nReceived an interrupt signal, sending EOS...\n\n");

  // check state of pipeline_main and send EOS only in PLAYING state
  gst_element_get_state (appctx->pipeline_main, &state, NULL, GST_CLOCK_TIME_NONE);
  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline_main, gst_event_new_eos ());
    return TRUE;
  }

  g_main_loop_quit (appctx->mloop);

  return TRUE;
}

// Function for send capture signal
static gboolean
capture_func (gpointer userdata)
{
  GstAppContext* appctx = (GstAppContext*) userdata;
  gboolean success = FALSE;
  GstElement* camera = NULL;
  GstState state = GST_STATE_NULL;

  if (appctx->exit)
    return FALSE;

  if (!appctx->meta_capture) {
    g_printerr ("ERROR: meta is not ready.\n");
    return FALSE;
  }

  gst_element_get_state (appctx->pipeline_main, &state, NULL, GST_CLOCK_TIME_NONE);
  if (state != GST_STATE_PLAYING) {
    g_printerr ("ERROR: pipeline is not in PLAYING state.\n");
    return FALSE;
  }

  camera = gst_bin_get_by_name (GST_BIN (appctx->pipeline_main), "camsrc");

  // Send capture-image signal
  g_signal_emit_by_name (G_OBJECT (camera), "capture-image", 1, 1, appctx->meta_capture, &success);
  g_print ("capture-image.\n");
  if (!success) {
    g_printerr ("ERROR: Failed to send capture request.\n");
  }

  gst_object_unref (camera);

  return TRUE;
}

// Set streams's state
static gboolean
streams_set_state (GstAppContext* appctx, GstState state)
{
  gboolean ret = TRUE;

  g_print ("Pipelines setting state to %s...\n",
      gst_element_state_get_name (state));

  if (appctx->pipeline_snapshot) {
      switch (gst_element_set_state (appctx->pipeline_snapshot, state)) {
        case GST_STATE_CHANGE_FAILURE:
          g_printerr ("ERROR: failed to change pipeline_snapshot state.\n");
          ret = FALSE;
          break;
        case GST_STATE_CHANGE_ASYNC:
          g_print ("Pipeline_snapshot is prerolling.\n");
          wait_for_async (appctx->pipeline_snapshot);
          break;
        case GST_STATE_CHANGE_SUCCESS:
          g_print ("State change successfully.\n");
          break;
        case GST_STATE_CHANGE_NO_PREROLL:
          g_print ("Pipeline state change with no preroll.\n");
          break;
    }
  }

  if (appctx->pipeline_main) {
      switch (gst_element_set_state (appctx->pipeline_main, state)) {
        case GST_STATE_CHANGE_FAILURE:
          g_printerr ("ERROR: failed to change pipeline_main state.\n");
          ret = FALSE;
          break;
        case GST_STATE_CHANGE_ASYNC:
          g_print ("Pipeline_main is prerolling.\n");
          wait_for_async (appctx->pipeline_main);
          break;
        case GST_STATE_CHANGE_SUCCESS:
          g_print ("State change successfully.\n");
          break;
        case GST_STATE_CHANGE_NO_PREROLL:
          g_print ("Pipeline state change with no preroll.\n");
          break;
    }
  }

  return ret;
}

gint
main (gint argc, gchar *argv[])
{
  GstAppContext* appctx = NULL;
  GOptionContext* ctx = NULL;
  guint interrupt = 0;
  gint capture_interval = DEFAULT_CAPTURE_INTERVAL;
  gchar* hostip = DEFAULT_HOST_IP;

  // Configure input parameters
  GOptionEntry entries[] = {
    { "capture_interval", 'c', 0, G_OPTION_ARG_INT,
      &capture_interval,
      "captureinterval",
      "Capture Interval (Unit: second; Default: 1 second)"
    },
    { "hostip", 'i', 0, G_OPTION_ARG_STRING,
      &hostip,
      "hostIP",
      "Host IP"
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

  // Init gst library
  gst_init (&argc, &argv);

  // Init GstAppContext
  appctx = appcontext_create ();
  if (!appctx) {
    g_printerr ("ERROR: failed to create appconxtext.\n");
    goto cleanup;
  }

  // Configure Host IP to send data to rtsp-server
  if (g_strcmp0 (hostip, DEFAULT_HOST_IP)) {
    GstElement* udpsink = NULL;

    udpsink = gst_bin_get_by_name (GST_BIN (appctx->pipeline_main), "udpsink");
    g_object_set (G_OBJECT (udpsink), "host", hostip, NULL);
    gst_object_unref (udpsink);

    g_print ("Udpsink host configured: %s\n", hostip);
  }

  // Add signals only for pipeline_main
  if (!signals_add (appctx)) {
    g_printerr ("ERROR: failed to add signals for pipeline_main.\n");
    goto cleanup;
  }

  // Add a function to handle CtrlC
  interrupt = g_unix_signal_add (SIGINT, interrupt_handler, appctx);

  // Start playing
  if (!streams_set_state (appctx, GST_STATE_PLAYING))
    g_printerr ("ERROR: failed to set state to PLAYING.\n");

  // Get capture metadata
  if (!get_metadata (appctx))
    g_printerr ("ERROR: Failed to get capture metadata.\n");

  if (!capture_func (appctx))
    g_print ("ERROR:failed to send capture-image");

  // Capture periodically
  g_timeout_add (capture_interval * 1000, capture_func, appctx);

  // Run main loop
  g_print ("g_main_loop_run.\n");
  g_main_loop_run (appctx->mloop);
  g_print ("g_main_loop_run ends.\n");

  streams_set_state (appctx, GST_STATE_NULL);

  // Remove unix source
  g_source_remove (interrupt);

cleanup:
  appcontext_clean (appctx);

  gst_deinit ();

  return 0;
}