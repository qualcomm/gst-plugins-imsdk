/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
 * GStreamer tracking camera
 *
 * Description:
 * This app has two streams, mainstream for UI/Preview and low-resolution
 * stream for ML to detect object/person. ML stream pipeline
 * has Appsink which is allowing the application to receive series of
 * bounding boxes. These bounding boxes further fed to software-based
 * filter which is smoothing the curve, further boxes are passed to
 * camera plugin to apply the live crop on mainstream.
 *
 * Usage:
 * gst-tracking-cam-app
 *
 * Help:
 * gst-tracking-cam-app --help
 *
 * Parameters:
 * -p - Position threshold
 * -d - Dimensions threshold
 * -m - Dimensions margin
 * -s - Speed
 * -w - Main stream width
 * -h - Main stream height
 * -f - Format
 * -c - Crop type
 * -y - Sync enable
 *
 */
#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <ml-meta/ml_meta.h>
#include <gst/base/gstdataqueue.h>
#include <iot-core-algs/auto-framing-alg.h>

#define MP4_FILE_LOCATION "/data/mux.mp4"
#define MJPEG_FILE_LOCATION "/data/mjpeg.avi"

#define DEFAULT_MAIN_STREAM_WIDTH 3840
#define DEFAULT_MAIN_STREAM_HEIGHT 2160
#define DEFAULT_MLE_STREAM_WIDTH 640
#define DEFAULT_MLE_STREAM_HEIGHT 360
#define DEFAULT_FORMAT FORMAT_NV12
#define DEFAULT_CROP_TYPE CROP_GST

// Main stream format
typedef enum
{
  FORMAT_YUY2,
  FORMAT_MJPEG,
  FORMAT_NV12,
} GstMainStreamFormat;

// Crop type
typedef enum
{
  CROP_GST,
  CROP_CAMERA,
} GstCropType;

typedef struct _GstTrackingCamera GstTrackingCamera;
struct _GstTrackingCamera
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // Pointer to the mainloop
  GMainLoop *mloop;
  // Instance of auto framing algorithm
  AutoFramingAlgo *framing_alg_inst;
  GstDataQueue *set_crop_queue;
  GstDataQueue *mle_data_queue;
  GMutex process_lock;
  GCond process_signal;
  // Parameter for the format selected
  GstMainStreamFormat format;
  // Parameter for the crop type selected
  GstCropType crop_type;
  // Parameter for enable synchronization
  gboolean sync_enable;

  gint finish;
};

// Contains MLE bounding box data
typedef struct _GstMleData GstMleData;
struct _GstMleData
{
  GstElement *pipeline;
  GstVideoRectangle rect;
  gboolean data_valid;
};

static void
gst_sample_release (GstSample * sample)
{
    gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
    gst_sample_set_buffer (sample, NULL);
#endif
}

static void
gst_free_queue_item (gpointer data)
{
  GstDataQueueItem *item = (GstDataQueueItem *) data;
  g_free (item->object);
  g_slice_free (GstDataQueueItem, item);
}

