/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
* Application:
* GStreamer Camera via RTSP (using qtirtspbin)
*
* Description:
* This application builds a single GStreamer pipeline that captures from
* camera, encodes to H.264, and streams via RTSP using qtirtspbin:
*
*   qtiqmmfsrc -> capsfilter -> [encoder] -> queue ->
*   h264parse(config-interval=1) -> queue -> qtirtspbin
*
* The encoder element is selectable at runtime (default: qtic2venc).
* When qtic2venc is used the caps include the memory:GBM feature and
* encoder QP/bitrate properties are applied automatically.
* When v4l2h264enc is used plain video/x-raw caps are used and
* capture-io-mode/output-io-mode are set to 4 (DMABUF).
*
* Usage:
* gst-camera-via-rtsp-example
*
* Help:
* gst-camera-via-rtsp-example --help
*
*/

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <glib-unix.h>
#include <gst/gst.h>

#define DEFAULT_RTSP_MOUNT   "/live"
#define DEFAULT_RTSP_ADDRESS "127.0.0.1"
#define DEFAULT_RTSP_PORT    "8900"
#define DEFAULT_CAMERA_ID    0
#define DEFAULT_WIDTH        1920
#define DEFAULT_HEIGHT       1080
#define DEFAULT_FPS_NUM      30
#define DEFAULT_BITRATE      70000000
#define DEFAULT_ENCODER      "qtic2venc"

#define ENCODER_MIN_QP_I 30
#define ENCODER_MIN_QP_P 30
#define ENCODER_MAX_QP_I 51
#define ENCODER_MAX_QP_P 51
#define ENCODER_QP_I     30
#define ENCODER_QP_P     30

#define PIPELINE_NULL_WAIT_TIMEOUT  (5 * GST_SECOND)
#define PIPELINE_ABORT_WAIT_TIMEOUT (2 * GST_SECOND)

typedef struct _GstCameraViaRtspCtx GstCameraViaRtspCtx;

struct _GstCameraViaRtspCtx
{
  GstElement *pipeline;
  GMainLoop *mloop;

  gchar *rtsp_address;
  gchar *rtsp_mount;
  gchar *rtsp_port;
  gchar *encoder_name;

  gint camera_id;
  gint width;
  gint height;
  gint fps_num;
  gint bitrate;
};

static const gchar *
get_rtsp_address (const GstCameraViaRtspCtx * appctx)
{
  if (appctx->rtsp_address != NULL && appctx->rtsp_address[0] != '\0')
    return appctx->rtsp_address;
  return DEFAULT_RTSP_ADDRESS;
}

static const gchar *
get_rtsp_mount (const GstCameraViaRtspCtx * appctx)
{
  if (appctx->rtsp_mount != NULL && appctx->rtsp_mount[0] != '\0')
    return appctx->rtsp_mount;
  return DEFAULT_RTSP_MOUNT;
}

static const gchar *
get_rtsp_port (const GstCameraViaRtspCtx * appctx)
{
  if (appctx->rtsp_port != NULL && appctx->rtsp_port[0] != '\0')
    return appctx->rtsp_port;
  return DEFAULT_RTSP_PORT;
}

static const gchar *
get_encoder_name (const GstCameraViaRtspCtx * appctx)
{
  if (appctx->encoder_name != NULL && appctx->encoder_name[0] != '\0')
    return appctx->encoder_name;
  return DEFAULT_ENCODER;
}

static gboolean
is_valid_hostname (const gchar * host)
{
  gsize len = 0;

  if (host == NULL || host[0] == '\0')
    return FALSE;

  if (!g_regex_match_simple ("^[A-Za-z0-9.-]+$", host, 0, 0))
    return FALSE;

  len = strlen (host);
  if (host[0] == '.' || host[0] == '-' ||
      host[len - 1] == '.' || host[len - 1] == '-')
    return FALSE;

  if (strstr (host, "..") != NULL)
    return FALSE;

  return TRUE;
}

