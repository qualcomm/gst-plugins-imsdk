/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "suite-camera-pipeline.h"

#include <string.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <gst/base/gstbasesink.h>
#include <gst/pbutils/pbutils.h>

void
print_tags (const GstTagList *tags, gchar *tag) {
  GValue val = G_VALUE_INIT;
  gchar *str;

  if (gst_tag_list_copy_value (&val, tags, tag)) {
    if (G_VALUE_HOLDS_STRING (&val)) {
      str = g_value_dup_string (&val);
    } else {
      str = gst_value_serialize (&val);
    }
    GST_DEBUG ("MP4 Tag: %s Value: %s ", tag, str);
  }

  g_free (str);
  g_value_unset (&val);
}

gboolean
mp4_check_video_info (GstDiscovererStreamInfo *stream_info,
    gint inwidth, gint inheight, gdouble inframerate, gdouble diff) {
  GList *tmp, *streams;
  streams = gst_discoverer_container_info_get_streams (
      GST_DISCOVERER_CONTAINER_INFO (stream_info));

  for (tmp = streams; tmp; tmp = tmp->next) {
    GstDiscovererStreamInfo *tmpinf = (GstDiscovererStreamInfo *) tmp->data;
    if (GST_IS_DISCOVERER_VIDEO_INFO (tmpinf)) {
      GstDiscovererVideoInfo *video_info = GST_DISCOVERER_VIDEO_INFO (tmpinf);
      gint width = gst_discoverer_video_info_get_width (video_info);
      gint height = gst_discoverer_video_info_get_height (video_info);
      gdouble framerate = gst_discoverer_video_info_get_framerate_num (
          video_info) / (gdouble)
          gst_discoverer_video_info_get_framerate_denom (video_info);

      // Compare
      GST_DEBUG ("Video width: %d, height: %d, Framerate: %.2f fps\n\n",
          width, height, framerate);
      if (inwidth != width || inheight != height || (inframerate-framerate) > diff)
        return FALSE;
    } else {
      GST_DEBUG("Stream is not a video stream\n");
    }
  }

  gst_discoverer_stream_info_list_free (streams);
  return TRUE;
}

