/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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
* GStreamer use the depth data to assist auto focus
*
* Description:
* This application demonstrate the ability of the qmmfsrc to use the
* dummy depth data to assist auto focus via camera vendor tags. to
* simulate the depth sensor works progress , add thread to continuce
* send the dummy depth data to camera hal layer in 30fps by default.
* the dummy depth data input with command line and keyboard.
*
* The auto focus position output in the camera hal layer and checked
* it wiht relevant log.
*
*
* Usage:
* gst-depth-assist-autofocus-example
*
*/

#include <glib.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <camera/CameraMetadata.h>
#include <camera/VendorTagDescriptor.h>

#ifndef CAMERA_METADATA_1_0_NS
namespace camera = android;
#else
namespace camera = android::hardware::camera::common::V1_0::helper;
#endif

#define DEFAULT_30_FPS 33

#define HASH_LINE  "##################################################"
#define EQUAL_LINE "=================================================="

#define APPEND_MENU_HEADER(string) \
  g_string_append_printf (string, "\n\n%.*s MENU %.*s\n\n", \
      37, HASH_LINE, 37, HASH_LINE);

#define APPEND_CONTROLS_SECTION(string) \
  g_string_append_printf (string, " %.*s Depth Parameter Controls %.*s\n", \
      30, EQUAL_LINE, 30, EQUAL_LINE);

#define GST_APP_CONTEXT_CAST(obj)           ((GstAppContext*)(obj))

#define STDIN_MESSAGE          "APP_STDIN_MSG"
#define TERMINATE_MESSAGE      "APP_TERMINATE_MSG"

#define DEPTH_AF_ENABLE_OPTION           "e"
#define DEPTH_AF_DISTENCE_OPTION         "d"
#define DEPTH_AF_CONFIDENCE_OPTION       "c"
#define DEPTH_AF_NEAR_LIMITATION_OPTION  "n"
#define DEPTH_AF_FAR_LIMITATION_OPTION   "f"

typedef struct _DepthAFOps DepthAFOps;
typedef struct _GstAppContext GstAppContext;

/// ML Auto Framing related command line options.
struct _DepthAFOps
{
  gboolean enable;
  gint     distance;
  gint     confidence;
  gint     nearLimitation;
  gint     farLimitation;
};

static DepthAFOps depthafops = {
  TRUE, 10000, 2, 100, 10000
};
struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;

  // Pointer to the qmmfsrc
  GstElement *qtiqmmfsrc;

  // Pointer to the mainloop
  GMainLoop *mloop;

  GMutex update_lock;
  GCond update_signal;

  // Asynchronous queue thread communication.
  GAsyncQueue *messages;

  // Flag of finish the metadata thread
  gint finish;
};

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = g_new0 (GstAppContext, 1);

  ctx->messages = g_async_queue_new_full ((GDestroyNotify) gst_structure_free);
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  g_mutex_init (&ctx->update_lock);
  g_cond_init (&ctx->update_signal);
  ctx->finish = 0;

  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  if (ctx->pipeline != NULL)
    gst_object_unref (ctx->pipeline);

  if(ctx->qtiqmmfsrc != NULL)
    gst_object_unref (ctx->qtiqmmfsrc);

  g_async_queue_unref (ctx->messages);
  g_free (ctx);

  return;
}

static gboolean
wait_stdin_message (GAsyncQueue * messages, gchar** input)
{
  GstStructure *message = NULL;

  // Cleanup input variable from previous uses.
  g_free (*input);
  *input = NULL;

  // Wait for either a STDIN or TERMINATE message.
  while ((message = (GstStructure *)g_async_queue_pop(messages)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      return FALSE;
    }

    if (gst_structure_has_name (message, STDIN_MESSAGE)) {
      *input = g_strdup (gst_structure_get_string (message, "input"));
      break;
    }

    gst_structure_free (message);
  }

  gst_structure_free (message);

  return TRUE;
}


static gboolean
extract_integer_value (const gchar * input, gint64 min, gint64 max, gint64 * value)
{
  // Convert string to integer value.
  gint64 newvalue = g_ascii_strtoll (input, NULL, 0);

  if (newvalue < min && newvalue > max) {
    g_printerr ("\nValue is outside range!\n");
    return FALSE;
  }

  *value = newvalue;
  return TRUE;
}

