/*
* Copyright (c) 2023 - 2025 Qualcomm Innovation Center, Inc. All rights reserved.
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
* Description:
* Switch bewteen preview stream and preview + video stream.
*/

#include <stdio.h>

#include <gst/gst.h>
#include <gst/utils/common-utils.h>
#include <glib-unix.h>
#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

#ifdef G_HAVE_ISO_VARARGS
#define SWITCH_VERBOSE(...) do { \
  if (g_debug_level > 2) { \
    g_print ("[Debug] " __VA_ARGS__); \
  } \
} while (0)
#define SWITCH_DEBUG(...) do { \
  if (g_debug_level > 1) { \
    g_print ("[Debug] " __VA_ARGS__); \
  } \
} while (0)
#define SWITCH_INFO(...) do { \
  if (g_debug_level > 0) { \
    g_print ("[INFO] " __VA_ARGS__); \
  } \
} while (0)
#define SWITCH_MSG(...) do { \
  g_print ("[MSG] " __VA_ARGS__); \
} while (0)
#define SWITCH_ERROR(...) do { \
  g_printerr ("[ERROR] " __VA_ARGS__); \
} while (0)
#define SWITCH_GST_DEBUG(...) do { \
  if (g_debug_level > 0) { \
    GST_DEBUG ("[Debug]: " __VA_ARGS__); \
  } \
} while (0)
#else
#define SWITCH_VERBOSE(...)
#define SWITCH_DEBUG (...)
#define SWITCH_INFO(...)
#define SWITCH_MSG(...)
#define SWITCH_ERROR(...)
#define SWITCH_GST_DEBUG (...)
#endif

#define MAX_PREVIEW_STEAM_NUM       3
#define MAX_VIDEO_STREAM_NUM        3
#define STRING_SIZE                 64

// pipeline default params
#define DEFAULT_PIPELINE_CAMERA_ID      0
#define DEFAULT_PIPELINE_ROUND          10
#define DEFAULT_PIPELINE_DURATION       5
#define DEFAULT_PIPELINE_FRAMESELECTION FALSE
#define DEFAULT_PIPELINE_VIDEO_SYNC     FALSE
#define DEFAULT_PIPELINE_SENSOR_SWITCH  FALSE
#define DEFAULT_PIPELINE_SENSOR_NUM     2

#define PIPELINE_SENSOR_SWITCH_SHIFT_MS 1000
#define MENU_THREAD_MSG_EXIT            "Exit"
#define MENU_THREAD_MSG_EMPTY           ""

// stream default params
#define DEFAULT_PREVIEW_STREAM_WIDTH    1920
#define DEFAULT_PREVIEW_STREAM_HEIGHT   1080
#define DEFAULT_PREVIEW_STREAM_FPS      30
#define DEFAULT_VIDEO_STREAM_WIDTH      1920
#define DEFAULT_VIDEO_STREAM_HEIGHT     1080
#define DEFAULT_VIDEO_STREAM_FPS        30

#define DEFAULT_MULTI_DISPLAY_WIDTH     960
#define DEFAULT_MULTI_DISPLAY_HEIGHT    540

#define STREAM_TO_DISPLAY_STREAM_BIN(stream) (&((GstUnifiedSwitchStream *)stream)->bin.dbin)
#define STREAM_TO_FILE_STREAM_BIN(stream) (&((GstUnifiedSwitchStream *)stream)->bin.fbin)

#define IS_STREAM_ACTIVE(stream) ((stream->info.src_width != 0) && \
    (stream->info.src_height != 0) && (stream->info.src_fps != 0))

typedef struct _GstSwitchPipelineInfo       GstSwitchPipelineInfo;
typedef struct _GstSwitchStreamInfo         GstSwitchStreamInfo;
typedef struct _DisplayControl              DisplayControl;
typedef struct _GstDisplayBin               GstDisplayBin;
typedef struct _GstFileBin                  GstFileBin;
typedef struct _GstSwitchStream             GstSwitchStream;
typedef struct _GstUnifiedSwitchStream      GstUnifiedSwitchStream;
typedef struct _GstSwitchPipelineControl    GstSwitchPipelineControl;
typedef struct _GstSwitchPipeline           GstSwitchPipeline;
typedef struct _GstPropMenuInfo             GstPropMenuInfo;

typedef enum {
  GST_SWITCHSTREAM_TYPE_PREVIEW,
  GST_SWITCHSTREAM_TYPE_VIDEO,
} GstSwitchStreamType;

typedef enum {
  GST_STREAM_PIPELINE_DISPLAY,
  GST_STREAM_PIPELINE_FILE,
} GstStreamPipelineType;

typedef enum {
  CAM_OPMODE_NONE               = (1 << 0),
  CAM_OPMODE_FRAMESELECTION     = (1 << 1),
  CAM_OPMODE_FASTSWITCH         = (1 << 2),
} GstSwitchOpMode;

typedef enum {
  GST_QMMFSRC_VIDEO_PAD_TYPE_VIDEO    = 0,
  GST_QMMFSRC_VIDEO_PAD_TYPE_PREVIEW  = 1,
} GstQmmfsrcVideoPadType;

typedef enum {
  GST_SWITCH_RUN_MODE_PREVIEW,
  GST_SWITCH_RUN_MODE_PREVIEW_PLUS_VIDEO,
} GstSwitchRunMode;

typedef enum {
  GST_LOGICAL_CAMERA_MODE_NONE    = -1,
  GST_LOGICAL_CAMERA_MODE_SAT     = 0,
  GST_LOGICAL_CAMERA_MODE_RTB     = 1,
} GstLogCamMode;

typedef enum {
  GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MIN = 0,
  GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MAX = 15,
  GST_PAD_LOGICAL_STREAM_TYPE_SIDEBYSIDE,
  GST_PAD_LOGICAL_STREAM_TYPE_PANORAMA,
  GST_PAD_LOGICAL_STREAM_TYPE_NONE,
  GST_PAD_LOGICAL_STREAM_TYPE_MAX,
} GstPadLogicalStreamType;

typedef enum {
  GST_CAMERA_STREAM_FORMAT_NV12 = 0,
  GST_CAMERA_STREAM_FORMAT_P010_10LE,
  GST_CAMERA_STREAM_FORMAT_MAX,
} GstCameraStreamFormat;

typedef enum {
  GST_PAD_ACTIVATION_MODE_NORMAL = 0,
  GST_PAD_ACTIVATION_MODE_SIGNAL = 1,
} GstQmmfSrcPadActivationMode;

const gchar *CameraStreamMaps[] = {
  [GST_CAMERA_STREAM_FORMAT_NV12] = "NV12",
  [GST_CAMERA_STREAM_FORMAT_P010_10LE] = "P010_10LE",
};

struct _GstSwitchPipelineInfo {
  gint                  camera_id;
  gint                  round;
  gint                  duration;
  gboolean              frameselection;
  gboolean              video_sync;
  gboolean              sensor_switch;
  gint                  sensor_num;
  GstLogCamMode         log_cam_mode;
  gboolean              menu;
  GOptionEntry          *options;
};

struct _GstSwitchStreamInfo {
  gchar                 name[STRING_SIZE];
  GstSwitchStreamType   stype;
  GstStreamPipelineType ptype;
  gint                  src_width;
  gint                  src_height;
  gint                  src_fps;
  GOptionEntry          *options;
  gint                  option_num;
  gint                  phy_cam_idx;
  gboolean              sbs;
  GstCameraStreamFormat cam_stream_format;
  gboolean              ubwc;
};

struct _DisplayControl {
  gboolean    fullscreen;
  gint        x;
  gint        y;
  gint        width;
  gint        height;
};

struct _GstDisplayBin {
  GstPad      *camera_pad;
  GstElement  *camera_capsfilter;
  GstElement  *display;

  DisplayControl display_control;
};

struct _GstFileBin {
  GstPad        *camera_pad;
  GstElement    *camera_capsfilter;
  GstElement    *encoder;
  GstElement    *encoder_capsfilter;
  GstElement    *h264parser;
  GstElement    *mp4mux;
  GstElement    *filesink;
};

struct _GstSwitchStream {
  GstSwitchStreamInfo   info;
  GstSwitchPipeline     *pipeline;
  gint                  index;
  gboolean              linked;
};

struct _GstUnifiedSwitchStream {
  GstSwitchStream basic;
  union {
    GstDisplayBin dbin;
    GstFileBin fbin;
  } bin;
};

struct _GstPropMenuInfo {
  GAsyncQueue *queue;
  GIOChannel  *iochannel_stdin;
  guint       watch_stdin;
  GstElement  *camera;
};

struct _GstSwitchPipelineControl {
  GMainLoop           *mloop;
  gint                current_round;
  gboolean            exit;
  GstSwitchRunMode    mode;
  GThread             *thread_menu;
  GAsyncQueue         *menu_msgs;

  gint                sensor_switch_index;
  gint                sensor_switch_duration_ms;

  guint               unix_signal;

  GstElement          *pipeline;
  GstElement          *camera;

  gint                pstream_num;
  GstSwitchStream     *preview_streams[MAX_PREVIEW_STEAM_NUM];

  gint                vstream_num;
  GstSwitchStream     *video_streams[MAX_PREVIEW_STEAM_NUM];
};

struct _GstSwitchPipeline {
  GstSwitchPipelineInfo       info;
  GstSwitchPipelineControl    control;
};

gint g_debug_level = 0;
GOptionEntry debug_option[] = {
  { "log", 0, 0, G_OPTION_ARG_INT,
    &g_debug_level,
    "log level, default 0, info=1, debug=2",
    NULL
  },
  { NULL }
};

GstSwitchPipeline *
pipeline_alloc (void)
{
  GstSwitchPipeline *ret = NULL;

  ret = g_new0 (GstSwitchPipeline, 1);

  if (ret != NULL) {
    ret->info.camera_id       = DEFAULT_PIPELINE_CAMERA_ID;
    ret->info.round           = DEFAULT_PIPELINE_ROUND;
    ret->info.duration        = DEFAULT_PIPELINE_DURATION;
    ret->info.frameselection  = DEFAULT_PIPELINE_FRAMESELECTION;
    ret->info.video_sync      = DEFAULT_PIPELINE_VIDEO_SYNC;
    ret->info.sensor_switch   = DEFAULT_PIPELINE_SENSOR_SWITCH;
    ret->info.sensor_num      = DEFAULT_PIPELINE_SENSOR_NUM;
    ret->info.log_cam_mode    = GST_LOGICAL_CAMERA_MODE_NONE;

    SWITCH_DEBUG ("alloc pipeline success\n");
  }

  return ret;
}

void
pipeline_free (GstSwitchPipeline *pipeline)
{
  g_return_if_fail (pipeline != NULL);

  g_free (pipeline);
}