gboolean
mp4_check (gchar *location, gint width, gint height,
    gdouble framerate, gdouble diff, guint induration) {

  fail_unless_equals_int (g_file_test (location,
      G_FILE_TEST_EXISTS), TRUE);

  gchar *prefix = "file://";
  gchar *expand_location = g_strconcat(prefix, location, NULL);

  GstDiscoverer *discoverer = gst_discoverer_new (GST_SECOND, NULL);
  GstDiscovererInfo *info = gst_discoverer_discover_uri (discoverer,
    expand_location, NULL);
  GST_DEBUG ("Done discovering %s\n", gst_discoverer_info_get_uri (info));

  if (info) {
    GstClockTime duration = gst_discoverer_info_get_duration (info);
    GST_DEBUG ("Duration: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS (duration));
    // if induration not 0, means check duration
    if (!induration) {
      if ((guint) duration != induration)
        g_free (expand_location);
        gst_discoverer_info_unref (info);
        g_object_unref (discoverer);
        return FALSE;
    }

    const GstTagList *tags = gst_discoverer_info_get_tags (info);
    if (tags) {
      gst_tag_list_foreach (tags, print_tags, NULL);
    } else {
      GST_DEBUG ("No tags found\n");
    }

    GstDiscovererStreamInfo *sinfo = gst_discoverer_info_get_stream_info (info);
    if (sinfo) {
      if (!mp4_check_video_info (sinfo, width, height, framerate, diff)) {
        g_free (expand_location);
        gst_discoverer_info_unref (info);
        g_object_unref (discoverer);
        return FALSE;
      }
    } else {
      GST_DEBUG ("No streams found\n");
    }
  } else {
      g_free(expand_location);
      gst_discoverer_info_unref (info);
      g_object_unref (discoverer);

      GST_DEBUG ("No info found\n");
      return FALSE;
  }

  g_free(expand_location);
  gst_discoverer_info_unref (info);
  g_object_unref (discoverer);
  return TRUE;
}

void
camera_pipeline (GstCapsParameters * params1,
    GstCapsParameters * params2, GstCapsParameters * rawparams,
    GstCapsParameters * jpegparams, guint duration) {
  GstElement *pipeline, *qtiqmmfsrc, *capsfilter1, *sink1,
      *capsfilter2, *sink2, *capsfilter3, *sink3, *capsfilter4, *sink4;
  GstPad *previewpad;
  GstCaps *filtercaps;
  GstMessage *msg;

  if (params1 == NULL)
    return;

  pipeline = gst_pipeline_new (NULL);

  // Create elements of first video stream.
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter1 = gst_element_factory_make ("capsfilter", "capsfilter1");
  sink1 = gst_element_factory_make ("fakesink", "fakesink1");

  fail_unless (pipeline && qtiqmmfsrc && capsfilter1 && sink1);

  // Configure caps of video stream1.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params1->format,
      "width", G_TYPE_INT, params1->width,
      "height", G_TYPE_INT, params1->height,
      "framerate", GST_TYPE_FRACTION, params1->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter1), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), qtiqmmfsrc, capsfilter1,
      sink1, NULL);

  // Create video stream.
  if (params2 != NULL) {
    capsfilter2 = gst_element_factory_make ("capsfilter", "capsfilter2");
    sink2 = gst_element_factory_make ("fakesink", "fakesink2");

    fail_unless (capsfilter2 && sink2);

    // Configure caps of video stream.
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, params2->format,
        "width", G_TYPE_INT, params2->width,
        "height", G_TYPE_INT, params2->height,
        "framerate", GST_TYPE_FRACTION, params2->fps, 1,
        NULL);
    g_object_set (G_OBJECT (capsfilter2), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    // Add elements to the pipeline
    gst_bin_add_many (GST_BIN (pipeline), capsfilter2, sink2, NULL);
  }

  // Create raw snapshot stream.
  if (rawparams != NULL) {
    capsfilter3 = gst_element_factory_make ("capsfilter", "capsfilter3");
    sink3 = gst_element_factory_make ("fakesink", "fakesink3");

    fail_unless (capsfilter3 && sink3);

    // Configure caps of raw stream.
    filtercaps = gst_caps_new_simple ("video/x-bayer",
        "format", G_TYPE_STRING, rawparams->format,
        "bpp", G_TYPE_STRING, "10",
        "width", G_TYPE_INT, rawparams->width,
        "height", G_TYPE_INT, rawparams->height,
        "framerate", GST_TYPE_FRACTION, rawparams->fps, 1,
        NULL);
    g_object_set (G_OBJECT (capsfilter3), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    g_object_set (G_OBJECT (sink3), "sync", FALSE, NULL);
    g_object_set (G_OBJECT (sink3), "async", FALSE, NULL);
    g_object_set (G_OBJECT (sink3), "enable-last-sample", FALSE, NULL);

    gst_bin_add_many (GST_BIN (pipeline), capsfilter3, sink3, NULL);
  }

  // Create jpeg snapshot stream.
  if (jpegparams!= NULL) {
    capsfilter4 = gst_element_factory_make ("capsfilter", "capsfilter4");
    sink4 = gst_element_factory_make ("fakesink", "fakesink4");

    fail_unless (capsfilter4 && sink4);

    // Configure caps of jpeg snapshot stream.
    filtercaps = gst_caps_new_simple ("image/jpeg",
        "width", G_TYPE_INT, jpegparams->width,
        "height", G_TYPE_INT, jpegparams->height,
        NULL);
    g_object_set (G_OBJECT (capsfilter4), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    g_object_set (G_OBJECT (sink4), "sync", FALSE, NULL);
    g_object_set (G_OBJECT (sink4), "async", FALSE, NULL);
    g_object_set (G_OBJECT (sink4), "enable-last-sample", FALSE, NULL);

    gst_bin_add_many (GST_BIN (pipeline), capsfilter4, sink4, NULL);
  }

  fail_unless (gst_element_link_many (qtiqmmfsrc, capsfilter1,
      sink1, NULL));

  // Set video_0 to preview.
  previewpad = gst_element_get_static_pad (qtiqmmfsrc, "video_0");
  fail_unless (previewpad);
  g_object_set (G_OBJECT (previewpad), "type", 1, NULL);

  if (params2 != NULL)
    fail_unless (gst_element_link_many (qtiqmmfsrc, capsfilter2,
        sink2, NULL));

  if (rawparams != NULL) {
    fail_unless (gst_element_link_pads (qtiqmmfsrc, "image_2",
        capsfilter3, NULL));
    fail_unless (gst_element_link_many (capsfilter3, sink3, NULL));
  }

  if (jpegparams != NULL) {
    fail_unless (gst_element_link_pads (qtiqmmfsrc, "image_3",
        capsfilter4, NULL));
    fail_unless (gst_element_link_many (capsfilter4, sink4, NULL));
  }

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      duration * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  if (msg) {
    fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_EOS);
    gst_message_unref (msg);
  }

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);
}

