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
 * GStreamer raw plus yuv appsink example
 *
 * Description:
 * This app connects the camera with two appsink elements,
 * once an appsink callback is connected to the new-sample signal,
 * it saves every buffer to device storage in /data/frame_n.raw or
 * /data/frame_n.yuv, accordingly. There is also an example for
 * how to retrieve stride and offset data for yuv frames.
 *
 * Usage:
 * gst-appsink-raw-plus-yuv-example
 *
 *
 */

#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/video/video.h>

static void
sample_unref (GstSample *sample) {
    gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
    gst_sample_set_buffer (sample, NULL);
#endif
}

// YUV callback to be connected to new-sample signal
static GstFlowReturn
new_sample_yuv (GstElement * sink, gpointer userdata)
{
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstMapInfo info;
  GstVideoMeta *vmeta = NULL;
  gchar *temp_str = NULL;
  GError *error = NULL;
  static guint64 frame_counter = 0;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!");
    return GST_FLOW_ERROR;
  }

  frame_counter++;

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!");
    sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  // Example for meta retrieval in order to get offset and stride
  vmeta = gst_buffer_get_video_meta_id (buffer, 0);
  if (!vmeta) {
    g_printerr ("ERROR: FAILED TO GET BUFFER META!\n");
    sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  temp_str =
    g_strdup_printf ("/data/frame_%lu_w_%u_h_%u_stride_%zu_scanline_%zu.yuv",
      frame_counter, vmeta->width, vmeta->height, vmeta->stride[0],
      vmeta->offset[1] / vmeta->stride[0]);

  // Writing data to file
  if (gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    if (!g_file_set_contents (temp_str, (guchar *) info.data, info.size,
        &error)) {
      g_printerr ("\nERROR writing to %s: %s\n", temp_str, error->message);
    }
    g_print ("\n%s written successfully!\n", temp_str);
    gst_buffer_unmap (buffer, &info);
  } else {
    g_printerr ("ERROR: Failed to map buffer memory!");
    sample_unref(sample);
    g_free (temp_str);
    return GST_FLOW_ERROR;
  }

  sample_unref (sample);
  g_free (temp_str);
  return GST_FLOW_OK;
}

// RAW callback to be connected to new-sample signal
static GstFlowReturn
new_sample_raw (GstElement * sink, gpointer userdata)
{
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstMapInfo info;
  GstVideoMeta *vmeta = NULL;
  gchar *temp_str = NULL;
  GError *error = NULL;
  static guint64 frame_counter = 0;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!");
    return GST_FLOW_ERROR;
  }

  frame_counter++;

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!");
    sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  // Example for meta retrieval in order to get offset and stride
  vmeta = gst_buffer_get_video_meta_id (buffer, 0);
  if (!vmeta) {
    g_printerr ("ERROR: FAILED TO GET BUFFER META!\n");
    sample_unref(sample);
    return GST_FLOW_ERROR;
  }

  temp_str = g_strdup_printf ("/data/frame_%lu_w_%u_h_%u_stride_%zu.raw",
      frame_counter, vmeta->width, vmeta->height, vmeta->stride[0]);

  // Writing data to file
  if (gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    if (!g_file_set_contents (temp_str, (guchar *) info.data, info.size,
        &error)) {
      g_printerr ("\nERROR writing to %s: %s\n", temp_str, error->message);
    }
    g_print ("\n%s written successfully!\n", temp_str);
    gst_buffer_unmap (buffer, &info);
  } else {
    g_printerr ("ERROR: Failed to map buffer memory!");
    sample_unref(sample);
    g_free (temp_str);
    return GST_FLOW_ERROR;
  }

  sample_unref (sample);
  g_free (temp_str);
  return GST_FLOW_OK;
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

