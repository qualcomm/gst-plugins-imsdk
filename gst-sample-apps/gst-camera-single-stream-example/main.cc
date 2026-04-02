/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application for single Camera usecases with different possible outputs
 *
 * Description:
 * This application Demonstrates single camera usecases with below possible outputs:
 *     --Live Camera Preview on Display
 *     --Store the Video encoder output of user choice
 *     --Dump the Camera YUV to a filesink of user choice
 *     --Live RTSP streaming
 *
 * Usage:
 * Live Camera Preview on Display:
 * gst-camera-single-stream-example -o 0 --width=1920 --height=1080
 * For Encoder dump on device:
 * gst-camera-single-stream-example -o 1 --width=1920 --height=1080
 * For YUV dump on device:
 * gst-camera-single-stream-example -o 2 --width=1920 --height=1080
 * For RTSP STREAMING on device:
 * gst-camera-single-stream-example -o 3 --width=1920 --height=1080 -i <ip> -p <port>
 *
 * Help:
 * gst-camera-single-stream-example --help
 *
 * *******************************************************************************
 * Dump the Camera YUV to a filesink:
 *     camera_source_bin -> capsfilter -> filesink
 * Live Camera Preview on Display:
 *     camera_source_bin -> capsfilter -> waylandsink
 * Pipeline For the Video Encoding:
 *     camera_source_bin -> capsfilter -> v4l2h264enc -> h264parse -> mp4mux -> filesink
 * Pipeline For the RTSPSTREAMING:
 *     camera_source_bin -> capsfilter -> v4l2h264enc -> h264parse -> rtph264pay -> udpsink
 * *******************************************************************************
 */

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>

#include "gst_sample_apps_utils.h"

#define DEFAULT_OP_YUV_FILENAME "/etc/media/yuv_dump%d.yuv"
#define DEFAULT_OP_MP4_FILENAME "/etc/media/video.mp4"
#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 8554

#define GST_APP_SUMMARY "This app enables the users to use single camera with" \
  " different outputs such as preview,encode,YUV Dump and RTSP streaming \n" \
  "\nCommand:\n" \
  "For Preview on Display:\n" \
  "  gst-camera-single-stream-example -o 0 -w 1920 -h 1080 \n" \
  "For Video Encoding:\n" \
  "  gst-camera-single-stream-example -o 1 -w 1920 -h 1080 \n" \
  "For YUV dump:\n" \
  "  gst-camera-single-stream-example -o 2 -w 1920 -h 1080 \n" \
  "For RTSP Streaming:(run the rtsp server or follow the docs steps ) \n" \
  "  gst-camera-single-stream-example -o 3 -w 1280 -h 720 \n" \
  "  Run below command on a separate shell to start the rtsp server:\n" \
  "  gst-rtsp-server -p 8900 -a <device_ip> -m /live \"( udpsrc name=pay0" \
  "port=<port> caps=\\\"application/x-rtp,media=video,clock-rate=90000," \
  "encoding-name=H264,payload=96\\\" )\"\n" \
  "\nOutput:\n" \
  "  Upon execution, application will generates output as user selected. \n" \
  "  In case Video Encoding the output video stored at /etc/media/video.mp4 \n" \
  "  In case YUV dump the output video stored at /etc/media/yuv_dump%d.yuv"

/* Structure to hold the application context */
struct GstCameraAppContext : GstAppContext {
  gchar *output_file;
  gchar *ip_address;
  GstSinkType sinktype;
  gint width;
  gint height;
  gint port_num;
};

/**
 * Create and initialize application context
 */
static GstCameraAppContext *
gst_app_context_new ()
{
  GstCameraAppContext *ctx =
      (GstCameraAppContext *) g_new0 (GstCameraAppContext, 1);

  if (ctx == NULL) {
    g_printerr ("\nUnable to create App Context\n");
    return NULL;
  }

  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->plugins = NULL;
  ctx->output_file = NULL;
  ctx->ip_address = const_cast<gchar *> (DEFAULT_IP);
  ctx->port_num = DEFAULT_PORT;
  ctx->sinktype = GST_WAYLANDSINK;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;

  return ctx;
}

