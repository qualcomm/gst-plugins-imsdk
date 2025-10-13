/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "suite-camera-pipeline.h"

#include <string.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <gst/base/gstbasesink.h>

void
camera_pipeline (GstCapsParameters * params0,
    GstCapsParameters * params1, GstCapsParameters * rawparams,
    GstCapsParameters * jpegparams, guint duration) {
  GstElement *pipeline, *qmmfsrc, *capsfilter0, *wayland,
      *capsfilter1, *sink1, *capsfilter2, *sink2, *capsfilter3, *sink3;
  GList *plugins = NULL;
  GstPad *previewpad;
  GstCaps *filtercaps;
  GstMessage *msg;

  if (params0 == NULL)
    return;

  pipeline = gst_pipeline_new (NULL);

  // Create elements of first video stream.
  qmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qmmfsrc");
  capsfilter0 = gst_element_factory_make ("capsfilter", "capsfilter0");
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");

  fail_unless (pipeline && qmmfsrc && capsfilter0 && wayland);

  // Add to GList for destroy.
  plugins = g_list_append (plugins, qmmfsrc);
  plugins = g_list_append (plugins, capsfilter0);
  plugins = g_list_append (plugins, wayland);

  // Configure caps of video stream1.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params0->format,
      "width", G_TYPE_INT, params0->width,
      "height", G_TYPE_INT, params0->height,
      "framerate", GST_TYPE_FRACTION, params0->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter0), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), qmmfsrc, capsfilter0,
      wayland, NULL);

  // Create video stream.
  if (params1 != NULL) {
    capsfilter1 = gst_element_factory_make ("capsfilter", "capsfilter1");
    sink1 = gst_element_factory_make ("fakesink", "fakesink1");

    fail_unless (capsfilter1 && sink1);

    // Configure caps of video stream.
    filtercaps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, params1->format,
        "width", G_TYPE_INT, params1->width,
        "height", G_TYPE_INT, params1->height,
        "framerate", GST_TYPE_FRACTION, params1->fps, 1,
        NULL);
    g_object_set (G_OBJECT (capsfilter1), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    // Add elements to the pipeline
    gst_bin_add_many (GST_BIN (pipeline), capsfilter1, sink1, NULL);

    plugins = g_list_append (plugins, capsfilter1);
    plugins = g_list_append (plugins, sink1);
  }

  // Create raw snapshot stream.
  if (rawparams != NULL) {
    capsfilter2 = gst_element_factory_make ("capsfilter", "capsfilter2");
    sink2 = gst_element_factory_make ("fakesink", "fakesink2");

    fail_unless (capsfilter2 && sink2);

    // Configure caps of raw stream.
    filtercaps = gst_caps_new_simple ("video/x-bayer",
        "format", G_TYPE_STRING, rawparams->format,
        "bpp", G_TYPE_STRING, "10",
        "width", G_TYPE_INT, rawparams->width,
        "height", G_TYPE_INT, rawparams->height,
        "framerate", GST_TYPE_FRACTION, rawparams->fps, 1,
        NULL);
    g_object_set (G_OBJECT (capsfilter2), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    g_object_set (G_OBJECT (sink2),
        "sync", FALSE, "async", FALSE,
        "enable-last-sample", FALSE, NULL);

    gst_bin_add_many (GST_BIN (pipeline), capsfilter2, sink2, NULL);

    plugins = g_list_append (plugins, capsfilter2);
    plugins = g_list_append (plugins, sink2);
  }

  // Create jpeg snapshot stream.
  if (jpegparams!= NULL) {
    capsfilter3 = gst_element_factory_make ("capsfilter", "capsfilter3");
    sink3 = gst_element_factory_make ("fakesink", "fakesink3");

    fail_unless (capsfilter3 && sink3);

    // Configure caps of jpeg snapshot stream.
    filtercaps = gst_caps_new_simple ("image/jpeg",
        "width", G_TYPE_INT, jpegparams->width,
        "height", G_TYPE_INT, jpegparams->height,
        NULL);
    g_object_set (G_OBJECT (capsfilter3), "caps", filtercaps, NULL);
    gst_caps_unref (filtercaps);

    g_object_set (G_OBJECT (sink3),
        "sync", FALSE, "async", FALSE,
        "enable-last-sample", FALSE, NULL);

    gst_bin_add_many (GST_BIN (pipeline), capsfilter3, sink3, NULL);

    plugins = g_list_append (plugins, capsfilter3);
    plugins = g_list_append (plugins, sink3);
  }

  fail_unless (gst_element_link_many (qmmfsrc, capsfilter0,
      wayland, NULL));

  // Set video_0 to preview.
  previewpad = gst_element_get_static_pad (qmmfsrc, "video_0");
  fail_unless (previewpad);
  g_object_set (G_OBJECT (previewpad), "type", 1, NULL);

  if (params1 != NULL)
    fail_unless (gst_element_link_many (qmmfsrc, capsfilter1,
        sink1, NULL));

  if (rawparams != NULL) {
    fail_unless (gst_element_link_pads (qmmfsrc, "image_2",
        capsfilter2, NULL));
    fail_unless (gst_element_link_many (capsfilter2, sink2, NULL));
  }

  if (jpegparams != NULL) {
    fail_unless (gst_element_link_pads (qmmfsrc, "image_3",
        capsfilter3, NULL));
    fail_unless (gst_element_link_many (capsfilter3, sink3, NULL));
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

  // Send eos to pipeline.
  fail_unless_equals_int (gst_element_send_eos (pipeline), 1);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_destroy_pipeline (&pipeline, &plugins);
}

void
camera_display_encode_pipeline (GstCapsParameters * params0,
    GstCapsParameters * params1, guint duration) {
  GstElement *pipeline, *qmmfsrc, *capsfilter0, *queue0, *wayland;
  GstElement *capsfilter1, *queue1, *venc, *parse, *mp4mux, *filesink;
  GList *plugins = NULL;
  gchar *location = NULL;
  GstPad *previewpad;
  GstCaps *filtercaps;
  GstMessage *msg;
  gchar *codec = GST_VIDEO_CODEC_H264;

  pipeline = gst_pipeline_new (NULL);
  // Create all elements
  qmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qmmfsrc");
  capsfilter0 = gst_element_factory_make ("capsfilter", "capsfilter0");
  queue0 = gst_element_factory_make ("queue", "queue0");
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");
  capsfilter1 = gst_element_factory_make ("capsfilter", "capsfilter1");
  queue1 = gst_element_factory_make ("queue", "queue1");
  venc = gst_element_factory_make ("v4l2h264enc", NULL);
  parse = gst_element_factory_make ("h264parse", NULL);
  mp4mux = gst_element_factory_make ("mp4mux", NULL);
  filesink = gst_element_factory_make ("filesink", NULL);

  fail_unless (pipeline && qmmfsrc && capsfilter0 && queue0 && wayland &&
      capsfilter1 && queue1 && venc && parse && mp4mux && filesink);

  plugins = g_list_append (plugins, qmmfsrc);
  plugins = g_list_append (plugins, capsfilter0);
  plugins = g_list_append (plugins, queue0);
  plugins = g_list_append (plugins, wayland);
  plugins = g_list_append (plugins, capsfilter1);
  plugins = g_list_append (plugins, queue1);
  plugins = g_list_append (plugins, venc);
  plugins = g_list_append (plugins, parse);
  plugins = g_list_append (plugins, mp4mux);
  plugins = g_list_append (plugins, filesink);

  // Set filesink properties
  location = g_strdup_printf ("%s/encode_%dx%d.mp4",
      TF_MEDIA_PREFIX, params1->width, params1->height);
  g_object_set (G_OBJECT (filesink), "location", location, NULL);
  g_object_set (G_OBJECT (filesink), "enable-last-sample", FALSE, NULL);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params0->format,
      "width", G_TYPE_INT, params0->width,
      "height", G_TYPE_INT, params0->height,
      "framerate", GST_TYPE_FRACTION, params0->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter0), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params1->format,
      "width", G_TYPE_INT, params1->width,
      "height", G_TYPE_INT, params1->height,
      "framerate", GST_TYPE_FRACTION, params1->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter1), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), qmmfsrc, capsfilter0,
      queue0, wayland, NULL);
  gst_bin_add_many (GST_BIN (pipeline), capsfilter1, queue1,
      venc, parse, mp4mux, filesink, NULL);

  fail_unless (gst_element_link_many (qmmfsrc,
      capsfilter0, queue0, wayland, NULL));

  fail_unless (gst_element_link_many (qmmfsrc, capsfilter1,
      queue1, venc, parse, mp4mux, filesink, NULL));

  previewpad = gst_element_get_static_pad (qmmfsrc, "video_0");
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

  // Send eos to pipeline.
  fail_unless_equals_int (gst_element_send_eos (pipeline), 1);

  // Verify the output Mp4.
  fail_unless_equals_int (gst_mp4_verification (location, params1->width,
      params1->height, params1->fps, 0.5f, 0, &codec), 1);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  g_free (location);
  gst_destroy_pipeline (&pipeline, &plugins);
}

