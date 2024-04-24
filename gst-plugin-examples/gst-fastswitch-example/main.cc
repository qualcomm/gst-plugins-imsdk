/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
* gst-fastswitch-exmaple
*
* Usage:
* --pwidth, Preview stream width
* --pheight, Preview stream height
* --prate, Preview stream framerate
* --v1width, Video stream 1 width
* --v1height, Video stream 1 height
* --v1rate, Video stream 1 framerate
* --v2width, Video stream 2 width
* --v2height, Video stream 2 height
* --v2rate, Video stream 2 framerate
* --switch_delay, Delay of switch
* --round, Round to Switch
*
* Description:
* Switch bewteen preview stream and preview + video stream.
*/

#include <stdio.h>

#include <gst/gst.h>
#include <glib-unix.h>

// Macro defination
#define DEFAULT_VIDEOSTREAM_NUMBER 1
#define DEFAULT_VIDEOSTREAM_FORMAT "NV12"
#define DEFAULT_VIDEOSTREAM_WIDTH 1920
#define DEFAULT_VIDEOSTREAM_HEIGHT 1080
#define DEFAULT_VIDEOSTREAM_FPS_NUMERATOR 30
#define DEFAULT_VIDEOSTREAM_FPS_DENOMINATOR 1
#define DEFAULT_VIDEOSTREAM_FILE_LOCATION "/data/fast-switch_%d.mp4"
#define DEFAULT_PREVIEWSTREAM_FORMAT "NV12"
#define DEFAULT_PREVIEWSTREAM_WIDTH 1920
#define DEFAULT_PREVIEWSTREAM_HEIGHT 1080
#define DEFAULT_PREVIEWSTREAM_FPS_NUMERATOR 30
#define DEFAULT_PREVIEWSTREAM_FPS_DENOMINATOR 1
#define DEFAULT_SWITCH_DELAY 5
#define DEFAULT_ROUND G_MININT32
#define DEFAULT_OPMODE "fastswitch"

// Global variable
static gchar *opmode = (gchar *)DEFAULT_OPMODE;
static gboolean frameselection_enabled = FALSE;

typedef struct _GstAppContext GstAppContext;
typedef struct _GstVideoStreamInfo GstVideoStreamInfo;
typedef struct _GstPreviewStreamInfo GstPreviewStreamInfo;
typedef struct _MetaInfo MetaInfo;

enum {
  CAM_OPMODE_NONE               = (1 << 0),
  CAM_OPMODE_FRAMESELECTION     = (1 << 1),
  CAM_OPMODE_FASTSWITCH         = (1 << 2),
};

/*** Data Structure ***/
struct _MetaInfo {
  gint width;
  gint height;
  gint framerate;
};

struct _GstVideoStreamInfo {
  GstPad* qmmf_pad;
  GstCaps* qmmf_caps;
  GstElement* capsfilter;
  GstElement* encoder;
  GstElement* capsfilter_dfps;
  GstElement* parser;
  GstElement* muxer;
  GstElement* filesinker;
  MetaInfo meta;
};

struct _GstPreviewStreamInfo {
  GstPad* qmmf_pad;
  GstCaps* qmmf_caps;
  GstElement* capsfilter;
  GstElement* displayer;
  MetaInfo meta;
};

struct _GstAppContext {
  GMainLoop* mloop;
  GstElement* pipeline;
  GstElement* source;
  GList* vstreams_list;
  GstPreviewStreamInfo* previewstream;
  gboolean exit;
  gint round;
};

/*** Function ***/
// Declaration
static gboolean source_add (GstAppContext* appctx);
static void source_remove (GstAppContext* appctx);
static gboolean appcontext_create (GstAppContext* appctx,
    const gint vnum);
static void appcontext_delete (GstAppContext* appctx);
static gboolean interrupt_handler (gpointer userdata);
static gboolean streams_create (GstAppContext* appctx);
static void streams_delete (GstAppContext* appctx);
static gboolean switch_func (gpointer userdata);
static void stream_meta_configure (MetaInfo* meta,
    const gint width, const gint height, const gint fps);