/**
 * Free Application context
 */
static void
gst_app_context_free (GstCameraAppContext * appctx)
{
  if (appctx->plugins != NULL) {
    GstElement *element_curr = (GstElement *) appctx->plugins->data;
    GstElement *element_next = NULL;

    GList *list = appctx->plugins->next;
    for (; list != NULL; list = list->next) {
      element_next = (GstElement *) list->data;
      gst_element_unlink (element_curr, element_next);
      gst_bin_remove (GST_BIN (appctx->pipeline), element_curr);
      element_curr = element_next;
    }
    gst_bin_remove (GST_BIN (appctx->pipeline), element_curr);

    g_list_free (appctx->plugins);
    appctx->plugins = NULL;
  }

  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  if (appctx != NULL)
    g_free (appctx);
}

/**
 * Create GST pipeline
 *
 * Source is abstracted through utils:
 *   create_camera_source_bin()
 *
 * Internally that bin chooses:
 *   - qtiqmmfsrc
 *   - or libcamerasrc -> qtivtransform
 */
static gboolean
create_pipe (GstCameraAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *camera_src_bin = NULL;
  GstElement *capsfilter = NULL;
  GstElement *waylandsink = NULL;
  GstElement *filesink = NULL;
  GstElement *v4l2h264enc = NULL;
  GstElement *h264parse = NULL;
  GstElement *mp4mux = NULL;
  GstElement *rtph264pay = NULL;
  GstElement *udpsink = NULL;
  GstCaps *filtercaps = NULL;
  GstStructure *fcontrols = NULL;
  gboolean ret = FALSE;

  appctx->plugins = NULL;

  // Create camera source element
  camera_src_bin =
        create_camera_source_bin ("camera_source_bin");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  if (!camera_src_bin || !capsfilter) {
    g_printerr (
        "\nCamera source bin or capsfilter could not be created. Exiting.\n");
    return FALSE;
  }

  // Set the source elements capability
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      "interlace-mode", G_TYPE_STRING, "progressive",
      "colorimetry", G_TYPE_STRING, "bt601",
      NULL);

  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // check the sink type and create the sink elements
  if (appctx->sinktype == GST_WAYLANDSINK) {
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    if (!waylandsink) {
      g_printerr ("\nwaylandsink element not created. Exiting.\n");
      return FALSE;
    }

    g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline),
        camera_src_bin, capsfilter, waylandsink, NULL);

    g_print ("\nLink pipeline for display elements ..\n");

    ret = gst_element_link_many (camera_src_bin, capsfilter, waylandsink, NULL);
    if (!ret) {
      g_printerr ("\nDisplay pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline),
          camera_src_bin, capsfilter, waylandsink, NULL);
      return FALSE;
    }

  } else if (appctx->sinktype == GST_YUV_DUMP) {
    appctx->output_file = const_cast<gchar *> (DEFAULT_OP_YUV_FILENAME);

    filesink = gst_element_factory_make ("multifilesink", "filesink");
    if (!filesink) {
      g_printerr ("\nYUV dump sink could not be created. Exiting.\n");
      return FALSE;
    }

    g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);
    g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);
    g_object_set (G_OBJECT (filesink), "max-files", 2, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline),
        camera_src_bin, capsfilter, filesink, NULL);

    g_print ("\nLink pipeline elements for yuv dump..\n");

    ret = gst_element_link_many (camera_src_bin, capsfilter, filesink, NULL);
    if (!ret) {
      g_printerr ("\nPipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline),
          camera_src_bin, capsfilter, filesink, NULL);
      return FALSE;
    }

  } else if (appctx->sinktype == GST_VIDEO_ENCODE ||
      appctx->sinktype == GST_RTSP_STREAMING) {
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    h264parse = gst_element_factory_make ("h264parse", "h264parse");

    if (!v4l2h264enc || !h264parse) {
      g_printerr ("\nEncoder elements could not be created. Exiting.\n");
      return FALSE;
    }

      g_object_set (G_OBJECT (v4l2h264enc),
          "capture-io-mode", GST_V4L2_IO_DMABUF, NULL);
      g_object_set (G_OBJECT (v4l2h264enc),
          "output-io-mode", GST_V4L2_IO_DMABUF_IMPORT, NULL);
      g_object_set (G_OBJECT (h264parse), "config-interval", -1, NULL);

    if (appctx->sinktype == GST_RTSP_STREAMING) {
      fcontrols = gst_structure_from_string (
          "fcontrols,video_bitrate=6000000,video_bitrate_mode=0", NULL);
      g_object_set (G_OBJECT (v4l2h264enc), "extra-controls", fcontrols, NULL);

      rtph264pay = gst_element_factory_make ("rtph264pay", "rtph264pay");
      udpsink = gst_element_factory_make ("udpsink", "udpsink");

      if (!rtph264pay || !udpsink) {
        g_printerr (
            "\nRTSP streaming elements could not be created. Exiting.\n");
        return FALSE;
      }

      g_object_set (G_OBJECT (rtph264pay), "pt", 96, NULL);
      g_object_set (G_OBJECT (udpsink), "host", appctx->ip_address, NULL);
      g_object_set (G_OBJECT (udpsink), "port", appctx->port_num, NULL);

      gst_bin_add_many (GST_BIN (appctx->pipeline),
          camera_src_bin, capsfilter, v4l2h264enc, h264parse,
          rtph264pay, udpsink, NULL);

      g_print ("\nLink pipeline for video streaming elements ..\n");

      ret = gst_element_link_many (camera_src_bin, capsfilter, v4l2h264enc,
          h264parse, rtph264pay, udpsink, NULL);
      if (!ret) {
        g_printerr (
            "\nPipeline video streaming elements cannot be linked. Exiting.\n");
        gst_bin_remove_many (GST_BIN (appctx->pipeline),
            camera_src_bin, capsfilter, v4l2h264enc, h264parse,
            rtph264pay, udpsink, NULL);
        return FALSE;
      }

    } else if (appctx->sinktype == GST_VIDEO_ENCODE) {
      fcontrols = gst_structure_from_string (
          "fcontrols,video_bitrate_mode=0", NULL);
      g_object_set (G_OBJECT (v4l2h264enc), "extra-controls", fcontrols, NULL);

      mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
      filesink = gst_element_factory_make ("filesink", "filesink");

      if (!mp4mux || !filesink) {
        g_printerr ("\nVideo encoder sink elements could not be created.\n");
        return FALSE;
      }

      appctx->output_file = const_cast<gchar *> (DEFAULT_OP_MP4_FILENAME);
      g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);

      gst_bin_add_many (GST_BIN (appctx->pipeline),
          camera_src_bin, capsfilter, v4l2h264enc, h264parse,
          mp4mux, filesink, NULL);

      g_print ("\nLink pipeline elements for encoder..\n");

      // Linking the encoder stream
      ret = gst_element_link_many (camera_src_bin, capsfilter, v4l2h264enc,
          h264parse, mp4mux, filesink, NULL);
      if (!ret) {
        g_printerr (
            "\nVideo Encoder Pipeline elements cannot be linked. Exiting.\n");
        gst_bin_remove_many (GST_BIN (appctx->pipeline),
            camera_src_bin, capsfilter, v4l2h264enc, h264parse,
            mp4mux, filesink, NULL);
        return FALSE;
      }
    }
  }

  // Append all elements to the plugins list for clean up
  appctx->plugins = g_list_append (appctx->plugins, camera_src_bin);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter);
  if (appctx->sinktype == GST_WAYLANDSINK) {
    appctx->plugins = g_list_append (appctx->plugins, waylandsink);
  } else if (appctx->sinktype == GST_YUV_DUMP) {
    appctx->plugins = g_list_append (appctx->plugins, filesink);
  } else if (appctx->sinktype == GST_VIDEO_ENCODE ||
      appctx->sinktype == GST_RTSP_STREAMING) {
    appctx->plugins = g_list_append (appctx->plugins, v4l2h264enc);
    appctx->plugins = g_list_append (appctx->plugins, h264parse);

    if (appctx->sinktype == GST_VIDEO_ENCODE) {
      appctx->plugins = g_list_append (appctx->plugins, mp4mux);
      appctx->plugins = g_list_append (appctx->plugins, filesink);
    } else if (appctx->sinktype == GST_RTSP_STREAMING) {
      appctx->plugins = g_list_append (appctx->plugins, rtph264pay);
      appctx->plugins = g_list_append (appctx->plugins, udpsink);
    }
  }

  g_print ("\nAll elements are linked successfully\n");
  return TRUE;
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  GstCameraAppContext *appctx = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\nFailed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
      "width", "camera width" },
    { "height", 'h', 0, G_OPTION_ARG_INT, &appctx->height,
      "height", "camera height" },
    { "output", 'o', 0, G_OPTION_ARG_INT, &appctx->sinktype,
      "Sinktype",
      "\n\t0-WAYLANDSINK"
      "\n\t1-VIDEOENCODING"
      "\n\t2-YUVDUMP"
      "\n\t3-RTSPSTREAMING" },
    { "ip", 'i', 0, G_OPTION_ARG_STRING, &appctx->ip_address,
      "RSTP server listening address.", "Valid IP Address" },
    { "port", 'p', 0, G_OPTION_ARG_INT, &appctx->port_num,
      "RSTP server listening port", "Port number." },
    { NULL, 0, 0, (GOptionArg) 0, NULL, NULL, NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new (GST_APP_SUMMARY)) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("\nFailed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (appctx);
      return -1;
    } else if (!success && (error == NULL)) {
      g_printerr ("\nInitializing: Unknown error!\n");
      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("\nFailed to create options context!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  g_set_prgname ("gst-camera-single-stream-example");
  g_print ("starting gst-camera-single-stream-example\n");

  if (appctx->sinktype < GST_WAYLANDSINK ||
      appctx->sinktype > GST_RTSP_STREAMING) {
    g_printerr ("\nInvalid user Input: gst-camera-single-stream-example --help\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Create the pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("\nfailed to create pipeline.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  appctx->pipeline = pipeline;

  // Build the pipeline
  ret = create_pipe (appctx);
  if (!ret) {
    g_printerr ("\nfailed to create GST pipe.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize main loop.
  mloop = g_main_loop_new (NULL, FALSE);
  if (mloop == NULL) {
    g_printerr ("\nFailed to create Main loop!\n");
    gst_app_context_free (appctx);
    return -1;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  if (bus == NULL) {
    g_printerr ("\nFailed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning",
      G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error",
      G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos",
      G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Set the pipeline to the PAUSED state, On successful transition
  // move application state to PLAYING state in state_changed_cb function
  g_print ("\nSetting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("\nFailed to transition to PAUSED state!\n");
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      gst_app_context_free (appctx);
      return -1;

    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("\nPipeline is live and does not need PREROLL.\n");
      break;

    case GST_STATE_CHANGE_ASYNC:
      g_print ("\nPipeline is PREROLLING ...\n");
      break;

    case GST_STATE_CHANGE_SUCCESS:
      g_print ("\nPipeline state change was successful\n");
      break;

  }

  // Start the main loop
  g_print ("\nApplication is running...\n");
  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // Set the pipeline to the NULL state
  g_print ("\nSetting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
  if (appctx->output_file)
    g_print ("\nVideo file will be stored at %s\n", appctx->output_file);

  // Free the application context
  g_print ("\nFree the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("\ngst_deinit\n");
  gst_deinit ();

  return 0;
}