static gboolean
is_valid_rtsp_address (const gchar * address)
{
  if (address == NULL || address[0] == '\0')
    return TRUE;

  if (g_hostname_is_ip_address (address))
    return TRUE;

  return is_valid_hostname (address);
}

static gboolean
is_valid_mount_point (const gchar * mount)
{
  if (mount == NULL || mount[0] == '\0')
    return FALSE;

  return g_regex_match_simple ("^/[A-Za-z0-9/_-]+$", mount, 0, 0);
}

static gboolean
validate_options (GstCameraViaRtspCtx * appctx)
{
  const gchar *mount = get_rtsp_mount (appctx);

  if (!is_valid_mount_point (mount)) {
    g_printerr ("Invalid mount point: '%s'\n", mount);
    g_printerr ("mount must match ^/[A-Za-z0-9/_-]+$\n");
    return FALSE;
  }

  if (!is_valid_rtsp_address (appctx->rtsp_address)) {
    g_printerr ("Invalid address: '%s'\n", appctx->rtsp_address);
    g_printerr ("address must be a valid IPv4/IPv6 address or hostname\n");
    return FALSE;
  }

  if (appctx->camera_id < 0) {
    g_printerr ("camera id must be >= 0\n");
    return FALSE;
  }

  if (appctx->width <= 0 || appctx->height <= 0) {
    g_printerr ("width and height must be positive integers\n");
    return FALSE;
  }

  if (appctx->fps_num <= 0) {
    g_printerr ("fps-num must be a positive integer\n");
    return FALSE;
  }

  if (appctx->bitrate <= 0) {
    g_printerr ("bitrate must be a positive integer\n");
    return FALSE;
  }

  return TRUE;
}

// Handles interrupt signals like Ctrl+C.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstCameraViaRtspCtx *appctx = (GstCameraViaRtspCtx *) userdata;
  g_print ("\n\nReceived an interrupt signal, shutting down ...\n");

  if (appctx->mloop != NULL)
    g_main_loop_quit (appctx->mloop);

  return G_SOURCE_CONTINUE;
}

// Handles state change transitions.
static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstCameraViaRtspCtx *appctx = (GstCameraViaRtspCtx *) userdata;
  GstState old, new_st, pending;

  if (appctx->pipeline == NULL)
    return;

  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (appctx->pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (new_st),
      gst_element_state_get_name (pending));
}

// Handles warnings.
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

// Handles errors.
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstCameraViaRtspCtx *appctx = (GstCameraViaRtspCtx *) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  if (appctx->mloop != NULL)
    g_main_loop_quit (appctx->mloop);
}

// Handles EOS.
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstCameraViaRtspCtx *appctx = (GstCameraViaRtspCtx *) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  if (appctx->mloop != NULL)
    g_main_loop_quit (appctx->mloop);
}

static gboolean
create_and_add_element (GstElement * pipeline, GstElement ** element,
    const gchar * factory, const gchar * name)
{
  *element = gst_element_factory_make (factory, name);
  if (*element == NULL) {
    g_printerr ("Failed to create element: %s (%s)\n", name, factory);
    return FALSE;
  }

  if (!gst_bin_add (GST_BIN (pipeline), *element)) {
    g_printerr ("Failed to add element into pipeline: %s\n", name);
    gst_object_unref (*element);
    *element = NULL;
    return FALSE;
  }

  return TRUE;
}