static gboolean signal_add (GstAppContext* appctx);


// Add source element
static gboolean
source_add (GstAppContext* appctx)
{
  gchar *propname = (gchar *)"op-mode";
  guint32 flags = CAM_OPMODE_NONE;

  if (frameselection_enabled)
    flags = CAM_OPMODE_FRAMESELECTION |  CAM_OPMODE_FASTSWITCH;
  else
    flags = CAM_OPMODE_FASTSWITCH;

  appctx->source = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");

  if (!appctx->source) {
    g_printerr ("ERROR: failed to create qtiqmmfsrc.\n");
    return FALSE;
  }

  g_object_set (G_OBJECT (appctx->source), "op-mode", flags, NULL);

  if (!gst_bin_add (GST_BIN (appctx->pipeline), appctx->source)) {
    g_printerr ("ERROR: failed to add source to bin.\n");
    return FALSE;
  }

  return TRUE;
}

// Remove source element
static void
source_remove (GstAppContext* appctx)
{
  gst_bin_remove (GST_BIN (appctx->pipeline), appctx->source);

  return;
}

// Init GstAppContext
static gboolean
appcontext_create (GstAppContext* appctx, const gint vnum)
{
  appctx->mloop = NULL;
  appctx->pipeline = NULL;
  appctx->source = NULL;
  appctx->vstreams_list = NULL;
  appctx->previewstream = NULL;
  appctx->exit = FALSE;
  appctx->round = DEFAULT_ROUND;

  appctx->mloop = g_main_loop_new (NULL, FALSE);
  if (!appctx->mloop) {
    g_printerr ("ERROR: failed to create main loop.\n");
    return FALSE;
  }

  appctx->pipeline = gst_pipeline_new ("gst-fastswitch-example");
  if (!appctx->pipeline) {
    g_printerr ("ERROR: failed to create pipeline.\n");
    return FALSE;
  }

  if (!source_add (appctx)) {
    g_printerr ("ERROR: failed to add source.\n");
    return FALSE;
  }

  for (gint i = 1; i <= vnum; ++i) {
    GstVideoStreamInfo* videostream = NULL;
    videostream = g_new0 (GstVideoStreamInfo, 1);
    if (!videostream) {
      g_printerr ("ERROR: failed to allocate videostream.\n");
      return FALSE;
    }
    appctx->vstreams_list = g_list_append (appctx->vstreams_list, videostream);
  }
  if (!appctx->vstreams_list) {
    g_printerr ("ERROR: failed to create video streams list.\n");
    return FALSE;
  }

  appctx->previewstream = g_new0 (GstPreviewStreamInfo, 1);
  if (!appctx->previewstream) {
    g_printerr ("ERROR: failed to allocate previewstream.\n");
    return FALSE;
  }

  return TRUE;
}

// Deinit GstAppContext
static void
appcontext_delete (GstAppContext* appctx)
{
  GList* list = g_list_first (appctx->vstreams_list);

  // Remove the source element
  source_remove (appctx);

  if (appctx->mloop)
    g_main_loop_unref (appctx->mloop);

  if (appctx->pipeline)
    gst_object_unref (appctx->pipeline);

  for (list = appctx->vstreams_list; list != NULL; list = list->next) {
    g_free (list->data);
  }

  if (appctx->previewstream)
    g_free (appctx->previewstream);

  g_free (appctx);

  return;
}

// Callback to handle state change, just print state
static void
state_change_callback (GstBus* bus, GstMessage* message, gpointer userdata)
{
  GstElement* pipe = GST_ELEMENT (userdata);
  GstState oldstate = GST_STATE_NULL, newstate = GST_STATE_NULL;
  GstState pendingstate = GST_STATE_NULL;

  // Only handle state change message from pipeline
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipe))
    return;

  gst_message_parse_state_changed (message, &oldstate, &newstate, &pendingstate);

  g_print ("\nPipeline state changed from %s to %s, pending:%s\n",
      gst_element_state_get_name (oldstate),
      gst_element_state_get_name (newstate),
      gst_element_state_get_name (pendingstate));
}