void
apply_crop (GstMleData * mle_data, GstTrackingCamera * tracking_camera)
{
  GstElement *crop_element = NULL;
  GstPad *element_pad = NULL;
  VideoRectangle output;

  if (!tracking_camera->sync_enable) {
    switch (tracking_camera->crop_type) {
      case CROP_CAMERA:
        // Get the qmmfsrc element which is used to apply the crop property
        crop_element =
            gst_bin_get_by_name (GST_BIN (mle_data->pipeline), "qmmf");
        // Get the second pad of the qmmfsrc element
        // in oder to apply the crop property
        element_pad =
            gst_element_get_static_pad (crop_element, "video_1");
        break;
      case CROP_GST:
      default:
        // Get the qtivtransform element which is used to
        // apply the crop property
        crop_element =
            gst_bin_get_by_name (GST_BIN (mle_data->pipeline), "transform");
        break;
    }
  }

  // Check if there is received a valid mle data
  if (mle_data->data_valid) {
    VideoRectangle rect;
    rect.x = mle_data->rect.x;
    rect.y = mle_data->rect.y;
    rect.w = mle_data->rect.w;
    rect.h = mle_data->rect.h;

    // Execute the process of the Auto Framing algorithm
    output = auto_framing_algo_process (
        tracking_camera->framing_alg_inst, &rect);
  } else {
    // Execute the process of the Auto Framing algorithm
    output = auto_framing_algo_process (
        tracking_camera->framing_alg_inst, NULL);
  }

  GValue *crop = g_new0 (GValue, 1);
  g_value_init (crop, GST_TYPE_ARRAY);
  GValue value = G_VALUE_INIT;
  g_value_init (&value, G_TYPE_INT);

  g_value_set_int (&value, output.x);
  gst_value_array_append_value (crop, &value);
  g_value_set_int (&value, output.y);
  gst_value_array_append_value (crop, &value);
  g_value_set_int (&value, output.w);
  gst_value_array_append_value (crop, &value);
  g_value_set_int (&value, output.h);
  gst_value_array_append_value (crop, &value);

  if (tracking_camera->sync_enable) {
    // Put the next rect data to queue
    GstDataQueueItem *item = NULL;
    item = g_slice_new0 (GstDataQueueItem);
    item->object = GST_MINI_OBJECT (crop);
    item->visible = TRUE;
    item->destroy = gst_free_queue_item;
    if (!gst_data_queue_push (tracking_camera->set_crop_queue, item)) {
      g_printerr ("ERROR: Cannot push data to the queue!\n");
      item->destroy (item);
    }
  } else {
    switch (tracking_camera->crop_type) {
      case CROP_CAMERA:
        g_object_set_property (G_OBJECT (element_pad), "crop", crop);
        gst_object_unref (element_pad);
        break;
      case CROP_GST:
      default:
        g_object_set_property (G_OBJECT (crop_element), "crop", crop);
        break;
    }
    gst_object_unref (crop_element);
    g_free (crop);
  }
}

// Process thread functions which takes all MLE data from the queue
// and calculate the next crop window movement
static gpointer
mle_samples_process_thread (void * data)
{
  GstTrackingCamera *tracking_camera = (GstTrackingCamera *) data;
  while (1) {
    g_mutex_lock (&tracking_camera->process_lock);
    while (!tracking_camera->finish &&
        gst_data_queue_is_empty (tracking_camera->mle_data_queue)) {
      gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (10000000);
      gboolean timeout = g_cond_wait_until (&tracking_camera->process_signal,
          &tracking_camera->process_lock, wait_time);
      if (!timeout) {
        g_print ("Timeout on wait for data\n");
      }
    }

    if (tracking_camera->finish) {
      g_mutex_unlock (&tracking_camera->process_lock);
      return NULL;
    }

    // Get the bounding box data from the queue
    GstDataQueueItem *item = NULL;
    gst_data_queue_pop (tracking_camera->mle_data_queue, &item);
    g_mutex_unlock (&tracking_camera->process_lock);

    GstMleData *mle_data = (GstMleData *) item->object;
    apply_crop (mle_data, tracking_camera);
    g_slice_free (GstDataQueueItem, item);
    g_free (mle_data);
  }

  return NULL;
}

