/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
 * Application: GStreamer RGBA Watermark Overlay (qtivoverlay).
 *
 * Description:
 * Composites one or more RGBA watermarks onto a YUV camera stream using
 * the qtivoverlay plugin.
 *
 * Pipeline:
 *   qtiqmmfsrc -> capsfilter (NV12/GBM) -> queue -> qtivoverlay (images=...)
 *             -> queue -> waylandsink (fullscreen=true sync=false)
 *
 * Usage:
 *   gst-rgba-watermark-example [OPTIONS]
 *
 * Options:
 *   -W, --video-width=W   Video frame width in pixels (default: 1920)
 *   -H, --video-height=H  Video frame height in pixels (default: 1080)
 *   -R, --framerate=FPS   Video framerate in fps (default: 30)
 *   -C, --camera=ID       Camera ID (default: 0)
 *   -I, --image=ENTRY     Watermark entry (repeatable):
 *                         path=<file>,resolution=<w>x<h>,destination=<x>,<y>,<w>,<h>
 *
 * Press Ctrl+C to stop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gst/video/video.h>

/* Default values. */
#define DEFAULT_CAMERA_ID           0
#define DEFAULT_VIDEO_WIDTH         1920
#define DEFAULT_VIDEO_HEIGHT        1080
#define DEFAULT_VIDEO_FRAMERATE     30
#define DEFAULT_WATERMARK_RGBA_FILE "/data/misc/overlay_test_464_109.rgba"
#define DEFAULT_WATERMARK_RES_W     464
#define DEFAULT_WATERMARK_RES_H     109
#define DEFAULT_WATERMARK_DST_X     800
#define DEFAULT_WATERMARK_DST_Y     600
#define DEFAULT_WATERMARK_DST_W     464
#define DEFAULT_WATERMARK_DST_H     109

/* Internal constants. */
#define QUEUE_COUNT  2

/* Watermark entry. */
typedef struct _WatermarkEntry WatermarkEntry;

struct _WatermarkEntry {
  gchar *path;  // path to raw RGBA file
  gint   res_w; // RGBA image resolution width
  gint   res_h; // RGBA image resolution height
  gint   dst_x; // destination rectangle X on YUV frame
  gint   dst_y; // destination rectangle Y on YUV frame
  gint   dst_w; // destination rectangle width  (may != res_w)
  gint   dst_h; // destination rectangle height (may != res_h)
};

/* Application context. */
typedef struct _GstAppContext GstAppContext;

struct _GstAppContext {
  GstElement   *pipeline;
  GstElement   *overlay;    // reference kept for images configuration
  GMainLoop    *mloop;
  GList        *plugins;    // all pipeline elements for cleanup

  // Configuration (set from command-line arguments)
  gint          camera_id;
  gint          video_width;
  gint          video_height;
  gint          video_framerate;
  GList        *watermarks; // list of WatermarkEntry*
};

/* Forward declarations. */
static gboolean create_watermark_pipeline (GstAppContext *appctx);
static void     destroy_pipe              (GstAppContext *appctx);

static void
watermark_entry_free (gpointer data)
{
  WatermarkEntry *entry = (WatermarkEntry *) data;

  if (entry) {
    g_free (entry->path);
    g_free (entry);
  }
}