void
camera_display_encode_pipeline (GstCapsParameters * params1,
    GstCapsParameters * params2, guint duration) {

  GstElement *pipeline, *qtiqmmfsrc, *capsfilter1, *wayland;
  GstElement *capsfilter2, *filesink, *venc, *parse, *mp4mux;
  GstPad *previewpad;
  GstCaps *filtercaps;
  GstMessage *msg;

  pipeline = gst_pipeline_new (NULL);
  // Create all elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter1 = gst_element_factory_make ("capsfilter", "capsfilter1");
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");
  capsfilter2 = gst_element_factory_make ("capsfilter", "capsfilter2");
  venc = gst_element_factory_make ("v4l2h264enc", NULL);
  parse = gst_element_factory_make ("h264parse", NULL);
  mp4mux = gst_element_factory_make ("mp4mux", NULL);
  filesink = gst_element_factory_make ("filesink", NULL);

  fail_unless (pipeline && qtiqmmfsrc && capsfilter1 && capsfilter2 &&
      wayland && venc && parse && mp4mux && filesink);

  // Set filesink properties
  g_object_set (G_OBJECT (filesink), "location", "/opt/mux.mp4", NULL);
  g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params1->format,
      "width", G_TYPE_INT, params1->width,
      "height", G_TYPE_INT, params1->height,
      "framerate", GST_TYPE_FRACTION, params1->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter1), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params2->format,
      "width", G_TYPE_INT, params2->width,
      "height", G_TYPE_INT, params2->height,
      "framerate", GST_TYPE_FRACTION, params2->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter2), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), qtiqmmfsrc, capsfilter1,
      wayland, NULL);
  gst_bin_add_many (GST_BIN (pipeline), capsfilter2, venc,
      parse, mp4mux, filesink, NULL);

  fail_unless (gst_element_link_many (qtiqmmfsrc, capsfilter1, wayland, NULL));

  fail_unless (gst_element_link_many (qtiqmmfsrc, capsfilter2, venc, parse,
       mp4mux, filesink, NULL));

  previewpad = gst_element_get_static_pad (qtiqmmfsrc, "video_0");
  fail_unless (previewpad);
  // Set video_0 to preview.
  g_object_set (G_OBJECT (previewpad), "type", 1, NULL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      duration * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  if (msg) {
    fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_EOS);
    gst_message_unref (msg);
  }

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);
}

void
camera_transform_display_pipeline (GstCapsParameters * params1,
    GstCapsParameters * params2, guint duration) {

  GstElement *pipeline, *qtiqmmfsrc, *capsfilter1, *wayland;
  GstElement *capsfilter2, *vtrans, *queue;
  GstCaps *filtercaps;
  GstMessage *msg;

  pipeline = gst_pipeline_new (NULL);
  // Create all elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter1 = gst_element_factory_make ("capsfilter", "capsfilter1");
  queue = gst_element_factory_make ("queue", "queue1");
  vtrans = gst_element_factory_make ("qtivtransform", NULL);
  capsfilter2 = gst_element_factory_make ("capsfilter", "capsfilter2");
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");

  fail_unless (pipeline && qtiqmmfsrc && capsfilter1 && queue &&
      vtrans && capsfilter2 && wayland);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params1->format,
      "width", G_TYPE_INT, params1->width,
      "height", G_TYPE_INT, params1->height,
      "framerate", GST_TYPE_FRACTION, params1->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter1), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Rotate 90CW.
  g_object_set (G_OBJECT (vtrans), "rotate", 1, NULL);

  // Configure vtrans output caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params2->format,
      "width", G_TYPE_INT, params2->width,
      "height", G_TYPE_INT, params2->height,
      "framerate", GST_TYPE_FRACTION, params2->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter2), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), qtiqmmfsrc, capsfilter1, queue,
      vtrans, capsfilter2, wayland, NULL);

  fail_unless (gst_element_link_many (qtiqmmfsrc, capsfilter1, queue,
      vtrans, capsfilter2, wayland, NULL));

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      duration * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  if (msg) {
    fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_EOS);
    gst_message_unref (msg);
  }

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);
}

