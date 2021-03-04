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
 *
 */
#include <stdio.h>
#include <math.h>
#include <glib-unix.h>
#include <gst/gst.h>
#include <ml-meta/ml_meta.h>
#include <gst/base/gstdataqueue.h>

#define MP4_FILE_LOCATION "/data/mux.mp4"
#define MJPEG_FILE_LOCATION "/data/mjpeg.avi"

#define DEFAULT_POS_PERCENT 6
#define DEFAULT_SIZE_PERCENT 13
#define DEFAULT_MARGIN_PERCENT 5
#define DEFAULT_SEED_PERCENT 15
#define DEFAULT_MAIN_STREAM_WIDTH 3840
#define DEFAULT_MAIN_STREAM_HEIGHT 2160
#define DEFAULT_MLE_STREAM_WIDTH 640
#define DEFAULT_MLE_STREAM_HEIGHT 360
#define DEFAULT_FORMAT FORMAT_NV12
#define DEFAULT_CROP_TYPE CROP_GST

// Main stream format
typedef enum {
  FORMAT_YUY2,
  FORMAT_MJPEG,
  FORMAT_NV12,
} GstMainStreamFormat;

// Crop type
typedef enum {
  CROP_GST,
  CROP_CAMERA,
} GstCropType;

// Contains tracking camera process parameters
typedef struct _GstTrackingCameraProcess GstTrackingCameraProcess;
struct _GstTrackingCameraProcess
{
  GstDataQueue *mle_data_queue;
  GMutex process_lock;
  GCond process_signal;
  gint finish;
  // Store the last received bounding box from the mle
  GstVideoRectangle last_rect;
  // Store the next crop which should be applied
  GstVideoRectangle next_rect;
};

// Contains tracking camera configuration parameters
typedef struct _GstTrackingCameraConfig GstTrackingCameraConfig;
struct _GstTrackingCameraConfig
{
  // Main stream dimensions
  gint stream_w;
  gint stream_h;

  // MLE stream dimensions
  gint stream_mle_w;
  gint stream_mle_h;

  // Parameter for the position threshold of the crop
  gint diff_pos_percent;
  // Parameter for the dimensions threshold of the crop
  gint diff_size_percent;
  // Parameter for the dimensions margin added to the calculated crop
  gint crop_margin_percent;
  // Parameter for the speed of movement to the final crop rectangle
  gint speed_move_percent;
  // Parameter for the format selected
  GstMainStreamFormat format;
  // Parameter for the crop type selected
  GstCropType crop_type;
};

typedef struct _GstTrackingCamera GstTrackingCamera;
struct _GstTrackingCamera
{
  GstElement *pipeline;
  GstTrackingCameraConfig config;
  GstTrackingCameraProcess process;
};

// Contains MLE bounding box data
typedef struct _GstMleData GstMleData;
struct _GstMleData
{
  GstElement *pipeline;
  GstVideoRectangle rect;
  gboolean dataValid;
};