void
camera_transform_display_pipeline (GstCapsParameters * params0,
    GstCapsParameters * params1, guint duration) {

  GstElement *pipeline, *qmmfsrc, *capsfilter0, *wayland;
  GstElement *capsfilter1, *vtrans, *queue;
  GList *plugins = NULL;
  GstCaps *filtercaps;
  GstMessage *msg;

  pipeline = gst_pipeline_new (NULL);
  // Create all elements
  qmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qmmfsrc");
  capsfilter0 = gst_element_factory_make ("capsfilter", "capsfilter0");
  queue = gst_element_factory_make ("queue", "queue0");
  vtrans = gst_element_factory_make ("qtivtransform", NULL);
  capsfilter1 = gst_element_factory_make ("capsfilter", "capsfilter1");
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");

  fail_unless (pipeline && qmmfsrc && capsfilter0 && queue &&
      vtrans && capsfilter1 && wayland);

  // Add to GList.
  plugins = g_list_append (plugins, qmmfsrc);
  plugins = g_list_append (plugins, capsfilter0);
  plugins = g_list_append (plugins, queue);
  plugins = g_list_append (plugins, vtrans);
  plugins = g_list_append (plugins, capsfilter1);
  plugins = g_list_append (plugins, wayland);

  // Configure the stream caps.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params0->format,
      "width", G_TYPE_INT, params0->width,
      "height", G_TYPE_INT, params0->height,
      "framerate", GST_TYPE_FRACTION, params0->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter0), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Rotate 90CW.
  g_object_set (G_OBJECT (vtrans), "rotate", 1, NULL);

  // Configure vtrans output caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params1->format,
      "width", G_TYPE_INT, params1->width,
      "height", G_TYPE_INT, params1->height,
      "framerate", GST_TYPE_FRACTION, params1->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter1), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), qmmfsrc, capsfilter0, queue,
      vtrans, capsfilter1, wayland, NULL);

  fail_unless (gst_element_link_many (qmmfsrc, capsfilter0, queue,
      vtrans, capsfilter1, wayland, NULL));

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

  // Send eos to pipeline.
  fail_unless_equals_int (gst_element_send_eos (pipeline), 1);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_destroy_pipeline (&pipeline, &plugins);
}

