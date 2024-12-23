/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Gstreamer Application:
 * Gstreamer Application for single USB Camera usecases with different possible
 * outputs
 *
 * Description:
 * This application Demonstrates single USB camera usecases with below possible
 * outputs:
 *     --Live Camera Preview on Display
 *     --Store the Video encoder output
 *     --Dump the Camera YUV to a file
 *     --Live RTSP streaming
 *
 * Usage:
 * Live Camera Preview on Display:
 * gst-usb-single-camera-app  -o 0 --width=640 --height=480 -f 30
 * For Encoder dump on device:
 * gst-usb-single-camera-app  -o 1 --width=640 --height=480 -f 30
 * For YUV dump on device:
 * gst-usb-single-camera-app -o 2 --width=640 --height=480 -f 30
 * For RTSP STREAMING on device:
 * gst-usb-single-camera-app -o 3 -w 640 -h 480 -f 30 -i <ip> -p <port>
 *
 * Help:
 * gst-usb-single-camera-app --help
 *
 * *******************************************************************************
 * Dump the Camera YUV to a filesink:
 *     camerasrc->qtivtransform->capsfilter->filesink
 * Live Camera Preview on Display:
 *     camerasrc->qtivtransform->capsfilter->waylandsink
 * Pipeline For the Video Encoding:
 * camerasrc->qtivtransform->capsfilter->v4l2h264enc->h264parse->mp4mux->filesink
 * Pipeline For the RTSPSTREAMING:
 * camerasrc->qtivtransform->capsfilter->v4l2h264enc->h264parse->qtirtspbin
 * *******************************************************************************
 */

#include <glib-unix.h>
#include <stdio.h>

#include <gst/gst.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <gst/sampleapps/gst_sample_apps_utils.h>

#define DEFAULT_OP_YUV_FILENAME "/opt/yuv_dump%d.yuv"
#define DEFAULT_OP_MP4_FILENAME "/opt/video.mp4"
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DEFAULT_FRAMERATE 30
#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT "8900"
#define DEFAULT_PROP_MPOINT "/live"
#define MAX_VID_DEV_CNT 64

#define GST_APP_SUMMARY                                                       \
  "This app enables the users to use single USB camera"                       \
  "  with different o/p such as preview,encode,YUV Dump & RTSP streaming \n"  \
  "\nCommand:\n"                                                              \
  "For Preview on Display:\n"                                                 \
  "  gst-usb-single-camera-app -o 0 -w 640 -h 480 -f 30\n"                    \
  "For Video Encoding:\n"                                                     \
  "  gst-usb-single-camera-app -o 1 -w 640 -h 480 -f 30\n"                    \
  "For YUV dump:\n"                                                           \
  "  gst-usb-single-camera-app -o 2 -w 640 -h 480 -f 30"                      \
  "\nFor RTSP Streaming: \n"   \
  "  gst-usb-single-camera-app -o 3 -w 640 -h 480 -f 30 -i <dut_ip> -p <port>" \
  "  \n Connect VLC to stream: 'rtsp://<dut_ip>:<port>/live' \n"              \
  "\nOutput:\n"                                                               \
  "  Upon execution, application will generates output as user selected. \n"  \
  "  In case of a preview, the output video will be displayed. \n"            \
  "  In case Video Encoding the output video stored at /opt/video.mp4 \n"     \
  "  In case Streaming the o/p video stream is generated to play on host.\n"  \
  "  In case YUV dump the output video stored at /opt/yuv_dump%d.yuv"

// Structure to hold the application context
struct GstCameraAppCtx {
  GstElement *pipeline;
  GMainLoop *mloop;
  gchar *output_file;
  gchar *ip_address;
  gchar *port_num;
  gchar dev_video[16];
  enum GstSinkType sinktype;
  gint width;
  gint height;
  gint framerate;
};
typedef struct GstCameraAppCtx GstCameraAppContext;

/**
 * Create and initialize application context:
 *
 * @param NULL
 */
static GstCameraAppContext *
gst_app_context_new ()
{
  // Allocate memory for the new context
  GstCameraAppContext *ctx = (GstCameraAppContext *) g_new0 (GstCameraAppContext,
      1);

  // If memory allocation failed, print an error message and return NULL
  if (NULL == ctx) {
    g_printerr ("\n Unable to create App Context");
    return NULL;
  }

  // Initialize the context fields
  ctx->pipeline = NULL;
  ctx->mloop = NULL;
  ctx->output_file = NULL;
  ctx->ip_address = NULL;
  ctx->port_num = NULL;
  ctx->sinktype = GST_WAYLANDSINK;
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;
  ctx->framerate = DEFAULT_FRAMERATE;
  return ctx;
}