GOptionEntry *
pipeline_alloc_options (GstSwitchPipeline *pipeline)
{
  GOptionEntry *entries;

  // allocate option number + 1, last one should be { NULL }
  entries = g_new0 (GOptionEntry, 10);

  if (entries != NULL) {
    entries[0].long_name = "cameraid";
    entries[0].short_name = 'c';
    entries[0].arg = G_OPTION_ARG_INT;
    entries[0].arg_data = (gpointer) &pipeline->info.camera_id;
    entries[0].description = "camera id";

    entries[1].long_name = "round";
    entries[1].short_name = 'r';
    entries[1].arg = G_OPTION_ARG_INT;
    entries[1].arg_data = (gpointer) &pipeline->info.round;
    entries[1].description = "switch round";

    entries[2].long_name = "duration";
    entries[2].short_name = 'd';
    entries[2].arg = G_OPTION_ARG_INT;
    entries[2].arg_data = (gpointer) &pipeline->info.duration;
    entries[2].description = "duration (seconds) for each streaming ";

    entries[3].long_name = "frameselection";
    entries[3].short_name = 'f';
    entries[3].arg = G_OPTION_ARG_NONE;
    entries[3].arg_data = (gpointer) &pipeline->info.frameselection;
    entries[3].description = "enable frameselection";

    entries[4].long_name = "video-sync";
    entries[4].short_name = 'v';
    entries[4].arg = G_OPTION_ARG_NONE;
    entries[4].arg_data = (gpointer) &pipeline->info.video_sync;
    entries[4].description = "video streams start / stop sync";

    entries[5].long_name = "sensor-switch";
    entries[5].short_name = 's';
    entries[5].arg = G_OPTION_ARG_NONE;
    entries[5].arg_data = (gpointer) &pipeline->info.sensor_switch;
    entries[5].description = "sensor switch in SAT mode for logical camera";

    entries[6].long_name = "sensor-num";
    entries[6].short_name = 'n';
    entries[6].arg = G_OPTION_ARG_INT;
    entries[6].arg_data = (gpointer) &pipeline->info.sensor_num;
    entries[6].description = "sensor num in SAT mode for logical camera";

    entries[7].long_name = "logical-camera-mode";
    entries[7].short_name = 'l';
    entries[7].arg = G_OPTION_ARG_INT;
    entries[7].arg_data = (gpointer) &pipeline->info.log_cam_mode;
    entries[7].description = "logical camera mode, 0=SAT, 1=RTB, default none";

    entries[8].long_name = "property-menu";
    entries[8].short_name = 'm';
    entries[8].arg = G_OPTION_ARG_NONE;
    entries[8].arg_data = (gpointer) &pipeline->info.menu;
    entries[8].description = "menu to set camera's dynamic properties";

    pipeline->info.options = entries;
  }

  return entries;
}

void
pipeline_free_options (GstSwitchPipeline *pipeline)
{
  g_return_if_fail (pipeline != NULL);
  g_return_if_fail (pipeline->info.options != NULL);

  g_free (pipeline->info.options);
  pipeline->info.options = NULL;
}

GstSwitchStream *
switchstream_alloc (GstSwitchPipeline *pipeline,
                    GstSwitchStreamType stype)
{
  GstSwitchStream *ret = NULL;
  GstSwitchPipelineControl *pcontrol = &pipeline->control;
  GstSwitchPipelineInfo *pinfo = &pipeline->info;

  ret = (GstSwitchStream *)g_new0 (GstUnifiedSwitchStream, 1);
  ret->info.ptype = (stype == GST_SWITCHSTREAM_TYPE_VIDEO) ?
    (GST_STREAM_PIPELINE_FILE) : (GST_STREAM_PIPELINE_DISPLAY);

  g_return_val_if_fail (ret != NULL, NULL);

  ret->pipeline = pipeline;
  ret->info.stype = stype;
  ret->info.phy_cam_idx = -1;
  ret->info.sbs = FALSE;
  ret->info.cam_stream_format = GST_CAMERA_STREAM_FORMAT_NV12;
  ret->info.ubwc = FALSE;

  if (stype == GST_SWITCHSTREAM_TYPE_VIDEO) {
    g_snprintf(ret->info.name, STRING_SIZE, "v%d",
        pcontrol->vstream_num + 1);
    ret->index = pcontrol->vstream_num;
    pcontrol->video_streams[pcontrol->vstream_num++] = ret;

    // in this example, at least one video stream is required.
    // so generate default stream configuration for video stream 0
    if (ret->index == 0) {
      ret->info.src_width = DEFAULT_VIDEO_STREAM_WIDTH;
      ret->info.src_height = DEFAULT_VIDEO_STREAM_HEIGHT;
      ret->info.src_fps = DEFAULT_VIDEO_STREAM_FPS;
    }
  } else {
    g_snprintf(ret->info.name, STRING_SIZE, "p%d",
        pcontrol->pstream_num + 1);
    ret->index = pcontrol->pstream_num;
    pcontrol->preview_streams[pcontrol->pstream_num++] = ret;

    // in this example, at least one preview stream is required.
    // so generate default stream configuration for preview stream 0
    if (ret->index == 0) {
      ret->info.src_width = DEFAULT_PREVIEW_STREAM_WIDTH;
      ret->info.src_height = DEFAULT_PREVIEW_STREAM_HEIGHT;
      ret->info.src_fps = DEFAULT_PREVIEW_STREAM_FPS;
    }
  }

  SWITCH_DEBUG ("alloc stream stype(%d) ptype(%d) index(%d) success\n",
      ret->info.stype, ret->info.ptype, ret->index);

  return ret;
}

void
switchstream_free (GstSwitchStream *stream)
{
  GstSwitchPipeline *pipeline = NULL;
  GstSwitchPipelineControl *pcontrol = NULL;
  gint stream_num;

  g_return_if_fail (stream != NULL);
  g_return_if_fail (stream->pipeline);

  SWITCH_DEBUG ("free stream stype(%d) ptype(%d) idx(%d)\n",
      stream->info.stype, stream->info.ptype, stream->index);

  pipeline = stream->pipeline;
  pcontrol = &pipeline->control;

  if (stream->info.stype == GST_SWITCHSTREAM_TYPE_VIDEO)
    pcontrol->video_streams[stream->index] = NULL;
  else
    pcontrol->preview_streams[stream->index] = NULL;

  g_free (stream);
}

GOptionEntry *
switchstream_alloc_options (GstSwitchStream *stream)
{
  GOptionEntry *entries = NULL;
  GstSwitchStreamInfo *info = &stream->info;
  // allocate option number + 1, keep last one { NULL }
  gint option_num = 6;
  gint option_idx = 0;

  // preview stream can choose to display on screen or store to file
  if (stream->index != 0 && info->stype == GST_SWITCHSTREAM_TYPE_PREVIEW)
    option_num++;

  // video stream can be part of logical camera stream, so physical camera id
  // and side-by-side will be enabled
  if (info->stype == GST_SWITCHSTREAM_TYPE_VIDEO)
    option_num += 2;

  entries = g_new0 (GOptionEntry, option_num);

  g_return_val_if_fail (entries != NULL, NULL);

  entries[option_idx].long_name = g_strdup_printf ("%swidth", info->name);
  entries[option_idx].arg = G_OPTION_ARG_INT;
  entries[option_idx].arg_data = (gpointer) &info->src_width;
  entries[option_idx].description = g_strdup_printf ("%s stream width", info->name);
  option_idx++;

  entries[option_idx].long_name = g_strdup_printf ("%sheight", info->name);
  entries[option_idx].arg = G_OPTION_ARG_INT;
  entries[option_idx].arg_data = (gpointer) &info->src_height;
  entries[option_idx].description = g_strdup_printf ("%s stream height", info->name);
  option_idx++;

  entries[option_idx].long_name = g_strdup_printf ("%sfps", info->name);
  entries[option_idx].arg = G_OPTION_ARG_INT;
  entries[option_idx].arg_data = (gpointer) &info->src_fps;
  entries[option_idx].description = g_strdup_printf ("%s stream fps", info->name);
  option_idx++;

  entries[option_idx].long_name = g_strdup_printf ("%sformat", info->name);
  entries[option_idx].arg = G_OPTION_ARG_INT;
  entries[option_idx].arg_data = (gpointer) &info->cam_stream_format;
  entries[option_idx].description = g_strdup_printf ("%s stream camera format "
      "0:NV12, 1:P010_10LE, default 0", info->name);
  option_idx++;

  entries[option_idx].long_name = g_strdup_printf ("%s-ubwc", info->name);
  entries[option_idx].arg = G_OPTION_ARG_NONE;
  entries[option_idx].arg_data = (gpointer) &info->ubwc;
  entries[option_idx].description = g_strdup_printf ("%s enable ubwc compression",
      info->name);
  option_idx++;

  if (stream->index != 0 && info->stype == GST_SWITCHSTREAM_TYPE_PREVIEW) {
    entries[option_idx].long_name = g_strdup_printf ("%sptype", info->name);
    entries[option_idx].arg = G_OPTION_ARG_INT;
    entries[option_idx].arg_data = (gpointer) &info->ptype;
    entries[option_idx].description = g_strdup_printf ("%s pipeline type,"
        " 0=display, 1=encode to file, default 0", info->name);
    option_idx++;
  }

  if (info->stype == GST_SWITCHSTREAM_TYPE_VIDEO) {
    entries[option_idx].long_name = g_strdup_printf ("%s-cam-idx", info->name);
    entries[option_idx].arg = G_OPTION_ARG_INT;
    entries[option_idx].arg_data = (gpointer) &info->phy_cam_idx;
    entries[option_idx].description = g_strdup_printf ("%s physical camera id "
        "attached to this stream, default -1", info->name);
    option_idx++;

    entries[option_idx].long_name = g_strdup_printf ("%s-sbs", info->name);
    entries[option_idx].arg = G_OPTION_ARG_NONE;
    entries[option_idx].arg_data = (gpointer) &info->sbs;
    entries[option_idx].description = g_strdup_printf ("%s side by side stream "
        "default 0 (false)", info->name);
    option_idx++;
  }

  info->options = entries;
  info->option_num = option_idx;

  return entries;
}

void
switchstream_free_options (GstSwitchStream *stream)
{
  gint option_idx;
  GstSwitchStreamInfo *info = NULL;
  GOptionEntry *entries = NULL;

  g_return_if_fail (stream != NULL);
  g_return_if_fail (stream->info.options != NULL);

  info = &stream->info;
  entries = info->options;

  for (option_idx = 0; option_idx < info->option_num; option_idx++) {
    g_free ((gpointer)entries[option_idx].long_name);
    g_free ((gpointer)entries[option_idx].description);
  }

  g_free (entries);
  info->options = NULL;
}

void
pipeline_streams_free (GstSwitchPipeline *pipeline)
{
  gint index;
  GstSwitchPipelineControl *pcontrol = &pipeline->control;

  g_return_if_fail (pipeline != NULL);

  SWITCH_DEBUG ("free streams and pipeline\n");

  for (index = 0; index < pcontrol->pstream_num; index++)
    switchstream_free (pcontrol->preview_streams[index]);

  for (index = 0; index < pcontrol->vstream_num; index++)
    switchstream_free (pcontrol->video_streams[index]);

  g_free (pipeline);
}

GstSwitchPipeline *
pipeline_streams_alloc (gint pnum, gint vnum)
{
  GstSwitchPipeline *ret = NULL;
  gint i;

  ret = pipeline_alloc ();
  g_return_val_if_fail (ret != NULL, NULL);

  for (i = 0; i < pnum; i++) {
    if (switchstream_alloc (ret, GST_SWITCHSTREAM_TYPE_PREVIEW) == NULL) {
      pipeline_streams_free (ret);
      return NULL;
    }
  }

  for (i = 0; i < vnum; i++) {
    if (switchstream_alloc (ret, GST_SWITCHSTREAM_TYPE_VIDEO) == NULL) {
      pipeline_streams_free (ret);
      return NULL;
    }
  }

  return ret;
}

gboolean
pipeline_streams_alloc_options (GOptionContext *ctx,
                                GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol = NULL;
  GOptionEntry *entries = NULL;
  gint index;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (pipeline != NULL, FALSE);

  pcontrol = &pipeline->control;
  entries = pipeline_alloc_options (pipeline);
  if (entries != NULL)
    g_option_context_add_main_entries (ctx, entries, NULL);

  for (index = 0; index < pcontrol->pstream_num; index++) {
    entries = switchstream_alloc_options (pcontrol->preview_streams[index]);
    if (entries != NULL)
      g_option_context_add_main_entries (ctx, entries, NULL);
  }

  for (index = 0; index < pcontrol->vstream_num; index++) {
    entries = switchstream_alloc_options (pcontrol->video_streams[index]);
    if (entries != NULL)
      g_option_context_add_main_entries (ctx, entries, NULL);
  }

  return TRUE;
}

void
pipeline_streams_free_options (GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol = NULL;
  gint index;

  g_return_if_fail (pipeline != NULL);

  pcontrol = &pipeline->control;

  for (index = 0; index < pcontrol->pstream_num; index++)
    switchstream_free_options(pcontrol->preview_streams[index]);

  for (index = 0; index < pcontrol->vstream_num; index++)
    switchstream_free_options(pcontrol->video_streams[index]);

  pipeline_free_options (pipeline);
}