gint
main (gint argc, gchar * argv[])
{
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  GstCaps *filtercaps = NULL;
  GValue value = G_VALUE_INIT;
  GstElement *pipeline = NULL;
  gboolean success = FALSE;
  gint sensor_width = 0;
  gint sensor_height = 0;

  GstElement *qtiqmmfsrc, *yuv_capsfilter, *raw_capsfilter;
  GstElement *queue1, *queue2, *yuv_appsink, *raw_appsink;

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline
  pipeline = gst_pipeline_new ("appsink-raw-plus-yuv-example");

  // Create all elements
  qtiqmmfsrc      = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  yuv_capsfilter  = gst_element_factory_make ("capsfilter", "capsfilter1");
  raw_capsfilter  = gst_element_factory_make ("capsfilter", "capsfilter2");
  queue1          = gst_element_factory_make ("queue", "queue1");
  queue2          = gst_element_factory_make ("queue", "queue2");
  yuv_appsink     = gst_element_factory_make ("appsink", "yuv_appsink");
  raw_appsink     = gst_element_factory_make ("appsink", "raw_appsink");

  // Check if all elements are created successfully
  if (!pipeline || !qtiqmmfsrc || !yuv_capsfilter || !raw_capsfilter ||
      !queue1 || !queue2 || !yuv_appsink || !raw_appsink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  // Configure YUV Output stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, 1280,
      "height", G_TYPE_INT, 720,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  g_object_set (G_OBJECT (yuv_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Set qmmfsrc properties
  g_object_set (G_OBJECT (qtiqmmfsrc), "name", "camera", NULL);

  // Set yuv_appsink properties
  g_object_set (G_OBJECT (yuv_appsink), "name", "yuv_appsink", NULL);
  g_object_set (G_OBJECT (yuv_appsink), "emit-signals", 1, NULL);

  // Add YUV elements to the pipeline
  g_print ("Adding YUV elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (pipeline), qtiqmmfsrc, yuv_capsfilter, queue1,
      yuv_appsink, NULL);

  g_print ("Linking YUV elements...\n");

  // Linking the YUV stream
  success = gst_element_link_many (qtiqmmfsrc, yuv_capsfilter, queue1,
      yuv_appsink, NULL);
  if (!success) {
    g_printerr ("YUV Pipeline elements cannot be linked. Exiting.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  // Set pipeline in READY state
  switch (gst_element_set_state (pipeline, GST_STATE_READY)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to READY state!\n");
      gst_object_unref (pipeline);
      return -1;
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  // Retrieve sensor width and height form active-sensor-size property
  g_value_init (&value, GST_TYPE_ARRAY);

  g_object_get_property (G_OBJECT (qtiqmmfsrc), "active-sensor-size", &value);

  if (4 != gst_value_array_get_size (&value)) {
    g_printerr ("ERROR: Expected 4 values for active sensor size, Recieved %d",
        gst_value_array_get_size (&value));
    gst_object_unref (pipeline);
    return -1;
  }

  sensor_width  = g_value_get_int (gst_value_array_get_value (&value, 2));
  sensor_height = g_value_get_int (gst_value_array_get_value (&value, 3));

  g_value_unset (&value);

  // Configure RAW Output stream caps
  filtercaps = gst_caps_new_simple ("video/x-bayer",
      "format", G_TYPE_STRING, "rggb",
      "bpp", G_TYPE_STRING, "10",
      "width", G_TYPE_INT, sensor_width,
      "height", G_TYPE_INT, sensor_height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  g_object_set (G_OBJECT (raw_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Set raw_appsink properties
  g_object_set (G_OBJECT (raw_appsink), "name", "raw_appsink", NULL);
  g_object_set (G_OBJECT (raw_appsink), "emit-signals", 1, NULL);

  // Add RAW elements to the pipeline
  g_print ("Adding RAW elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (pipeline), raw_capsfilter, queue2, raw_appsink,
      NULL);

  g_print ("Linking RAW elements...\n");

  // Linking the RAW stream
  success = gst_element_link_many (qtiqmmfsrc, raw_capsfilter, queue2,
      raw_appsink, NULL);
  if (!success) {
    g_printerr ("RAW Pipeline elements cannot be linked. Exiting.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  g_print ("All elements are linked successfully\n");

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_object_unref (pipeline);
    return -1;
  }

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

  // Connect a YUV callback to the new-sample signal.
  {
    GstElement *element = gst_bin_get_by_name (
        GST_BIN (pipeline), "yuv_appsink");
    g_signal_connect (element, "new-sample",
        G_CALLBACK (new_sample_yuv), NULL);
  }

  // Connect a RAW callback to the new-sample signal.
  {
    GstElement *element = gst_bin_get_by_name (
        GST_BIN (pipeline), "raw_appsink");
    g_signal_connect (element, "new-sample",
        G_CALLBACK (new_sample_raw), NULL);
  }

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