// Process thread functions which takes all MLE data from the queue
// and calculate the next crop window movement
static gpointer
mle_samples_process_thread (void *data)
{
  GstElement *crop_element = NULL;
  GstPad *element_pad = NULL;
  GstTrackingCamera *tracking_camera = (GstTrackingCamera *) data;
  GstTrackingCameraConfig *config = &tracking_camera->config;
  GstTrackingCameraProcess *process = &tracking_camera->process;
  float aspect_ratio = (float) config->stream_w / (float) config->stream_h;

  GstDataQueueItem *item = NULL;

  while (1) {
    g_mutex_lock (&process->process_lock);
    while (!process->finish &&
        gst_data_queue_is_empty (process->mle_data_queue)) {
      gint64 wait_time = g_get_monotonic_time () + G_GINT64_CONSTANT (10000000);
      gboolean timeout = g_cond_wait_until (&process->process_signal,
          &process->process_lock, wait_time);
      if (!timeout) {
        g_print ("Timeout on wait for data\n");
      }
    }

    if (process->finish) {
      g_mutex_unlock (&process->process_lock);
      return NULL;
    }

    // Get the bounding box data from the queue
    gst_data_queue_pop (process->mle_data_queue, &item);
    g_mutex_unlock (&process->process_lock);

    GstMleData *mle_data = (GstMleData *) item->object;
    GValue crop = G_VALUE_INIT;
    g_value_init (&crop, GST_TYPE_ARRAY);
    GValue value = G_VALUE_INIT;
    g_value_init (&value, G_TYPE_INT);
    gint x = 0;
    gint y = 0;
    gint width = 0;
    gint height = 0;

    switch (config->crop_type) {
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

    // Check if there is received a valid mle data
    // Otherwise move smoothly to the
    // last bounding box received (if not already done)
    if (mle_data->dataValid) {
      // Apply new position if reached the position threshold
      if (abs (process->last_rect.x - mle_data->rect.x) >
          ((mle_data->rect.w + mle_data->rect.h) / 2 *
              config->diff_pos_percent / 100) ||
          abs (process->last_rect.y - mle_data->rect.y) >
          ((mle_data->rect.w + mle_data->rect.h) / 2 *
              config->diff_pos_percent / 100)) {

        process->last_rect.x = mle_data->rect.x;
        process->last_rect.y = mle_data->rect.y;
      }

      // Apply new size if reached the dimensions threshold
      if (abs (process->last_rect.w - mle_data->rect.w) >
          (mle_data->rect.w * config->diff_size_percent / 100) ||
          abs (process->last_rect.h - mle_data->rect.h) >
          (mle_data->rect.h * config->diff_size_percent / 100)) {

        process->last_rect.w = mle_data->rect.w;
        process->last_rect.h = mle_data->rect.h;
      }
    }

    // Calculate the next X movement of the crop based on
    // the speed movement parameter
    gint move_val = ceil (abs (process->last_rect.x - process->next_rect.x) *
        config->speed_move_percent / 100.0);
    if (process->last_rect.x > process->next_rect.x) {
      process->next_rect.x += move_val;
    } else {
      process->next_rect.x -= move_val;
    }

    // Calculate the next Y movement of the crop based on
    // the speed movement parameter
    move_val = ceil (abs (process->last_rect.y - process->next_rect.y) *
        config->speed_move_percent / 100.0);
    if (process->last_rect.y > process->next_rect.y) {
      process->next_rect.y += move_val;
    } else {
      process->next_rect.y -= move_val;
    }

    // Calculate the next WIDTH movement of the crop based on
    // the speed movement parameter
    move_val = ceil (abs (process->last_rect.w - process->next_rect.w) *
        config->speed_move_percent / 100.0);
    if (process->last_rect.w > process->next_rect.w) {
      process->next_rect.w += move_val;
    } else {
      process->next_rect.w -= move_val;
    }

    // Calculate the next HEIGHT movement of the crop based on
    // the speed movement parameter
    move_val = ceil (abs (process->last_rect.h - process->next_rect.h) *
        config->speed_move_percent / 100.0);
    if (process->last_rect.h > process->next_rect.h) {
      process->next_rect.h += move_val;
    } else {
      process->next_rect.h -= move_val;
    }

    // Calculate the aspect ratio to follow the original aspect ratio
    if ((process->next_rect.w / process->next_rect.h) < aspect_ratio) {
      width = process->next_rect.h * aspect_ratio;
      height = process->next_rect.h;
    } else {
      width = process->next_rect.w;
      height = process->next_rect.w / aspect_ratio;
    }

    // Center the detected object to the middle of the crop rectangle
    if (width >= height) {
      x = process->next_rect.x - ((width-height) * 0.75);
      y = process->next_rect.y;
    } else {
      x = process->next_rect.x;
      y = process->next_rect.y - ((height-width) * 0.75);
    }

    // Apply the margin of the final crop window
    // It is used to increase additionally the final size of the
    // crop rectangle by given percent of the size as a parameter
    gint crop_w_diff = width * config->crop_margin_percent / 100;
    gint crop_h_diff = height * config->crop_margin_percent / 100;
    x -= crop_w_diff;
    y -= crop_h_diff;
    width += crop_w_diff*2;
    height += crop_h_diff*2;

    // Do not allow negative position value
    if (x < 0) {
      x = 0;
    }
    if (y < 0) {
      y = 0;
    }

    // Do not go outside of the width of the rectangle
    if (x + width > config->stream_mle_w) {
      x -= x + width - config->stream_mle_w;
      if (x < 0) {
        width -= abs(x);
        x = 0;
        height = width / aspect_ratio;
      }
    }

    // Do not go outside of the height of the rectangle
    if (y + height > config->stream_mle_h) {
      y -= y + height - config->stream_mle_h;
      if (y < 0) {
        height -= abs(y);
        y = 0;
        width = height * aspect_ratio;
      }
    }

    // Align the crop dimensions based on the input size of the mle plugin
    gfloat w_coef = config->stream_w / config->stream_mle_w;
    gfloat h_coef = config->stream_h / config->stream_mle_h;
    x *= w_coef;
    y *= h_coef;
    width *= w_coef;
    height *= h_coef;

    // Apply the crop to the camera
    g_value_set_int (&value, x);
    gst_value_array_append_value (&crop, &value);
    g_value_set_int (&value, y);
    gst_value_array_append_value (&crop, &value);
    g_value_set_int (&value, width);
    gst_value_array_append_value (&crop, &value);
    g_value_set_int (&value, height);
    gst_value_array_append_value (&crop, &value);

    switch (config->crop_type) {
      case CROP_CAMERA:
        g_object_set_property (G_OBJECT (element_pad), "crop", &crop);
        gst_object_unref (element_pad);
        break;
      case CROP_GST:
      default:
        g_object_set_property (G_OBJECT (crop_element), "crop", &crop);
        break;
    }

    gst_object_unref (crop_element);
    g_free (mle_data);
    g_slice_free (GstDataQueueItem, item);
  }

  return NULL;
}

static void
gst_free_queue_item (gpointer data)
{
  GstDataQueueItem *item = data;
  GstMleData *mle_data = (GstMleData *) item->object;
  g_free (mle_data);
  g_slice_free (GstDataQueueItem, item);
}

// Event handler for all data received from the MLE
static GstFlowReturn
mle_detect_new_sample (GstElement *sink, gpointer userdata)
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
    gst_sample_unref (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ |
      GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    g_printerr ("ERROR: Failed to map the pulled buffer!\n");
    gst_sample_unref (sample);
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
      mle_data->dataValid = TRUE;

      // Put the detected bounding box in a queue
      GstDataQueueItem *item = NULL;
      item = g_slice_new0 (GstDataQueueItem);
      item->object = GST_MINI_OBJECT (mle_data);
      item->visible = TRUE;
      item->destroy = gst_free_queue_item;
      g_mutex_lock (&tracking_camera->process.process_lock);
      if (!gst_data_queue_push (
          tracking_camera->process.mle_data_queue, item)) {
        g_printerr ("ERROR: Cannot push data to the queue!\n");
        item->destroy (item);
        g_mutex_unlock (&tracking_camera->process.process_lock);
        gst_buffer_unmap (buffer, &info);
        gst_sample_unref (sample);
        return GST_FLOW_ERROR;
      }
      g_cond_signal (&tracking_camera->process.process_signal);
      g_mutex_unlock (&tracking_camera->process.process_lock);
      is_meta_queued = TRUE;
    }
  }

  if (!is_meta_queued) {
    GstMleData *mle_data = g_new0 (GstMleData, 1);
    mle_data->pipeline = pipeline;
    mle_data->dataValid = FALSE;

    GstDataQueueItem *item = NULL;
    item = g_slice_new0 (GstDataQueueItem);
    item->object = GST_MINI_OBJECT (mle_data);
    item->visible = TRUE;
    item->destroy = gst_free_queue_item;
    g_mutex_lock (&tracking_camera->process.process_lock);
    if (!gst_data_queue_push (
        tracking_camera->process.mle_data_queue, item)) {
      g_printerr ("ERROR: Cannot push data to the queue!\n");
      item->destroy (item);
      g_mutex_unlock (&tracking_camera->process.process_lock);
      gst_buffer_unmap (buffer, &info);
      gst_sample_unref (sample);
      return GST_FLOW_ERROR;
    }
    g_cond_signal (&tracking_camera->process.process_signal);
    g_mutex_unlock (&tracking_camera->process.process_lock);
  }

  gst_buffer_unmap (buffer, &info);
  gst_sample_unref (sample);

  return GST_FLOW_OK;
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstElement *pipeline = userdata;
  guint idx = 0;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");
  gst_element_send_event (pipeline, gst_event_new_eos ());
  return TRUE;
}

