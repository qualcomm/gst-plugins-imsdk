/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <config.h>

#include "gsthexagonnn.h"

#define GST_VIDEO_FORMATS "{NV12, NV21}"

#define GST_CAT_DEFAULT gst_hexagonnn_debug
GST_DEBUG_CATEGORY_STATIC (gst_hexagonnn_debug);

#define gst_hexagonnn_parent_class parent_class
G_DEFINE_TYPE (GstHexagonNN, gst_hexagonnn, GST_TYPE_VIDEO_FILTER);

static GstStaticCaps gst_hexagonnn_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS));

enum {
  PROP_0,
  PROP_HEXAGONNN_ENGINE,
  PROP_HEXAGONNN_MODE,
  PROP_HEXAGONNN_SKIP_COUNT,
  PROP_HEXAGONNN_DATA_FOLDER,
  PROP_HEXAGONNN_LABEL_FILE
};


static GstCaps *
gst_hexagonnn_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;
  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_hexagonnn_format_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_hexagonnn_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_hexagonnn_caps ());
}

static GstPadTemplate *
gst_hexagonnn_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_hexagonnn_caps ());
}

static void
gst_hexagonnn_finalize (GObject * object)
{
  GstHexagonNN *hnn = GST_HEXAGONNN (object);

  if (hnn->engine) {
    hnn->engine->DeInit();
    delete (hnn->engine);
    hnn->engine = nullptr;
  }

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (hnn));
}