gboolean
check_pipeline_streams_options (GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol;
  GstSwitchPipelineInfo *pinfo;
  gint valid_pstreams, valid_vstreams;
  gint display_pipeline_num;

  g_return_val_if_fail (pipeline != NULL, FALSE);
  pcontrol = &pipeline->control;
  pinfo = &pipeline->info;

  SWITCH_MSG ("***************************************************\n");

  SWITCH_MSG ("general options: camera(%d) round(%d) duration(%d)\n",
      pinfo->camera_id, pinfo->round, pinfo->duration);

  SWITCH_MSG ("general options: frameselection(%d) video-sync(%d)\n",
      pinfo->frameselection, pinfo->video_sync);

  SWITCH_MSG ("general options: sensor-switch(%d) logical camera mode (%d)\n",
      pinfo->sensor_switch, pinfo->log_cam_mode);

  valid_pstreams = valid_vstreams = 0;
  display_pipeline_num = 0;

  for (int i = 0; i < pcontrol->pstream_num; i++) {
    GstSwitchStream *stream = pcontrol->preview_streams[i];

    if (IS_STREAM_ACTIVE (stream)) {
      SWITCH_MSG ("\n");
      SWITCH_MSG ("preview stream index(%d) options:\n", stream->index);

      SWITCH_MSG ("\twidth(%d) height(%d) fps(%d) pipeline(%s)\n",
          stream->info.src_width, stream->info.src_height, stream->info.src_fps,
          stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY ? "display" : "file");

      SWITCH_MSG ("\tstream format(%s) ubwc(%d)\n",
          CameraStreamMaps[stream->info.cam_stream_format], stream->info.ubwc);
      valid_pstreams++;

      if (stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY) {
        GstDisplayBin *dbin = STREAM_TO_DISPLAY_STREAM_BIN (stream);

        dbin->display_control.fullscreen = TRUE;
        dbin->display_control.x = 0;
        dbin->display_control.y = 0;
        dbin->display_control.width = 0;
        dbin->display_control.height = 0;

        display_pipeline_num++;
      }
    }
  }

  for (int i = 0; i < pcontrol->vstream_num; i++) {
    GstSwitchStream *stream = pcontrol->video_streams[i];

    if (IS_STREAM_ACTIVE (stream)) {
      SWITCH_MSG ("\n");
      SWITCH_MSG ("video stream index(%d) options:\n", stream->index);

      SWITCH_MSG ("\twidth(%d) height(%d) fps(%d) pipeline(%s)\n",
          stream->info.src_width, stream->info.src_height, stream->info.src_fps,
          stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY ? "display" : "file");

      SWITCH_MSG ("\tstream format(%s) ubwc(%d)\n",
          CameraStreamMaps[stream->info.cam_stream_format], stream->info.ubwc);

      SWITCH_MSG ("\tphy-cam-id(%d) side-by-side(%d)\n",
          stream->info.phy_cam_idx, stream->info.sbs);

      if (stream->info.phy_cam_idx != -1 && stream->info.sbs == TRUE) {
        SWITCH_ERROR ("video stream index (%d) can not have both physical"
            " camera id and side-by-side set\n", stream->index);

        return FALSE;
      }

      valid_vstreams++;
    }
  }

  SWITCH_MSG ("\n");
  SWITCH_MSG ("valid preview streams (%d) valid video streams (%d)\n",
      valid_pstreams, valid_vstreams);

  if (display_pipeline_num > 1) {
    gint last_y = 0;

    for (int i = 0; i < pcontrol->pstream_num; i++) {
      GstSwitchStream *stream = pcontrol->preview_streams[i];

      if (IS_STREAM_ACTIVE (stream) &&
          (stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY)) {
        GstDisplayBin *dbin = STREAM_TO_DISPLAY_STREAM_BIN (stream);

        dbin->display_control.fullscreen = FALSE;
        dbin->display_control.x = 0;
        dbin->display_control.y = last_y;
        dbin->display_control.width = DEFAULT_MULTI_DISPLAY_WIDTH;
        dbin->display_control.height = DEFAULT_MULTI_DISPLAY_HEIGHT;

        last_y = last_y + dbin->display_control.height;

        SWITCH_MSG ("\n");
        SWITCH_MSG ("preview stream index(%d) display params:\n",
            stream->index);

        SWITCH_MSG ("\tx(%d) y(%d) width(%d) height(%d)\n",
            dbin->display_control.x,
            dbin->display_control.y,
            dbin->display_control.width,
            dbin->display_control.height);
      }
    }
  }

  if (pinfo->sensor_switch) {
    if ((pinfo->sensor_num * 1000 + PIPELINE_SENSOR_SWITCH_SHIFT_MS) >
        (pinfo->duration * 1000)) {
      SWITCH_ERROR ("duration is too short for sensor switch\n");

      return FALSE;
    }

    pcontrol->sensor_switch_index = 0;
    pcontrol->sensor_switch_duration_ms =
      ((pinfo->duration * 1000 - PIPELINE_SENSOR_SWITCH_SHIFT_MS) / pinfo->sensor_num);

    SWITCH_MSG ("\n");
    SWITCH_MSG ("sensor switch enabled:\n");
    SWITCH_MSG ("\tsensor num(%d) switch duration (%d)ms\n",
        pinfo->sensor_num, pcontrol->sensor_switch_duration_ms);
  }

  SWITCH_MSG ("\n");
  SWITCH_MSG ("***************************************************\n");

  if ((valid_pstreams > 0) && (valid_vstreams > 0))
    return TRUE;
  else
    return FALSE;
}

gboolean
pipeline_init (GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol = NULL;
  GstSwitchPipelineInfo *pinfo = NULL;
  gint opmode = CAM_OPMODE_FASTSWITCH;

  g_return_val_if_fail (pipeline != NULL, FALSE);
  pcontrol = &pipeline->control;
  pinfo = &pipeline->info;

  pcontrol->mloop = g_main_loop_new (NULL, FALSE);
  g_return_val_if_fail (pcontrol->mloop != NULL, FALSE);

  pcontrol->pipeline = gst_pipeline_new ("gst-fastswitch-example");
  if (pcontrol->pipeline == NULL) {
    SWITCH_ERROR ("creating gst pipeline failed\n");
    goto free_pipeline;
  }

  pcontrol->camera = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  if (pcontrol->camera == NULL) {
    SWITCH_ERROR ("creating gst camera plugin failed\n");
    goto free_pipeline;
  }

  if (pinfo->frameselection)
    opmode |= CAM_OPMODE_FRAMESELECTION;

  // configure camera plugin
  g_object_set (G_OBJECT (pcontrol->camera),
      "camera", pinfo->camera_id,
      "op-mode", opmode,
      "video-pads-activation-mode", pinfo->video_sync ?
        GST_PAD_ACTIVATION_MODE_SIGNAL : GST_PAD_ACTIVATION_MODE_NORMAL,
      NULL);

  if (! gst_bin_add (GST_BIN (pcontrol->pipeline), pcontrol->camera)) {
    SWITCH_ERROR ("failed to add camera to pipeline.\n");
    goto free_pipeline;
  }
  pcontrol->current_round = 0;

  SWITCH_DEBUG ("pipeline create succesfully, add camera into pipeline\n");

  return TRUE;

free_pipeline:
  if (pcontrol->camera) {
    gst_object_unref (pcontrol->camera);
    pcontrol->camera = NULL;
  }

  if (pcontrol->pipeline) {
    gst_object_unref (pcontrol->pipeline);
    pcontrol->pipeline = NULL;
  }

  if (pcontrol->mloop) {
    g_main_loop_unref (pcontrol->mloop);
    pcontrol->mloop = NULL;
  }

  return FALSE;
}

void
pipeline_deinit (GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol = NULL;
  GstSwitchPipelineInfo *pinfo = NULL;

  g_return_if_fail (pipeline != NULL);
  pcontrol = &pipeline->control;
  pinfo = &pipeline->info;

  // remove element will unref it
  gst_bin_remove_many (GST_BIN (pcontrol->pipeline), pcontrol->camera, NULL);
  pcontrol->camera = NULL;

  gst_object_unref (pcontrol->pipeline);
  pcontrol->pipeline = NULL;

  g_main_loop_unref (pcontrol->mloop);
  pcontrol->mloop = NULL;
}

