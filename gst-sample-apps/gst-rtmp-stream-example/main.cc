/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Gstreamer Application:
* Gstreamer Application for Demonstrating the Real Time Messaging Protocol
*
* Description:
* This application Demonstrates RTMP usecases with below possible outputs:
*     --ISPCamera to RTMP
*     --RTSPCamera AVC to RTMP
*
* Usage:
* For RTSPCamera AVC to RTMP:
* gst-rtmp-example -u 0 -d rtmp://192.168.1.171/live/01 -r rtspNetworkUrl
* For ISPCamera to RTMP:
* gst-rtmp-example -u 1 -d rtmp://192.168.1.171/live/01
*
* Help:
* gst-rtmp-example --help
*
* *******************************************************************************
* Pipeline for RTSPCamera AVC to RTMP:
* rtspsrc->rtph264depay->h264parse->flvmux->rtmp2sink
* Pipeline For ISPCamera to RTMP:
* qtiqmmfsrc->capsfilter->v4l2h264enc->h264parse-flvmux->rtmp2sink
* *******************************************************************************
*/

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

#define GST_APP_SUMMARY                                                      \
  "This RTMP app enables the users to execute the RTMP usecases     \n"      \
  "\nFor RTSP Camera AVC to RTMP:\n"                                         \
  "gst-rtmp-example -u 0 -d rtmp://<deviceIp>/live/01 -r rtspNetworkUrl  \n" \
  "\nFor ISP camera to RTMP:\n"                                              \
  "gst-rtmp-example -u 1 -d rtmp://<deviceIp>/live/01 \n"                    \


// Enum to define the type of sink type that user can set
enum Rtmp
{
  RTSPCamera,
  ISPCamera,
};

// Structure to hold the application context
struct GstRTMPAppContext:GstAppContext
{
  gchar *deviceIp;
  gchar *rtspAddress;
  Rtmp usecase;
  gint width;
  gint height;
};

// Function to create a new application context
static GstRTMPAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstRTMPAppContext *ctx = (GstRTMPAppContext *) g_new0 (GstRTMPAppContext, 1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context");
    return NULL;
  }
  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->deviceIp = NULL;
  ctx->rtspAddress = NULL;
  ctx->usecase = ISPCamera;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;
  return ctx;
}

// Function encapsulates pipeline state transitions and provides error handling
static gboolean
update_pipeline_state (GstElement * pipeline, GstState state)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstState current, pending;

  // First check current and pending states of pipeline.
  ret = gst_element_get_state (pipeline, &current, &pending, 0);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("ERROR: Failed to retrieve pipeline state!\n");
    return TRUE;
  }

  if (state == current) {
    g_print ("Already in %s state\n", gst_element_state_get_name (state));
    return TRUE;
  } else if (state == pending) {
    g_print ("Pending %s state\n", gst_element_state_get_name (state));
    return TRUE;
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
      ret = gst_element_get_state (pipeline, NULL,NULL,
          GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("ERROR: Pipeline failed to PREROLL!\n");
        return TRUE;
      }

      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  GstState currstate;
  while (currstate != state) {
    ret = gst_element_get_state (pipeline, &currstate, NULL,
        GST_CLOCK_TIME_NONE);
  }

  return TRUE;
}

// Function to free the application context
static void
gst_app_context_free (GstRTMPAppContext * appctx)
{
  // If the plugins list is not empty, unlink and remove all elements
  if (appctx->plugins != NULL) {
    GstElement *element_curr = (GstElement *) appctx->plugins->data;
    GstElement *element_next;

    GList *list = appctx->plugins->next;
    for (; list != NULL; list = list->next) {
      element_next = (GstElement *) list->data;
      gst_element_unlink (element_curr, element_next);
      gst_bin_remove (GST_BIN (appctx->pipeline), element_curr);
      element_curr = element_next;
    }
    gst_bin_remove (GST_BIN (appctx->pipeline), element_curr);

    // Free the plugins list
    g_list_free (appctx->plugins);
    appctx->plugins = NULL;
  }
  // If specific pointer is not NULL, unref it

  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  if (appctx->deviceIp != NULL)
    g_free (appctx->deviceIp);

  if (appctx->rtspAddress != NULL)
    g_free (appctx->rtspAddress);

  if (appctx != NULL)
    g_free (appctx);
}