// Callback to handle warning
static void
warning_callback (GstBus* bus, GstMessage* message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

// Callback to handle error
static void
error_callback (GstBus* bus, GstMessage* message, gpointer userdata)
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

// Callback to handle eos
static void
eos_callback (GstBus* bus, GstMessage* message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\n\nReceived End-of-Stream from '%s' ...\n\n",
      GST_MESSAGE_SRC_NAME (message));

  g_main_loop_quit (mloop);
}

// Retrieve bus and add signals
static gboolean
signal_add (GstAppContext* appctx)
{
  GstBus* bus = NULL;

  bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline));
  if (!bus) {
    g_printerr ("ERROR: failed to retrieve bus from pipeline.\n");
    return FALSE;
  }

  // Add signal for bus
  gst_bus_add_signal_watch (bus);

  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_change_callback), appctx->pipeline);
  g_signal_connect (bus, "message::warning",
      G_CALLBACK (warning_callback), NULL);
  g_signal_connect (bus, "message::error",
      G_CALLBACK (error_callback), appctx->mloop);
  g_signal_connect (bus, "message::eos",
      G_CALLBACK (eos_callback), appctx->mloop);

  gst_object_unref (bus);

  return TRUE;
}

// Handler for CtrlC
static gboolean
interrupt_handler (gpointer userdata)
{
  GstAppContext* appctx = (GstAppContext*) userdata;
  GstState state = GST_STATE_NULL;
  gboolean ret = FALSE;

  // Set exit to true
  appctx->exit = TRUE;

  g_print ("\n\nReceived an interrupt signal, sending EOS...\n\n");

  // Check state of pipeline and send EOS only in PLAYING state
  gst_element_get_state (appctx->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  g_main_loop_quit (appctx->mloop);

  return TRUE;
}

// Configure meta of stream
static void
stream_meta_configure (MetaInfo* meta,
    const gint width, const gint height, const gint fps) {
  meta->width = width;
  meta->height = height;
  meta->framerate = fps;
}

// Create Preview stream
static gboolean
preview_stream_create (GstAppContext* appctx)
{
  GstPreviewStreamInfo* previewstream = NULL;
  GstElementClass* qtiqmmfsrc_klass = NULL;
  GstPadTemplate* qtiqmmfsrc_template = NULL;
  gboolean ret = FALSE;

  // Get qtiqmmfsrc element pad template
  qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (appctx->source);
  qtiqmmfsrc_template =
      gst_element_class_get_pad_template (qtiqmmfsrc_klass, "video_%u");

  // Create and link for preview stream
  previewstream = (GstPreviewStreamInfo*)(appctx->previewstream);
  g_print ("Create preview stream: %d x %d, %d fps\n",
      previewstream->meta.width, previewstream->meta.height,
      previewstream->meta.framerate);

  previewstream->qmmf_caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, DEFAULT_PREVIEWSTREAM_FORMAT,
      "width", G_TYPE_INT, previewstream->meta.width,
      "height", G_TYPE_INT, previewstream->meta.height,
      "framerate", GST_TYPE_FRACTION,
      previewstream->meta.framerate, DEFAULT_PREVIEWSTREAM_FPS_DENOMINATOR,
      NULL);
  gst_caps_set_features (previewstream->qmmf_caps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  previewstream->qmmf_pad = gst_element_request_pad (appctx->source,
      qtiqmmfsrc_template, "video_%u", previewstream->qmmf_caps);
  if (!previewstream->qmmf_pad) {
    g_printerr ("ERROR: failed to request a pad of preview stream.\n");
    return FALSE;
  }

  g_print ("Pad requested - %s\n", gst_pad_get_name (previewstream->qmmf_pad));

  // Create other elements of preview stream
  previewstream->capsfilter = gst_element_factory_make ("capsfilter", NULL);
  previewstream->displayer = gst_element_factory_make ("waylandsink", NULL);

  if (!previewstream->capsfilter || !previewstream->displayer) {
    g_printerr ("ERROR: elements in preview stream could not created.\n");
    return FALSE;
  }

  // Set properties of elements
  g_object_set (G_OBJECT (previewstream->qmmf_pad),
      "type", 1, NULL);

  g_object_set (G_OBJECT (previewstream->capsfilter),
      "caps", previewstream->qmmf_caps, NULL);

  g_object_set (G_OBJECT (previewstream->displayer), "sync", FALSE, NULL);

  // Add elements to bin
  gst_bin_add_many(GST_BIN (appctx->pipeline), appctx->source,
      previewstream->capsfilter, previewstream->displayer, NULL);

  // Link elements
  ret = gst_element_link_pads_full (
      appctx->source, gst_pad_get_name (previewstream->qmmf_pad),
      previewstream->capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);

  ret = gst_element_link_many (previewstream->capsfilter,
      previewstream->displayer, NULL);

  if (!ret) {
    g_printerr ("ERROR: failed to link preview stream.\n");
    return FALSE;
  }

  return TRUE;
}

// Create Video stream
static gboolean
video_stream_create (GstAppContext* appctx)
{
  GList* list = NULL;
  GstVideoStreamInfo* videostream = NULL;
  GstElementClass* qtiqmmfsrc_klass = NULL;
  GstPadTemplate* qtiqmmfsrc_template = NULL;
  gboolean ret = FALSE;
  gchar location[] = DEFAULT_VIDEOSTREAM_FILE_LOCATION;
  guint index = 0;

  // Get qtiqmmfsrc element pad template
  qtiqmmfsrc_klass = GST_ELEMENT_GET_CLASS (appctx->source);
  qtiqmmfsrc_template =
      gst_element_class_get_pad_template (qtiqmmfsrc_klass, "video_%u");

  for (list = appctx->vstreams_list; list != NULL; list = list->next) {
    videostream = (GstVideoStreamInfo*) (list->data);

    // Create and link for video stream
    g_print ("Create video stream: %d x %d, %d fps\n",
        videostream->meta.width, videostream->meta.height,
        videostream->meta.framerate);

    if (!frameselection_enabled)
      videostream->qmmf_caps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, DEFAULT_VIDEOSTREAM_FORMAT,
          "width", G_TYPE_INT, videostream->meta.width,
          "height", G_TYPE_INT, videostream->meta.height,
          "framerate", GST_TYPE_FRACTION,
          videostream->meta.framerate, DEFAULT_VIDEOSTREAM_FPS_DENOMINATOR,
          NULL);
    else
      videostream->qmmf_caps = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, DEFAULT_VIDEOSTREAM_FORMAT,
          "width", G_TYPE_INT, videostream->meta.width,
          "height", G_TYPE_INT, videostream->meta.height,
          "framerate", GST_TYPE_FRACTION, 0, 1,
          "max-framerate", GST_TYPE_FRACTION,
          videostream->meta.framerate, DEFAULT_VIDEOSTREAM_FPS_DENOMINATOR,
          NULL);

    gst_caps_set_features (videostream->qmmf_caps, 0,
        gst_caps_features_new ("memory:GBM", NULL));

    videostream->qmmf_pad = gst_element_request_pad (appctx->source,
        qtiqmmfsrc_template, "video_%u", videostream->qmmf_caps);
    if (!videostream->qmmf_pad) {
      g_printerr ("ERROR: failed to request a pad of video stream.\n");
      return FALSE;
    }

    g_print ("Pad requested - %s\n", gst_pad_get_name (videostream->qmmf_pad));

    // Create other elements of video stream
    videostream->capsfilter = gst_element_factory_make ("capsfilter", NULL);
    videostream->encoder = gst_element_factory_make ("qtic2venc", NULL);
    videostream->capsfilter_dfps = gst_element_factory_make ("capsfilter", NULL);
    videostream->parser = gst_element_factory_make ("h264parse", NULL);
    videostream->muxer = gst_element_factory_make ("mp4mux", NULL);
    videostream->filesinker = gst_element_factory_make ("filesink", NULL);

    if (!videostream->capsfilter || !videostream->encoder ||
        !videostream->parser || !videostream->muxer ||
        !videostream->filesinker) {
      g_printerr ("ERROR: elements in video stream could not created.\n");
      return FALSE;
    }

    // Set properties of elements
    g_object_set (G_OBJECT (videostream->capsfilter),
        "caps", videostream->qmmf_caps, NULL);

    snprintf (location, sizeof (location), DEFAULT_VIDEOSTREAM_FILE_LOCATION, index);
    ++index;
    g_object_set (G_OBJECT (videostream->filesinker),
        "location", location, NULL);

    g_object_set (G_OBJECT (videostream->encoder), "control-rate", 3, NULL);
    g_object_set (G_OBJECT (videostream->encoder), "target-bitrate", 30000000, NULL);
    g_object_set (G_OBJECT (videostream->encoder), "priority", 0, NULL);
    g_object_set (G_OBJECT (videostream->encoder), "min-quant-i-frames",
        30, NULL);
    g_object_set (G_OBJECT (videostream->encoder), "min-quant-p-frames",
        30, NULL);
    g_object_set (G_OBJECT (videostream->encoder), "max-quant-i-frames",
        51, NULL);
    g_object_set (G_OBJECT (videostream->encoder), "max-quant-p-frames",
        51, NULL);
    g_object_set (G_OBJECT (videostream->encoder), "quant-i-frames",
        30, NULL);
    g_object_set (G_OBJECT (videostream->encoder), "quant-p-frames",
        30, NULL);

    if (frameselection_enabled) {
      GstCaps* caps_dfps = NULL;

      caps_dfps = gst_caps_new_simple ("video/x-h264",
          "framerate", GST_TYPE_FRACTION,
          videostream->meta.framerate, DEFAULT_VIDEOSTREAM_FPS_DENOMINATOR,
          NULL);
      g_object_set (G_OBJECT (videostream->capsfilter_dfps),
          "caps", caps_dfps, NULL);
      gst_caps_unref (caps_dfps);
    }

    // Add elements to bin
    gst_bin_add_many (GST_BIN (appctx->pipeline),
        appctx->source, videostream->capsfilter,
        videostream->encoder, videostream->capsfilter_dfps,
        videostream->parser, videostream->muxer, videostream->filesinker, NULL);

    // Link elements
    ret = gst_element_link_pads_full (
        appctx->source, gst_pad_get_name (videostream->qmmf_pad),
        videostream->capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);

    ret = gst_element_link_many (videostream->capsfilter, videostream->encoder,
        videostream->capsfilter_dfps, videostream->parser, videostream->muxer,
        videostream->filesinker, NULL);

    if (!ret) {
      g_printerr ("ERROR: failed to link video stream.\n");
      return FALSE;
    }
  }

  return TRUE;
}