gboolean
switchstream_display_init (GstUnifiedSwitchStream *ustream)
{
  GstSwitchPipeline *pipeline = NULL;
  GstSwitchPipelineControl *pcontrol = NULL;
  GstSwitchPipelineInfo *pinfo = NULL;
  GstSwitchStream *stream = NULL;
  GstSwitchStreamInfo *sinfo = NULL;
  GstDisplayBin *dbin = NULL;
  GstCaps *caps = NULL;

  g_return_val_if_fail (ustream != NULL, FALSE);
  stream = &ustream->basic;
  pipeline = stream->pipeline;
  pcontrol = &pipeline->control;
  pinfo = &pipeline->info;
  sinfo = &stream->info;
  dbin = &ustream->bin.dbin;

  // only preview stream will use display pipeline
  g_assert (sinfo->stype == GST_SWITCHSTREAM_TYPE_PREVIEW);

  dbin->camera_pad = gst_element_request_pad (pcontrol->camera,
      gst_element_class_get_pad_template(
          GST_ELEMENT_GET_CLASS (pcontrol->camera),
          "video_%u"),
      "video_%u",
      NULL);

  if (dbin->camera_pad == NULL) {
    SWITCH_ERROR ("request pad for stream(stype:%d ptype:%d index:%d) failed\n",
        sinfo->stype, sinfo->ptype, stream->index);

    return FALSE;
  }

  SWITCH_INFO ("request pad %s for stream(stype:%d ptype:%d index:%d)\n",
      GST_PAD_NAME (dbin->camera_pad), sinfo->stype,
      sinfo->ptype, stream->index);

  g_object_set (dbin->camera_pad, "type",
      GST_QMMFSRC_VIDEO_PAD_TYPE_PREVIEW, NULL);

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, CameraStreamMaps[sinfo->cam_stream_format],
      "width", G_TYPE_INT, sinfo->src_width,
      "height", G_TYPE_INT, sinfo->src_height,
      "framerate", GST_TYPE_FRACTION, sinfo->src_fps, 1,
      NULL);

  if (sinfo->ubwc == TRUE)
    gst_caps_set_simple (caps, "compression", G_TYPE_STRING, "ubwc", NULL);

  gst_caps_set_features (caps, 0, gst_caps_features_new ("memory:GBM", NULL));

  dbin->camera_capsfilter = gst_element_factory_make ("capsfilter", NULL);
  if (dbin->camera_capsfilter == NULL) {
    SWITCH_ERROR ("create caps filter for stream(stype:%d ptype:%d index:%d) "
        "failed\n", sinfo->stype, sinfo->ptype, stream->index);
    goto free_dbin;
  }
  g_object_set (G_OBJECT (dbin->camera_capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  dbin->display = gst_element_factory_make ("waylandsink", NULL);
  if (dbin->display == NULL) {
    SWITCH_ERROR ("create waylandsink for stream(stype:%d ptype:%d index:%d) "
        "failed\n", sinfo->stype, sinfo->ptype, stream->index);
    goto free_dbin;
  }
  g_object_set (G_OBJECT (dbin->display), "sync", FALSE, NULL);

  if (dbin->display_control.fullscreen == FALSE)
    g_object_set (G_OBJECT (dbin->display),
        "x", dbin->display_control.x,
        "y", dbin->display_control.y,
        "width", dbin->display_control.width,
        "height", dbin->display_control.height,
        NULL);
  else
    g_object_set (G_OBJECT (dbin->display),
        "fullscreen", TRUE,
        NULL);

  gst_bin_add_many (GST_BIN (pcontrol->pipeline),
      dbin->camera_capsfilter, dbin->display, NULL);

  if (!gst_element_link_many (dbin->camera_capsfilter, dbin->display, NULL)) {
    SWITCH_ERROR ("link stream (stype:%d ptype:%d index:%d) elements failed\n",
        sinfo->stype, sinfo->ptype, stream->index);
    goto remove_dbin;
  }

  SWITCH_INFO ("stream (stype:%d ptype:%d index:%d) "
      "init ,add to bin and link succesfully\n",
      sinfo->stype, sinfo->ptype, stream->index);

  return TRUE;

remove_dbin:
  // remove will unref elements
  gst_bin_remove_many (GST_BIN (pipeline->control.pipeline),
      dbin->camera_capsfilter,
      dbin->display,
      NULL);
  dbin->camera_capsfilter = NULL;
  dbin->display = NULL;

  gst_element_release_request_pad (pcontrol->camera, dbin->camera_pad);
  dbin->camera_pad = NULL;

  return FALSE;

free_dbin:
  if (dbin->display != NULL) {
    gst_object_unref (dbin->display);
    dbin->display = NULL;
  }

  if (dbin->camera_capsfilter != NULL) {
    gst_object_unref (dbin->camera_capsfilter);
    dbin->camera_capsfilter = NULL;
  }

  if (dbin->camera_pad != NULL) {
    gst_element_release_request_pad (pcontrol->camera, dbin->camera_pad);
    dbin->camera_pad = NULL;
  }

  return FALSE;
}

void
switchstream_display_deinit (GstUnifiedSwitchStream *ustream)
{
  GstDisplayBin *dbin = NULL;
  GstSwitchPipeline *pipeline = NULL;
  GstSwitchPipelineControl *pcontrol = NULL;

  g_return_if_fail (ustream != NULL);
  dbin = &ustream->bin.dbin;
  pipeline = ustream->basic.pipeline;
  pcontrol = &pipeline->control;

  if (dbin->camera_capsfilter == NULL) {
    SWITCH_DEBUG ("stream stype(%d) ptype(%d) index(%d) already deinit\n",
        ustream->basic.info.stype,
        ustream->basic.info.ptype,
        ustream->basic.index);

    return;
  }

  gst_element_unlink_many (dbin->camera_capsfilter, dbin->display, NULL);

  // remove will unref elements
  gst_bin_remove_many (GST_BIN (pipeline->control.pipeline),
      dbin->camera_capsfilter,
      dbin->display,
      NULL);
  dbin->camera_capsfilter = NULL;
  dbin->display = NULL;

  gst_element_release_request_pad (pcontrol->camera, dbin->camera_pad);
  dbin->camera_pad = NULL;
}

gboolean
switchstream_file_init (GstUnifiedSwitchStream *ustream)
{
  GstSwitchPipeline *pipeline = NULL;
  GstSwitchPipelineControl *pcontrol = NULL;
  GstSwitchPipelineInfo *pinfo = NULL;
  GstSwitchStream *stream = NULL;
  GstSwitchStreamInfo *sinfo = NULL;
  GstFileBin *fbin = NULL;
  GstCaps *caps = NULL;
  gchar *location = NULL;

  g_return_val_if_fail (ustream != NULL, FALSE);
  stream = &ustream->basic;
  pipeline = stream->pipeline;
  pcontrol = &pipeline->control;
  pinfo = &pipeline->info;
  sinfo = &stream->info;
  fbin = &ustream->bin.fbin;

  fbin->camera_pad = gst_element_request_pad (pcontrol->camera,
      gst_element_class_get_pad_template(
          GST_ELEMENT_GET_CLASS (pcontrol->camera),
          "video_%u"),
      "video_%u",
      NULL);

  if (fbin->camera_pad == NULL) {
    SWITCH_ERROR ("request pad for stream(stype:%d ptype:%d index:%d) failed\n",
        sinfo->stype, sinfo->ptype, stream->index);

    return FALSE;
  }

  SWITCH_INFO ("request pad %s for stream(stype:%d ptype:%d index:%d)\n",
      GST_PAD_NAME (fbin->camera_pad), sinfo->stype,
      sinfo->ptype, stream->index);

  if (sinfo->stype == GST_SWITCHSTREAM_TYPE_PREVIEW)
    g_object_set (fbin->camera_pad, "type",
        GST_QMMFSRC_VIDEO_PAD_TYPE_PREVIEW, NULL);
  else
    g_object_set (fbin->camera_pad, "type",
        GST_QMMFSRC_VIDEO_PAD_TYPE_VIDEO, NULL);

  if (sinfo->sbs)
    g_object_set (fbin->camera_pad, "logical-stream-type",
        GST_PAD_LOGICAL_STREAM_TYPE_SIDEBYSIDE,
        NULL);
  else if (sinfo->phy_cam_idx != -1)
    g_object_set (fbin->camera_pad, "logical-stream-type",
        (sinfo->phy_cam_idx + GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MIN),
        NULL);
  else
    ;

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, CameraStreamMaps[sinfo->cam_stream_format],
      "width", G_TYPE_INT, sinfo->src_width,
      "height", G_TYPE_INT, sinfo->src_height,
      "framerate", GST_TYPE_FRACTION, sinfo->src_fps, 1,
      NULL);

  if (pipeline->info.frameselection == TRUE)
    gst_caps_set_simple (caps, "max-framerate", GST_TYPE_FRACTION, 1,
        sinfo->src_fps, NULL);

  if (sinfo->ubwc == TRUE)
    gst_caps_set_simple (caps, "compression", G_TYPE_STRING, "ubwc", NULL);

  gst_caps_set_features (caps, 0, gst_caps_features_new ("memory:GBM", NULL));

  fbin->camera_capsfilter = gst_element_factory_make ("capsfilter", NULL);
  if (fbin->camera_capsfilter == NULL) {
    SWITCH_ERROR ("create caps filter for stream(stype:%d ptype:%d index:%d) "
        "failed\n", sinfo->stype, sinfo->ptype, stream->index);
    goto free_fbin;
  }
  g_object_set (G_OBJECT (fbin->camera_capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  fbin->encoder = gst_element_factory_make ("qtic2venc", NULL);
  if (fbin->encoder == NULL) {
    SWITCH_ERROR ("create encoder for stream(stype:%d ptype:%d index:%d) "
        "failed\n", sinfo->stype, sinfo->ptype, stream->index);
    goto free_fbin;
  }
  g_object_set (G_OBJECT (fbin->encoder), "control-rate", 3,
      "priority", 0,
      "min-quant-i-frames", 30,
      "min-quant-p-frames", 30,
      "max-quant-i-frames", 51,
      "max-quant-p-frames", 51,
      "quant-i-frames", 30,
      "quant-p-frames", 30,
      NULL);

  if (sinfo->cam_stream_format == GST_CAMERA_STREAM_FORMAT_P010_10LE)
    g_object_set (G_OBJECT (fbin->encoder), "target-bitrate", 80000000, NULL);
  else
    g_object_set (G_OBJECT (fbin->encoder), "target-bitrate", 30000000, NULL);

  fbin->encoder_capsfilter = gst_element_factory_make ("capsfilter", NULL);
  if (fbin->encoder_capsfilter == NULL) {
    SWITCH_ERROR ("create encoder capsfilter for "
        "stream(stype:%d ptype:%d index:%d) failed\n",
        sinfo->stype, sinfo->ptype, stream->index);
    goto free_fbin;
  }

  if (pipeline->info.frameselection) {
    caps= gst_caps_new_simple ("video/x-h264",
        "framerate", GST_TYPE_FRACTION, sinfo->src_fps, 1,
        NULL);
    g_object_set (G_OBJECT (fbin->encoder_capsfilter),
        "caps", caps, NULL);
    gst_caps_unref (caps);
  }

  if (sinfo->cam_stream_format == GST_CAMERA_STREAM_FORMAT_P010_10LE) {
    SWITCH_MSG ("use h265parse for stream %s\n", sinfo->name);
    fbin->h264parser = gst_element_factory_make ("h265parse", NULL);
  } else {
    fbin->h264parser = gst_element_factory_make ("h264parse", NULL);
  }

  if (fbin->h264parser == NULL) {
    SWITCH_ERROR ("create h264parser for stream(stype:%d ptype:%d index:%d) "
        "failed\n", sinfo->stype, sinfo->ptype, stream->index);
    goto free_fbin;
  }

  fbin->mp4mux = gst_element_factory_make ("mp4mux", NULL);
  if (fbin->mp4mux == NULL) {
    SWITCH_ERROR ("create mp4mux for stream(stype:%d ptype:%d index:%d) "
        "failed\n", sinfo->stype, sinfo->ptype, stream->index);
    goto free_fbin;
  }

  fbin->filesink = gst_element_factory_make ("filesink", NULL);
  if (fbin->filesink == NULL) {
    SWITCH_ERROR ("create filesink for stream(stype:%d ptype:%d index:%d) "
        "failed\n", sinfo->stype, sinfo->ptype, stream->index);
    goto free_fbin;
  }
  location = g_strdup_printf ("/data/fastswitch-%s.mp4", sinfo->name);
  g_object_set (G_OBJECT (fbin->filesink), "location", location,
      "async", FALSE, NULL);
  g_free (location);

  gst_bin_add_many (GST_BIN (pcontrol->pipeline),
      fbin->camera_capsfilter, fbin->encoder,
      fbin->encoder_capsfilter, fbin->h264parser,
      fbin->mp4mux, fbin->filesink, NULL);

  if (!gst_element_link_many (fbin->camera_capsfilter, fbin->encoder,
        fbin->encoder_capsfilter, fbin->h264parser,
        fbin->mp4mux, fbin->filesink, NULL)) {
    SWITCH_ERROR ("link stream (stype:%d ptype:%d index:%d) failed\n",
        sinfo->stype, sinfo->ptype, stream->index);
    goto remove_fbin;
  }

  SWITCH_INFO ("stream (stype:%d ptype:%d index:%d) "
      "init and add to pipeline succesfully\n",
      sinfo->stype, sinfo->ptype, stream->index);

  return TRUE;

remove_fbin:
  // remove will unref elements
  gst_bin_remove_many (GST_BIN (pipeline->control.pipeline),
      fbin->camera_capsfilter,
      fbin->encoder,
      fbin->encoder_capsfilter,
      fbin->h264parser,
      fbin->mp4mux,
      fbin->filesink,
      NULL);

  fbin->camera_capsfilter = NULL;
  fbin->encoder = NULL;
  fbin->h264parser = NULL;
  fbin->mp4mux = NULL;
  fbin->filesink = NULL;

  gst_element_release_request_pad (pcontrol->camera, fbin->camera_pad);
  fbin->camera_pad = NULL;

  return FALSE;

free_fbin:

  if (fbin->filesink) {
    gst_object_unref (fbin->filesink);
    fbin->filesink = NULL;
  }

  if (fbin->mp4mux) {
    gst_object_unref (fbin->mp4mux);
    fbin->mp4mux = NULL;
  }

  if (fbin->h264parser) {
    gst_object_unref (fbin->h264parser);
    fbin->h264parser = NULL;
  }

  if (fbin->encoder_capsfilter) {
    gst_object_unref (fbin->encoder_capsfilter);
    fbin->encoder_capsfilter = NULL;
  }

  if (fbin->encoder) {
    gst_object_unref (fbin->encoder);
    fbin->encoder = NULL;
  }

  if (fbin->camera_capsfilter) {
    gst_object_unref (fbin->camera_capsfilter);
    fbin->camera_capsfilter = NULL;
  }

  if (fbin->camera_pad) {
    gst_element_release_request_pad (pcontrol->camera, fbin->camera_pad);
    fbin->camera_pad = NULL;
  }

  return FALSE;
}

void
switchstream_file_deinit (GstUnifiedSwitchStream *ustream)
{
  GstFileBin *fbin = NULL;
  GstSwitchPipeline *pipeline = NULL;
  GstSwitchPipelineControl *pcontrol = NULL;

  g_return_if_fail (ustream != NULL);
  fbin = &ustream->bin.fbin;
  pipeline = ustream->basic.pipeline;
  pcontrol = &pipeline->control;

  if (fbin->camera_capsfilter == NULL) {
    SWITCH_DEBUG ("stream stype(%d) ptype(%d) index(%d) already deinit\n",
        ustream->basic.info.stype,
        ustream->basic.info.ptype,
        ustream->basic.index);

    return;
  }

  gst_element_unlink_many (fbin->camera_capsfilter,
      fbin->encoder,
      fbin->encoder_capsfilter,
      fbin->h264parser,
      fbin->mp4mux,
      fbin->filesink,
      NULL);

  // remove will unref elements
  gst_bin_remove_many (GST_BIN (pipeline->control.pipeline),
      fbin->camera_capsfilter,
      fbin->encoder,
      fbin->encoder_capsfilter,
      fbin->h264parser,
      fbin->mp4mux,
      fbin->filesink,
      NULL);

  fbin->camera_capsfilter = NULL;
  fbin->encoder = NULL;
  fbin->h264parser = NULL;
  fbin->mp4mux = NULL;
  fbin->filesink = NULL;

  gst_element_release_request_pad (pcontrol->camera, fbin->camera_pad);
  fbin->camera_pad = NULL;
}

gboolean
switchstream_init (GstSwitchStream *stream)
{
  GstUnifiedSwitchStream *ustream = (GstUnifiedSwitchStream *)stream;

  if (stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY)
    return switchstream_display_init (ustream);
  else
    return switchstream_file_init (ustream);
}

void
switchstream_deinit (GstSwitchStream *stream)
{
  GstUnifiedSwitchStream *ustream = (GstUnifiedSwitchStream *)stream;

  if (stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY)
    switchstream_display_deinit (ustream);
  else
    switchstream_file_deinit (ustream);
}

gboolean
pipeline_streams_init (GstSwitchPipeline *pipeline)
{
  gint pidx, vidx;
  GstSwitchPipelineControl *pcontrol;

  g_return_val_if_fail(pipeline != NULL, FALSE);
  pcontrol = &pipeline->control;

  if (! pipeline_init (pipeline)) {
    SWITCH_ERROR ("pipeline init failed\n");
    return FALSE;
  }

  for (pidx = 0; pidx < pcontrol->pstream_num; pidx++) {
    if (IS_STREAM_ACTIVE (pcontrol->preview_streams[pidx])) {
      if (! switchstream_init (pcontrol->preview_streams[pidx])) {
        SWITCH_ERROR ("init preview stream (%d) failed\n", pidx);
        goto deinit_preview_streams;
      }
    }
  }

  for (vidx = 0; vidx < pcontrol->vstream_num; vidx++) {
    if (IS_STREAM_ACTIVE (pcontrol->video_streams[vidx])) {
      if (! switchstream_init (pcontrol->video_streams[vidx])) {
        SWITCH_ERROR ("init video stream (%d) failed\n", vidx);
        goto deinit_video_streams;
      }
    }
  }

  return TRUE;

deinit_video_streams:
  vidx--;
  while (vidx >= 0) {
    switchstream_deinit (pcontrol->video_streams[vidx]);
    vidx--;
  }

deinit_preview_streams:
  pidx--;
  while (pidx >= 0) {
    switchstream_deinit (pcontrol->preview_streams[pidx]);
    pidx--;
  }

  return FALSE;
}

void
pipeline_streams_deinit (GstSwitchPipeline *pipeline)
{
  gint pidx, vidx;
  GstSwitchPipelineControl *pcontrol;

  g_return_if_fail(pipeline != NULL);
  pcontrol = &pipeline->control;

  for (vidx = 0; vidx < pcontrol->vstream_num; vidx++) {
    if (IS_STREAM_ACTIVE (pcontrol->video_streams[vidx])) {
      SWITCH_INFO ("deinit video stream (%d)\n", vidx);
      switchstream_deinit (pcontrol->video_streams[vidx]);
    }
  }

  for (pidx = 0; pidx < pcontrol->pstream_num; pidx++) {
    if (IS_STREAM_ACTIVE (pcontrol->preview_streams[pidx])) {
      SWITCH_INFO ("deinit preview stream (%d)\n", pidx);
      switchstream_deinit (pcontrol->preview_streams[pidx]);
    }
  }

  if (pcontrol->menu_msgs)
    g_async_queue_unref (pcontrol->menu_msgs);

  if (pcontrol->thread_menu)
    g_thread_join (pcontrol->thread_menu);

  pipeline_deinit (pipeline);
}

gboolean
pipeline_add_stream (GstSwitchPipeline *pipeline, GstSwitchStream *stream)
{
  GstSwitchPipelineControl *pcontrol;
  GstSwitchStreamInfo *sinfo = NULL;
  GstDisplayBin *dbin = NULL;
  GstFileBin *fbin = NULL;
  GstPad *camera_pad = NULL;
  GstElement *link_target = NULL;

  g_return_val_if_fail (pipeline != NULL, FALSE);
  g_return_val_if_fail (stream != NULL, FALSE);
  pcontrol = &pipeline->control;
  sinfo = &stream->info;
  g_return_val_if_fail (pcontrol->pipeline, FALSE);

  if (stream->linked == TRUE) {
    SWITCH_DEBUG ("stream stype(%d) ptype(%d) index(%d) already linked\n",
        sinfo->stype, sinfo->ptype, stream->index);

    return TRUE;
  }

  if (stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY) {
    dbin = STREAM_TO_DISPLAY_STREAM_BIN (stream);
    camera_pad = dbin->camera_pad;
    link_target = dbin->camera_capsfilter;
  } else {
    fbin = STREAM_TO_FILE_STREAM_BIN (stream);
    camera_pad = fbin->camera_pad;
    link_target = fbin->camera_capsfilter;
  }

  if (! gst_element_link_pads_full (pcontrol->camera, GST_PAD_NAME (camera_pad),
      link_target, NULL, GST_PAD_LINK_CHECK_DEFAULT)) {
    stream->linked = FALSE;
    SWITCH_ERROR ("link stream stype(%d) ptype(%d) index(%d) pad(%s) "
        "to pipeline failed\n", sinfo->stype, sinfo->ptype, stream->index,
        GST_PAD_NAME (camera_pad));
    return FALSE;
  } else {
    stream->linked = TRUE;
    SWITCH_DEBUG ("link stream stype(%d) ptype(%d) index(%d) pad(%s) "
        "to pipeline success\n", sinfo->stype, sinfo->ptype, stream->index,
        GST_PAD_NAME (camera_pad));
    return TRUE;
  }
}

void
pipeline_remove_stream (GstSwitchPipeline *pipeline, GstSwitchStream *stream)
{
  GstSwitchPipelineControl *pcontrol;
  GstSwitchStreamInfo *sinfo = NULL;
  GstDisplayBin *dbin = NULL;
  GstFileBin *fbin = NULL;
  GstElement *unlink_target = NULL;
  GstPad *pad = NULL;

  g_return_if_fail (pipeline != NULL);
  g_return_if_fail (stream != NULL);
  pcontrol = &pipeline->control;
  sinfo = &stream->info;
  g_return_if_fail (pcontrol->pipeline);

  if (stream->linked == FALSE) {
    SWITCH_DEBUG ("stream stype(%d) ptype(%d) index(%d) already unlinked\n",
        sinfo->stype, sinfo->ptype, stream->index);

    return;
  }

  if (stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY) {
    dbin = STREAM_TO_DISPLAY_STREAM_BIN (stream);
    unlink_target = dbin->camera_capsfilter;
    pad = dbin->camera_pad;
  } else {
    fbin = STREAM_TO_FILE_STREAM_BIN (stream);
    unlink_target = fbin->camera_capsfilter;
    pad = fbin->camera_pad;
  }

  SWITCH_DEBUG ("remove stream stype(%d) ptype(%d) index(%d) pad(%s) "
      "from pipeline\n",
      sinfo->stype, sinfo->ptype, stream->index, GST_PAD_NAME (pad));

  gst_element_unlink (pcontrol->camera, unlink_target);
  stream->linked = FALSE;
}

gboolean
pipeline_add_streams (GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol;
  GstSwitchStream *stream;
  gint pidx, vidx;

  g_return_val_if_fail (pipeline != NULL, FALSE);
  pcontrol = &pipeline->control;

  for (pidx = 0; pidx < pcontrol->pstream_num; pidx++) {
    stream = pcontrol->preview_streams[pidx];

    if (IS_STREAM_ACTIVE (stream)) {
      if (! pipeline_add_stream (pipeline, stream))
        goto remove_preview_streams;
    }
  }

  for (vidx = 0; vidx < pcontrol->vstream_num; vidx++) {
    stream = pcontrol->video_streams[vidx];

    if (IS_STREAM_ACTIVE (stream)) {
      if (! pipeline_add_stream (pipeline, stream))
        goto remove_video_streams;
    }
  }

  SWITCH_DEBUG ("add all streams to pipeline success\n");

  return TRUE;

remove_video_streams:
  while (vidx >= 0) {
    pipeline_remove_stream (pipeline, pcontrol->video_streams[vidx]);
    vidx--;
  }

remove_preview_streams:
  while (pidx >= 0) {
    pipeline_remove_stream (pipeline, pcontrol->preview_streams[pidx]);
    pidx--;
  }

  return FALSE;
}

void
pipeline_remove_streams (GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol = NULL;
  gint pidx, vidx;

  g_return_if_fail (pipeline != NULL);
  pcontrol = &pipeline->control;

  if (pcontrol->exit && pcontrol->thread_menu)
    g_async_queue_push (pcontrol->menu_msgs, g_strdup(MENU_THREAD_MSG_EXIT));

  SWITCH_DEBUG ("remove all streams from pipeline\n");

  for (pidx = 0; pidx < pcontrol->pstream_num; pidx++) {
    if (IS_STREAM_ACTIVE (pcontrol->preview_streams[pidx]))
      pipeline_remove_stream (pipeline, pcontrol->preview_streams[pidx]);
  }

  for (vidx = 0; vidx < pcontrol->vstream_num; vidx++) {
    if (IS_STREAM_ACTIVE (pcontrol->video_streams[vidx]))
      pipeline_remove_stream (pipeline, pcontrol->video_streams[vidx]);
  }
}

void
pipeline_remove_video_streams (GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol = NULL;
  gint vidx;

  g_return_if_fail (pipeline != NULL);
  pcontrol = &pipeline->control;

  SWITCH_DEBUG ("remove all video streams from pipeline\n");

  for (vidx = 0; vidx < pcontrol->vstream_num; vidx++) {
    if (IS_STREAM_ACTIVE (pcontrol->video_streams[vidx]))
      pipeline_remove_stream (pipeline, pcontrol->video_streams[vidx]);
  }
}

gboolean
pipeline_add_video_streams (GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol;
  GstSwitchStream *stream;
  gint vidx;

  g_return_val_if_fail (pipeline != NULL, FALSE);
  pcontrol = &pipeline->control;

  for (vidx = 0; vidx < pcontrol->vstream_num; vidx++) {
    stream = pcontrol->video_streams[vidx];

    if (IS_STREAM_ACTIVE (stream)) {
      if (! pipeline_add_stream (pipeline, stream))
        goto remove_video_streams;
    }
  }

  SWITCH_DEBUG ("add all video streams to pipeline success\n");

  return TRUE;

remove_video_streams:
  while (vidx >= 0) {
    pipeline_remove_stream (pipeline, pcontrol->video_streams[vidx]);
    vidx--;
  }

  return FALSE;
}

void
switchstream_source_activate (GstSwitchStream *stream,
                       gboolean activate)
{
  GstDisplayBin *dbin = NULL;
  GstFileBin *fbin = NULL;
  GstPad *camera_pad = NULL;

  g_return_if_fail (stream != NULL);
  if (stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY) {
    dbin = STREAM_TO_DISPLAY_STREAM_BIN (stream);
    camera_pad = dbin->camera_pad;
  } else {
    fbin = STREAM_TO_FILE_STREAM_BIN (stream);
    camera_pad = fbin->camera_pad;
  }

  SWITCH_DEBUG ("stream stype(%d) ptype(%d) index(%d) pad(%s) activate(%d)\n",
      stream->info.stype, stream->info.ptype, stream->index,
      GST_PAD_NAME (camera_pad), activate);

  gst_pad_set_active (camera_pad, activate);
}

void
switchstream_source_add_to_array (GstSwitchStream *stream,
                                  GPtrArray *pads)
{
  GstDisplayBin *dbin = NULL;
  GstFileBin *fbin = NULL;

  GstPad *camera_pad = NULL;

  g_return_if_fail (stream != NULL);
  if (stream->info.ptype == GST_STREAM_PIPELINE_DISPLAY) {
    dbin = STREAM_TO_DISPLAY_STREAM_BIN (stream);
    camera_pad = dbin->camera_pad;
  } else {
    fbin = STREAM_TO_FILE_STREAM_BIN (stream);
    camera_pad = fbin->camera_pad;
  }

  SWITCH_DEBUG ("add to array: stream stype(%d) ptype(%d) index(%d) pad(%s) \n",
      stream->info.stype, stream->info.ptype, stream->index,
      GST_PAD_NAME (camera_pad));

  g_ptr_array_add (pads, (gpointer)(GST_PAD_NAME (camera_pad)));
}

void
pipeline_activate_video_streams_sources(GstSwitchPipeline *pipeline,
                                        gboolean activate)
{
  GstSwitchPipelineControl *pcontrol = NULL;
  GstSwitchPipelineInfo *pinfo = NULL;
  gint vidx;

  g_return_if_fail (pipeline != NULL);
  pcontrol = &pipeline->control;
  pinfo = &pipeline->info;

  if (pinfo->video_sync) {
    GPtrArray *pads = NULL;
    gboolean success = FALSE;

    pads = g_ptr_array_new();
    for (vidx = 0; vidx < pcontrol->vstream_num; vidx++) {
      if (IS_STREAM_ACTIVE (pcontrol->video_streams[vidx])) {
        // gst_pad_set_active to update pad state
        // then use signal to submit these pads to qmmf backend
        switchstream_source_activate (pcontrol->video_streams[vidx], activate);
        switchstream_source_add_to_array (pcontrol->video_streams[vidx], pads);
      }
    }
    g_signal_emit_by_name (pcontrol->camera, "video-pads-activation",
        activate, pads, &success);

    if (success)
      SWITCH_DEBUG ("signal sent success\n");
    else
      SWITCH_DEBUG ("signal sent failed\n");

    g_ptr_array_free (pads, FALSE);
  } else  {
    for (vidx = 0; vidx < pcontrol->vstream_num; vidx++) {
      if (IS_STREAM_ACTIVE (pcontrol->video_streams[vidx])) {
        switchstream_source_activate (pcontrol->video_streams[vidx], activate);
      }
    }
  }
}

void
switchstream_video_control (GstSwitchStream *stream, gboolean on)
{
  GstFileBin *fbin = NULL;
  GstSwitchPipelineControl *pcontrol = NULL;
  GstState encoder_state, e_state_pending;

  g_return_if_fail (stream != NULL);
  g_return_if_fail (stream->info.stype == GST_SWITCHSTREAM_TYPE_VIDEO);

  // video streams should always use file pipeline
  fbin = STREAM_TO_FILE_STREAM_BIN (stream);
  pcontrol = &stream->pipeline->control;

  if (on) {
    SWITCH_DEBUG ("stream stype(%d) ptype(%d) idx(%d) video on,"
        "set all plugins to Playing state\n",
        stream->info.stype, stream->info.ptype, stream->index);

    gst_bin_add_many (GST_BIN (pcontrol->pipeline),
        fbin->camera_capsfilter, fbin->encoder,
        fbin->encoder_capsfilter, fbin->h264parser,
        fbin->mp4mux, fbin->filesink, NULL);

    gst_element_link_many (fbin->camera_capsfilter, fbin->encoder,
            fbin->encoder_capsfilter, fbin->h264parser,
            fbin->mp4mux, fbin->filesink, NULL);

    gst_element_set_state (fbin->camera_capsfilter, GST_STATE_PLAYING);
    gst_element_set_state (fbin->encoder, GST_STATE_PLAYING);
    gst_element_set_state (fbin->encoder_capsfilter, GST_STATE_PLAYING);
    gst_element_set_state (fbin->h264parser, GST_STATE_PLAYING);
    gst_element_set_state (fbin->mp4mux, GST_STATE_PLAYING);
    gst_element_set_state (fbin->filesink, GST_STATE_PLAYING);

  } else {
    SWITCH_DEBUG ("stream stype(%d) ptype(%d) idx(%d) video off\n",
        stream->info.stype, stream->info.ptype, stream->index);

    gst_element_get_state (fbin->encoder, &encoder_state, &e_state_pending,
        GST_CLOCK_TIME_NONE);

    if (encoder_state == GST_STATE_PLAYING) {
      SWITCH_DEBUG ("stream stype(%d) ptype(%d) idx(%d) send EoS to encoder\n",
          stream->info.stype, stream->info.ptype, stream->index);
      gst_element_send_event (fbin->encoder, gst_event_new_eos ());
    }

    gst_element_set_state (fbin->camera_capsfilter, GST_STATE_NULL);
    gst_element_set_state (fbin->encoder, GST_STATE_NULL);
    gst_element_set_state (fbin->encoder_capsfilter, GST_STATE_NULL);
    gst_element_set_state (fbin->h264parser, GST_STATE_NULL);
    gst_element_set_state (fbin->mp4mux, GST_STATE_NULL);
    gst_element_set_state (fbin->filesink, GST_STATE_NULL);

    gst_element_unlink_many (fbin->camera_capsfilter,
        fbin->encoder,
        fbin->encoder_capsfilter,
        fbin->h264parser,
        fbin->mp4mux,
        fbin->filesink,
        NULL);

    gst_object_ref (fbin->camera_capsfilter);
    gst_object_ref (fbin->encoder);
    gst_object_ref (fbin->encoder_capsfilter);
    gst_object_ref (fbin->h264parser);
    gst_object_ref (fbin->mp4mux);
    gst_object_ref (fbin->filesink);

    gst_bin_remove_many (GST_BIN (pcontrol->pipeline),
        fbin->camera_capsfilter,
        fbin->encoder,
        fbin->encoder_capsfilter,
        fbin->h264parser,
        fbin->mp4mux,
        fbin->filesink,
        NULL);
  }
}

void
pipeline_switchstream_video_control (GstSwitchPipeline *pipeline, gboolean on)
{
  GstSwitchPipelineControl *pcontrol = NULL;
  gint vidx;

  g_return_if_fail (pipeline != NULL);

  pcontrol = &pipeline->control;
  for (vidx = 0; vidx < pcontrol->vstream_num; vidx++) {
    if (IS_STREAM_ACTIVE (pcontrol->video_streams[vidx]))
      switchstream_video_control (pcontrol->video_streams[vidx], on);
  }
}

void
pipeline_switch_camera_sensor (GstSwitchPipeline *pipeline, gint index)
{
  GstSwitchPipelineControl *pcontrol = &pipeline->control;
  GstSwitchPipelineInfo *pinfo = &pipeline->info;

  g_return_if_fail (index > -2 && index < pinfo->sensor_num);

  SWITCH_MSG ("switch to sensor index (%d)\n", index);

  g_object_set (G_OBJECT (pcontrol->camera),
      "camera-switch-index", index, NULL);
}

gboolean
pipeline_sensor_switch_tmr_func(gpointer userdata)
{
  GstSwitchPipeline *pipeline = (GstSwitchPipeline *)userdata;
  GstSwitchPipelineControl *pcontrol = &pipeline->control;
  GstSwitchPipelineInfo *pinfo = &pipeline->info;

  SWITCH_DEBUG ("enter %s\n", __func__);

  if (pcontrol->exit)
    return FALSE;

  if (pcontrol->sensor_switch_index == 0) {
    pipeline_switch_camera_sensor (pipeline, pcontrol->sensor_switch_index);
    pcontrol->sensor_switch_index++;

    g_timeout_add (pcontrol->sensor_switch_duration_ms,
        pipeline_sensor_switch_tmr_func, pipeline);

    return FALSE;
  } else if (pcontrol->sensor_switch_index < (pinfo->sensor_num - 1)) {
    pipeline_switch_camera_sensor (pipeline, pcontrol->sensor_switch_index);
    pcontrol->sensor_switch_index++;

    return TRUE;
  } else if (pcontrol->sensor_switch_index == (pinfo->sensor_num - 1)) {
    pipeline_switch_camera_sensor (pipeline, pcontrol->sensor_switch_index);
    pcontrol->sensor_switch_index = 0;

    return FALSE;
  } else {
    return FALSE;
  }
}

gboolean
pipeline_switch_tmr_func (gpointer userdata)
{
  GstSwitchPipeline *pipeline = (GstSwitchPipeline *)userdata;
  GstSwitchPipelineControl *pcontrol = &pipeline->control;
  GstSwitchPipelineInfo *pinfo = &pipeline->info;

  SWITCH_DEBUG ("enter %s\n", __func__);

  if (pcontrol->exit)
    return FALSE;

  if (pcontrol->mode == GST_SWITCH_RUN_MODE_PREVIEW) {
    SWITCH_DEBUG ("Switching to Preview Plus Video start\n");
    pipeline_activate_video_streams_sources (pipeline, TRUE);
    pipeline_switchstream_video_control (pipeline, TRUE);
    pipeline_add_video_streams (pipeline);
    SWITCH_DEBUG ("Switching to Preview Plus Video finish\n");
    pcontrol->mode = GST_SWITCH_RUN_MODE_PREVIEW_PLUS_VIDEO;
  } else {
    SWITCH_DEBUG ("Switching to Preview start\n");
    pipeline_remove_video_streams (pipeline);
    pipeline_switchstream_video_control (pipeline, FALSE);
    pipeline_activate_video_streams_sources (pipeline, FALSE);
    SWITCH_DEBUG ("Switching to Preview finish\n");
    pcontrol->mode = GST_SWITCH_RUN_MODE_PREVIEW;
  }

  if (pinfo->sensor_switch)
    g_timeout_add (PIPELINE_SENSOR_SWITCH_SHIFT_MS,
        pipeline_sensor_switch_tmr_func, pipeline);

  if (pcontrol->mode == GST_SWITCH_RUN_MODE_PREVIEW) {
    pcontrol->current_round++;
    if (pcontrol->current_round >= pinfo->round) {
      SWITCH_MSG ("Max round(%d) reached, exit\n", pcontrol->current_round);
      pcontrol->exit = TRUE;
      gst_element_send_event (pcontrol->pipeline, gst_event_new_eos ());
      return  FALSE;
    } else {
      SWITCH_MSG ("%d round start\n", pcontrol->current_round + 1);
    }
  }

  if (pcontrol->mode == GST_SWITCH_RUN_MODE_PREVIEW_PLUS_VIDEO)
    SWITCH_MSG ("*** Current Mode: Preview Plus Video ***\n");
  else
    SWITCH_MSG ("*** Current Mode: Preview ***\n");

  return TRUE;
}

gboolean
system_signal_handler (gpointer userdata)
{
  GstSwitchPipeline *pipeline = (GstSwitchPipeline *)userdata;
  GstSwitchPipelineControl *pcontrol = &pipeline->control;
  GstState state = GST_STATE_VOID_PENDING;

  if (pcontrol->thread_menu)
    g_async_queue_push (pcontrol->menu_msgs, g_strdup(MENU_THREAD_MSG_EXIT));

  SWITCH_MSG ("Receive CTRL+C, send EoS to pipeline\n");

  gst_element_send_event (pcontrol->pipeline, gst_event_new_eos ());
  pcontrol->exit = TRUE;
  gst_element_get_state (pcontrol->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
  if (state != GST_STATE_PLAYING)
    g_main_loop_quit (pcontrol->mloop);

  return TRUE;
}

void
gst_signal_handler(GstBus *bus, GstMessage *message, gpointer userdata)
{
  GstSwitchPipeline *pipeline = (GstSwitchPipeline *)userdata;
  GstSwitchPipelineControl *pcontrol = &pipeline->control;

  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pcontrol->pipeline))
    return;

  SWITCH_VERBOSE ("receive message from pipeline, type(%x)\n",
      GST_MESSAGE_TYPE (message));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      {
        SWITCH_DEBUG ("Get EoS from piepline\n");
        pcontrol->exit = TRUE;
        g_main_loop_quit (pcontrol->mloop);
      }
      break;
    case GST_MESSAGE_WARNING:
      {
        GError *gerror;
        gchar *debug;

        gst_message_parse_warning (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        g_clear_error (&gerror);
        g_free (debug);
      }
      break;
    case GST_MESSAGE_ERROR:
      {
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        g_clear_error (&gerror);
        g_free (debug);
        g_main_loop_quit (pcontrol->mloop);
      }
      break;
    case GST_MESSAGE_STATE_CHANGED:
      {
        GstState s_old, s_new, s_pending;
        gst_message_parse_state_changed (message, &s_old, &s_new, &s_pending);

        SWITCH_DEBUG ("Pipeline state change from %s to %s, pending %s\n",
            gst_element_state_get_name (s_old),
            gst_element_state_get_name (s_new),
            gst_element_state_get_name (s_pending));
      }
      break;
    default:
      break;
  }
}

gboolean
pipeline_signals_register (GstSwitchPipeline *pipeline)
{
  GstBus *bus = NULL;
  GstSwitchPipelineControl *pcontrol = NULL;
  GstSwitchPipelineInfo *pinfo = NULL;

  g_return_val_if_fail (pipeline != NULL, FALSE);
  pcontrol = &pipeline->control;
  pinfo = &pipeline->info;

  // add bus signal handler
  bus = gst_pipeline_get_bus (GST_PIPELINE (pcontrol->pipeline));

  if (bus == NULL) {
    SWITCH_ERROR ("fail to get bus from pipeline\n");
    return FALSE;
  }

  gst_bus_add_signal_watch (bus);

  g_signal_connect (bus, "message::eos",
      G_CALLBACK(gst_signal_handler), pipeline);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK(gst_signal_handler), pipeline);
  g_signal_connect (bus, "message::warning",
      G_CALLBACK(gst_signal_handler), pipeline);
  g_signal_connect (bus, "message::error",
      G_CALLBACK(gst_signal_handler), pipeline);

  gst_object_unref (bus);

  // add CTRL+C handler
  pcontrol->unix_signal = g_unix_signal_add (SIGINT,
      system_signal_handler, pipeline);

  return TRUE;
}