static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new),
      gst_element_state_get_name (pending));

  if ((new == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
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
main (gint argc, gchar *argv[])
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

  // Set default settings
  tracking_camera.config.diff_pos_percent = DEFAULT_POS_PERCENT;
  tracking_camera.config.diff_size_percent = DEFAULT_SIZE_PERCENT;
  tracking_camera.config.crop_margin_percent = DEFAULT_MARGIN_PERCENT;
  tracking_camera.config.speed_move_percent = DEFAULT_SEED_PERCENT;
  tracking_camera.config.stream_w = DEFAULT_MAIN_STREAM_WIDTH;
  tracking_camera.config.stream_h = DEFAULT_MAIN_STREAM_HEIGHT;
  tracking_camera.config.stream_mle_w = DEFAULT_MLE_STREAM_WIDTH;
  tracking_camera.config.stream_mle_h = DEFAULT_MLE_STREAM_HEIGHT;
  tracking_camera.config.format = DEFAULT_FORMAT;
  tracking_camera.config.crop_type = DEFAULT_CROP_TYPE;

  GOptionEntry entries[] = {
      { "pos-percent", 'p', 0, G_OPTION_ARG_INT,
        &tracking_camera.config.diff_pos_percent,
        "Position threshold",
        "Parameter for the position threshold of the crop"
      },
      { "dim-percent", 'd', 0, G_OPTION_ARG_INT,
        &tracking_camera.config.diff_size_percent,
        "Dimensions threshold",
        "Parameter for the dimensions threshold of the crop"
      },
      { "margin-percent", 'm', 0, G_OPTION_ARG_INT,
        &tracking_camera.config.crop_margin_percent,
        "Dimensions margin",
        "Parameter for the dimensions margin added to the calculated crop"
      },
      { "speed-percent", 's', 0, G_OPTION_ARG_INT,
        &tracking_camera.config.speed_move_percent,
        "Speed of movement",
        "Parameter for the speed of movement to the final crop rectangle"
      },
      { "stream-width", 'w', 0, G_OPTION_ARG_INT,
        &tracking_camera.config.stream_w,
        "Main stream width",
        "Main stream width"
      },
      { "stream-height", 'h', 0, G_OPTION_ARG_INT,
        &tracking_camera.config.stream_h,
        "Main stream height",
        "Main stream height"
      },
      { "format", 'f', 0, G_OPTION_ARG_INT,
        &tracking_camera.config.format,
        "Format",
        "Parameter for the format selected"
      },
      { "crop-type", 'c', 0, G_OPTION_ARG_INT,
        &tracking_camera.config.crop_type,
        "Crop type",
        "Parameter for the crop type selected"
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
  if (tracking_camera.config.diff_pos_percent < 0 ||
      tracking_camera.config.diff_size_percent < 0 ||
      tracking_camera.config.crop_margin_percent < 0 ||
      tracking_camera.config.speed_move_percent < 0 ||
      tracking_camera.config.diff_pos_percent > 100 ||
      tracking_camera.config.diff_size_percent > 100 ||
      tracking_camera.config.crop_margin_percent > 100 ||
      tracking_camera.config.speed_move_percent > 100 ||
      tracking_camera.config.stream_w <= 0 ||
      tracking_camera.config.stream_h <= 0 ||
      tracking_camera.config.stream_mle_w <= 0 ||
      tracking_camera.config.stream_mle_h <= 0) {

    g_print ("Incorrect configuration\n");
    return -1;
  }

  // Print the current parameters configuration
  g_print ("\nParameters:\n");
  g_print ("Position threshold - %d\n",
      tracking_camera.config.diff_pos_percent);
  g_print ("Dimensions threshold - %d\n",
      tracking_camera.config.diff_size_percent);
  g_print ("Dimensions margin - %d\n",
      tracking_camera.config.crop_margin_percent);
  g_print ("Speed - %d\n", tracking_camera.config.speed_move_percent);
  g_print ("Format - %d\n", tracking_camera.config.format);
  g_print ("Crop type - %d\n", tracking_camera.config.crop_type);
  g_print ("Main stream width - %d\n", tracking_camera.config.stream_w);
  g_print ("Main stream height - %d\n", tracking_camera.config.stream_h);

  // Initialize GST library.
  gst_init (&argc, &argv);

  GstElement *qtiqmmfsrc, *qtiqmmfsrc2, *mle_capsfilter, *main_capsfilter,
             *out_capsfilter, *qtimletflite, *appsink, *qtivtransform,
             *waylandsink, *omxh264enc, *tee, *filesink, *h264parse,
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
  omxh264enc      = gst_element_factory_make ("omxh264enc", "omxh264enc");
  filesink        = gst_element_factory_make ("filesink", "filesink");
  h264parse       = gst_element_factory_make ("h264parse", "h264parse");
  mp4mux          = gst_element_factory_make ("mp4mux", "mp4mux");
  queue1          = gst_element_factory_make ("queue", "queue1");
  queue2          = gst_element_factory_make ("queue", "queue2");
  avimux          = gst_element_factory_make ("avimux", "avimux");

  // Check if all elements are created successfully
  if (!pipeline || !qtiqmmfsrc || !mle_capsfilter || !main_capsfilter ||
      !out_capsfilter || !qtimletflite || !appsink || !qtivtransform ||
      !tee || !waylandsink || !omxh264enc || !filesink || !h264parse ||
      !mp4mux || !queue1 || !queue2 || !avimux) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  // Configure the MLE stream caps
  snprintf (temp_str, sizeof (temp_str),
      "video/x-raw(memory:GBM), format=NV12,"
      "width=(int)%d, height=(int)%d, framerate=30/1",
      tracking_camera.config.stream_mle_w, tracking_camera.config.stream_mle_h);
  filtercaps = gst_caps_from_string (temp_str);
  g_object_set (G_OBJECT (mle_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the Main stream caps
  switch (tracking_camera.config.format) {
    case FORMAT_YUY2:
      // Set YUY2 format
      snprintf (temp_str, sizeof (temp_str),
          "video/x-raw(memory:GBM), format=YUY2,"
          "width=(int)%d, height=(int)%d, framerate=30/1",
          tracking_camera.config.stream_w, tracking_camera.config.stream_h);
      filtercaps = gst_caps_from_string (temp_str);
      g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
      break;
    case FORMAT_MJPEG:
      // Set MJPEG format
      snprintf (temp_str, sizeof (temp_str),
          "image/jpeg, width=(int)%d, height=(int)%d, framerate=30/1",
          tracking_camera.config.stream_w, tracking_camera.config.stream_h);
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
          tracking_camera.config.stream_w, tracking_camera.config.stream_h);
      filtercaps = gst_caps_from_string (temp_str);
      g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
      gst_caps_unref (filtercaps);
      break;
  }

  // Configure the Output stream caps
  snprintf (temp_str, sizeof (temp_str),
      "video/x-raw(memory:GBM), format=NV12,"
      "width=(int)%d, height=(int)%d, framerate=30/1",
      tracking_camera.config.stream_w, tracking_camera.config.stream_h);
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

  // Set appsink properties
  g_object_set (G_OBJECT (appsink), "name", "sink_mle_detect", NULL);
  g_object_set (G_OBJECT (appsink), "emit-signals", 1, NULL);

  // Set waylandsink properties
  g_object_set (G_OBJECT (waylandsink), "fullscreen", 1, NULL);
  g_object_set (G_OBJECT (waylandsink), "async", 1, NULL);
  g_object_set (G_OBJECT (waylandsink), "sync", 0, NULL);
  g_object_set (G_OBJECT (waylandsink), "enable-last-sample", 0, NULL);

  // Set encoder properties
  g_object_set (G_OBJECT (omxh264enc), "target-bitrate", 20000000, NULL);

  // Set qtivtransform properties
  g_object_set (G_OBJECT (qtivtransform), "name", "transform", NULL);

  // Set filesink properties
  switch (tracking_camera.config.format) {
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

  switch (tracking_camera.config.format) {
    case FORMAT_MJPEG:
      gst_bin_add_many (GST_BIN (pipeline),
          avimux, NULL);
      break;
    case FORMAT_YUY2:
    case FORMAT_NV12:
    default:
      gst_bin_add_many (GST_BIN (pipeline),
          qtivtransform, tee, waylandsink, omxh264enc,
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

  switch (tracking_camera.config.format) {
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
          tee, omxh264enc, h264parse, mp4mux, filesink, NULL);
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
      g_unix_signal_add (SIGINT, handle_interrupt_signal, pipeline);

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

  tracking_camera.process.mle_data_queue =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, mloop);
  g_mutex_init (&tracking_camera.process.process_lock);
  g_cond_init (&tracking_camera.process.process_signal);

  // Create the process thread the new queued data
  GThread *thread = g_thread_new ("MleProcessThread",
      mle_samples_process_thread, &tracking_camera);
  gst_data_queue_set_flushing (tracking_camera.process.mle_data_queue, FALSE);

  g_print ("g_main_loop_run\n");
  // Run main loop.
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  // Set the finish flag in order to terminate the process thread
  g_mutex_lock (&tracking_camera.process.process_lock);
  tracking_camera.process.finish = 1;
  g_cond_signal (&tracking_camera.process.process_signal);
  g_mutex_unlock (&tracking_camera.process.process_lock);

  // Wait for process thread termination
  g_thread_join (thread);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_cond_clear (&tracking_camera.process.process_signal);
  g_mutex_clear (&tracking_camera.process.process_lock);
  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);
  gst_deinit ();

  return 0;
}