// Parse --image entry string into a WatermarkEntry.
// Format: "path=<file>,resolution=<w>x<h>,destination=<x>,<y>,<w>,<h>"
// Returns newly allocated WatermarkEntry, or NULL on error.
static WatermarkEntry *
parse_watermark_entry (const gchar *str)
{
  WatermarkEntry *entry    = NULL;
  gchar          *buf      = NULL;
  gchar          *p        = NULL;
  gchar          *end      = NULL;
  gboolean        got_path = FALSE;
  gboolean        got_res  = FALSE;
  gboolean        got_dst  = FALSE;
  gint            w        = 0;
  gint            h        = 0;
  gint            x        = 0;
  gint            y        = 0;
  gint            commas   = 0;

  if (!str || *str == '\0') {
    g_printerr ("ERROR: Empty --image entry.\n");
    return NULL;
  }

  entry = g_new0 (WatermarkEntry, 1);
  buf   = g_strdup (str);
  p     = buf;

  while (p && *p) {
    // Skip leading whitespace/commas.
    while (*p == ',' || *p == ' ') p++;

    if (*p == '\0')
      break;

    if (g_str_has_prefix (p, "path=")) {
      p += 5; // skip "path="
      // Path ends at the next ",resolution=" or end of string.
      end = g_strstr_len (p, -1, ",resolution=");
      if (!end) end = p + strlen (p);
      entry->path = g_strndup (p, end - p);
      p = end;
      got_path = TRUE;

    } else if (g_str_has_prefix (p, "resolution=")) {
      p += 11; // skip "resolution="
      if (sscanf (p, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
        g_printerr (
            "ERROR: --image: bad resolution (expected <w>x<h>): %s\n", p);
        goto fail;
      }
      entry->res_w = w;
      entry->res_h = h;
      while (*p && *p != ',') p++;
      got_res = TRUE;

    } else if (g_str_has_prefix (p, "destination=")) {
      p += 12; // skip "destination="
      if (sscanf (p, "%d,%d,%d,%d", &x, &y, &w, &h) != 4 ||
          w <= 0 || h <= 0) {
        g_printerr (
            "ERROR: --image: bad destination (expected x,y,w,h): %s\n", p);
        goto fail;
      }
      entry->dst_x = x;
      entry->dst_y = y;
      entry->dst_w = w;
      entry->dst_h = h;
      // Advance past x,y,w,h (4 comma-separated numbers).
      commas = 0;
      while (*p && commas < 3) {
        if (*p == ',') commas++;
        p++;
      }
      while (*p && *p != ',') p++;
      got_dst = TRUE;

    } else {
      // Unknown key, skip to next comma.
      while (*p && *p != ',') p++;
    }
  }

  g_free (buf);

  if (!got_path) {
    g_printerr ("ERROR: --image: missing 'path=' in: %s\n", str);
    goto fail_no_buf;
  }

  if (!got_res) {
    g_printerr ("ERROR: --image: missing 'resolution=' in: %s\n", str);
    goto fail_no_buf;
  }

  if (!got_dst) {
    g_printerr ("ERROR: --image: missing 'destination=' in: %s\n", str);
    goto fail_no_buf;
  }

  return entry;

fail:
  g_free (buf);
fail_no_buf:
  watermark_entry_free (entry);
  return NULL;
}

// Build the qtivoverlay 'images' property string and apply it to the overlay.
static gboolean
configure_overlay (GstAppContext *appctx)
{
  WatermarkEntry *entry      = NULL;
  GString        *images_str = g_string_new ("{");
  GList          *l          = NULL;
  gchar          *images     = NULL;
  gint            idx        = 1;

  for (l = appctx->watermarks; l != NULL; l = l->next, idx++) {
    entry = (WatermarkEntry *) l->data;

    if (idx > 1)
      g_string_append (images_str, ",");

    // Each entry: (structure)"ImageN,path=...,resolution=<w,h>,destination=<x,y,w,h>;"
    g_string_append_printf (images_str,
        "(structure)\"Image%d,path=%s,"
        "resolution=<%d,%d>,destination=<%d,%d,%d,%d>;\"",
        idx, entry->path,
        entry->res_w, entry->res_h,
        entry->dst_x, entry->dst_y, entry->dst_w, entry->dst_h);
  }

  g_string_append (images_str, "}");

  images = g_string_free (images_str, FALSE);
  g_print ("  images: %s\n", images);

  // Use gst_util_set_object_arg to handle GstValueArray-of-GstStructure.
  gst_util_set_object_arg (G_OBJECT (appctx->overlay), "images", images);
  g_free (images);

  return TRUE;
}

// Handle SIGINT: send EOS or quit the main loop.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstState state, pending;

  g_print ("\n\nReceived interrupt signal, sending EOS ...\n");

  if (!gst_element_get_state (
      appctx->pipeline, &state, &pending, GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: Cannot get pipeline state!\n");
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  if (state == GST_STATE_PLAYING)
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  else
    g_main_loop_quit (appctx->mloop);

  return TRUE;
}

// Handle pipeline state-changed bus messages.
static void
state_changed_cb (GstBus *bus, GstMessage *message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);

  if ((new_st == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {
    if (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr ("\nPipeline cannot transition to PLAYING state!\n");
    }
  }
}

// Handle pipeline warning bus messages.
static void
warning_cb (GstBus *bus, GstMessage *message, gpointer userdata)
{
  GError *error = NULL;
  gchar  *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

// Handle pipeline error bus messages.
static void
error_cb (GstBus *bus, GstMessage *message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop *) userdata;
  GError    *error = NULL;
  gchar     *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (mloop);
}

// Handle pipeline EOS bus messages.
static void
eos_cb (GstBus *bus, GstMessage *message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop *) userdata;

  g_print ("\nReceived EOS from '%s'.\n", GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

// Build and link the watermark pipeline:
//   qtiqmmfsrc -> capsfilter (NV12/GBM) -> queue[0] -> qtivoverlay
//             -> queue[1] -> waylandsink
static gboolean
create_watermark_pipeline (GstAppContext *appctx)
{
  GstElement *camsrc     = NULL;
  GstElement *capsfilter = NULL;
  GstElement *overlay    = NULL;
  GstElement *sink       = NULL;
  GstElement *queue[QUEUE_COUNT];
  GstCaps    *filtercaps = NULL;
  gchar       element_name[64];
  gboolean    ret = FALSE;
  gint        i   = 0;

  // Create elements.
  camsrc     = gst_element_factory_make ("qtiqmmfsrc",  "camsrc");
  capsfilter = gst_element_factory_make ("capsfilter",  "capsfilter");
  overlay    = gst_element_factory_make ("qtivoverlay", "overlay");
  sink       = gst_element_factory_make ("waylandsink", "sink");

  if (!camsrc || !capsfilter || !overlay || !sink) {
    g_printerr ("ERROR: One or more pipeline elements could not be created.\n");
    return FALSE;
  }

  for (i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, sizeof (element_name) - 1, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("ERROR: Failed to create %s\n", element_name);
      return FALSE;
    }
    appctx->plugins = g_list_append (appctx->plugins, queue[i]);
  }

  // Track all elements for cleanup.
  appctx->plugins = g_list_append (appctx->plugins, camsrc);
  appctx->plugins = g_list_append (appctx->plugins, capsfilter);
  appctx->plugins = g_list_append (appctx->plugins, overlay);
  appctx->plugins = g_list_append (appctx->plugins, sink);

  appctx->overlay = overlay;

  // Configure camera source.
  g_object_set (G_OBJECT (camsrc), "camera", appctx->camera_id, NULL);

  // Caps filter: NV12 in GBM memory.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format",    G_TYPE_STRING,     "NV12",
      "width",     G_TYPE_INT,        appctx->video_width,
      "height",    G_TYPE_INT,        appctx->video_height,
      "framerate", GST_TYPE_FRACTION, appctx->video_framerate, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Wayland sink: fullscreen, no sync.
  g_object_set (G_OBJECT (sink), "fullscreen", TRUE, "sync", FALSE, NULL);

  // Add elements to pipeline.
  g_print ("Adding elements to pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      camsrc, capsfilter, overlay, sink, NULL);

  for (i = 0; i < QUEUE_COUNT; i++)
    gst_bin_add (GST_BIN (appctx->pipeline), queue[i]);

  // Link: camsrc -> capsfilter -> queue[0] -> overlay -> queue[1] -> sink
  g_print ("Linking pipeline elements...\n");
  ret = gst_element_link_many (
      camsrc, capsfilter, queue[0], overlay, queue[1], sink, NULL);
  if (!ret) {
    g_printerr ("ERROR: Pipeline elements cannot be linked.\n");
    return FALSE;
  }

  g_print ("Pipeline created and linked successfully.\n");
  return TRUE;
}

static void
destroy_pipe (GstAppContext *appctx)
{
  GstElement *e1   = NULL;
  GstElement *e2   = NULL;
  GList      *list = NULL;

  if (!appctx->plugins)
    return;

  e1   = GST_ELEMENT_CAST (g_list_nth_data (appctx->plugins, 0));
  list = appctx->plugins->next;

  for (; list != NULL; list = list->next) {
    e2 = (GstElement *) list->data;
    gst_element_unlink (e1, e2);
    gst_bin_remove (GST_BIN (appctx->pipeline), e1);
    e1 = e2;
  }

  gst_bin_remove (GST_BIN (appctx->pipeline), e1);
  g_list_free (appctx->plugins);
  appctx->plugins = NULL;

  gst_object_unref (appctx->pipeline);
  appctx->pipeline = NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GOptionContext *ctx             = NULL;
  GMainLoop      *mloop           = NULL;
  GstBus         *bus             = NULL;
  WatermarkEntry *entry           = NULL;
  GList          *l               = NULL;
  GError         *parse_error     = NULL;
  gchar          *help_text       = NULL;
  gchar         **opt_images      = NULL;
  guint           intrpt_watch_id = 0;
  gint            opt_vid_width   = DEFAULT_VIDEO_WIDTH;
  gint            opt_vid_height  = DEFAULT_VIDEO_HEIGHT;
  gint            opt_framerate   = DEFAULT_VIDEO_FRAMERATE;
  gint            opt_camera      = DEFAULT_CAMERA_ID;
  gint            idx             = 1;
  gint            i               = 0;
  gboolean        opt_show_help   = FALSE;
  gboolean        ret             = FALSE;
  GstAppContext   appctx;
  GOptionEntry    entries[]       = {
    { "video-width",  'W', 0, G_OPTION_ARG_INT,          &opt_vid_width,
      "Video frame width in pixels (default: "
      G_STRINGIFY (DEFAULT_VIDEO_WIDTH) ")",
      "W" },
    { "video-height", 'H', 0, G_OPTION_ARG_INT,          &opt_vid_height,
      "Video frame height in pixels (default: "
      G_STRINGIFY (DEFAULT_VIDEO_HEIGHT) ")",
      "H" },
    { "framerate",    'R', 0, G_OPTION_ARG_INT,          &opt_framerate,
      "Video framerate in fps (default: "
      G_STRINGIFY (DEFAULT_VIDEO_FRAMERATE) ")",
      "FPS" },
    { "camera",       'C', 0, G_OPTION_ARG_INT,          &opt_camera,
      "Camera ID (default: " G_STRINGIFY (DEFAULT_CAMERA_ID) ")",
      "ID" },
    { "image",        'I', 0, G_OPTION_ARG_STRING_ARRAY, &opt_images,
      "Watermark entry (repeatable). "
      "Format: path=<file>,resolution=<w>x<h>,destination=<x>,<y>,<w>,<h>",
      "ENTRY" },
    { "help-h", 'h', G_OPTION_FLAG_HIDDEN | G_OPTION_FLAG_NO_ARG,
      G_OPTION_ARG_NONE, &opt_show_help, NULL, NULL },
    { NULL }
  };

  // Parse command-line arguments.
  ctx = g_option_context_new (NULL);
  g_option_context_set_summary (ctx,
      "GStreamer RGBA watermark overlay example (qtivoverlay).\n"
      "Composites one or more RGBA watermarks onto a YUV camera stream.\n\n"
      "Image entry format:\n"
      "  path=<file>,resolution=<w>x<h>,destination=<x>,<y>,<w>,<h>\n\n"
      "  path        : path to raw RGBA file\n"
      "  resolution  : RGBA image dimensions (e.g. 464x109)\n"
      "  destination : render rect on YUV frame: x,y,width,height\n"
      "                width/height may differ from resolution");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &parse_error)) {
    g_printerr ("ERROR: %s\n", parse_error->message);
    g_error_free (parse_error);
    g_option_context_free (ctx);
    return 1;
  }

  // -h is an alias for --help: print help and exit.
  if (opt_show_help) {
    help_text = g_option_context_get_help (ctx, TRUE, NULL);
    g_print ("%s", help_text);
    g_free (help_text);
    g_option_context_free (ctx);
    return 0;
  }

  g_option_context_free (ctx);

  // Initialize application context.
  memset (&appctx, 0, sizeof (appctx));
  appctx.camera_id       = opt_camera;
  appctx.video_width     = opt_vid_width;
  appctx.video_height    = opt_vid_height;
  appctx.video_framerate = opt_framerate;

  // Parse watermark entries.
  if (opt_images && opt_images[0]) {
    for (i = 0; opt_images[i] != NULL; i++) {
      entry = parse_watermark_entry (opt_images[i]);
      if (!entry) {
        g_strfreev (opt_images);
        return 1;
      }
      appctx.watermarks = g_list_append (appctx.watermarks, entry);
    }
  } else {
    // No --image specified: use built-in default watermark.
    entry        = g_new0 (WatermarkEntry, 1);
    entry->path  = g_strdup (DEFAULT_WATERMARK_RGBA_FILE);
    entry->res_w = DEFAULT_WATERMARK_RES_W;
    entry->res_h = DEFAULT_WATERMARK_RES_H;
    entry->dst_x = DEFAULT_WATERMARK_DST_X;
    entry->dst_y = DEFAULT_WATERMARK_DST_Y;
    entry->dst_w = DEFAULT_WATERMARK_DST_W;
    entry->dst_h = DEFAULT_WATERMARK_DST_H;
    appctx.watermarks = g_list_append (appctx.watermarks, entry);
  }

  g_strfreev (opt_images);

  // Set environment variables required by EGL/Wayland display stack.
  g_setenv ("XDG_RUNTIME_DIR", "/run/user/root", FALSE);
  g_setenv ("WAYLAND_DISPLAY", "wayland-1",      FALSE);

  // Print configuration.
  g_print ("=== GStreamer RGBA Watermark Overlay Example (qtivoverlay) ===\n");
  g_print ("Camera     : ID %d\n", appctx.camera_id);
  g_print ("Video      : %dx%d @ %d fps (NV12/GBM)\n",
      appctx.video_width, appctx.video_height, appctx.video_framerate);
  g_print ("Watermarks : %d\n", g_list_length (appctx.watermarks));

  for (l = appctx.watermarks; l != NULL; l = l->next, idx++) {
    entry = (WatermarkEntry *) l->data;
    g_print ("  [%d] path=%s resolution=%dx%d destination=%d,%d,%d,%d\n",
        idx, entry->path, entry->res_w, entry->res_h,
        entry->dst_x, entry->dst_y, entry->dst_w, entry->dst_h);
  }
  g_print ("\n");

  // Step 1: Create GStreamer pipeline.
  g_print ("Creating GStreamer pipeline...\n");
  appctx.pipeline = gst_pipeline_new ("gst-rgba-watermark-app");
  if (!appctx.pipeline) {
    g_printerr ("ERROR: Failed to create pipeline.\n");
    goto cleanup;
  }

  ret = create_watermark_pipeline (&appctx);
  if (!ret) {
    g_printerr ("ERROR: Failed to build watermark pipeline.\n");
    goto cleanup;
  }
  g_print ("Done.\n\n");

  // Step 2: Configure qtivoverlay with watermark images.
  g_print ("Configuring qtivoverlay with watermark images...\n");
  configure_overlay (&appctx);
  g_print ("Done.\n\n");

  // Step 3: Setup GLib main loop.
  mloop = g_main_loop_new (NULL, FALSE);
  if (!mloop) {
    g_printerr ("ERROR: Failed to create main loop.\n");
    goto cleanup;
  }
  appctx.mloop = mloop;

  // Step 4: Setup pipeline bus message handling.
  bus = gst_pipeline_get_bus (GST_PIPELINE (appctx.pipeline));
  if (!bus) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus.\n");
    goto cleanup;
  }

  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx.pipeline);
  g_signal_connect (bus, "message::warning",
      G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error",
      G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos",
      G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Step 5: Register SIGINT handler.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal,
      &appctx);

  // Step 6: Start the pipeline.
  g_print ("Starting pipeline...\n");
  g_print ("Press Ctrl+C to stop.\n\n");

  switch (gst_element_set_state (appctx.pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
      goto cleanup;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful.\n");
      break;
  }

  g_print ("Running...\n");
  g_main_loop_run (mloop);
  g_print ("Main loop ended.\n");

  g_source_remove (intrpt_watch_id);

  g_print ("Setting pipeline to NULL state...\n");
  gst_element_set_state (appctx.pipeline, GST_STATE_NULL);

cleanup:
  g_print ("Cleaning up...\n");
  destroy_pipe (&appctx);
  g_list_free_full (appctx.watermarks, watermark_entry_free);

  gst_deinit ();
  g_print ("Done.\n");

  return 0;
}