gboolean
retrieve_element_properties (GstElement *element, GstStructure *property)
{
  GParamSpec **prop_spec = NULL;
  GString *options = NULL;
  GstState state = GST_STATE_VOID_PENDING;
  guint num_props = 0, index = 0;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (property != NULL, FALSE);

  options = g_string_new (NULL);

  gst_element_get_state (element, &state, NULL, 0);
  if (state < GST_STATE_PAUSED) {
    SWITCH_ERROR ("element is not ready to set properties, state:%s\n",
        gst_element_state_get_name (state));
    return FALSE;
  }

  prop_spec = g_object_class_list_properties (
      G_OBJECT_GET_CLASS (element), &num_props);
  if (*prop_spec == NULL) {
    SWITCH_ERROR ("failed to get properties list\n");
    return FALSE;
  }

  for (guint i = 0; i < num_props; ++i) {
    GParamSpec *param_spec = prop_spec[i];
    gchar *field_value = NULL, *field_name = NULL;

    if (!GST_PROPERTY_IS_MUTABLE_IN_CURRENT_STATE (param_spec, state))
      continue;

    field_name = g_strdup_printf ("%u", index);
    field_value = g_strdup (g_param_spec_get_name (param_spec));

    gst_structure_set (property, field_name, G_TYPE_STRING, field_value, NULL);

    g_string_append_printf (options, "   (%2u) %-25s: %s\n", index,
        field_value, g_param_spec_get_blurb (param_spec));

    ++index;

    g_free (field_name);
    g_free (field_value);
  }

  SWITCH_MSG ("****Prop Menu****\n%s", options->str);

  g_string_free (options, TRUE);

  return TRUE;
}

