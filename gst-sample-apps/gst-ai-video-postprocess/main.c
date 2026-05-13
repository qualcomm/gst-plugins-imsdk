/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <glib-unix.h>
#include <stdio.h>

#include <gst_sample_apps_utils.h>

/*
 * Description of the pipeline.
 */
#define DESCRIPTION \
  "This application sets up a GStreamer pipeline for classification using a " \
  "quantized ResNet101 model. It does a preprocess, runs inference " \
  "and outputs the results in JSON format. It supports various video input sources."

/*
 * Default input branch used by the sample application.
 */
#define DEFAULT_INPUT \
  "filesrc location=/etc/media/video.mp4 ! qtdemux ! h264parse ! "\
  "v4l2h264dec capture-io-mode=4 output-io-mode=4 ! video/x-raw,format=NV12"

/**
 * Release GstSample when processing is done:
 *
 * @param sample buffer to release
 */
static void
gst_sample_release (GstSample * sample)
{
  gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
  gst_sample_set_buffer (sample, NULL);
#endif
}

/*
 * appsink "new-sample" callback.
 *
 * This handler is called by appsink whenever a decoded buffer reaches the end
 * of the pipeline. It pulls the GstSample from appsink, extracts the GstBuffer,
 * maps the buffer for CPU read access and writes the raw frame payload to a
 * file. In production code this is the place where the application would pass
 * frames to custom processing, IPC, storage, diagnostics or another subsystem.
 *
 */
static GstFlowReturn
on_new_sample (GstElement * appsink, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstMapInfo mapinfo = GST_MAP_INFO_INIT;
  size_t written = 0;
  gsize buffer_size = 0;
  GstFlowReturn ret = GST_FLOW_OK;

  /* Pull the next available sample from appsink. */
  g_signal_emit_by_name (appsink, "pull-sample", &sample, &ret);
  if (ret != GST_FLOW_OK || sample == NULL)
    return GST_FLOW_EOS;

  /* Extract the GstBuffer that contains the actual frame payload. */
  buffer = gst_sample_get_buffer (sample);
  if (buffer == NULL) {
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  /* Map the buffer so its bytes can be accessed by the application CPU code. */
  if (!gst_buffer_map (buffer, &mapinfo, GST_MAP_READ)) {
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  /* Store the mapped buffer size before writing the frame to disk. */
  buffer_size = mapinfo.size;
  
  g_print ("New sample received, saving...\n");

  /*
   * Persist the latest raw frame payload for debugging or integration tests.
   * The file is overwritten on each sample, so it always contains the most
   * recent buffer observed by appsink.
   */
  FILE *f = fopen ("/etc/media/tensor.bin", "wb");
  if (f) {
    fwrite (mapinfo.data, 1, buffer_size, f);
    fclose (f);
  }

  /* Always unmap and release the sample after the application is done reading. */
  gst_buffer_unmap (buffer, &mapinfo);
  gst_sample_release (sample);

  return GST_FLOW_OK;
}

/*
 * Application entry point.
 */
int main(int argc, char * argv[])
{
  GstAppContext appctx = { 0 };
  guint interrupt_watch_id = 0;
  GstBus *bus = NULL;
  GError *error = NULL;
  gchar *source = g_strdup (DEFAULT_INPUT);

  /* Initialize GStreamer before using any GstElement, GstBus or caps API. */
  gst_init (&argc, &argv);

  /* Command line options allow the default input branch to be replaced. */
  GOptionEntry entries[] = {
    { "source", 's', 0, G_OPTION_ARG_STRING, &source,
      "GStreamer source pipeline", NULL },
    { NULL }
  };

  GOptionContext *context =
      g_option_context_new (DESCRIPTION);
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, NULL)) {
    g_printerr ("ERROR: Failed to parse arguments\n");
    return -1;
  }

  /*
   * Compose the full pipeline string from the selected source branch and a
   * named appsink. The appsink emits signals so this application can receive
   * buffers through on_new_sample().
   */
  gchar *pipeline_str = g_strdup_printf ("%s ! %s",
    // Video source input
    source,

    // Preprocess the video for inference
    "qtimlvconverter name=preprocess ! queue ! "

    // Run inference using the resnext101 model
    "qtimltflite name=inference delegate=external "
    "external-delegate-path=libQnnTFLiteDelegate.so "
    "external-delegate-options=\"QNNExternalDelegate,backend_type=htp;\" "
    "model=/etc/models/resnext101-w8a8.tflite ! queue ! "

    // Postprocess inference results and send them to the appsink
    "qtimlpostprocess name=postprocess results=1 module=mobilenet-softmax "
    "labels=/etc/labels/imagenet.txt settings=\"{\\\"confidence\\\": 51.0}\" ! "
    "text/x-raw ! queue ! "

    // Convert GStreamer ML results to JSON format
    "qtimlmetaparser module=json ! queue ! "

    // Output appsink
    "appsink name=appsink sync=false emit-signals=true"
  );

  g_print ("Pipeline:\n%s\n\n", pipeline_str);

  /* Create the GStreamer pipeline instance from the final pipeline string. */
  appctx.pipeline = gst_parse_launch (pipeline_str, &error);
  g_free (pipeline_str);
  if (!appctx.pipeline) {
    g_printerr ("ERROR: Pipeline creation failed: %s\n",
               error ? error->message : "unknown error");
    if (error) g_error_free (error);
    return -1;
  }
  
  /* The main loop keeps the application alive while the pipeline is running. */
  appctx.mloop = g_main_loop_new (NULL, FALSE);

  /*
   * Watch the pipeline bus for EOS. The EOS callback stops the main loop, which
   * then lets the application continue into the shutdown path below.
   */
  bus = gst_pipeline_get_bus (GST_PIPELINE (appctx.pipeline));
  if (bus == NULL) {
    g_printerr ("ERROR: Failed to get pipeline bus.\n");
    return FALSE;
  }
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), appctx.mloop);
  gst_object_unref (bus);


  /* Find the named appsink and connect the frame callback. */
  GstElement *appsink =
      gst_bin_get_by_name (GST_BIN(appctx.pipeline), "appsink");
  if (!appsink) {
    g_printerr ("ERROR: appsink not found\n");
    return -1;
  }
  g_signal_connect (appsink, "new-sample", G_CALLBACK (on_new_sample), NULL);
  gst_object_unref (appsink);

  /* Install a Ctrl+C handler so the pipeline receives EOS before shutdown. */
  interrupt_watch_id = g_unix_signal_add (SIGINT,
      handle_interrupt_signal, &appctx);

  g_print ("Starting pipeline...\n");

  /* Move the pipeline to PLAYING; streaming starts after this state change. */
  gst_element_set_state (appctx.pipeline, GST_STATE_PLAYING);

  /* Block here until EOS, Ctrl+C or another callback quits the main loop. */
  g_main_loop_run (appctx.mloop);

  g_print ("Main loop stopped.\n");

  /* Remove the Unix signal source before destroying the application context. */
  g_source_remove (interrupt_watch_id);

  /* Stop the pipeline and release the pipeline object. */
  gst_element_set_state (appctx.pipeline, GST_STATE_NULL);
  gst_object_unref (appctx.pipeline);

  /* Release the GLib main loop and command line string allocation. */
  g_main_loop_unref (appctx.mloop);

  g_free (source);

  /* Deinitialize GStreamer after all GStreamer objects have been released. */
  gst_deinit ();

  return 0;
}