static void
gst_sample_release (GstSample * sample)
{
    gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
    gst_sample_set_buffer (sample, NULL);
#endif
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);

  g_print ("\n\nReceived an interrupt signal, quit main loop ...\n");
  gst_element_send_event (pipeline, gst_event_new_eos ());

  return TRUE;
}

static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, newst, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &newst, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (newst),
      gst_element_state_get_name (pending));

  if ((newst == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {
    g_print ("\nSetting pipeline to PLAYING state ...\n");

    if (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr ("\nPipeline doesn't want to transition to PLAYING state!\n");
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

static guint
get_vendor_tag_by_name (const gchar * section, const gchar * name)
{
  ::android::sp<::camera::VendorTagDescriptor> vtags;
  ::android::status_t status = 0;
  guint tag_id = 0;

  vtags = ::camera::VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (vtags.get() == NULL) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    return 0;
  }

  status = vtags->lookupTag(::android::String8(name),
      ::android::String8(section), &tag_id);
  if (status != 0) {
    GST_WARNING ("Unable to locate tag for '%s', section '%s'!", name, section);
    return 0;
  }

  return tag_id;
}

static GstFlowReturn
new_sample (GstElement *sink, gpointer userdata)
{
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  guint64 timestamp = 0;
  GstMapInfo info;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!");
    return GST_FLOW_ERROR;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    g_printerr ("ERROR: Failed to map the pulled buffer!");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  // Extract the original camera timestamp from GstBuffer OFFSET_END field
  timestamp = GST_BUFFER_OFFSET_END (buffer);

  gst_buffer_unmap (buffer, &info);
  gst_sample_release (sample);

  return GST_FLOW_OK;
}

static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  g_main_loop_quit (mloop);
}

static gboolean
handle_stdin_source (GIOChannel * source, GIOCondition condition,
    gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  GIOStatus status = G_IO_STATUS_NORMAL;
  gchar *input = NULL;

  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((G_IO_STATUS_ERROR == status) && (error != NULL)) {
      g_printerr ("Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    } else if ((G_IO_STATUS_ERROR == status) && (NULL == error)) {
      g_printerr ("Unknown error!\n");
      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  // Clear trailing whitespace and newline.
  input = g_strchomp (input);

  // Push stdin string into the inputs queue.
  g_async_queue_push (appctx->messages, gst_structure_new (STDIN_MESSAGE,
      "input", G_TYPE_STRING, input, NULL));
  g_free (input);

  return TRUE;
}

// update the depth data with vendor tag at the default speed
static gpointer
metadata_update_thread (gpointer userdata)
{
  gint32   value = 0;
  gint64   timestamp = 0;
  guint    tag_id = 0;

  GstAppContext *appctx = GST_APP_CONTEXT_CAST(userdata);

  g_mutex_lock (&appctx->update_lock);
  while (!appctx->finish) {

    //waiting to timeout
    gint64 wait_time = g_get_monotonic_time () +
        DEFAULT_30_FPS* G_TIME_SPAN_MILLISECOND;
    gboolean timeout = g_cond_wait_until (&appctx->update_signal,
        &appctx->update_lock, wait_time);
    if ((!timeout) && (!appctx->finish)) {

      // Get video metadata
      ::camera::CameraMetadata *meta = nullptr;
      g_object_get (G_OBJECT (appctx->qtiqmmfsrc),
          "video-metadata", &meta, NULL);
      if (meta) {

        // Set auto focus mode
        guchar afmode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        meta->update(ANDROID_CONTROL_AF_MODE, &afmode, 1);

        //Set depth assist af valid flag
        value = depthafops.enable;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "isvalid");
        if (tag_id != 0)
          meta->update(tag_id, &value, 1);

        // Calculated object distance in mm
        value = depthafops.distance;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "distanceInMilliMeters");
        if (tag_id != 0)
          meta->update(tag_id, &value, 1);

        //Set object distance confidence level
        value = depthafops.confidence;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "confidence");
        if (tag_id != 0)
          meta->update(tag_id, &value, 1);

        //Set min object distance measured
        value = depthafops.nearLimitation;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "nearLimitation");
        if (tag_id != 0)
          meta->update(tag_id, &value, 1);

        //Set max distanc measured
        value = depthafops.farLimitation;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "farLimitation");
        if (tag_id != 0)
          meta->update(tag_id, &value, 1);

        // Set timestamp of arrival of the laser data
        timestamp = g_get_monotonic_time ();
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "timestamp");
        if (tag_id != 0)
          meta->update(tag_id, &timestamp, 1);

        g_object_set (G_OBJECT (appctx->qtiqmmfsrc),
            "video-metadata", meta, NULL);
      } else {
        g_print ("Get video-metadata failed!\n");
      }
    }

  }
  g_mutex_unlock (&appctx->update_lock);

  g_print ("Meta update thread exit\n");
  return NULL;
}