static gboolean
create_pipeline (GstCameraViaRtspCtx * appctx)
{
  GstElement *qtiqmmfsrc = NULL;
  GstElement *capsfilter = NULL;
  GstElement *encoder = NULL;
  GstElement *queue_enc = NULL;
  GstElement *h264parse = NULL;
  GstElement *queue_rtsp = NULL;
  GstElement *qtirtspbin = NULL;
  GstCaps *caps = NULL;

  const gchar *enc_name = get_encoder_name (appctx);
  const gchar *address = get_rtsp_address (appctx);
  const gchar *port = get_rtsp_port (appctx);
  const gchar *mount = get_rtsp_mount (appctx);
  gboolean use_qtic2v = (g_strcmp0 (enc_name, "qtic2venc") == 0);
  gboolean use_v4l2h264 = (g_strcmp0 (enc_name, "v4l2h264enc") == 0);

  appctx->pipeline = gst_pipeline_new ("camera-via-rtsp-pipeline");
  if (appctx->pipeline == NULL) {
    g_printerr ("Failed to create pipeline\n");
    return FALSE;
  }

  if (!create_and_add_element (appctx->pipeline, &qtiqmmfsrc,
          "qtiqmmfsrc", "qtiqmmfsrc"))
    goto fail;
  if (!create_and_add_element (appctx->pipeline, &capsfilter,
          "capsfilter", "capsfilter"))
    goto fail;
  if (!create_and_add_element (appctx->pipeline, &encoder, enc_name, enc_name))
    goto fail;
  if (!create_and_add_element (appctx->pipeline, &queue_enc,
          "queue", "queue_enc"))
    goto fail;
  if (!create_and_add_element (appctx->pipeline, &h264parse,
          "h264parse", "h264parse"))
    goto fail;
  if (!create_and_add_element (appctx->pipeline, &queue_rtsp,
          "queue", "queue_rtsp"))
    goto fail;
  if (!create_and_add_element (appctx->pipeline, &qtirtspbin,
          "qtirtspbin", "qtirtspbin"))
    goto fail;

  // caps: add memory:GBM feature only for qtic2venc.
  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, appctx->width,
      "height", G_TYPE_INT, appctx->height,
      "framerate", GST_TYPE_FRACTION, appctx->fps_num, 1,
      NULL);
  if (use_qtic2v)
    gst_caps_set_features (caps, 0,
        gst_caps_features_new ("memory:GBM", NULL));

  g_object_set (G_OBJECT (qtiqmmfsrc), "camera", appctx->camera_id, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);
  caps = NULL;

  // Encoder-specific properties for qtic2venc.
  if (use_qtic2v) {
    g_object_set (G_OBJECT (encoder),
        "target-bitrate", appctx->bitrate,
        "min-quant-i-frames", ENCODER_MIN_QP_I,
        "min-quant-p-frames", ENCODER_MIN_QP_P,
        "max-quant-i-frames", ENCODER_MAX_QP_I,
        "max-quant-p-frames", ENCODER_MAX_QP_P,
        "quant-i-frames", ENCODER_QP_I,
        "quant-p-frames", ENCODER_QP_P,
        NULL);
    gst_util_set_object_arg (G_OBJECT (encoder), "control-rate", "VBR-CFR");
  }

  // Encoder-specific properties for v4l2h264enc.
  if (use_v4l2h264) {
    g_object_set (G_OBJECT (encoder),
        "capture-io-mode", 4,
        "output-io-mode", 4,
        NULL);
  }

  g_object_set (G_OBJECT (h264parse), "config-interval", 1, NULL);

  // qtirtspbin properties are all String type.
  g_object_set (G_OBJECT (qtirtspbin),
      "address", address,
      "port", port,
      "mpoint", mount,
      NULL);

  if (!gst_element_link_many (qtiqmmfsrc, capsfilter, encoder,
          queue_enc, h264parse, queue_rtsp, qtirtspbin, NULL)) {
    g_printerr ("Error: element link failed\n");
    goto fail;
  }

  g_print ("Pipeline created:\n");
  g_print ("  qtiqmmfsrc(camera=%d) -> video/x-raw%s,NV12,%dx%d,%d/1\n",
      appctx->camera_id, use_qtic2v ? "(memory:GBM)" : "",
      appctx->width, appctx->height, appctx->fps_num);
  g_print ("  %s%s -> queue -> h264parse(config-interval=1)"
      " -> queue -> qtirtspbin\n",
      enc_name, use_qtic2v ? "(VBR-CFR)" : use_v4l2h264 ? "(io-mode=4)" : "");
  g_print ("  qtirtspbin: address=%s port=%s mpoint=%s\n",
      address, port, mount);

  return TRUE;