void
camera_composer_display_pipeline (GstCapsParameters * params,
    gchar *filename, guint duration) {
  GstElement *pipeline, *qmmfsrc, *capsfilter, *vcomps, *wayland;
  GstElement *filesrc, *demux, *parse, *vdec, *queue0, *queue1;
  GList *plugins = NULL;
  GValue position = G_VALUE_INIT, dimension = G_VALUE_INIT, val = G_VALUE_INIT;
  GstPad *sink0, *sink1;
  GstCaps *filtercaps;
  GstMessage *msg;
  gchar *codec = NULL;
  gchar *location = g_strdup_printf ("%s/%s", TF_MEDIA_PREFIX, filename);

  // Check if file is existing and is Mp4.
  fail_unless_equals_int (gst_mp4_verification (location,
      0, 0, 0, 0, 0, &codec), 1);
  fail_unless (codec);

  pipeline = gst_pipeline_new (NULL);
  // Create all elements
  qmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qmmfsrc");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter0");
  queue0 = gst_element_factory_make ("queue", "queue0");
  filesrc = gst_element_factory_make ("filesrc", NULL);
  demux = gst_element_factory_make ("qtdemux", NULL);
  queue1 = gst_element_factory_make ("queue", "queue1");

  if (g_str_has_prefix (codec, "H.264")) {
    parse = gst_element_factory_make ("h264parse", NULL);
    vdec = gst_element_factory_make ("v4l2h264dec", NULL);
  } else if (g_str_has_prefix (codec, "H.265")) {
    parse = gst_element_factory_make ("h265parse", NULL);
    vdec = gst_element_factory_make ("v4l2h265dec", NULL);
  } else {
    fail();
  }

  vcomps = gst_element_factory_make ("qtivcomposer", "mixer");
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");

  fail_unless (pipeline && qmmfsrc && capsfilter && queue0 &&
      filesrc && demux && parse && vdec && queue1 && vcomps && wayland);

  // Add to GList.
  plugins = g_list_append (plugins, qmmfsrc);
  plugins = g_list_append (plugins, capsfilter);
  plugins = g_list_append (plugins, queue0);
  plugins = g_list_append (plugins, vcomps);
  plugins = g_list_append (plugins, wayland);
  plugins = g_list_append (plugins, filesrc);
  plugins = g_list_append (plugins, demux);
  plugins = g_list_append (plugins, parse);
  plugins = g_list_append (plugins, vdec);
  plugins = g_list_append (plugins, queue1);

  // Configure the stream caps
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, params->format,
      "width", G_TYPE_INT, params->width,
      "height", G_TYPE_INT, params->height,
      "framerate", GST_TYPE_FRACTION, params->fps, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  g_object_set (filesrc, "location", location, NULL);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), qmmfsrc, capsfilter, queue0,
      filesrc, demux, parse, vdec, queue1, vcomps, wayland, NULL);

  fail_unless (gst_element_link_many (qmmfsrc, capsfilter, queue0,
      vcomps, wayland, NULL));
  fail_unless (gst_element_link (filesrc, demux));
  fail_unless (gst_element_link_many (parse, vdec, queue1, vcomps, NULL));
  g_signal_connect (demux, "pad-added",
      G_CALLBACK (gst_element_on_pad_added), parse);

  sink0 = gst_element_get_static_pad (vcomps, "sink_0");
  sink1 = gst_element_get_static_pad (vcomps, "sink_1");
  fail_unless (sink0 && sink1);

  // Set sink0 position and dimension.
  g_value_init (&val, G_TYPE_INT);
  g_value_init (&position, GST_TYPE_ARRAY);
  g_value_init (&dimension, GST_TYPE_ARRAY);
  g_value_set_int (&val, 0);
  gst_value_array_append_value (&position, &val);
  g_value_set_int (&val, 0);
  gst_value_array_append_value (&position, &val);

  g_value_set_int (&val, params->width);
  gst_value_array_append_value (&dimension, &val);
  g_value_set_int (&val, params->height);
  gst_value_array_append_value (&dimension, &val);
  g_object_set_property (G_OBJECT (sink0), "position", &position);
  g_object_set_property (G_OBJECT (sink0), "dimensions", &dimension);
  g_value_unset (&position);
  g_value_unset (&dimension);
  gst_object_unref (sink0);

  // Set sink1 position and dimension.
  g_value_init (&position, GST_TYPE_ARRAY);
  g_value_init (&dimension, GST_TYPE_ARRAY);
  g_value_set_int (&val, 100);
  gst_value_array_append_value (&position, &val);
  g_value_set_int (&val, 100);
  gst_value_array_append_value (&position, &val);
  g_value_set_int (&val, 640);
  gst_value_array_append_value (&dimension, &val);
  g_value_set_int (&val, 480);
  gst_value_array_append_value (&dimension, &val);
  g_value_unset (&val);
  g_object_set_property (G_OBJECT (sink1), "position", &position);
  g_object_set_property (G_OBJECT (sink1), "dimensions", &dimension);
  g_value_unset (&position);
  g_value_unset (&dimension);
  gst_object_unref (sink1);

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

  // Send eos to pipeline.
  fail_unless_equals_int (gst_element_send_eos (pipeline), 1);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  g_free (codec);
  g_free (location);
  gst_destroy_pipeline (&pipeline, &plugins);
}

