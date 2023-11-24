/*
* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <glib-unix.h>
#include <gst/gst.h>
#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

#define GST_CAMERA_PIPELINE "qtiqmmfsrc name=camera " \
    "camera.video_0 ! video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=30/1 ! " \
    "queue ! waylandsink sync=false fullscreen=true enable-last-sample=false " \
    "camera.image_1 ! image/jpeg,width=1920,height=1080,framerate=30/1 ! " \
    "appsink name=sink emit-signals=true sync=false async=false enable-last-sample=false"

#define TERMINATE_MESSAGE          "APP_TERMINATE_MSG"
#define PIPELINE_STATE_MESSAGE     "APP_PIPELINE_STATE_MSG"
#define PIPELINE_EOS_MESSAGE       "APP_PIPELINE_EOS_MSG"
#define IMAGE_CAPTURE_DONE_MESSAGE "APP_IMG_CAPTURE_DONE_MSG"

#define GST_APP_CONTEXT_CAST(obj)           ((GstAppContext*)(obj))

typedef struct _GstAppContext GstAppContext;

struct _GstAppContext
{
  // Main application event loop.
  GMainLoop   *mloop;

  // GStreamer pipeline instance.
  GstElement  *pipeline;

  // Asynchronous queue thread communication.
  GAsyncQueue *messages;
};

/// Command line option variables.
static gboolean eos_on_shutdown = TRUE;
static gint     n_images        = 7;
static gint     imgtype         = 0;

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = g_new0 (GstAppContext, 1);

  ctx->messages = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);
  ctx->pipeline = NULL;
  ctx->mloop = NULL;

  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  if (ctx->pipeline != NULL)
    gst_object_unref (ctx->pipeline);

  g_async_queue_unref (ctx->messages);
  g_free (ctx);

  return;
}

static void
gst_sample_release (GstSample * sample)
{
    gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
    gst_sample_set_buffer (sample, NULL);
#endif
}

static void
gst_camera_metadata_release (gpointer data)
{
  ::camera::CameraMetadata *meta = (::camera::CameraMetadata*) data;
  delete meta;
}

static guint
get_vendor_tag_by_name (const gchar * section, const gchar * name)
{
  std::shared_ptr<::camera::VendorTagDescriptor> vtags;
  status_t status = 0;
  guint tag_id = 0;

  vtags = ::camera::VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (vtags.get() == NULL) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    return 0;
  }

  status = vtags->lookupTag(std::string(name),
      std::string(section), &tag_id);
  if (status != 0) {
    GST_WARNING ("Unable to locate tag for '%s', section '%s'!", name, section);
    return 0;
  }

  return tag_id;
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstState state = GST_STATE_VOID_PENDING;
  static gboolean waiting_eos = FALSE;

  // Signal menu thread to quit.
  g_async_queue_push (appctx->messages,
      gst_structure_new_empty (TERMINATE_MESSAGE));

  // Get the current state of the pipeline.
  gst_element_get_state (appctx->pipeline, &state, NULL, 0);

  if (eos_on_shutdown && !waiting_eos && (state == GST_STATE_PLAYING)) {
    g_print ("\nEOS enabled -- Sending EOS on the pipeline\n");

    gst_element_post_message (GST_ELEMENT (appctx->pipeline),
        gst_message_new_custom (GST_MESSAGE_EOS, GST_OBJECT (appctx->pipeline),
            gst_structure_new_empty ("GST_PIPELINE_INTERRUPT")));

    g_print ("\nWaiting for EOS ...\n");
    waiting_eos = TRUE;
  } else if (eos_on_shutdown && waiting_eos) {
    g_print ("\nInterrupt while waiting for EOS - quit main loop...\n");

    gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
    g_main_loop_quit (appctx->mloop);

    waiting_eos = FALSE;
  } else {
    g_print ("\n\nReceived an interrupt signal, stopping pipeline ...\n");
    gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
    g_main_loop_quit (appctx->mloop);
  }

  return TRUE;
}

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  static GstState target_state = GST_STATE_VOID_PENDING;
  static gboolean in_progress = FALSE, buffering = FALSE;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_error (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);

      g_print ("\nSetting pipeline to NULL ...\n");
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (TERMINATE_MESSAGE));
      g_main_loop_quit (appctx->mloop);
    }
      break;
    case GST_MESSAGE_WARNING:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      g_print ("\n\n");
      gst_message_parse_warning (message, &error, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

      g_free (debug);
      g_error_free (error);
    }
      break;
    case GST_MESSAGE_EOS:
      g_print ("\nReceived End-of-Stream from '%s' ...\n",
          GST_MESSAGE_SRC_NAME (message));

      g_async_queue_push (appctx->messages,
          gst_structure_new_empty (PIPELINE_EOS_MESSAGE));

      // Stop pipeline and quit main loop in case user interrupt has been sent.
      gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
      g_main_loop_quit (appctx->mloop);
      break;
    case GST_MESSAGE_REQUEST_STATE:
    {
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      GstState state;

      gst_message_parse_request_state (message, &state);
      g_print ("\nSetting pipeline state to %s as requested by %s...\n",
          gst_element_state_get_name (state), name);

      gst_element_set_state (appctx->pipeline, state);
      target_state = state;

      g_free (name);
    }
      break;
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState oldstate, newstate, pending;

      // Handle state changes only for the pipeline.
      if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (appctx->pipeline))
        break;

      gst_message_parse_state_changed (message, &oldstate, &newstate, &pending);
      g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
          gst_element_state_get_name (oldstate),
          gst_element_state_get_name (newstate),
          gst_element_state_get_name (pending));

      g_async_queue_push (appctx->messages, gst_structure_new (
          PIPELINE_STATE_MESSAGE, "new", G_TYPE_UINT, newstate,
          "pending", G_TYPE_UINT, pending, NULL));
    }
      break;
    default:
      break;
  }

  return TRUE;
}

static GstFlowReturn
new_sample (GstElement * element, gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
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

  // Decrease the number of images that we wait to receive.
  if ((--n_images) == 0) {
    // Signal work task that we have received all images.
    g_async_queue_push (appctx->messages,
        gst_structure_new_empty (IMAGE_CAPTURE_DONE_MESSAGE));
  }

  // Extract the original camera timestamp from GstBuffer OFFSET_END field
  timestamp = GST_BUFFER_OFFSET_END (buffer);
  g_print ("Camera timestamp: %" G_GUINT64_FORMAT "\n", timestamp);

  filename = g_strdup_printf ("/data/frame_%" G_GUINT64_FORMAT ".jpg", timestamp);

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
wait_image_capture_done_message (GAsyncQueue * messages)
{
  GstStructure *message = NULL;

  // Wait for either a PIPELINE_EOS or TERMINATE message.
  while ((message = (GstStructure*) g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, IMAGE_CAPTURE_DONE_MESSAGE))
      break;

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
wait_pipeline_eos_message (GAsyncQueue * messages)
{
  GstStructure *message = NULL;

  // Wait for either a PIPELINE_EOS or TERMINATE message.
  while ((message = (GstStructure*) g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_EOS_MESSAGE))
      break;

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
wait_pipeline_state_message (GAsyncQueue * messages, GstState state)
{
  GstStructure *message = NULL;

  // Pipeline does not notify us when changing to NULL state, skip wait.
  if (state == GST_STATE_NULL)
    return TRUE;

  // Wait for either a PIPELINE_STATE or TERMINATE message.
  while ((message = (GstStructure*) g_async_queue_pop (messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, PIPELINE_STATE_MESSAGE)) {
      GstState newstate = GST_STATE_VOID_PENDING;
      gst_structure_get_uint (message, "new", (guint*) &newstate);

      if (newstate == state)
        break;
    }

    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static gboolean
update_pipeline_state (GstElement * pipeline, GAsyncQueue * messages,
    GstState state)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstState current, pending;

  // First check current and pending states of the pipeline.
  ret = gst_element_get_state (pipeline, &current, &pending, 0);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to retrieve pipeline state!\n");
    return TRUE;
  }

  if (state == current) {
    g_print ("Already in %s state\n", gst_element_state_get_name (state));
    return TRUE;
  } else if (state == pending) {
    g_print ("Pending %s state\n", gst_element_state_get_name (state));
    return TRUE;
  }

  // Check whether to send an EOS event on the pipeline.
  if (eos_on_shutdown &&
      (current == GST_STATE_PLAYING) && (state == GST_STATE_NULL)) {
    g_print ("EOS enabled -- Sending EOS on the pipeline\n");

    if (!gst_element_send_event (pipeline, gst_event_new_eos ())) {
      g_printerr ("Failed to send EOS event!");
      return TRUE;
    }

    if (!wait_pipeline_eos_message (messages))
      return FALSE;
  }

  g_print ("Setting pipeline to %s\n", gst_element_state_get_name (state));
  ret = gst_element_set_state (pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to %s state!\n",
          gst_element_state_get_name (state));
      return TRUE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");

      ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Pipeline failed to PREROLL!\n");
        return TRUE;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  if (!wait_pipeline_state_message (messages, state))
    return FALSE;

  return TRUE;
}

static gpointer
work_task (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GstElement *camsrc = NULL;
  GPtrArray *metas = NULL;
  ::camera::CameraMetadata *smeta = nullptr, *meta = nullptr;
  gboolean success = FALSE;


  // Transition to READY state in order to initilize the camera.
  if (!update_pipeline_state (appctx->pipeline, appctx->messages, GST_STATE_READY)) {
    g_main_loop_quit (appctx->mloop);
    return NULL;
  }

  // Get a reference to the camera plugin.
  camsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "camera");

  // Get static metadata, containing the camera capabilities.
  g_object_get (G_OBJECT (camsrc), "static-metadata", &smeta, NULL);

  if (smeta == nullptr) {
    g_printerr ("ERROR: Failed to fetch static camera metadata!\n");

    gst_object_unref (camsrc);
    g_main_loop_quit (appctx->mloop);

    return NULL;
  }

  g_print ("\nGot static-metadata entries - %ld\n", smeta->entryCount());

  // Transition to PLAYING state.
  if (!update_pipeline_state (appctx->pipeline, appctx->messages, GST_STATE_PLAYING)) {

    gst_object_unref (camsrc);
    g_main_loop_quit (appctx->mloop);

    return NULL;
  }

  // Get high quality metadata, which will be used for submitting capture-image.
  g_object_get (G_OBJECT (camsrc), "image-metadata", &meta, NULL);

  if (meta == nullptr) {
    g_printerr ("ERROR: Failed to fetch camera capture metadata!\n");

    delete smeta;
    gst_object_unref (camsrc);
    g_main_loop_quit (appctx->mloop);

    return NULL;
  }

  g_print ("\nGot capture-metadata entries - %ld\n", meta->entryCount());

  metas = g_ptr_array_new_full (0, gst_camera_metadata_release);

  // Capture burst of images with AE bracketing.
  if (smeta->exists(ANDROID_CONTROL_AE_COMPENSATION_RANGE)) {
    camera_metadata_entry entry = {};
    gint32 idx = 0, compensation = 0, step = 0;

    entry = smeta->find(ANDROID_CONTROL_AE_COMPENSATION_RANGE);

    compensation = entry.data.i32[1];
    step = (entry.data.i32[0] - entry.data.i32[1]) / (n_images - 1);

    g_print ("\nCapturing images with bracketing from %d to %d step %d\n",
        entry.data.i32[1], entry.data.i32[0], step);

    // Modify a copy of the capture metadata and add it to the meta array.
    for (idx = 0; idx < n_images; idx++) {
      ::camera::CameraMetadata *metadata = new ::camera::CameraMetadata(*meta);

      metadata->update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &compensation, 1);
      compensation += step;

      g_ptr_array_add (metas, (gpointer) metadata);
    }

    imgtype = 1; // 1 - Still image capture mode.

    g_signal_emit_by_name (camsrc, "capture-image", imgtype, n_images, metas,
        &success);

    // Remove the metadatas as they are no longer needed.
    g_ptr_array_remove_range (metas, 0, n_images);
  } else {
    g_printerr ("ERROR: EV Compensation not supported!\n");
  }

  // Delete the high quality image metadata, no longer needed.
  delete meta;
  // Delete the static metadata, no longer needed.
  delete smeta;

  // Wait until all images are received or terimnate is received.
  if (!wait_image_capture_done_message (appctx->messages)) {
    gst_object_unref (camsrc);
    g_main_loop_quit (appctx->mloop);
    return NULL;
  }

  // Get video metadata, which will be used for video streams.
  g_object_get (G_OBJECT (camsrc), "video-metadata", &meta, NULL);

  // Change the video streams AWB mode.
  guchar mode = ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT;
  meta->update(ANDROID_CONTROL_AWB_MODE, &mode, 1);

  g_object_set (G_OBJECT (camsrc), "video-metadata", meta, NULL);

  g_print ("\nSwitching to continuously capturing images\n");

  n_images = 0; // 0 - Continously capture images until canceled.
  imgtype = 0; // 0 - Video image capture mode.

  g_signal_emit_by_name (camsrc, "capture-image", imgtype, n_images, metas,
      &success);

  // Free the metadatas array as it's no longer needed.
  g_ptr_array_free (metas, TRUE);

  // Decrease the reference count to the camera element, no longer needed.
  gst_object_unref (camsrc);

  // Run the pipeline for 15 more seconds.
  sleep(15);

  // Stop the pipeline.
  update_pipeline_state (appctx->pipeline, appctx->messages, GST_STATE_NULL);

  g_main_loop_quit (appctx->mloop);
  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GstAppContext *appctx = gst_app_context_new ();
  GstElement *element = NULL;
  GThread *mthread = NULL;
  guint bus_watch_id = 0, intrpt_watch_id = 0;

  g_set_prgname ("gst-camera-metadata-example");

  // Initialize GST library.
  gst_init (&argc, &argv);

  {
    GError *error = NULL;

    appctx->pipeline = gst_parse_launch (GST_CAMERA_PIPELINE, &error);

    // Check for errors on pipe creation.
    if ((NULL == appctx->pipeline) && (error != NULL)) {
      g_printerr ("Failed to create pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);

      gst_app_context_free (appctx);
      return -1;
    } else if ((NULL == appctx->pipeline) && (NULL == error)) {
      g_printerr ("Failed to create pipeline, unknown error!\n");

      gst_app_context_free (appctx);
      return -1;
    } else if ((appctx->pipeline != NULL) && (error != NULL)) {
      g_printerr ("Erroneous pipeline, error: %s!\n",
          GST_STR_NULL (error->message));

      g_clear_error (&error);
      gst_app_context_free (appctx);
      return -1;
    }
  }

  // Connect a callback to the new-sample signal.
  element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "sink");
  g_signal_connect (element, "new-sample", G_CALLBACK (new_sample), appctx);
  gst_object_unref (element);

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  {
    GstBus *bus = NULL;

    // Retrieve reference to the pipeline's bus.
    if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
      g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
      gst_app_context_free (appctx);
      return -1;
    }

    // Watch for messages on the pipeline's bus.
    bus_watch_id = gst_bus_add_watch (bus, handle_bus_message, appctx);
    gst_object_unref (bus);
  }

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Initiate the main thread in which we will work with the camera element.
  if ((mthread = g_thread_new ("WorkTask", work_task, appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create event loop thread!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Run main loop.
  g_main_loop_run (appctx->mloop);

  // Waits until main menu thread finishes.
  g_thread_join (mthread);

  g_source_remove (bus_watch_id);
  g_source_remove (intrpt_watch_id);

  gst_app_context_free (appctx);

  gst_deinit ();

  return 0;
}