void
camera_composer_display_pipeline (GstCapsParameters * params1,
    gchar *location, guint duration) {

  GstElement *pipeline, *qtiqmmfsrc, *capsfilter, *vcomps, *wayland;
  GstElement *filesrc, *demux, *parse, *vdec, *queue1, *queue2;
  GstPad *sink0, *sink1;
  GstCaps *filtercaps;
  GstMessage *msg;

  pipeline = gst_pipeline_new (NULL);
  // Create all elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter1");
  queue1 = gst_element_factory_make ("queue", "queue1");
  filesrc = gst_element_factory_make ("filesrc", NULL);
  demux = gst_element_factory_make ("qtdemux", NULL);
  parse = gst_element_factory_make ("h264parse", NULL);
  vdec = gst_element_factory_make ("v4l2h264dec", NULL);
  queue2 = gst_element_factory_make ("queue", "queue2");

  vcomps = gst_element_factory_make ("qtivcomposer", "mixer");
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");

  fail_unless (pipeline && qtiqmmfsrc && capsfilter && queue1 &&
        filesrc && demux && parse && vdec && queue2 && vcomps && wayland);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params1->format,
      "width", G_TYPE_INT, params1->width,
      "height", G_TYPE_INT, params1->height,
      "framerate", GST_TYPE_FRACTION, params1->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  g_object_set (filesrc, "location", location, NULL);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), qtiqmmfsrc, capsfilter, queue1,
        filesrc, demux, parse, vdec, queue2, vcomps, wayland, NULL);

  fail_unless (gst_element_link_many (qtiqmmfsrc, capsfilter, queue1,
        vcomps, wayland, NULL));
  fail_unless (gst_element_link_many (filesrc, demux, parse, vdec, queue2,
        vcomps, NULL));

  sink0 = gst_element_get_static_pad (qtiqmmfsrc, "sink_0");
  sink1 = gst_element_get_static_pad (qtiqmmfsrc, "sink_1");
  fail_unless (sink0 && sink1);

  g_object_set (G_OBJECT (sink0), "position", "<0, 0>",
      "dimensions", "<640, 480>", NULL);

  g_object_set (G_OBJECT (sink1), "position", "<640, 0>",
      "dimensions", "<640, 480>", NULL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline), duration*GST_SECOND,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  if (msg) {
    fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_EOS);
    gst_message_unref (msg);
  }

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);

}

void on_pad_added (GstElement *element, GstPad *pad, gpointer data)
{

  GstPad *sink_pad = gst_element_get_static_pad ((GstElement *)data, "sink");
  if (gst_pad_is_linked (sink_pad)) {
    g_object_unref (sink_pad);
    return;
  }

  GstCaps *new_pad_caps = gst_pad_get_current_caps (pad);
  GstStructure *new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  const gchar *new_pad_type = gst_structure_get_name (new_pad_struct);

  if (g_str_has_prefix (new_pad_type, "video/x-h264") ||
      g_str_has_prefix (new_pad_type, "video/x-h265")) {
    gst_pad_link(pad, sink_pad);
  }

  gst_caps_unref (new_pad_caps);
  g_object_unref (sink_pad);
}

void
camera_decoder_display_pipeline (guint duration)
{

  GstElement *pipeline, *filesrc, *demux, *parse, *vdec, *wayland;
  GstMessage *msg;

  pipeline = gst_pipeline_new (NULL);
  // Create all elements
  filesrc = gst_element_factory_make ("filesrc", NULL);
  demux = gst_element_factory_make ("qtdemux", NULL);
  parse = gst_element_factory_make ("h264parse", NULL);
  vdec = gst_element_factory_make ("v4l2h264dec", NULL);
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");

  fail_unless (pipeline && filesrc && demux && parse && vdec && wayland);
  fail_unless_equals_int (mp4_check (CASE_DECODE_DISPLAT_FILE,
      1920, 1080, 30.0f, 0.5f, 180014), 1);

  // Configure the stream
  g_object_set (filesrc, "location", CASE_DECODE_DISPLAT_FILE, NULL);
  g_object_set (wayland, "sync", TRUE, NULL);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), filesrc, demux, parse,
      vdec, wayland, NULL);
  fail_unless (gst_element_link (filesrc, demux));
  fail_unless (gst_element_link_many (parse, vdec, wayland, NULL));
  g_signal_connect (demux, "pad-added", G_CALLBACK (on_pad_added), parse);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      duration * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  if (msg) {
    fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_EOS);
    gst_message_unref (msg);
  }

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);
}