static gboolean
depth_ops_menu (GAsyncQueue * messages)
{
  GString *options = g_string_new (NULL);
  gchar *input = NULL;

  APPEND_MENU_HEADER (options);

  APPEND_CONTROLS_SECTION (options);
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      DEPTH_AF_ENABLE_OPTION, "Depth tof data flag",
      "Enable/Disable Depth tof data");
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      DEPTH_AF_DISTENCE_OPTION, "Distance value (in millimeters)",
      "Set the distance value  ");
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      DEPTH_AF_CONFIDENCE_OPTION, "Distance confidence level",
      "Set the distance confidence level ");
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      DEPTH_AF_NEAR_LIMITATION_OPTION, "Depth distance near limitation",
      "Set depth distance min vaue in millimeter");
  g_string_append_printf (options, "   (%s) %-35s: %s\n",
      DEPTH_AF_FAR_LIMITATION_OPTION, "Depth distance far limitation",
      "Set depth distance max value in millimeters");

  g_print ("%s", options->str);
  g_string_free (options, TRUE);

  g_print ("\n\nChoose an option: ");

  // If FALSE is returned termination signal has been issued.
  if (!wait_stdin_message (messages, &input))
    return FALSE;

  if (g_str_equal (input, DEPTH_AF_ENABLE_OPTION)) {
    gint64 value = depthafops.enable;

    g_print ("\nCurrent value: %d - [0 - disable, 1 - enable]\n",
        depthafops.enable);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 0, 1, &value);

      depthafops.enable = value;
  } else if (g_str_equal (input, DEPTH_AF_DISTENCE_OPTION)) {
    gint64 value = depthafops.distance;

    g_print ("\nCurrent value: %d - [100 - 10000]\n", depthafops.distance);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 100, 10000, &value);

    depthafops.distance = value;
  } else if (g_str_equal (input, DEPTH_AF_CONFIDENCE_OPTION)) {
    gint64 value = depthafops.confidence;

    g_print ("\nCurrent value: %d - [0 - 2]\n", depthafops.confidence);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 0, 2, &value);

    depthafops.confidence= value;
  } else if (g_str_equal (input, DEPTH_AF_NEAR_LIMITATION_OPTION)) {
    gint64 value = depthafops.nearLimitation;

    g_print ("\nCurrent value: %d - [100 - 10000]\n", depthafops.nearLimitation);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 100, 10000, &value);

    depthafops.nearLimitation= value;
  } else if (g_str_equal (input, DEPTH_AF_FAR_LIMITATION_OPTION)) {
    gint64 value = depthafops.farLimitation;

    g_print ("\nCurrent value: %d - [100 - 10000]\n", depthafops.farLimitation);
    g_print ("\nEnter new value (or press Enter to keep current one): ");

    if (!wait_stdin_message (messages, &input))
      return FALSE;

    if (!g_str_equal (input, ""))
      extract_integer_value (input, 100, 10000, &value);

    depthafops.farLimitation= value;
  }

  g_free (input);
  input = NULL;

  return TRUE;
}