/**
 * Free Application context:
 *
 * @param appctx Application Context object
 */
static void
gst_app_context_free (GstCameraAppContext * appctx)
{
  // If specific pointer is not NULL, unref it

  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  g_free (appctx->ip_address);
  g_free (appctx->port_num);

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  if (appctx != NULL)
    g_free (appctx);
}

/**
 * Find USB camera node:
 *
 * @param appctx Application Context object
 */
static gboolean
find_usb_camera_node (GstCameraAppContext * appctx)
{
  struct v4l2_capability v2cap;
  gint idx = 0, ret = 0, mFd = -1;

  while (idx < MAX_VID_DEV_CNT) {
    memset (appctx->dev_video, 0, sizeof (appctx->dev_video));

    ret = snprintf (appctx->dev_video, sizeof (appctx->dev_video), "/dev/video%d",
        idx);
    if (ret <= 0) {
      return FALSE;
    }

    g_print ("open USB camera device: %s\n", appctx->dev_video);
    mFd = open (appctx->dev_video, O_RDWR);
    if (mFd < 0) {
      mFd = -1;
      g_printerr ("Failed to open USB camera device: %s (%s)\n", appctx->dev_video,
          strerror (errno));
      idx++;
      continue;
    }

    if (ioctl (mFd, VIDIOC_QUERYCAP, &v2cap) == 0) {
      g_print ("ID_V4L_CAPABILITIES=: %s", v2cap.driver);
      if (strcmp ((const char *)v2cap.driver, "uvcvideo") != 0) {
        idx++;
        close (mFd);
        continue;
      }
    } else {
      g_printerr ("Failed to QUERYCAP device: %s (%s)\n", appctx->dev_video,
          strerror (errno));
      idx++;
      close (mFd);
      continue;
    }
    break;
  }

  if (idx >= MAX_VID_DEV_CNT || mFd < 0 || ret < 0) {
    g_printerr ("Failed to open video device");
    close (mFd);
    return FALSE;
  }

  close (mFd);
  g_print ("open %s successful \n", appctx->dev_video);
  return TRUE;
}

/**
 * Create GST pipeline involves 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Object.
 */