void
camera_decoder_display_pipeline (gchar *filename, guint duration)
{
  GstElement *pipeline, *filesrc, *demux, *parse, *vdec, *queue, *wayland;
  GList *plugins = NULL;
  GstMessage *msg;
  gchar *location = g_strdup_printf ("%s/%s", TF_MEDIA_PREFIX, filename);
  gchar *codec = NULL;

  // Check if file is existing and is Mp4.
  fail_unless_equals_int (gst_mp4_verification (location,
      0, 0, 0, 0, 0, &codec), 1);
  fail_unless (codec);

  pipeline = gst_pipeline_new (NULL);
  // Create all elements
  filesrc = gst_element_factory_make ("filesrc", NULL);
  demux = gst_element_factory_make ("qtdemux", NULL);
  queue = gst_element_factory_make ("queue", NULL);
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");

  if (g_str_has_prefix (codec, "H.264")) {
    parse = gst_element_factory_make ("h264parse", NULL);
    vdec = gst_element_factory_make ("v4l2h264dec", NULL);
  } else if (g_str_has_prefix (codec, "H.265")) {
    parse = gst_element_factory_make ("h265parse", NULL);
    vdec = gst_element_factory_make ("v4l2h265dec", NULL);
  } else {
    fail();
  }

  fail_unless (pipeline && filesrc && demux && parse &&
      vdec && queue && wayland);

  plugins = g_list_append (plugins, filesrc);
  plugins = g_list_append (plugins, demux);
  plugins = g_list_append (plugins, queue);
  plugins = g_list_append (plugins, parse);
  plugins = g_list_append (plugins, vdec);
  plugins = g_list_append (plugins, wayland);

  // Set properties.
  g_object_set (G_OBJECT (filesrc), "location", location, NULL);
  g_object_set (G_OBJECT (wayland), "sync", TRUE, NULL);

  // Add elements to the pipeline and link them
  gst_bin_add_many (GST_BIN (pipeline), filesrc, demux, parse,
      vdec, queue, wayland, NULL);
  fail_unless (gst_element_link (filesrc, demux));
  fail_unless (gst_element_link_many (parse, vdec, queue, wayland, NULL));
  g_signal_connect (demux, "pad-added",
      G_CALLBACK (gst_element_on_pad_added), parse);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_ASYNC);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      duration * GST_SECOND, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  if (msg) {
    fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_EOS);
    gst_message_unref (msg);
  }

  // Send eos to pipeline.
  fail_unless_equals_int (gst_element_send_eos (pipeline), 1);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  g_free (codec);
  g_free (location);
  gst_destroy_pipeline (&pipeline, &plugins);
}
