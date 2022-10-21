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

#include <glib-unix.h>
#include <gst/gst.h>
#include <camera/CameraMetadata.h>
#include <camera/VendorTagDescriptor.h>

#define GST_PROTECTION_META_CAST(obj) ((GstProtectionMeta *) obj)

static void
sample_unref (GstSample *sample) {
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

static GstProtectionMeta *
gst_buffer_get_protection_meta_id (GstBuffer * buffer, const gchar * name)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_PROTECTION_META_API_TYPE))) {
    if (gst_structure_has_name (GST_PROTECTION_META_CAST (meta)->info, name))
      return GST_PROTECTION_META_CAST (meta);
  }

  return NULL;
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
  ::android::sp<::android::VendorTagDescriptor> vtags;
  ::android::status_t status = 0;
  guint tag_id = 0;

  vtags = ::android::VendorTagDescriptor::getGlobalVendorTagDescriptor();
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
    sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    g_printerr ("ERROR: Failed to map the pulled buffer!");
    sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  // Extract the original camera timestamp from GstBuffer OFFSET_END field
  timestamp = GST_BUFFER_OFFSET_END (buffer);
  g_print ("Camera timestamp: %" G_GUINT64_FORMAT "\n", timestamp);

  gst_buffer_unmap (buffer, &info);
  sample_unref (sample);

  return GST_FLOW_OK;
}