// Callback to link dynamic pad from rtspsrc
static void
on_pad_added (GstElement * src, GstPad * new_pad, gpointer user_data)
{
  GstElement *queue = (GstElement *) user_data;
  GstPad *sink_pad = gst_element_get_static_pad (queue, "sink");

  if (gst_pad_is_linked (sink_pad)) {
    gst_object_unref (sink_pad);
    return;
  }
  if (gst_pad_link (new_pad, sink_pad) != GST_PAD_LINK_OK) {
    g_printerr ("\n Failed to link dynamic pad.\n");
  } else {
    g_print ("\n Linked dynamic pad to queue.\n");
  }
  gst_object_unref (sink_pad);
}

// Function to create the pipeline and link all elements
static gboolean
create_pipe (GstRTMPAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *qtiqmmfsrc = NULL;
  GstElement *rtspsrc = NULL;
  GstElement *capsfilter = NULL;
  GstElement *v4l2h264enc = NULL;
  GstElement *rtph264depay = NULL;
  GstElement *h264parse = NULL;
  GstElement *flvmux = NULL;
  GstElement *rtmp2sink = NULL;
  GstElement *queue = NULL;
  appctx->plugins = NULL;
  GstCaps *filtercaps;
  gboolean ret = FALSE;

  rtmp2sink = gst_element_factory_make ("rtmp2sink", "rtmp2sink");
  h264parse = gst_element_factory_make ("h264parse", "h264parse");
  flvmux = gst_element_factory_make ("flvmux", "flvmux");
  queue = gst_element_factory_make ("queue", "queue");

  g_object_set (G_OBJECT (rtmp2sink), "sync", true, NULL);      //
  g_object_set (G_OBJECT (rtmp2sink), "location", appctx->deviceIp, NULL);

  if (!rtmp2sink || !h264parse || !flvmux || !queue) {
    g_printerr
        ("\n rtmp2sink or h264parse or flvmux or queue: element not created. Exiting.\n");
    gst_bin_remove_many (GST_BIN (appctx->pipeline), rtmp2sink, h264parse,
        flvmux, queue, NULL);
    return FALSE;
  }
  // check the sink type and create the sink elements
  if (appctx->usecase == RTSPCamera) {
    rtspsrc = gst_element_factory_make ("rtspsrc", "rtspsrc");
    rtph264depay = gst_element_factory_make ("rtph264depay", "rtph264depay");

    if (!rtspsrc || !rtph264depay) {
      g_printerr ("\n rtspsrc or rtph264depay element not created. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), rtmp2sink, h264parse,
          flvmux, queue, rtspsrc, rtph264depay, NULL);
      return FALSE;
    }

    g_object_set (G_OBJECT (rtspsrc), "location", appctx->rtspAddress, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline), rtspsrc, queue, rtph264depay,
        h264parse, flvmux, rtmp2sink, NULL);

    g_print ("\n Linking elements ..\n");

    ret = gst_element_link_many (queue, rtph264depay, h264parse, flvmux,
        rtmp2sink, NULL);

    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), queue, rtph264depay,
          h264parse, flvmux, rtmp2sink, NULL);
      return FALSE;
    }
    // Connect pad-added signal
    g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added), queue);

  } else if (appctx->usecase == ISPCamera) {

    // Create camera source element
    qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
    capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");

    // Set the source elements capability
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING,
        "NV12",
        "width", G_TYPE_INT, appctx->width,
        "height", G_TYPE_INT, appctx->height,
        "framerate", GST_TYPE_FRACTION, 30, 1, NULL);

    g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    gst_element_set_enum_property (v4l2h264enc, "capture-io-mode", "dmabuf");
    gst_element_set_enum_property (v4l2h264enc, "output-io-mode",
        "dmabuf-import");

    if (!qtiqmmfsrc || !capsfilter || !v4l2h264enc) {
      g_printerr ("\n One element could not be created. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
          v4l2h264enc, NULL);
      return FALSE;
    }

    gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
        v4l2h264enc, h264parse, flvmux, rtmp2sink, NULL);

    g_print ("\n Linking elements yuv dump..\n");

    ret = gst_element_link_many (qtiqmmfsrc, capsfilter, v4l2h264enc, h264parse,
        flvmux, rtmp2sink, NULL);
    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, capsfilter,
          v4l2h264enc, h264parse, flvmux, rtmp2sink, NULL);
      return FALSE;
    }
  }
  // Append all elements to the plugins list for clean up
  appctx->plugins = g_list_append (appctx->plugins, rtmp2sink);
  appctx->plugins = g_list_append (appctx->plugins, h264parse);
  appctx->plugins = g_list_append (appctx->plugins, flvmux);

  if (appctx->usecase == RTSPCamera) {
    appctx->plugins = g_list_append (appctx->plugins, rtspsrc);
    appctx->plugins = g_list_append (appctx->plugins, rtph264depay);
  } else if (appctx->usecase == ISPCamera) {
    appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
    appctx->plugins = g_list_append (appctx->plugins, capsfilter);
    appctx->plugins = g_list_append (appctx->plugins, v4l2h264enc);
  }

  g_print ("\n All elements are linked successfully\n");
  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GstRTMPAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // If the user only provided the application name, print the help option
  if (argc < 2) {
    g_print ("\n usage: gst-rtmp-example --help \n");
    return -1;
  }
  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }
  // Configure input parameters
  GOptionEntry entries[] = {
    {"width", 'w', 0, G_OPTION_ARG_INT, &appctx->width, "width",
        "image width"},
    {"height", 'h', 0, G_OPTION_ARG_INT, &appctx->height, "height",
        "image height"},
    {"usecase", 'u', 0, G_OPTION_ARG_INT, &appctx->usecase,
          "\t\t\t\t\t   usecase",
        "\n\t0-RTSPCamera" "\n\t1-ISPCamera"},
    {"RTSPNetworkURL", 'r', 0, G_OPTION_ARG_STRING, &appctx->rtspAddress,
        "RTSPNetworkURL ", "RTSPNetworkURL"},
    {"DeviceIp", 'd', 0, G_OPTION_ARG_STRING, &appctx->deviceIp,
        "DeviceIp", "Device IP"},
    {NULL, 0, 0, (GOptionArg) 0, NULL, NULL, NULL}
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("gst-rtmp-example")) != NULL) {
    g_option_context_set_summary (ctx, GST_APP_SUMMARY);
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("\n Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("\n Initializing: Unknown error!\n");
      return -1;
    }
  } else {
    g_printerr ("\n Failed to create options context!\n");
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  g_set_prgname ("gst-rtmp-example");

  // Create the pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    return -1;
  }

  appctx->pipeline = pipeline;

  // Build the pipeline
  ret = create_pipe (appctx);
  if (!ret) {
    g_printerr ("\n failed to create GST pipe.\n");
    gst_app_context_free (appctx);
    return -1;
  }
  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("\n Failed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("\n Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    gst_app_context_free (appctx);
    return -1;
  }
  // Watch for messages on the pipeline's bus
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("\n Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\n Failed to transition to PAUSED state!\n");
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("\n Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("\n Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("\n Pipeline state change was successful\n");
      break;
  }

  // Start the main loop
  g_print ("\n Application is running... \n");
  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  g_source_remove (intrpt_watch_id);

  // Set the pipeline to the NULL state
  g_print ("\n Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  if (update_pipeline_state (appctx->pipeline, GST_STATE_NULL))
    g_print("\nPipeline successfully transitioned to NULL state.\n");
  else
    g_printerr("\nPipeline failed to transition to NULL state");

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return 0;
}