// Create streams and set to PAUSED to configure_streams once
static gboolean
streams_create (GstAppContext* appctx)
{
  // Create preview stream
  if (!preview_stream_create (appctx)) {
    g_printerr ("ERROR: failed to create preview stream.\n");
    return FALSE;
  }

  // Create video stream
  if (!video_stream_create (appctx)) {
    g_printerr ("ERROR: failed to create video stream.\n");
    return FALSE;
  }

  // Set pipeline to PAUSED state to configure_streams
  g_print ("Set pipeline to PAUSED state\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED);

  return TRUE;
}

// Delete streams
static void
streams_delete(GstAppContext* appctx) {
  GList* list = NULL;
  GstPreviewStreamInfo* previewstream = appctx->previewstream;

  for (list = appctx->vstreams_list; list != NULL; list = list->next) {
    GstVideoStreamInfo* videostream = (GstVideoStreamInfo*) (list->data);

    if (videostream->qmmf_pad)
      gst_element_release_request_pad (appctx->source, videostream->qmmf_pad);

    gst_bin_remove_many (GST_BIN (appctx->pipeline),
        videostream->capsfilter, videostream->encoder,
        videostream->capsfilter_dfps, videostream->parser,
        videostream->muxer, videostream->filesinker, NULL);
  }

  if (previewstream->qmmf_pad)
    gst_element_release_request_pad (appctx->source, previewstream->qmmf_pad);

  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      previewstream->capsfilter, previewstream->displayer, NULL);
}