// Event handler for all data received from the MLE
static GstFlowReturn
mle_detect_new_sample (GstElement * sink, gpointer userdata)
{
  GstTrackingCamera *tracking_camera = (GstTrackingCamera *) userdata;
  GstElement *pipeline = GST_ELEMENT (tracking_camera->pipeline);
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstMapInfo info;
  gboolean is_meta_queued = FALSE;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!\n");
    return GST_FLOW_ERROR;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &info, (GstMapFlags) (GST_MAP_READ |
      GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    g_printerr ("ERROR: Failed to map the pulled buffer!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  // Get the received meta data
  GSList *meta_list = gst_buffer_get_detection_meta (buffer);
  if (meta_list != NULL) {
    guint meta_num = g_slist_length (meta_list);
    gint process_meta_idx = -1;
    gfloat conf = 0.0;

    // Loop all detected objects and get the "person" type
    // with the higher confidence
    for (gint x = 0; x < meta_num; x++) {
      gpointer metadata = g_slist_nth_data (meta_list, x);
      if (metadata != NULL) {
        GstMLDetectionMeta * meta = (GstMLDetectionMeta *) metadata;
        GstMLClassificationResult * meta_info =
              (GstMLClassificationResult *)
              g_slist_nth_data (meta->box_info, 0);
        if (g_strcmp0 (meta_info->name, "person") == 0 &&
            meta_info->confidence >= conf) {
          conf = meta_info->confidence;
          process_meta_idx = x;
        }
      }
    }

    // Check if something is detected
    if (process_meta_idx != -1) {
      gpointer metadata = g_slist_nth_data (meta_list, process_meta_idx);
      g_assert (metadata != NULL);
      GstMLDetectionMeta * meta = (GstMLDetectionMeta *) metadata;
      GstMLClassificationResult * meta_info =
            (GstMLClassificationResult *) g_slist_nth_data (meta->box_info, 0);

      GstMleData *mle_data = g_new0 (GstMleData, 1);
      mle_data->pipeline = pipeline;
      mle_data->rect.x = meta->bounding_box.x;
      mle_data->rect.y = meta->bounding_box.y;
      mle_data->rect.w = meta->bounding_box.width;
      mle_data->rect.h = meta->bounding_box.height;
      mle_data->data_valid = TRUE;

      if (tracking_camera->sync_enable) {
        apply_crop (mle_data, tracking_camera);
        g_free (mle_data);
      } else {
        // Put the detected bounding box in a queue
        GstDataQueueItem *item = NULL;
        item = g_slice_new0 (GstDataQueueItem);
        item->object = GST_MINI_OBJECT (mle_data);
        item->visible = TRUE;
        item->destroy = gst_free_queue_item;
        g_mutex_lock (&tracking_camera->process_lock);
        if (!gst_data_queue_push (
            tracking_camera->mle_data_queue, item)) {
          g_printerr ("ERROR: Cannot push data to the queue!\n");
          item->destroy (item);
          g_mutex_unlock (&tracking_camera->process_lock);
          gst_buffer_unmap (buffer, &info);
          gst_sample_release (sample);
          return GST_FLOW_ERROR;
        }
        g_cond_signal (&tracking_camera->process_signal);
        g_mutex_unlock (&tracking_camera->process_lock);
      }
      is_meta_queued = TRUE;
    }
  }

  if (!is_meta_queued) {
    GstMleData *mle_data = g_new0 (GstMleData, 1);
    mle_data->pipeline = pipeline;
    mle_data->data_valid = FALSE;

    if (tracking_camera->sync_enable) {
      apply_crop (mle_data, tracking_camera);
      g_free (mle_data);
    } else {
      GstDataQueueItem *item = NULL;
      item = g_slice_new0 (GstDataQueueItem);
      item->object = GST_MINI_OBJECT (mle_data);
      item->visible = TRUE;
      item->destroy = gst_free_queue_item;
      g_mutex_lock (&tracking_camera->process_lock);
      if (!gst_data_queue_push (
          tracking_camera->mle_data_queue, item)) {
        g_printerr ("ERROR: Cannot push data to the queue!\n");
        item->destroy (item);
        g_mutex_unlock (&tracking_camera->process_lock);
        gst_buffer_unmap (buffer, &info);
        gst_sample_release (sample);
        return GST_FLOW_ERROR;
      }
      g_cond_signal (&tracking_camera->process_signal);
      g_mutex_unlock (&tracking_camera->process_lock);
    }
  }

  gst_buffer_unmap (buffer, &info);
  gst_sample_release (sample);

  return GST_FLOW_OK;
}

static void
submit_frame_signal (gpointer * data1, gpointer * data2)
{
  GstTrackingCamera *tracking_camera = (GstTrackingCamera *) data2;
  GstElement *crop_element = NULL;
  GstPad *element_pad = NULL;
  GstDataQueueItem *item = NULL;

  if (!gst_data_queue_is_empty (tracking_camera->set_crop_queue)) {
    gst_data_queue_pop (tracking_camera->set_crop_queue, &item);

    GValue *crop = (GValue *) item->object;
    switch (tracking_camera->crop_type) {
      case CROP_CAMERA:
        // Get the qmmfsrc element which is used to apply the crop property
        crop_element =
            gst_bin_get_by_name (GST_BIN (tracking_camera->pipeline), "qmmf");
        // Get the second pad of the qmmfsrc element
        // in oder to apply the crop property
        element_pad =
            gst_element_get_static_pad (crop_element, "video_1");
        break;
      case CROP_GST:
      default:
        // Get the qtivtransform element which is used to
        // apply the crop property
        crop_element =
            gst_bin_get_by_name (
                GST_BIN (tracking_camera->pipeline), "transform");
        break;
    }

    switch (tracking_camera->crop_type) {
     case CROP_CAMERA:
       g_object_set_property (G_OBJECT (element_pad), "crop", crop);
       gst_object_unref (element_pad);
       break;
     case CROP_GST:
     default:
       g_object_set_property (G_OBJECT (crop_element), "crop", crop);
       break;
    }

    gst_object_unref (crop_element);
    g_free (crop);
    g_slice_free (GstDataQueueItem, item);
  }
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstTrackingCamera *tracking_camera = (GstTrackingCamera *) userdata;
  guint idx = 0;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (
      tracking_camera->pipeline, &state, &pending, GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (tracking_camera->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (tracking_camera->pipeline, gst_event_new_eos ());
  } else {
    g_main_loop_quit (tracking_camera->mloop);
  }

  return TRUE;
}

static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new_st),
      gst_element_state_get_name (pending));

  if ((new_st == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {
    g_print ("\nSetting pipeline to PLAYING state ...\n");

    if (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr (
          "\nPipeline doesn't want to transition to PLAYING state!\n");
      return;
    }
  }
}

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
  GstElement *element = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  gchar temp_str[1000];
  GstCaps *filtercaps;
  GstElement *pipeline = NULL;
  gboolean ret = FALSE;
  GstTrackingCamera tracking_camera = {};
  AutoFramingConfig auto_framing_config = {};
  gint diff_pos_threshold, diff_size_threshold, speed_movement = 0;

  // Set default settings
  auto_framing_config.out_width = DEFAULT_MAIN_STREAM_WIDTH;
  auto_framing_config.out_height = DEFAULT_MAIN_STREAM_HEIGHT;
  auto_framing_config.in_width = DEFAULT_MLE_STREAM_WIDTH;
  auto_framing_config.in_height = DEFAULT_MLE_STREAM_HEIGHT;
  tracking_camera.format = DEFAULT_FORMAT;
  tracking_camera.crop_type = DEFAULT_CROP_TYPE;
  tracking_camera.sync_enable = FALSE;
  diff_pos_threshold = AFR_DEFAULT_POS_THRESHOLD;
  diff_size_threshold = AFR_DEFAULT_SIZE_THRESHOLD;
  speed_movement = AFR_DEFAULT_SPEED_MOVEMENT;

  GOptionEntry entries[] = {
      { "pos-percent", 'p', 0, G_OPTION_ARG_INT,
        &diff_pos_threshold,
        "Position threshold",
        "Parameter for the position threshold of the crop"
      },
      { "dim-percent", 'd', 0, G_OPTION_ARG_INT,
        &diff_size_threshold,
        "Dimensions threshold",
        "Parameter for the dimensions threshold of the crop"
      },
      { "speed-percent", 's', 0, G_OPTION_ARG_INT,
        &speed_movement,
        "Speed of movement",
        "Parameter for the speed of movement to the final crop rectangle"
      },
      { "stream-width", 'w', 0, G_OPTION_ARG_INT,
        &auto_framing_config.out_width,
        "Main stream width",
        "Main stream width"
      },
      { "stream-height", 'h', 0, G_OPTION_ARG_INT,
        &auto_framing_config.out_height,
        "Main stream height",
        "Main stream height"
      },
      { "format", 'f', 0, G_OPTION_ARG_INT,
        &tracking_camera.format,
        "Format",
        "Parameter for the format selected"
      },
      { "crop-type", 'c', 0, G_OPTION_ARG_INT,
        &tracking_camera.crop_type,
        "Crop type",
        "Parameter for the crop type selected"
      },
      { "sync", 'y', 0, G_OPTION_ARG_NONE,
        &tracking_camera.sync_enable,
        "Synchronization enabled",
        "Parameter for enable synchronization"
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

  // Check whether the parameters are correct
  if (diff_pos_threshold < 0 ||
      diff_size_threshold < 0 ||
      speed_movement < 0 ||
      diff_pos_threshold > 100 ||
      diff_size_threshold > 100 ||
      speed_movement > 100 ||
      auto_framing_config.out_width <= 0 ||
      auto_framing_config.out_height <= 0 ||
      auto_framing_config.in_width <= 0 ||
      auto_framing_config.in_height <= 0) {

    g_print ("Incorrect configuration\n");
    return -1;
  }

  // Initialization of the Auto Framing algorithm
  tracking_camera.framing_alg_inst =
      auto_framing_algo_new (auto_framing_config);
  if (!tracking_camera.framing_alg_inst) {
    g_printerr ("ERROR: Cannot create instance to Framing algorithm\n");
    return -1;
  }

  // Set Auto Framing algorithm parameters
  auto_framing_algo_set_position_threshold (tracking_camera.framing_alg_inst,
      diff_pos_threshold);
  auto_framing_algo_set_dims_threshold (tracking_camera.framing_alg_inst,
      diff_size_threshold);
  auto_framing_algo_set_movement_speed (tracking_camera.framing_alg_inst,
      speed_movement);

  // Print the current parameters configuration
  g_print ("\nParameters:\n");
  g_print ("Position threshold - %d\n", diff_pos_threshold);
  g_print ("Dimensions threshold - %d\n", diff_size_threshold);
  g_print ("Speed - %d\n", speed_movement);
  g_print ("Format - %d\n", tracking_camera.format);
  g_print ("Crop type - %d\n", tracking_camera.crop_type);
  g_print ("Sync enable - %d\n", tracking_camera.sync_enable);
  g_print ("Main stream width - %d\n", auto_framing_config.out_width);
  g_print ("Main stream height - %d\n", auto_framing_config.out_height);

  // Initialize GST library.
  gst_init (&argc, &argv);

  GstElement *qtiqmmfsrc, *qtiqmmfsrc2, *mle_capsfilter, *main_capsfilter,
             *out_capsfilter, *qtimletflite, *appsink, *qtivtransform,
             *waylandsink, *encoder, *tee, *filesink, *h264parse,
             *mp4mux, *queue1, *queue2, *avimux;

  // Create the pipeline
  pipeline = gst_pipeline_new ("tracking-camera");
  tracking_camera.pipeline = pipeline;

  // Create all elements
  qtiqmmfsrc      = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  mle_capsfilter  = gst_element_factory_make ("capsfilter", "capsfilter1");
  main_capsfilter = gst_element_factory_make ("capsfilter", "capsfilter2");
  out_capsfilter  = gst_element_factory_make ("capsfilter", "capsfilter3");
  qtimletflite    = gst_element_factory_make ("qtimletflite", "qtimletflite");
  appsink         = gst_element_factory_make ("appsink", "appsink");
  qtivtransform   = gst_element_factory_make ("qtivtransform", "qtivtransform");
  tee             = gst_element_factory_make ("tee", "tee");
  waylandsink     = gst_element_factory_make ("waylandsink", "waylandsink");
#ifdef CODEC2_ENCODE
  encoder         = gst_element_factory_make ("qtic2venc", "qtic2venc");
#else
  encoder         = gst_element_factory_make ("omxh264enc", "omxh264enc");
#endif
  filesink        = gst_element_factory_make ("filesink", "filesink");
  h264parse       = gst_element_factory_make ("h264parse", "h264parse");
  mp4mux          = gst_element_factory_make ("mp4mux", "mp4mux");
  queue1          = gst_element_factory_make ("queue", "queue1");
  queue2          = gst_element_factory_make ("queue", "queue2");
  avimux          = gst_element_factory_make ("avimux", "avimux");

  // Check if all elements are created successfully
  if (!pipeline || !qtiqmmfsrc || !mle_capsfilter || !main_capsfilter ||
      !out_capsfilter || !qtimletflite || !appsink || !qtivtransform ||
      !tee || !waylandsink || !encoder || !filesink || !h264parse ||
      !mp4mux || !queue1 || !queue2 || !avimux) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  // Configure the MLE stream caps
  snprintf (temp_str, sizeof (temp_str),
      "video/x-raw(memory:GBM), format=NV12,"
      "width=(int)%d, height=(int)%d, framerate=30/1",
      auto_framing_config.in_width, auto_framing_config.in_height);
  filtercaps = gst_caps_from_string (temp_str);
  g_object_set (G_OBJECT (mle_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the Main stream caps
  switch (tracking_camera.format) {
    case FORMAT_YUY2:
      // Set YUY2 format
      snprintf (temp_str, sizeof (temp_str),
          "video/x-raw(memory:GBM), format=YUY2,"
          "width=(int)%d, height=(int)%d, framerate=30/1",
          auto_framing_config.out_width, auto_framing_config.out_height);
      filtercaps = gst_caps_from_string (temp_str);
      g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
      break;
    case FORMAT_MJPEG:
      // Set MJPEG format
      snprintf (temp_str, sizeof (temp_str),
          "image/jpeg, width=(int)%d, height=(int)%d, framerate=30/1",
          auto_framing_config.out_width, auto_framing_config.out_height);
      filtercaps = gst_caps_from_string (temp_str);
      g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
      break;
    case FORMAT_NV12:
    default:
      // Set NV12 format
      snprintf (temp_str, sizeof (temp_str),
          "video/x-raw(memory:GBM), format=NV12,"
          "width=(int)%d, height=(int)%d, framerate=30/1",
          auto_framing_config.out_width, auto_framing_config.out_height);
      filtercaps = gst_caps_from_string (temp_str);
      g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
      break;
  }

  // Configure the Output stream caps
  snprintf (temp_str, sizeof (temp_str),
      "video/x-raw(memory:GBM), format=NV12,"
      "width=(int)%d, height=(int)%d, framerate=30/1",
      auto_framing_config.out_width, auto_framing_config.out_height);
  filtercaps = gst_caps_from_string (temp_str);
  g_object_set (G_OBJECT (out_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Set qmmfsrc properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "qmmf", NULL);

  // Set MLE properties
  g_object_set (G_OBJECT (qtimletflite),
      "delegate", 0, NULL);
  g_object_set (G_OBJECT (qtimletflite),
      "config", "/data/misc/camera/mle_tflite.config", NULL);
  g_object_set (G_OBJECT (qtimletflite),
      "model", "/data/misc/camera/detect.tflite", NULL);
  g_object_set (G_OBJECT (qtimletflite),
      "labels", "/data/misc/camera/labelmap.txt", NULL);
  g_object_set (G_OBJECT (qtimletflite),
      "postprocessing", "detection", NULL);
  g_object_set (G_OBJECT (qtimletflite),
      "preprocess-accel", 1, NULL);

  // Set appsink properties
  g_object_set (G_OBJECT (appsink), "name", "sink_mle_detect", NULL);
  g_object_set (G_OBJECT (appsink), "emit-signals", 1, NULL);

  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "fullscreen", 1, NULL);
  g_object_set (G_OBJECT (waylandsink), "async", 1, NULL);
  g_object_set (G_OBJECT (waylandsink), "sync", 0, NULL);
  g_object_set (G_OBJECT (waylandsink), "enable-last-sample", 0, NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (encoder), "target-bitrate", 10000000, NULL);
#ifndef CODEC2_ENCODE
  g_object_set (G_OBJECT (encoder), "periodicity-idr", 1, NULL);
  g_object_set (G_OBJECT (encoder), "interval-intraframes", 59, NULL);
  g_object_set (G_OBJECT (encoder), "control-rate", 2, NULL);
#endif

  // Set qtivtransform properties
  g_object_set (G_OBJECT (qtivtransform), "name", "transform", NULL);
  if (tracking_camera.sync_enable) {
    g_signal_connect (
        qtivtransform, "submit-frame",
        G_CALLBACK (submit_frame_signal), &tracking_camera);
  }

  // Set filesink properties
  switch (tracking_camera.format) {
    case FORMAT_MJPEG:
      g_object_set (G_OBJECT (filesink), "location", MJPEG_FILE_LOCATION, NULL);
      break;
    case FORMAT_YUY2:
    case FORMAT_NV12:
    default:
      g_object_set (G_OBJECT (filesink), "location", MP4_FILE_LOCATION, NULL);
      break;
  }

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (pipeline),
      qtiqmmfsrc, mle_capsfilter, main_capsfilter, out_capsfilter,
      qtimletflite, queue1, queue2, appsink, filesink, NULL);

  switch (tracking_camera.format) {
    case FORMAT_MJPEG:
      gst_bin_add_many (GST_BIN (pipeline),
          avimux, NULL);
      break;
    case FORMAT_YUY2:
    case FORMAT_NV12:
    default:
      gst_bin_add_many (GST_BIN (pipeline),
          qtivtransform, tee, waylandsink, encoder,
          h264parse, mp4mux, NULL);
      break;
  }

  g_print ("Linking elements...\n");
  // Linking the MLE stream
  ret = gst_element_link_many (
      qtiqmmfsrc, mle_capsfilter, qtimletflite, queue1, appsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    return -1;
  }

  switch (tracking_camera.format) {
    case FORMAT_MJPEG:
      // Linking the Main stream to filesink
      ret = gst_element_link_many (
          qtiqmmfsrc, main_capsfilter, queue2, avimux, filesink, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
        return -1;
      }
      break;
    case FORMAT_YUY2:
    case FORMAT_NV12:
    default:
      // Linking the Main stream to the Wayland
      ret = gst_element_link_many (
          qtiqmmfsrc, main_capsfilter, qtivtransform, out_capsfilter, tee,
          queue2, waylandsink, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
        return -1;
      }
      // Linking the Main stream to the encoder
      ret = gst_element_link_many (
          tee, encoder, h264parse, mp4mux, filesink, NULL);
      if (!ret) {
        g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
        return -1;
      }
      break;
  }
  g_print ("All elements are linked successfully\n");

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    return -1;
  }
  tracking_camera.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Connect a callback to the new-sample signal.
  element = gst_bin_get_by_name (GST_BIN (pipeline), "sink_mle_detect");
  g_signal_connect (element, "new-sample",
      G_CALLBACK (mle_detect_new_sample), &tracking_camera);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &tracking_camera);

  g_print ("Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
      break;
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

  tracking_camera.mle_data_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, mloop);
  gst_data_queue_set_flushing (tracking_camera.mle_data_queue, FALSE);

  tracking_camera.set_crop_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, mloop);
  gst_data_queue_set_flushing (tracking_camera.set_crop_queue, FALSE);

  g_mutex_init (&tracking_camera.process_lock);
  g_cond_init (&tracking_camera.process_signal);

  GThread *thread = NULL;
  if (!tracking_camera.sync_enable) {
    // Create the process thread the new queued data
    thread = g_thread_new ("MleProcessThread",
        mle_samples_process_thread, &tracking_camera);
  }

  g_print ("g_main_loop_run\n");
  // Run main loop.
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  // Set the finish flag in order to terminate the process thread
  g_mutex_lock (&tracking_camera.process_lock);
  tracking_camera.finish = 1;
  g_cond_signal (&tracking_camera.process_signal);
  g_mutex_unlock (&tracking_camera.process_lock);

  if (!tracking_camera.sync_enable) {
    // Wait for process thread termination
    g_thread_join (thread);
  }

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  // Deinitialization of the Auto Framing algorithm
  if (tracking_camera.framing_alg_inst)
    auto_framing_algo_free (tracking_camera.framing_alg_inst);

  g_cond_clear (&tracking_camera.process_signal);
  g_mutex_clear (&tracking_camera.process_lock);
  gst_data_queue_flush (tracking_camera.set_crop_queue);
  gst_data_queue_flush (tracking_camera.mle_data_queue);
  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);
  gst_deinit ();

  return 0;
}