static GstFlowReturn
result_metadata (gpointer userdata, guint camera_id, gpointer metadata)
{
  ::android::CameraMetadata *meta_ptr = (::android::CameraMetadata*) metadata;
  guint tag_id = 0;

  if (meta_ptr != nullptr) {
    g_print ("\nResult metadata ... entries - %ld\n", meta_ptr->entryCount());

    // Exposure time
    if (meta_ptr->exists(ANDROID_SENSOR_EXPOSURE_TIME)) {
      gint64 sensorExpTime =
          meta_ptr->find(ANDROID_SENSOR_EXPOSURE_TIME).data.i64[0];
      g_print ("Result sensor_exp_time - %ld\n", sensorExpTime);
    }

    // Sensor Timestamp
    if (meta_ptr->exists(ANDROID_SENSOR_TIMESTAMP)) {
      gint64 timestamp =
          meta_ptr->find(ANDROID_SENSOR_TIMESTAMP).data.i64[0];
      g_print ("Result timestamp - %ld\n", timestamp);
    }

    // AE mode for manual control
    if (meta_ptr->exists(ANDROID_CONTROL_AE_MODE)) {
      gint ae_mode = meta_ptr->find(ANDROID_CONTROL_AE_MODE).data.u8[0];
      g_print ("Result ae_mode - %d\n", ae_mode);
    }

    // AE Target
    if (meta_ptr->exists(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION)) {
      gint exp_compensation =
        meta_ptr->find(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION).data.i32[0];
      g_print ("Result exp_compensation - %d\n", exp_compensation);
    }

    // AE Lock
    if (meta_ptr->exists(ANDROID_CONTROL_AE_LOCK)) {
      gint exp_lock =
        meta_ptr->find(ANDROID_CONTROL_AE_LOCK).data.u8[0];
      g_print ("Result exp_lock - %d\n", exp_lock);
    }

     // sensor analog + digital gain
    if (meta_ptr->exists(ANDROID_SENSOR_SENSITIVITY)) {
      gint32 sensitivity =
        meta_ptr->find(ANDROID_SENSOR_SENSITIVITY).data.i32[0];
      g_print ("Result sensitivity - %d\n", sensitivity);
    }

     // EV mode
    if (meta_ptr->exists(ANDROID_CONTROL_AE_COMPENSATION_RANGE)) {
      gint32 min =
        meta_ptr->find(ANDROID_CONTROL_AE_COMPENSATION_RANGE).data.i32[0];
      gint32 max =
        meta_ptr->find(ANDROID_CONTROL_AE_COMPENSATION_RANGE).data.i32[1];
      g_print ("Result AE compensation range - %d - %d\n", min, max);
    }

     // EV steps
    if (meta_ptr->exists(ANDROID_CONTROL_AE_COMPENSATION_STEP)) {
      gint numerator =
        meta_ptr->find(ANDROID_CONTROL_AE_COMPENSATION_STEP).data.r[0].numerator;
      gint denominator =
        meta_ptr->find(ANDROID_CONTROL_AE_COMPENSATION_STEP).data.r[0].denominator;
      g_print ("Result AE compensation step - %d/%d\n", numerator, denominator);
    }

    // Sensor analog gain
    if (meta_ptr->exists(ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY)) {
      gint32 maxsensitivity =
        meta_ptr->find(ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY).data.i32[0];
      g_print ("Result max sensitivity - %d\n", maxsensitivity);
    }

    // Sensor Read Result
    gboolean flag = 0;
    tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.sensorreadoutput", "SensorReadResult");
    if (meta_ptr->exists(tag_id)) {
      flag = meta_ptr->find(tag_id).data.u8[0];
      g_print ("Sensor Read Result: %d\n", flag);
    }

    if (flag) {
      // Sensor Read Output
      tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.sensorreadoutput", "SensorReadOutput");
      if (meta_ptr->exists(tag_id)) {
        guint value = (meta_ptr->find(tag_id).data.u8[0]) | (meta_ptr->find(tag_id).data.u8[1] << 8);
        g_print ("Sensor Read Output: %d\n", value);
      }
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
urgent_metadata (gpointer userdata, guint camera_id, gpointer metadata)
{
  ::android::CameraMetadata *meta_ptr = (::android::CameraMetadata*) metadata;

  if (meta_ptr != nullptr) {
    g_print ("\nUrgent metadata ... entries - %ld\n", meta_ptr->entryCount());

    // AWB Mode
    if (meta_ptr->exists(ANDROID_CONTROL_AWB_MODE)) {
      gint8 AWBMode = meta_ptr->find(ANDROID_CONTROL_AWB_MODE).data.u8[0];
      g_print ("Urgent AWB mode - %ld\n", AWBMode);
    }

    // AWB State
    if (meta_ptr->exists(ANDROID_CONTROL_AWB_STATE)) {
      gint8 AWBState = meta_ptr->find(ANDROID_CONTROL_AWB_STATE).data.u8[0];
      g_print ("Urgent AWB state - %ld\n", AWBState);
    }

    // AF Mode
    if (meta_ptr->exists(ANDROID_CONTROL_AF_MODE)) {
      gint8 AFMode = meta_ptr->find(ANDROID_CONTROL_AF_MODE).data.u8[0];
      g_print ("Urgent AF mode - %ld\n", AFMode);
    }

    // AF State
    if (meta_ptr->exists(ANDROID_CONTROL_AF_STATE)) {
      gint8 AFState = meta_ptr->find(ANDROID_CONTROL_AF_STATE).data.u8[0];
      g_print ("Urgent AF state - %ld\n", AFState);
    }

    // AE Mode
    if (meta_ptr->exists(ANDROID_CONTROL_AE_MODE)) {
      gint8 AEMode = meta_ptr->find(ANDROID_CONTROL_AE_MODE).data.u8[0];
      g_print ("Urgent AE mode - %ld\n", AEMode);
    }

    // AE State
    if (meta_ptr->exists(ANDROID_CONTROL_AE_STATE)) {
      gint8 AEState = meta_ptr->find(ANDROID_CONTROL_AE_STATE).data.u8[0];
      g_print ("Urgent AE state - %ld\n", AEState);
    }
  }

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

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline = NULL;
  GMainLoop *mloop = NULL;
  guint intrpt_watch_id = 0;

  g_set_prgname ("gst-camera-metadata-example");

  // Initialize GST library.
  gst_init (&argc, &argv);

  {
    GError *error = NULL;

    pipeline = gst_parse_launch ("qtiqmmfsrc name=camera ! \
        video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=30/1 ! \
        queue ! appsink name=sink emit-signals=true",
        &error);

    // Check for errors on pipe creation.
    if ((NULL == pipeline) && (error != NULL)) {
      g_printerr ("Failed to create pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -1;
    } else if ((NULL == pipeline) && (NULL == error)) {
      g_printerr ("Failed to create pipeline, unknown error!\n");
      return -1;
    } else if ((pipeline != NULL) && (error != NULL)) {
      g_printerr ("Erroneous pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_object_unref (pipeline);
      return -1;
    }
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_object_unref (pipeline);
    return -1;
  }

  {
    GstBus *bus = NULL;

    // Retrieve reference to the pipeline's bus.
    if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
      g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");

      g_main_loop_unref (mloop);
      gst_object_unref (pipeline);

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
  }

  // Connect a callback to the new-sample signal.
  {
    GstElement *element = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
    g_signal_connect (element, "new-sample", G_CALLBACK (new_sample), NULL);
  }

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, pipeline);

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

  // Get instance to qmmfsrc
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (pipeline), "camera");
  g_signal_connect (qtiqmmfsrc, "result-metadata",
      G_CALLBACK (result_metadata), NULL);
  g_signal_connect (qtiqmmfsrc, "urgent-metadata",
      G_CALLBACK (urgent_metadata), NULL);

  // Get static metadata
  ::android::CameraMetadata *st_meta_ptr = nullptr;
  g_object_get (G_OBJECT (qtiqmmfsrc), "camera-characteristics",
      &st_meta_ptr, NULL);
  if (st_meta_ptr) {
    g_print ("Get static-metadata entries - %ld\n", st_meta_ptr->entryCount());
    delete st_meta_ptr;
  } else {
    g_printerr ("Get static-metadata failed\n");
  }

  // Get capture metadata
  ::android::CameraMetadata *meta_ptr = nullptr;
  g_object_get (G_OBJECT (qtiqmmfsrc), "capture-metadata", &meta_ptr, NULL);
  if (meta_ptr) {
    g_print ("Get capture-metadata entries - %ld\n", meta_ptr->entryCount());

    // Set capture metadata
    guchar awb = 6;
    meta_ptr->update(ANDROID_CONTROL_AWB_MODE, &awb, 1);

    // Sensor Read Input
    guchar flag = 1;
    guint tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.sensorreadinput", "SensorReadFlag");
    meta_ptr->update(tag_id, &flag, 1);

    g_object_set (G_OBJECT (qtiqmmfsrc), "capture-metadata", meta_ptr, NULL);

    // Release metadata
    delete meta_ptr;
  } else {
    g_printerr ("Get capture-metadata failed\n");
  }

  // Run main loop.
  g_main_loop_run (mloop);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_source_remove (intrpt_watch_id);

  g_main_loop_unref (mloop);
  gst_object_unref (pipeline);

  gst_deinit ();

  return 0;
}