static gpointer
main_menu(gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  gboolean active = TRUE;

  while (active) {
    active = depth_ops_menu (appctx->messages);
  }

  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GstAppContext *appctx = gst_app_context_new ();

  GIOChannel *iostdin = NULL;
  GThread *mthread = NULL;
  guint intrpt_watch_id = 0, stdin_watch_id = 0;

  g_set_prgname ("gst-depth-assist-autofocus-example");

  // Initialize GST library.
  gst_init (&argc, &argv);

  {
    GError *error = NULL;

    appctx->pipeline = gst_parse_launch ("qtiqmmfsrc name=camera ! \
        video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=30/1 ! \
        queue ! appsink name=sink emit-signals=true",
        &error);

    // Check for errors on pipe creation.
    if ((NULL == appctx->pipeline) && (error != NULL)) {
      g_printerr ("Failed to create pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -1;
    } else if ((NULL == appctx->pipeline) && (NULL == error)) {
      g_printerr ("Failed to create pipeline, unknown error!\n");
      return -1;
    } else if ((appctx->pipeline != NULL) && (error != NULL)) {
      g_printerr ("Erroneous pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_object_unref (appctx->pipeline);
      return -1;
    }
  }

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_object_unref (appctx->pipeline);
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
    gst_bus_add_signal_watch (bus);

    g_signal_connect (bus, "message::state-changed",
        G_CALLBACK (state_changed_cb), appctx->pipeline);
    g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
    g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), appctx->mloop);
    g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), appctx->mloop);

    // Register function for handling interrupt signals with the main loop.
    intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx->pipeline);
    gst_object_unref (bus);

    // Create IO channel from the stdin stream.
    if ((iostdin = g_io_channel_unix_new (fileno (stdin))) == NULL) {
      g_printerr ("ERROR: Failed to initialize Main loop!\n");
      gst_app_context_free (appctx);
      return -1;
    }

    // Register handing function with the main loop for stdin channel data.
    GIOCondition flag = (GIOCondition)(G_IO_IN | G_IO_PRI);
    stdin_watch_id = g_io_add_watch (
        iostdin, flag, handle_stdin_source, appctx);
    g_io_channel_unref (iostdin);

    g_print ("\n init main menu thread appctx: %p\n", appctx);

    // Initiate the main menu thread.
    if ((mthread = g_thread_new ("MainMenu", main_menu, appctx)) == NULL) {
      g_printerr ("ERROR: Failed to create event loop thread!\n");
      g_print ("\n init main menu thread failed appctx: %p\n", appctx);
      gst_app_context_free (appctx);
      g_print ("ERROR: Failed to create event loop thread!appctx - %p\n",appctx);
      return -1;
    }
  }

  // Connect a callback to the new-sample signal.
  {
    GstElement *element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "sink");
    g_signal_connect (element, "new-sample", G_CALLBACK (new_sample), NULL);
  }

  g_print ("Setting pipeline to PAUSED state ...\n");

  switch (gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
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

  // Get instance to qmmfsrc
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "camera");

  // Get video metadata
  ::camera::CameraMetadata *meta_ptr = nullptr;
  g_object_get (G_OBJECT (qtiqmmfsrc), "video-metadata", &meta_ptr, NULL);
  if (meta_ptr) {
    g_print ("Get video-metadata entries - %ld\n", meta_ptr->entryCount());

    // Set auto focus mode metadata
    guchar afmode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
    meta_ptr->update(ANDROID_CONTROL_AF_MODE, &afmode, 1);

    appctx->qtiqmmfsrc = qtiqmmfsrc;

    // Release metadata
    delete meta_ptr;
  } else {
    g_printerr ("Get video-metadata failed\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initiate the metadata update thread.
  GThread *thread = NULL;
  thread = g_thread_new ("metadataUpdateThread",
      metadata_update_thread, appctx);
  // Run main loop.
  g_main_loop_run (appctx->mloop);
  g_print ("g_main_loop_run ends\n");

  // Signal menu thread to quit.
  g_async_queue_push (appctx->messages,
      gst_structure_new_empty (TERMINATE_MESSAGE));

  // Waits until main menu thread finishes.
  g_thread_join (mthread);

  // Set the finish flag in order to terminate the update thread
  g_mutex_lock (&appctx->update_lock);
  appctx->finish = 1;
  g_print ("g_main_loop_run STOP the thread\n");
  g_cond_signal (&appctx->update_signal);
  g_mutex_unlock (&appctx->update_lock);

  g_thread_join (thread);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  g_source_remove (stdin_watch_id);
  g_source_remove (intrpt_watch_id);

  gst_app_context_free (appctx);

  gst_deinit ();

  return 0;
}