fail:
  if (caps != NULL)
    gst_caps_unref (caps);

  if (appctx->pipeline != NULL) {
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  return FALSE;
}

static void
stop_pipeline (GstCameraViaRtspCtx * appctx)
{
  GstStateChangeReturn ret;
  GstStateChangeReturn wait_ret;
  GstState state = GST_STATE_VOID_PENDING;
  GstState pending = GST_STATE_VOID_PENDING;

  if (appctx->pipeline == NULL)
    return;

  ret = gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("WARNING: failed to set pipeline to NULL state\n");
    return;
  }

  if (ret != GST_STATE_CHANGE_ASYNC)
    return;

  wait_ret = gst_element_get_state (appctx->pipeline, &state, &pending,
      PIPELINE_NULL_WAIT_TIMEOUT);
  if (wait_ret == GST_STATE_CHANGE_SUCCESS ||
      wait_ret == GST_STATE_CHANGE_NO_PREROLL)
    return;

  g_printerr ("WARNING: timeout while waiting pipeline to enter NULL state, "
      "aborting pending state change\n");
  gst_element_abort_state (appctx->pipeline);

  ret = gst_element_set_state (appctx->pipeline, GST_STATE_NULL);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("WARNING: failed to set pipeline to NULL after abort\n");
    return;
  }

  if (ret != GST_STATE_CHANGE_ASYNC)
    return;

  wait_ret = gst_element_get_state (appctx->pipeline, &state, &pending,
      PIPELINE_ABORT_WAIT_TIMEOUT);
  if (wait_ret != GST_STATE_CHANGE_SUCCESS &&
      wait_ret != GST_STATE_CHANGE_NO_PREROLL) {
    g_printerr ("WARNING: pipeline still not in NULL after abort "
        "(state=%s pending=%s)\n",
        gst_element_state_get_name (state),
        gst_element_state_get_name (pending));
  }
}