// Function to switch operation mode
static gboolean
switch_func (gpointer userdata) {
  GstAppContext* appctx = (GstAppContext*) userdata;
  GList* list = NULL;
  GstState state_encoder = GST_STATE_NULL;
  static GstState state = GST_STATE_NULL;
  gboolean ret = FALSE;

  // Check exit
  if (appctx->exit)
    return FALSE;

  // Round of Switch to exit
  if (appctx->round != DEFAULT_ROUND && appctx->round >= 0) {
    --(appctx->round);
  } else if (appctx->round != DEFAULT_ROUND && appctx->round < 0) {
    interrupt_handler (appctx);
    return FALSE;
  }

  list = appctx->vstreams_list;
  // Check pad activation to link or unlink video stream
  if (gst_pad_is_active (((GstVideoStreamInfo*) (list->data))->qmmf_pad)) {
    g_print ("Preview + Video stream end.\n");

    for (list = appctx->vstreams_list; list != NULL; list = list->next) {
      GstVideoStreamInfo* videostream = (GstVideoStreamInfo*) (list->data);

      // Unlink video stream
      gst_element_unlink_many (appctx->source, videostream->capsfilter, NULL);

        // Send eos in PLAYING state
      gst_element_get_state (videostream->encoder,
          &state_encoder, NULL, GST_CLOCK_TIME_NONE);

      if (state_encoder == GST_STATE_PLAYING)
        gst_element_send_event (videostream->encoder, gst_event_new_eos ());

      gst_element_set_state (videostream->capsfilter, GST_STATE_NULL);
      gst_element_set_state (videostream->encoder, GST_STATE_NULL);
      gst_element_set_state (videostream->capsfilter_dfps, GST_STATE_NULL);
      gst_element_set_state (videostream->parser, GST_STATE_NULL);
      gst_element_set_state (videostream->muxer, GST_STATE_NULL);
      gst_element_set_state (videostream->filesinker, GST_STATE_NULL);

      gst_element_unlink_many (videostream->capsfilter, videostream->encoder,
          videostream->capsfilter_dfps, videostream->parser, videostream->muxer,
          videostream->filesinker, NULL);

        // Reference to keep usage after remove from bin
      gst_object_ref (videostream->capsfilter);
      gst_object_ref (videostream->encoder);
      gst_object_ref (videostream->capsfilter_dfps);
      gst_object_ref (videostream->parser);
      gst_object_ref (videostream->muxer);
      gst_object_ref (videostream->filesinker);

        // Remove from bin
      gst_bin_remove_many (GST_BIN (appctx->pipeline),
          videostream->capsfilter, videostream->encoder,
          videostream->capsfilter_dfps, videostream->parser,
          videostream->muxer, videostream->filesinker, NULL);

        // Deactivate the pad
      gst_pad_set_active (videostream->qmmf_pad, FALSE);
    }

    // Set to PLAYING if not
    if (state != GST_STATE_PLAYING) {
      gst_element_set_state (appctx->pipeline, GST_STATE_PLAYING);

      gst_element_get_state (appctx->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
      if (state != GST_STATE_PLAYING) {
        g_print ("ERROR: failed to set pipeline to PLAYING state.\n");
        return FALSE;
      }
    }

    g_print ("Preview stream start.\n");
  } else {
    g_print ("Preview stream end.\n");

    for (list = appctx->vstreams_list; list != NULL; list = list->next) {
      GstVideoStreamInfo* videostream = (GstVideoStreamInfo*) (list->data);

      // Link video stream
        // Activate the pad
      gst_pad_set_active (videostream->qmmf_pad, TRUE);

        // Add into bin
      gst_bin_add_many (GST_BIN (appctx->pipeline),
          videostream->capsfilter, videostream->encoder,
          videostream->capsfilter_dfps, videostream->parser,
          videostream->muxer, videostream->filesinker, NULL);

        // Sync state
      gst_element_sync_state_with_parent (videostream->capsfilter);
      gst_element_sync_state_with_parent (videostream->encoder);
      gst_element_sync_state_with_parent (videostream->capsfilter_dfps);
      gst_element_sync_state_with_parent (videostream->parser);
      gst_element_sync_state_with_parent (videostream->muxer);
      gst_element_sync_state_with_parent (videostream->filesinker);

        // Link elements
      ret = gst_element_link_pads_full (
          appctx->source, gst_pad_get_name (videostream->qmmf_pad),
          videostream->capsfilter, NULL, GST_PAD_LINK_CHECK_DEFAULT);

      if (!ret) {
        g_printerr ("ERROR: failed to link video pad.\n");
        return FALSE;
      }
      ret = gst_element_link_many (videostream->capsfilter,
          videostream->encoder, videostream->capsfilter_dfps,
          videostream->parser, videostream->muxer, videostream->filesinker,
          NULL);

      if (!ret) {
        g_printerr ("ERROR: failed to link video stream.\n");
        return FALSE;
      }
    }

    g_print ("Preview + Video stream start.\n");
  }

  return TRUE;
}

gint
main (gint argc, gchar* argv[])
{
  GstAppContext* appctx = NULL;
  guint interrupt = 0;
  GOptionContext* ctx = NULL;
  gint previewfps = DEFAULT_PREVIEWSTREAM_FPS_NUMERATOR;
  gint previewwidth = DEFAULT_PREVIEWSTREAM_WIDTH;
  gint previewheight = DEFAULT_PREVIEWSTREAM_HEIGHT;
  gint vnum = DEFAULT_VIDEOSTREAM_NUMBER;
  gint videowidth_1 = DEFAULT_VIDEOSTREAM_WIDTH;
  gint videoheight_1 = DEFAULT_VIDEOSTREAM_HEIGHT;
  gint videofps_1 = DEFAULT_VIDEOSTREAM_FPS_NUMERATOR;
  gint videowidth_2 = DEFAULT_VIDEOSTREAM_WIDTH;
  gint videoheight_2 = DEFAULT_VIDEOSTREAM_HEIGHT;
  gint videofps_2 = DEFAULT_VIDEOSTREAM_FPS_NUMERATOR;
  gint switchdelay = DEFAULT_SWITCH_DELAY;
  gint round = DEFAULT_ROUND;

  // Configure input parameters
  GOptionEntry entries[] = {
    { "pwidth", 0, 0, G_OPTION_ARG_INT,
      &previewwidth,
      "previewwidth",
      "Preview Stream Width"
    },
    { "pheight", 0, 0, G_OPTION_ARG_INT,
      &previewheight,
      "previewheight",
      "Preview Stream Height"
    },
    { "prate", 0, 0, G_OPTION_ARG_INT,
      &previewfps,
      "previewfps",
      "Preview Stream Framerate"
    },
    { "vnum", 0, 0, G_OPTION_ARG_INT,
      &vnum,
      "videonumber",
      "Video Stream Number"
    },
    { "v1width", 0, 0, G_OPTION_ARG_INT,
      &videowidth_1,
      "video1width",
      "Video Stream 1 Width"
    },
    { "v1height", 0, 0, G_OPTION_ARG_INT,
      &videoheight_1,
      "video1height",
      "Video Stream 1 Height"
    },
    { "v1rate", 0, 0, G_OPTION_ARG_INT,
      &videofps_1,
      "video1fps",
      "Video Stream 1 Framerate"
    },
    { "v2width", 0, 0, G_OPTION_ARG_INT,
      &videowidth_2,
      "video2width",
      "Video Stream 2 Width"
    },
    { "v2height", 0, 0, G_OPTION_ARG_INT,
      &videoheight_2,
      "video2height",
      "Video Stream 2 Height"
    },
    { "v2rate", 0, 0, G_OPTION_ARG_INT,
      &videofps_2,
      "video2fps",
      "Video Stream 2 Framerate"
    },
    { "switch_delay", 0, 0, G_OPTION_ARG_INT,
      &switchdelay,
      "switchdelay",
      "Switch Delay"
    },
    { "round", 0, 0, G_OPTION_ARG_INT,
      &round,
      "round",
      "Round to Switch"
    },
    { "op-mode", 0, 0, G_OPTION_ARG_STRING,
      &opmode,
      "opmode",
      "Operation mode of camera, interval with comma"
    },
    { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new ("DESCRIPTION")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    return -EFAULT;
  }

  // Check op-mode
  frameselection_enabled = (strstr (opmode, "frameselection")) ? TRUE : FALSE;
  g_print ("frameselection: %d\n", frameselection_enabled);


  // Init GST Library
  gst_init (&argc, &argv);

  appctx = g_new0 (GstAppContext, 1);
  if (!appctx) {
    g_printerr ("ERROR: failed to allocate AppContext.\n");
    return 0;
  }

  // Create GstAppContext
  if (!appcontext_create (appctx, vnum)) {
    g_printerr ("ERROR: failed to init GstAppContext.\n");
    goto cleanup;
  }

  // Add signals
  if (!signal_add (appctx))
    goto cleanup;

  // Register function to handle CtrlC (unix signal:SIGINT)
  interrupt = g_unix_signal_add (SIGINT, interrupt_handler, appctx);

  // Configure meta of stream
  stream_meta_configure (&appctx->previewstream->meta,
      previewwidth, previewheight, previewfps);
  if (vnum == 1) {
    GList* list = g_list_nth (appctx->vstreams_list, 0);
    stream_meta_configure (&(((GstVideoStreamInfo*)(list->data))->meta),
        videowidth_1, videoheight_1, videofps_1);
  } else if (vnum == 2) {
    GList* list = g_list_nth (appctx->vstreams_list, 0);
    stream_meta_configure (&(((GstVideoStreamInfo*)(list->data))->meta),
        videowidth_1, videoheight_1, videofps_1);
    list = g_list_nth (appctx->vstreams_list, 1);
    stream_meta_configure (&(((GstVideoStreamInfo*)(list->data))->meta),
        videowidth_2, videoheight_2, videofps_2);
  } else {
    g_print ("ERROR: wrong video stream number, Select between 1 or 2.\n");
    goto cleanup;
  }

  // Create Streams
  if (!streams_create (appctx)) {
    streams_delete (appctx);
    goto cleanup;
  }

  // Call once to skip the first round of delay
  appctx->round = round;
  switch_func (appctx);

  // Add function to switch
  g_timeout_add (switchdelay * 1000, switch_func, appctx);

  // Run main loop
  g_print ("g_main_loop_run starts\n");
  g_main_loop_run (appctx->mloop);
  g_print ("g_main_loop_run ends\n");

  // Set pipeline to NULL
  g_print ("Setting pipeline to NULL state.\n");
  gst_element_set_state (appctx->pipeline, GST_STATE_NULL);

  // Clean
  g_source_remove (interrupt);

cleanup:
  appcontext_delete (appctx);

  gst_deinit ();
  g_print ("Main: exit.\n");

  return 0;
}