static void
gst_hexagonnn_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHexagonNN *hnn = GST_HEXAGONNN (object);

  GST_OBJECT_LOCK (hnn);
  switch (prop_id) {
    case PROP_HEXAGONNN_ENGINE:
      hnn->model = g_strdup(g_value_get_string (value));
      break;
    case PROP_HEXAGONNN_MODE:
      hnn->mode = g_strdup(g_value_get_string (value));
      break;
    case PROP_HEXAGONNN_SKIP_COUNT:
      hnn->frame_skip_count = g_value_get_uint (value);
      break;
    case PROP_HEXAGONNN_DATA_FOLDER:
      hnn->data_folder = g_strdup(g_value_get_string (value));
      break;
    case PROP_HEXAGONNN_LABEL_FILE:
      hnn->label_file = g_strdup(g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (hnn);
}

static void
gst_hexagonnn_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstHexagonNN *hnn = GST_HEXAGONNN (object);

  GST_OBJECT_LOCK (hnn);
  switch (prop_id) {
    case PROP_HEXAGONNN_ENGINE:
      g_value_set_string (value, hnn->model);
      break;
    case PROP_HEXAGONNN_MODE:
      g_value_set_string (value, hnn->mode);
      break;
    case PROP_HEXAGONNN_SKIP_COUNT:
      g_value_set_uint (value, hnn->frame_skip_count);
      break;
    case PROP_HEXAGONNN_DATA_FOLDER:
      g_value_set_string (value, hnn->data_folder);
      break;
    case PROP_HEXAGONNN_LABEL_FILE:
      g_value_set_string (value, hnn->label_file);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (hnn);
}


static gboolean
gst_hexagonnn_set_info (GstVideoFilter * filter, GstCaps * in,
    GstVideoInfo * ininfo, GstCaps * out, GstVideoInfo * outinfo)
{
  GstHexagonNN *hnn = GST_HEXAGONNN (filter);
  NNImgFormat new_format;

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);

  switch (GST_VIDEO_INFO_FORMAT (ininfo)) {
    case GST_VIDEO_FORMAT_NV12:
      new_format = NN_FORMAT_NV12;
      break;
    case GST_VIDEO_FORMAT_NV21:
      new_format = NN_FORMAT_NV21;
      break;
    default:
      GST_ERROR_OBJECT (hnn, "Unhandled gst format: %d",
        GST_VIDEO_INFO_FORMAT (ininfo));
      return FALSE;
  }

  if (hnn->engine &&
      hnn->source_info.img_format == new_format &&
      hnn->source_info.img_width == GST_VIDEO_INFO_WIDTH (ininfo) &&
      hnn->source_info.img_height == GST_VIDEO_INFO_HEIGHT (ininfo)) {
    GST_DEBUG_OBJECT (hnn, "Already initialized");
    return TRUE;
  }

  if (hnn->engine) {
    GST_DEBUG_OBJECT (hnn, "Re-initialize");
    hnn->engine->DeInit ();
    delete (hnn->engine);
    hnn->engine = nullptr;
  }

  if (!hnn->model) {
    GST_ERROR_OBJECT (hnn, "Engine name is not set");
  }

  if (!g_strcmp0(hnn->model, "segmentation")) {
    hnn->engine = static_cast <NNEngine *> (new DeepLabv3Engine ());
  } else if (!g_strcmp0(hnn->model, "posenet")) {
    hnn->engine = static_cast <NNEngine *> (new PoseNetEngine ());
  } else if (!g_strcmp0(hnn->model, "mnetssd")) {
    hnn->engine = static_cast <NNEngine *> (new MnetSSDEngine ());
  } else {
    GST_ERROR_OBJECT (hnn, "Cannot find engine for: %s", hnn->model);
    return FALSE;
  }

  hnn->source_info.img_width = GST_VIDEO_INFO_WIDTH (ininfo);
  hnn->source_info.img_height = GST_VIDEO_INFO_HEIGHT (ininfo);
  hnn->source_info.img_format = new_format;
  g_strlcpy (hnn->source_info.data_folder, hnn->data_folder,
      sizeof (hnn->source_info.data_folder));
  g_strlcpy (hnn->source_info.label_file, hnn->data_folder,
      sizeof (hnn->source_info.label_file));

  if (hnn->engine->Init (&hnn->source_info)) {
    GST_ERROR_OBJECT (hnn, "Engine Init Failed");
    delete (hnn->engine);
    hnn->engine = nullptr;
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_hexagonnn_transform_frame_ip (GstVideoFilter * trans,
                                     GstVideoFrame * frame)
{
  GstHexagonNN *hnn = GST_HEXAGONNN (trans);
  guint ret;

  NNFrameInfo pFrameInfo;
  pFrameInfo.frame_data[0] = (uint8_t * ) GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  pFrameInfo.frame_data[1] = (uint8_t * ) GST_VIDEO_FRAME_PLANE_DATA (frame, 1);
  pFrameInfo.stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);

  if (!g_strcmp0(hnn->mode, "default")) {
    ret = hnn->engine->Process (&pFrameInfo, frame->buffer, true);
  } else if (!g_strcmp0(hnn->mode, "auto")) {
    ret = hnn->engine->Process (&pFrameInfo, frame->buffer, false);
  } else if (!g_strcmp0(hnn->mode, "frame_skip_count")) {
    ret = hnn->engine->Process (&pFrameInfo, frame->buffer,
      hnn->frame_skip_count);
  } else {
    ret = NN_FAIL;
  }
  if (ret != NN_OK) {
    GST_ERROR_OBJECT (hnn, "Engine Process Failed! mode %s skip: %d",
      hnn->mode, hnn->frame_skip_count);
  }

  return GST_FLOW_OK;
}

static void
gst_hexagonnn_class_init (GstHexagonNNClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstVideoFilterClass *filter = GST_VIDEO_FILTER_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_hexagonnn_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_hexagonnn_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_hexagonnn_finalize);

  g_object_class_install_property(
          gobject,
          PROP_HEXAGONNN_ENGINE,
          g_param_spec_string(
              "model-name",
              "model",
              "Specify which of model to execute."
              "Supported models: segmentation, posenet, mnetssd",
              "segmentation",
              static_cast<GParamFlags>(G_PARAM_CONSTRUCT |
                                       G_PARAM_READWRITE |
                                       G_PARAM_STATIC_STRINGS )));

  g_object_class_install_property(
          gobject,
          PROP_HEXAGONNN_MODE,
          g_param_spec_string(
              "execution-mode",
              "mode",
              "Specify operating mode of execution. "
              "Supported models: default, auto, frame_skip_count",
              "default",
              static_cast<GParamFlags>(G_PARAM_CONSTRUCT |
                                       G_PARAM_READWRITE |
                                       G_PARAM_STATIC_STRINGS )));

  g_object_class_install_property(
          gobject,
          PROP_HEXAGONNN_SKIP_COUNT,
          g_param_spec_uint(
              "skip-count",
              "count",
              "Specify frame skip count. "
              "Work only if operating mode is frame_skip_count",
              0,
              30,
              0,
              static_cast<GParamFlags>(G_PARAM_CONSTRUCT |
                                       G_PARAM_READWRITE |
                                       G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property(
          gobject,
          PROP_HEXAGONNN_DATA_FOLDER,
          g_param_spec_string(
              "data-folder",
              "path",
              "Specify folder where label files are stored. "
              "Default forlder: /data/misc/camera",
              "/data/misc/camera",
              static_cast<GParamFlags>(G_PARAM_CONSTRUCT |
                                       G_PARAM_READWRITE |
                                       G_PARAM_STATIC_STRINGS )));

  g_object_class_install_property(
          gobject,
          PROP_HEXAGONNN_LABEL_FILE,
          g_param_spec_string(
              "label_file",
              "label",
              "Specify name of label file if any. "
              "If label file is not set default name will be used.",
              "default",
              static_cast<GParamFlags>(G_PARAM_CONSTRUCT |
                                       G_PARAM_READWRITE |
                                       G_PARAM_STATIC_STRINGS )));


  gst_element_class_set_static_metadata (gstelement_class,
      "hexagonnn", "Filter/Effect/Converter/Video/Scaler",
      "hexagonnn", "QTI");

  gst_element_class_add_pad_template (gstelement_class,
    gst_hexagonnn_sink_template ());
  gst_element_class_add_pad_template (gstelement_class,
    gst_hexagonnn_src_template ());

  filter->set_info = GST_DEBUG_FUNCPTR (gst_hexagonnn_set_info);
  filter->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_hexagonnn_transform_frame_ip);

  GST_DEBUG_CATEGORY_INIT (gst_hexagonnn_debug, "qtihexagonnn", 0,
      "QTI HexagonNN plugin");

}

static void
gst_hexagonnn_init (GstHexagonNN * hexagonnn)
{
  hexagonnn->model = nullptr;
  hexagonnn->engine = nullptr;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtihexagonnn", GST_RANK_PRIMARY,
      GST_TYPE_HEXAGONNN);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtihexagonnn,
    "QTI HexagonNN plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