static void
deinit_context (GstCameraViaRtspCtx * appctx)
{
  if (appctx->pipeline != NULL) {
    stop_pipeline (appctx);
    gst_object_unref (appctx->pipeline);
    appctx->pipeline = NULL;
  }

  if (appctx->mloop != NULL) {
    g_main_loop_unref (appctx->mloop);
    appctx->mloop = NULL;
  }

  g_free (appctx->rtsp_address);
  appctx->rtsp_address = NULL;

  g_free (appctx->rtsp_mount);
  appctx->rtsp_mount = NULL;

  g_free (appctx->rtsp_port);
  appctx->rtsp_port = NULL;

  g_free (appctx->encoder_name);
  appctx->encoder_name = NULL;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GstBus *bus = NULL;
  GError *parse_err = NULL;
  guint intrpt_watch_id = 0;
  guint sigterm_watch_id = 0;
  GstStateChangeReturn ret;
  gboolean gst_initialized = FALSE;
  gboolean bus_watch_added = FALSE;
  gint exit_code = EXIT_FAILURE;

  GstCameraViaRtspCtx appctx = {
    .pipeline = NULL,
    .mloop = NULL,
    .rtsp_address = NULL,
    .rtsp_mount = NULL,
    .rtsp_port = NULL,
    .encoder_name = NULL,
    .camera_id = DEFAULT_CAMERA_ID,
    .width = DEFAULT_WIDTH,
    .height = DEFAULT_HEIGHT,
    .fps_num = DEFAULT_FPS_NUM,
    .bitrate = DEFAULT_BITRATE,
  };

  GOptionEntry entries[] = {
    {"camera", 'c', 0, G_OPTION_ARG_INT, &appctx.camera_id,
      "Camera id for qtiqmmfsrc", "ID"},
    {"width", 0, 0, G_OPTION_ARG_INT, &appctx.width,
      "Input width in pixels", "PIXELS"},
    {"height", 0, 0, G_OPTION_ARG_INT, &appctx.height,
      "Input height in pixels", "PIXELS"},
    {"fps-num", 0, 0, G_OPTION_ARG_INT, &appctx.fps_num,
      "Framerate numerator", "FPS"},
    {"encoder", 'e', 0, G_OPTION_ARG_STRING, &appctx.encoder_name,
      "H.264 encoder element name (qtic2venc or v4l2h264enc)", "ELEMENT"},
    {"bitrate", 'b', 0, G_OPTION_ARG_INT, &appctx.bitrate,
      "Target bitrate in bps for qtic2venc", "BPS"},
    {"address", 'a', 0, G_OPTION_ARG_STRING, &appctx.rtsp_address,
      "RTSP server address for qtirtspbin", "ADDR"},
    {"port", 'p', 0, G_OPTION_ARG_STRING, &appctx.rtsp_port,
      "RTSP port for qtirtspbin", "PORT"},
    {"mount", 'm', 0, G_OPTION_ARG_STRING, &appctx.rtsp_mount,
      "RTSP mount point", "PATH"},
    {NULL}
  };

  gst_init (&argc, &argv);
  gst_initialized = TRUE;

  ctx = g_option_context_new ("- camera H.264 RTSP stream via qtirtspbin");
  g_option_context_add_main_entries (ctx, entries, NULL);

  if (!g_option_context_parse (ctx, &argc, &argv, &parse_err)) {
    g_printerr ("Option parsing failed: %s\n", parse_err->message);
    g_clear_error (&parse_err);
    goto cleanup;
  }

  g_option_context_free (ctx);
  ctx = NULL;

  if (!validate_options (&appctx))
    goto cleanup;

  appctx.mloop = g_main_loop_new (NULL, FALSE);
  if (appctx.mloop == NULL) {
    g_printerr ("Failed to create main loop\n");
    goto cleanup;
  }

  if (!create_pipeline (&appctx))
    goto cleanup;

  bus = gst_element_get_bus (appctx.pipeline);
  if (bus == NULL) {
    g_printerr ("Failed to get bus from pipeline\n");
    goto cleanup;
  }

  gst_bus_add_signal_watch (bus);
  bus_watch_added = TRUE;
  g_signal_connect (G_OBJECT (bus), "message::state-changed",
      G_CALLBACK (state_changed_cb), &appctx);
  g_signal_connect (G_OBJECT (bus), "message::warning",
      G_CALLBACK (warning_cb), &appctx);
  g_signal_connect (G_OBJECT (bus), "message::error",
      G_CALLBACK (error_cb), &appctx);
  g_signal_connect (G_OBJECT (bus), "message::eos",
      G_CALLBACK (eos_cb), &appctx);

  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &appctx);
  sigterm_watch_id =
      g_unix_signal_add (SIGTERM, handle_interrupt_signal, &appctx);
  if (intrpt_watch_id == 0 || sigterm_watch_id == 0) {
    g_printerr ("Failed to install signal handlers\n");
    goto cleanup;
  }

  ret = gst_element_set_state (appctx.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to set pipeline to PLAYING\n");
    goto cleanup;
  }

  g_print ("\nPipeline running.\n");
  g_print ("RTSP URL: rtsp://%s:%s%s\n",
      get_rtsp_address (&appctx), get_rtsp_port (&appctx),
      get_rtsp_mount (&appctx));

  g_main_loop_run (appctx.mloop);
  g_print ("\nMain loop exited.\n");

  exit_code = EXIT_SUCCESS;

cleanup:
  if (intrpt_watch_id > 0) {
    g_source_remove (intrpt_watch_id);
    intrpt_watch_id = 0;
  }

  if (sigterm_watch_id > 0) {
    g_source_remove (sigterm_watch_id);
    sigterm_watch_id = 0;
  }

  if (bus != NULL) {
    if (bus_watch_added)
      gst_bus_remove_signal_watch (bus);
    gst_object_unref (bus);
    bus = NULL;
  }

  deinit_context (&appctx);

  if (ctx != NULL)
    g_option_context_free (ctx);

  if (gst_initialized)
    gst_deinit ();

  return exit_code;
}