static gboolean
create_pipe (GstCameraAppContext * appctx)
{
  // Declare the elements of the pipeline
  GstElement *camerasrc, *capsfilter, *waylandsink, *filesink, *v4l2h264enc;
  GstElement *qtivtransform, *h264parse, *mp4mux, *queue, *qtirtspbin;
  GstCaps *filtercaps;
  GstStructure *fcontrols;
  gboolean ret = FALSE;

  // Create comman element
  camerasrc = gst_element_factory_make ("v4l2src", "camerasrc");
  qtivtransform = gst_element_factory_make ("qtivtransform", "qtivtransform");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  if (!camerasrc || !capsfilter || !qtivtransform) {
    g_printerr ("\n YUV dump elements could not be created. Exiting.\n");
    return FALSE;
  }

  // Set properties for element
  g_object_set (G_OBJECT (camerasrc), "io-mode", "dmabuf-import", NULL);
  g_object_set (G_OBJECT (camerasrc), "device", appctx->dev_video, NULL);
  g_object_set (G_OBJECT (qtivtransform), "rotate", 0, NULL);

  // Set the source elements capability and in case YUV dump disable UBWC
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, appctx->framerate, 1,
      NULL);

  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // check the sink type and create the sink elements
  if (appctx->sinktype == GST_WAYLANDSINK) {
    waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
    if (!waylandsink) {
      g_printerr ("\n waylandsink element not created. Exiting.\n");
      return FALSE;
    }
    g_object_set (G_OBJECT (waylandsink), "fullscreen", TRUE, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline), camerasrc, qtivtransform,
        capsfilter, waylandsink, NULL);

    g_print ("\n Link pipeline for display elements ..\n");

    ret = gst_element_link_many (camerasrc, qtivtransform, capsfilter, waylandsink,
        NULL);
    if (!ret) {
      g_printerr ("\n Display Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), camerasrc, capsfilter,
          qtivtransform, waylandsink, NULL);
      return FALSE;
    }
  } else if (appctx->sinktype == GST_YUV_DUMP) {
    // set the output file location for filesink element
    appctx->output_file = DEFAULT_OP_YUV_FILENAME;
    filesink = gst_element_factory_make ("multifilesink", "filesink");
    if (!filesink) {
      g_printerr ("\n YUV dump elements could not be created. Exiting.\n");
      return FALSE;
    }
    g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);
    g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);
    g_object_set (G_OBJECT (filesink), "max-files", 2, NULL);

    gst_bin_add_many (GST_BIN (appctx->pipeline), camerasrc, qtivtransform,
        capsfilter, filesink, NULL);

    g_print ("\n Link pipeline elements for yuv dump..\n");

    ret = gst_element_link_many (camerasrc, qtivtransform, capsfilter, filesink,
        NULL);
    if (!ret) {
      g_printerr ("\n Pipeline elements cannot be linked. Exiting.\n");
      gst_bin_remove_many (GST_BIN (appctx->pipeline), camerasrc, capsfilter,
          qtivtransform, filesink, NULL);
      return FALSE;
    }
  } else if (appctx->sinktype == GST_VIDEO_ENCODE ||
      appctx->sinktype == GST_RTSP_STREAMING) {
    // Create v4l2h264enc element and set the properties
    v4l2h264enc = gst_element_factory_make ("v4l2h264enc", "v4l2h264enc");
    // Create h264parse element for parsing the stream
    h264parse = gst_element_factory_make ("h264parse", "h264parse");
    if (!v4l2h264enc || !h264parse) {
      g_printerr ("\n Video Encoder elements could not be created \n");
      return FALSE;
    }
    g_object_set (G_OBJECT (v4l2h264enc), "capture-io-mode", 5, NULL);
    g_object_set (G_OBJECT (v4l2h264enc), "output-io-mode", 5, NULL);
    g_object_set (G_OBJECT (h264parse), "config-interval", -1, NULL);

    if (appctx->sinktype == GST_RTSP_STREAMING) {
      // Set bitrate for streaming usecase
      fcontrols = gst_structure_from_string (
          "fcontrols,video_bitrate=10000000,video_bitrate_mode=0", NULL);
      g_object_set (G_OBJECT (v4l2h264enc), "extra-controls", fcontrols, NULL);

      queue = gst_element_factory_make ("queue", "queue");
      qtirtspbin = gst_element_factory_make ("qtirtspbin", "qtirtspbin");
      if (!queue || !qtirtspbin) {
        g_printerr ("\n Video Streaming elements could not be created \n");
        return FALSE;
      }
      g_object_set (G_OBJECT (qtirtspbin), "address", appctx->ip_address, NULL);
      g_object_set (G_OBJECT (qtirtspbin), "port", appctx->port_num, NULL);

      gst_bin_add_many (GST_BIN (appctx->pipeline), camerasrc, qtivtransform,
          capsfilter, v4l2h264enc, h264parse, queue, qtirtspbin, NULL);

      g_print ("\n Link pipeline for video streaming elements ..\n");

      ret = gst_element_link_many (camerasrc, qtivtransform, capsfilter,
          v4l2h264enc, h264parse, queue, qtirtspbin, NULL);
      if (!ret) {
        g_printerr (
            "\n Pipeline video streaming elements cannot be linked. Exiting.\n");
        gst_bin_remove_many (GST_BIN (appctx->pipeline), camerasrc, capsfilter,
            v4l2h264enc, qtivtransform, h264parse, queue, qtirtspbin, NULL);
        return FALSE;
      }
    } else if (appctx->sinktype == GST_VIDEO_ENCODE) {
      fcontrols = gst_structure_from_string ("fcontrols,video_bitrate_mode=0",
          NULL);
      g_object_set (G_OBJECT (v4l2h264enc), "extra-controls", fcontrols, NULL);
      // Create mp4mux element for muxing the stream
      mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");

      // Create filesink element for storing the encoding stream
      filesink = gst_element_factory_make ("filesink", "filesink");
      if (!mp4mux || !filesink) {
        g_printerr ("\n Elements for video storage could not be created \n");
        return FALSE;
      }
      appctx->output_file = DEFAULT_OP_MP4_FILENAME;
      g_object_set (G_OBJECT (filesink), "location", appctx->output_file, NULL);

      gst_bin_add_many (GST_BIN (appctx->pipeline), camerasrc, qtivtransform,
          capsfilter, v4l2h264enc, h264parse, mp4mux, filesink, NULL);

      g_print ("\n Link pipeline elements for encoder..\n");

      // Linking the encoder stream
      ret = gst_element_link_many (camerasrc, qtivtransform, capsfilter,
          v4l2h264enc, h264parse, mp4mux, filesink, NULL);
      if (!ret) {
        g_printerr (
            "\n Video Encoder Pipeline elements cannot be linked. Exiting.\n");
        gst_bin_remove_many (GST_BIN (appctx->pipeline), camerasrc, capsfilter,
            qtivtransform, v4l2h264enc, h264parse, mp4mux, filesink, NULL);
        return FALSE;
      }
    }
  }

  g_print ("\n All elements are linked successfully\n");
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
  gchar *ip_address = NULL;
  gchar *port_num = NULL;
  gboolean ret = FALSE;
  guint intrpt_watch_id = 0;

  // Setting Display environment variables
  setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", 0);
  setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

  // create the application context
  appctx = gst_app_context_new ();
  if (appctx == NULL) {
    g_printerr ("\n Failed app context Initializing: Unknown error!\n");
    return -1;
  }

  // Configure input parameters
  GOptionEntry entries[] = {
    { "width", 'w', 0, G_OPTION_ARG_INT, &appctx->width,
      "width", "camera width" },
    { "height", 'h', 0, G_OPTION_ARG_INT, &appctx->height, "height",
      "camera height" },
    { "framerate", 'f', 0, G_OPTION_ARG_INT, &appctx->framerate, "framerate",
      "camera framerate" },
    { "output", 'o', 0, G_OPTION_ARG_INT, &appctx->sinktype,
      "Sinktype",
      "\n\t0-PREVIEW"
      "\n\t1-VIDEOENCODING"
      "\n\t2-YUVDUMP"
      "\n\t3-RTSPSTREAMING" },
    { "ip", 'i', 0, G_OPTION_ARG_STRING,
      &ip_address,
      "RSTP server listening address.", "Valid IP Address" },
    { "port", 'p', 0, G_OPTION_ARG_STRING,
      &port_num,
      "RSTP server listening port", "Port number." },
    { NULL, 0, 0, (GOptionArg)0, NULL, NULL, NULL }
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
      g_printerr ("\n Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_app_context_free (appctx);
      return -1;
    } else if (!success && (NULL == error)) {
      g_printerr ("\n Initializing: Unknown error!\n");
      gst_app_context_free (appctx);
      return -1;
    }
  } else {
    g_printerr ("\n Failed to create options context!\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  if (appctx->sinktype < GST_WAYLANDSINK ||
      appctx->sinktype > GST_RTSP_STREAMING) {
    g_printerr ("\n Invalid user Input:gst-usb-single-camera-app --help \n");
    gst_app_context_free (appctx);
    return -1;
  }

  //check for ip and port number
  if (appctx->sinktype == GST_RTSP_STREAMING) {
    if (ip_address == NULL)
      appctx->ip_address = g_strdup (DEFAULT_IP);
    else
      appctx->ip_address = ip_address;

    if (port_num == NULL)
      appctx->port_num = g_strdup (DEFAULT_PORT);
    else
      appctx->port_num = port_num;
  }

  // Create the pipeline
  pipeline = gst_pipeline_new ("pipeline");
  if (!pipeline) {
    g_printerr ("\n failed to create pipeline.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  appctx->pipeline = pipeline;
  // Check for avaiable USB camera
  ret = find_usb_camera_node (appctx);
  if (!ret) {
    g_printerr ("\n Failed to find the USB camera.\n");
    gst_app_context_free (appctx);
    return -1;
  }

  // Build the pipeline
  ret = create_pipe (appctx);
  if (!ret) {
    g_printerr ("\n Failed to create GST pipe.\n");
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
    gst_app_context_free (appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (state_changed_cb),
      pipeline);
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
      if (intrpt_watch_id)
        g_source_remove (intrpt_watch_id);
      gst_app_context_free (appctx);
      return -1;
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

  if (appctx->sinktype == GST_RTSP_STREAMING)
    g_print ("\n Stream ready at rtsp://%s:%s%s \n",
        appctx->ip_address, appctx->port_num, DEFAULT_PROP_MPOINT);

  g_main_loop_run (mloop);

  // Remove the interrupt signal handler
  if (intrpt_watch_id)
    g_source_remove (intrpt_watch_id);

  // Set the pipeline to the NULL state
  g_print ("\n Setting pipeline to NULL state ...\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
  if (appctx->output_file)
    g_print ("\n Video file will be stored at %s\n", appctx->output_file);

  // Free the application context
  g_print ("\n Free the Application context\n");
  gst_app_context_free (appctx);

  // Deinitialize the GST library
  g_print ("\n gst_deinit\n");
  gst_deinit ();

  return 0;
}
