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
camera_pipeline (GstCapsParameters * params1,
    GstCapsParameters * params2, GstCapsParameters * rawparams,
    GstCapsParameters * jpegparams, guint duration) {
  GstElement *pipeline, *qtiqmmfsrc, *capsfilter1, *wayland,
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
  wayland = gst_element_factory_make ("waylandsink", "waylandsink");

  fail_unless (pipeline && qtiqmmfsrc && capsfilter1 && wayland);

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
      wayland, NULL);

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
      wayland, NULL));

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

  // Send eos to pipeline.
  fail_unless_equals_int (gst_element_send_eos (pipeline), 1);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);
}

void
camera_display_encode_pipeline (GstCapsParameters * params1,
    GstCapsParameters * params2, guint duration) {
  GstElement *pipeline, *qtiqmmfsrc, *capsfilter1, *wayland;
  GstElement *capsfilter2, *filesink, *venc, *parse, *mp4mux;
  gchar *location = NULL;
  GstPad *previewpad;
  GstCaps *filtercaps;
  GstMessage *msg;
  gchar *codec = GST_VIDEO_CODEC_H264;

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
  location = g_strdup_printf ("%s/encode_%dx%d.mp4",
      CAMERA_FILE_PREFIX, params2->width, params2->height);
  g_object_set (G_OBJECT (filesink), "location", location, NULL);
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

  // Send eos to pipeline.
  fail_unless_equals_int (gst_element_send_eos (pipeline), 1);

  // Verify the output Mp4.
  fail_unless_equals_int (gst_mp4_verification (location, params2->width,
      params2->height, params2->fps, 0.5f, 0, &codec), 1);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  g_free (location);
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

  // Send eos to pipeline.
  fail_unless_equals_int (gst_element_send_eos (pipeline), 1);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);
}

void
camera_composer_display_pipeline (GstCapsParameters * params1,
    gchar *filename, guint duration) {
  GstElement *pipeline, *qtiqmmfsrc, *capsfilter, *vcomps, *wayland;
  GstElement *filesrc, *demux, *parse, *vdec, *queue1, *queue2;
  GValue position = G_VALUE_INIT, dimension = G_VALUE_INIT, val = G_VALUE_INIT;
  GstPad *sink0, *sink1;
  GstCaps *filtercaps;
  GstMessage *msg;
  gchar *codec = NULL;
  gchar *location = g_strdup_printf ("%s/%s", CAMERA_FILE_PREFIX, filename);

  // Check if file is existing and is Mp4.
  fail_unless_equals_int (gst_mp4_verification (location,
      0, 0, 0, 0, 0, &codec), 1);
  fail_unless (codec);

  pipeline = gst_pipeline_new (NULL);
  // Create all elements
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter1");
  queue1 = gst_element_factory_make ("queue", "queue1");
  filesrc = gst_element_factory_make ("filesrc", NULL);
  demux = gst_element_factory_make ("qtdemux", NULL);
  queue2 = gst_element_factory_make ("queue", "queue2");

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
  fail_unless (gst_element_link (filesrc, demux));
  fail_unless (gst_element_link_many (parse, vdec, queue2, vcomps, NULL));
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

  g_value_set_int (&val, params1->width);
  gst_value_array_append_value (&dimension, &val);
  g_value_set_int (&val, params1->height);
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
  gst_object_unref (pipeline);
}

void
camera_decoder_display_pipeline (gchar *filename, guint duration)
{
  GstElement *pipeline, *filesrc, *demux, *parse, *vdec, *queue, *wayland;
  GstMessage *msg;
  gchar *location = g_strdup_printf ("%s/%s", CAMERA_FILE_PREFIX, filename);
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

  fail_unless (pipeline && filesrc && demux && parse && vdec && queue && wayland);

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
  gst_object_unref (pipeline);
}