/*
  This function will only return TRUE while terminating, otherwise return FALSE;
  *msg will be null in case of empty input;
  Call g_free (gpointer mem) to free *msg.
*/
gboolean
take_stdin_message (GstPropMenuInfo *minfo, gchar **msg)
{
  GstPropMenuInfo *info = NULL;
  gchar *message = NULL, *tmp_msg = NULL;

  g_return_val_if_fail (minfo != NULL, FALSE);

  info = minfo;

  if (!info->watch_stdin) {
    SWITCH_DEBUG ("read stdin removed, exiting menu thread.\n");
    return TRUE;
  }

  message = (gchar *)g_async_queue_pop (info->queue);

  if (g_str_equal (message, MENU_THREAD_MSG_EXIT))
    goto exit;
  else if (g_str_equal(message, MENU_THREAD_MSG_EMPTY))
    tmp_msg = NULL;
  else
    tmp_msg = g_strdup (message);

  g_free (message);
  *msg = tmp_msg;

  return FALSE;

exit:
  g_free (message);

  return TRUE;
}

gboolean
retrieve_option_info (GObject * object, GParamSpec *spec)
{
  GString *info = NULL;
  gboolean ret = TRUE;

  g_return_val_if_fail (object != NULL, FALSE);
  g_return_val_if_fail (spec != NULL, FALSE);
  g_return_val_if_fail (spec->flags & G_PARAM_READABLE, FALSE);

  if (!(spec->flags & G_PARAM_READABLE)) {
    SWITCH_MSG ("unreadable property.\n");
    return FALSE;
  }

  info = g_string_new (NULL);

  if (G_IS_PARAM_SPEC_CHAR (spec)) {
    gint8 value;
    GParamSpecChar *range = G_PARAM_SPEC_CHAR (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %d, Range: %d - %d\n",
        value, range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_UCHAR (spec)) {
    guint8 value;
    GParamSpecUChar *range = G_PARAM_SPEC_UCHAR (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %d, Range: %d - %d\n",
        value, range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_BOOLEAN (spec)) {
    gboolean value;
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %s, Possible values: "
        "0(false), 1(true)\n", value ? "true" : "false");
  } else if (G_IS_PARAM_SPEC_INT (spec)) {
    gint value;
    GParamSpecInt *range = G_PARAM_SPEC_INT (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %d, Range: %d - %d\n",
        value, range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_UINT (spec)) {
    guint value;
    GParamSpecUInt *range = G_PARAM_SPEC_UINT (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %u, Range: %u - %u\n",
        value, range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_LONG (spec)) {
    glong value;
    GParamSpecLong *range = G_PARAM_SPEC_LONG (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %ld, Range: %ld - %ld\n",
        value, range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_ULONG (spec)) {
    gulong value;
    GParamSpecULong *range = G_PARAM_SPEC_ULONG (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %lu, Range: %lu - %lu\n",
        value, range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_INT64 (spec)) {
    gint64 value;
    GParamSpecInt64 *range = G_PARAM_SPEC_INT64 (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %" G_GINT64_FORMAT ", "
        "Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT "\n", value,
        range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_UINT64 (spec)) {
    guint64 value;
    GParamSpecUInt64 *range = G_PARAM_SPEC_UINT64 (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %" G_GUINT64_FORMAT ", "
        "Range: %" G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT "\n", value,
        range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_UNICHAR (spec)) {
    gunichar value;
    GParamSpecUnichar *pspec = G_PARAM_SPEC_UNICHAR (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, "Default value: %u\n", pspec->default_value);
  } else if (G_IS_PARAM_SPEC_ENUM(spec)) {
    GEnumClass *klass = NULL;
    const gchar *nick = NULL;
    gint value = 0;
    guint idx = 0;

    g_object_get (object, spec->name, &value, NULL);
    klass = G_ENUM_CLASS (g_type_class_ref (spec->value_type));
    if (!klass) {
      g_string_append_printf (info, "Failed to get enum class\n");
      SWITCH_MSG ("%s", info->str);
      g_string_free (info, TRUE);

      return FALSE;
    }

    g_string_append_printf (info, "\n");

    for (idx = 0; idx < klass->n_values; idx++) {
      GEnumValue *genum = &(klass->values[idx]);

      if (genum->value == value)
        nick = genum->value_nick;

      g_string_append_printf (info, "   (%d): %-16s - %s\n",
          genum->value, genum->value_nick, genum->value_name);
    }

    g_type_class_unref (klass);

    g_string_append_printf (info, "\n Current value: %d, \"%s\"\n",
        value, nick);
  } else if (G_IS_PARAM_SPEC_FLAGS (spec)) {
    // TODO: pending add in case some plugins used
    g_string_append_printf (info, "Unsupported GParamSpecFlags\n");
    ret = FALSE;
  } else if (G_IS_PARAM_SPEC_FLOAT (spec)) {
    gfloat value;
    GParamSpecFloat *range = G_PARAM_SPEC_FLOAT (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %15.7g, "
        "Range: %15.7g - %15.7g\n", value, range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_DOUBLE (spec)) {
    gdouble value;
    GParamSpecDouble *range = G_PARAM_SPEC_DOUBLE (spec);
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %15.7g, "
        "Range: %15.7g - %15.7g\n", value, range->minimum, range->maximum);
  } else if (G_IS_PARAM_SPEC_STRING (spec)) {
    gchar *value;
    g_object_get (object, spec->name, &value, NULL);

    g_string_append_printf (info, " Current value: %s\n", value);
    g_free (value);
  } else if (G_IS_PARAM_SPEC_PARAM (spec)) {
    // TODO: pending add in case some plugins used
    g_string_append_printf (info, "Unsupported GParamSpecParam\n");
    ret = FALSE;
  } else if (G_IS_PARAM_SPEC_BOXED (spec)) {
    // TODO: pending add in case of metadata
    g_string_append_printf (info, "Unsupported GParamSpecBoxed\n");
    ret = FALSE;
  } else if (G_IS_PARAM_SPEC_POINTER (spec)) {
    // TODO: pending add in case of metadata
    g_string_append_printf (info, "Unsupported GParamSpecPointer\n");
    ret = FALSE;
  } else if (G_IS_PARAM_SPEC_OBJECT (spec)) {
    // TODO: pending add in case some plugins used
    g_string_append_printf (info, "Unsupported GParamSpecObject\n");
    ret = FALSE;
  } else if (G_IS_PARAM_SPEC_OVERRIDE (spec)) {
    // TODO: pending add in case some plugins used
    g_string_append_printf (info, "Unsupported GParamSpecOverride\n");
    ret = FALSE;
  } else if (G_IS_PARAM_SPEC_GTYPE (spec)) {
    // TODO: pending add in case some plugins used
    g_string_append_printf (info, "Unsupported GParamSpecGType\n");
    ret = FALSE;
  } else if (G_IS_PARAM_SPEC_VARIANT (spec)) {
    // TODO: pending add in case some plugins used
    g_string_append_printf (info, "Unsupported GParamSpecVariant\n");
    ret = FALSE;
  } else if (GST_IS_PARAM_SPEC_ARRAY_LIST (spec)) {
    GValue value = G_VALUE_INIT;
    gchar *string = NULL;

    g_value_init (&value, GST_TYPE_ARRAY);
    g_object_get_property (object, spec->name, &value);

    string = gst_value_serialize (&value);
    g_string_append_printf (info, "\n Current value: %s\n", string);

    g_value_unset (&value);
    g_free (string);
  } else {
    g_string_append_printf (info, "Unknown type %ld \"%s\"\n",
      (glong) spec->value_type, g_type_name (spec->value_type));
    ret = FALSE;
  }

  SWITCH_MSG ("%s", info->str);
  g_string_free (info, TRUE);

  return ret;
}

gboolean
element_properties (GstPropMenuInfo *minfo)
{
  GstPropMenuInfo *info = NULL;
  GstElement *element = NULL;
  GstStructure *props = NULL;   // table saving properties
  gchar *in_name = NULL;
  gboolean ret = FALSE;         // TRUE: menu thread should exit

  g_return_val_if_fail (minfo != NULL, TRUE);

  info = minfo;
  element = info->camera;
  props = gst_structure_new_empty ("properties");

  if (!retrieve_element_properties (element, props)) {
    SWITCH_ERROR ("failed to print camera properties\n");
    goto exit;
  }

  SWITCH_MSG ("Choose your option:\n");
  ret = take_stdin_message (info, &in_name);
  if (ret)
    goto exit;
  else if (!in_name)
    goto cleanup;

  if (gst_structure_has_field (props, in_name)) {
    GObject *object = G_OBJECT (element);
    GParamSpec *propspec = NULL;
    GValue prop_value = G_VALUE_INIT;
    const gchar *prop_name = NULL;
    gchar *in_value = NULL;

    prop_name = gst_structure_get_string (props, in_name);
    g_free (in_name);

    propspec = g_object_class_find_property (
        G_OBJECT_GET_CLASS (object), prop_name);

    if (!retrieve_option_info (G_OBJECT (element), propspec))
      goto cleanup;

    if (propspec->flags & G_PARAM_WRITABLE)
      SWITCH_MSG ("Enter value:\n");
    else
      SWITCH_MSG ("none writable value, press enter to continue.\n");

    ret = take_stdin_message (info, &in_value);
    if (ret)
      goto exit;
    else if (!in_value)
      goto cleanup;

    if (!(propspec->flags & G_PARAM_WRITABLE)) {
      g_free (in_value);
      goto cleanup;
    }

    g_value_init (&prop_value, G_PARAM_SPEC_VALUE_TYPE (propspec));
    gst_value_deserialize (&prop_value, in_value);
    g_free (in_value);

    g_object_set_property (G_OBJECT(element), prop_name, &prop_value);
  } else {
    SWITCH_ERROR ("Unsupport option: %s\n", GST_STR_NULL(in_name));
    g_free (in_name);
  }

cleanup:
  if (props)
    gst_structure_free (props);

  return FALSE;

exit:
  if (props)
    gst_structure_free (props);

  return TRUE;
}

gboolean
read_stdin_messages (GIOChannel* source, GIOCondition condition, gpointer data)
{
  GAsyncQueue *queue = NULL;
  gchar *input = NULL;
  GIOStatus status = G_IO_STATUS_NORMAL;

  g_return_val_if_fail (data != NULL, FALSE);

  queue = (GAsyncQueue *)data;

  do {
    GError *error = NULL;

    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);
    switch (status) {
      case G_IO_STATUS_ERROR:
        SWITCH_ERROR ("failed to read line from stdin, error: %s\n",
          GST_STR_NULL (error->message));
        g_clear_error (&error);
        return FALSE;
      case G_IO_STATUS_EOF:
        SWITCH_ERROR ("IO status: EOF\n");
        return FALSE;
      case G_IO_STATUS_NORMAL:
      case G_IO_STATUS_AGAIN:
        break;
      default:
        SWITCH_ERROR ("Unknown IO status\n");
        return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  g_async_queue_push (queue, (gpointer)g_strstrip (input));

  return TRUE;
}

void
read_stdin_removed (gpointer data)
{
  GstPropMenuInfo *info = NULL;

  g_return_if_fail (data != NULL);

  info = (GstPropMenuInfo *)data;

  SWITCH_DEBUG ("stop reading from stdin.\n");
  info->watch_stdin = 0;
}

void
prop_menu_init (GstPropMenuInfo **minfo, gpointer userdata)
{
  GstPropMenuInfo *info = NULL;
  GstSwitchPipelineControl *pcontrol = (GstSwitchPipelineControl *)userdata;

  g_return_if_fail (pcontrol != NULL);
  g_return_if_fail (pcontrol->camera != NULL);

  info = g_new0 (GstPropMenuInfo, 1);
  if (info == NULL) {
    SWITCH_ERROR ("failed to allocate menu thread info\n");
    g_free (info);
    info = NULL;

    return;
  }

  info->iochannel_stdin = g_io_channel_unix_new (fileno(stdin));
  info->watch_stdin = g_io_add_watch_full (info->iochannel_stdin,
      G_PRIORITY_DEFAULT, (GIOCondition)(G_IO_IN | G_IO_PRI),
      read_stdin_messages, pcontrol->menu_msgs,
      (GDestroyNotify)read_stdin_removed);
  if (!info->iochannel_stdin || !info->watch_stdin) {
    GError *error = NULL;

    if (info->watch_stdin)
      g_source_remove (info->watch_stdin);

    if (info->iochannel_stdin)
      g_io_channel_shutdown (info->iochannel_stdin, TRUE, &error);

    g_clear_error (&error);

    g_free (info);

    return;
  }

  info->queue = pcontrol->menu_msgs;
  info->camera = pcontrol->camera;
  gst_object_ref (info->camera);

  *minfo = info;
}

void
prop_menu_deinit (GstPropMenuInfo *info)
{
  GError *error = NULL;

  g_return_if_fail (info != NULL);

  if (info->watch_stdin)
    g_source_remove (info->watch_stdin);

  if (info->iochannel_stdin)
    g_io_channel_shutdown (info->iochannel_stdin, TRUE, &error);

  g_clear_error (&error);

  if (info->camera) {
    gst_object_unref (info->camera);
    info->camera = NULL;
  }

  g_free (info);

  SWITCH_MSG ("menu thread cleaned\n");
}

static gpointer
prop_menu (gpointer userdata)
{
  GstPropMenuInfo *info = NULL;

  g_return_val_if_fail (userdata != NULL, NULL);

  prop_menu_init (&info, userdata);
  if (!info)
    return NULL;

  while (!element_properties (info)) {}

  prop_menu_deinit (info);

  return NULL;
}

gboolean
pipeline_prepare_to_run (GstSwitchPipeline *pipeline)
{
  GstSwitchPipelineControl *pcontrol = NULL;
  GstSwitchPipelineInfo *pinfo = NULL;
  GstStateChangeReturn ret;
  GstState state, pending;
  ::camera::CameraMetadata session_metadata(128, 128);
  ::camera::CameraMetadata *pstatic_meta = nullptr;
  uint32_t tag;
  uint8_t tag_val;
  gboolean metadata_update = FALSE;

  g_return_val_if_fail (pipeline != NULL, FALSE);
  pcontrol = &pipeline->control;
  pinfo = &pipeline->info;

  ret = gst_element_set_state (pcontrol->pipeline, GST_STATE_PAUSED);
  SWITCH_MSG ("set pipeline to PAUSED state, return val(%d)\n", ret);

  // prepare static meta data for vendor tags searching
  g_object_get (G_OBJECT (pcontrol->camera), "static-metadata",
      &pstatic_meta, NULL);

  if (pinfo->log_cam_mode != GST_LOGICAL_CAMERA_MODE_NONE) {
    gint ret;

    ret = pstatic_meta->getTagFromName ("android.control.extendedSceneMode",
        NULL, &tag);

    if (ret == 0) {

      SWITCH_MSG ("extendedSceneMode (%d) found, set to %s\n", tag,
          pinfo->log_cam_mode == GST_LOGICAL_CAMERA_MODE_SAT ? "SAT" : "RTB");

      tag_val = (uint8_t)pinfo->log_cam_mode;
      session_metadata.update (tag, &tag_val, 1);
      metadata_update = TRUE;
    } else {
      SWITCH_MSG ("extendedSceneMode not found\n");
    }
  }

  if (metadata_update == TRUE)
    g_object_set (G_OBJECT (pcontrol->camera), "session-metadata",
        &session_metadata, NULL);

  pipeline_remove_video_streams (pipeline);
  pipeline_switchstream_video_control (pipeline, FALSE);
  pipeline_activate_video_streams_sources (pipeline, FALSE);
  SWITCH_MSG ("remove video streams\n");

  ret = gst_element_set_state (pcontrol->pipeline, GST_STATE_PLAYING);
  SWITCH_MSG ("set pipeline to PLAYING state, return val(%d)\n", ret);
  gst_element_get_state (pcontrol->pipeline, &state, &pending,
      GST_CLOCK_TIME_NONE);

  // thread to set camera's dynamic properties.
  if (pinfo->menu) {
    pcontrol->thread_menu = g_thread_new ("PropMenu", prop_menu, pcontrol);
    if (!pcontrol->thread_menu) {
      SWITCH_ERROR ("failed to create menu thread!\n");
      return FALSE;
    }
  }

  pcontrol->menu_msgs = g_async_queue_new_full ((GDestroyNotify) g_free);
  if (!pcontrol->menu_msgs) {
    SWITCH_ERROR ("failed to allocate GAsyncQueue\n");
    return FALSE;
  }

  pcontrol->mode = GST_SWITCH_RUN_MODE_PREVIEW;
  SWITCH_MSG ("%d round start\n", pcontrol->current_round + 1);
  SWITCH_MSG ("*** Current Mode: Preview ***\n");

  // add timer function to execute streams switch
  g_timeout_add (pinfo->duration * 1000, pipeline_switch_tmr_func, pipeline);

  if (pinfo->sensor_switch)
    g_timeout_add (PIPELINE_SENSOR_SWITCH_SHIFT_MS,
        pipeline_sensor_switch_tmr_func, pipeline);

  return TRUE;
}

gint
main (gint argc, gchar* argv[])
{
  GOptionContext *ctx;
  GstSwitchPipeline *pipeline;
  GError *error = NULL;
  gint ret = 0;

  gst_init (&argc, &argv);

  pipeline = pipeline_streams_alloc (3, 3);

  if (pipeline == NULL) {
    SWITCH_ERROR ("fail to allocate pipeline and streams\n");
    return -EFAULT;
  }

  ctx = g_option_context_new ("fastswitch example options\n");
  if (ctx == NULL) {
    SWITCH_ERROR ("create option context fail\n");
    ret = -EFAULT;
    goto free0;
  }

  // add --debug
  g_option_context_add_main_entries (ctx, debug_option, NULL);

  // add pipeline and stream options
  pipeline_streams_alloc_options (ctx, pipeline);

  // add system default options
  g_option_context_add_group (ctx, gst_init_get_option_group());

  // parse options
  if (! g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_option_context_free (ctx);
    SWITCH_ERROR ("failed to parse command line options:%s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);
    ret = -EFAULT;
    goto free1;
  }
  g_option_context_free (ctx);

  if (! check_pipeline_streams_options (pipeline)) {
    SWITCH_ERROR ("check options failed\n");
    ret = -EFAULT;
    goto free1;
  }

  if (! pipeline_streams_init (pipeline)) {
    SWITCH_ERROR ("pipeline and streams init failed\n");
    ret = -EFAULT;
    goto free1;
  }

  if (! pipeline_add_streams (pipeline)) {
    SWITCH_ERROR ("pipeline add streams failed\n");
    ret = -EFAULT;
    goto free2;
  }

  if (! pipeline_signals_register (pipeline)) {
    SWITCH_ERROR ("pipeline register signals failed\n");
    ret = -EFAULT;
    goto free3;
  }

  if (! pipeline_prepare_to_run (pipeline)) {
    SWITCH_ERROR ("prepare pipeline to run failed\n");
    ret = -EFAULT;
    goto free3;
  }

  g_main_loop_run (pipeline->control.mloop);

  gst_element_set_state (pipeline->control.pipeline, GST_STATE_NULL);

free3:
  pipeline_remove_streams (pipeline);

free2:
  pipeline_streams_deinit (pipeline);

free1:
  pipeline_streams_free_options (pipeline);

free0:
  pipeline_streams_free (pipeline);

  return ret;
}
