/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <string.h>

#include "plugin-suite.h"
#include "suite-camera-pipeline.h"

gint runningtime = 3; // Default running time 3 seconds.

GST_START_TEST (test_stream_NV12_1920x1080_30fps)
{
  GstCapsParameters params = { "NV12", 1920, 1080, 30};
  camera_pipeline (&params, NULL, NULL, NULL, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1280x70_30fps_NV12_1920x1080_30fps)
{
  GstCapsParameters params1 = { "NV12", 1920, 1080, 30};
  GstCapsParameters params2 = { "NV12", 1280, 720, 30};
  camera_pipeline (&params1, &params2, NULL, NULL, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1280x720_30fps_JPEG_1920x1080)
{
  GstCapsParameters params = { "NV12", 1280, 720, 30};
  GstCapsParameters jpegparams = { "JPEG", 1920, 1080, 1};
  camera_pipeline (&params, NULL, NULL, &jpegparams, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1280x720_30fps_JPEG_1920x1080_RAW_)
{
  GstCapsParameters params = { "NV12", 1280, 720, 30};
  GstCapsParameters rawparams= { "rggb", 4096, 3072, 1};
  GstCapsParameters jpegparams = { "JPEG", 1280, 720, 1};
  camera_pipeline (&params, NULL, &rawparams, &jpegparams, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1920x1080_30FPS_NV12_1280x720_60fps_JPEG_1920x1080)
{
  GstCapsParameters params1 = { "NV12", 1920, 1080, 30};
  GstCapsParameters params2 = { "NV12", 1280, 720, 30};
  GstCapsParameters jpegparams = { "JPEG", 1280, 720, 1};
  camera_pipeline (&params1, &params2, NULL, &jpegparams, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1920x1080_30FPS_NV12_1280x720_60fps_JPEG_1920x1080_RAW)
{
  GstCapsParameters params1 = { "NV12", 1920, 1080, 30};
  GstCapsParameters params2 = { "NV12", 1280, 720, 30};
  GstCapsParameters rawparams = { "rggb", 4096, 3072, 1};
  GstCapsParameters jpegparams = { "JPEG", 1280, 720, 1};
  camera_pipeline (&params1, &params2, &rawparams, &jpegparams, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1920x1080_DISPLAY_NV12_1280x720_60fps_ENCODE)
{
  GstCapsParameters params1 = { "NV12", 1920, 1080, 30};
  GstCapsParameters params2 = { "NV12", 1280, 720, 60};
  camera_display_encode_pipeline (&params1, &params2, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1920x1080_VTRANS_BGRA_1280x720_30fps_R90_DISPLAY)
{
  GstCapsParameters params1 = { "NV12", 1920, 1080, 30};
  GstCapsParameters params2 = { "BGRA", 1280, 720, 30};
  camera_transform_display_pipeline (&params1, &params2, runningtime);
}
GST_END_TEST;

static Suite *
camera_suite (gint iteration, gint duration)
{
  Suite *s = suite_create ("camera");
  TCase *tc;
  int start = 0, end = 1;
  // TCase timeout in seconds.
  int tctimeout = 5;

  if (iteration > 0)
    end = iteration;

  if (duration > 0)
    runningtime = duration;

  tctimeout = runningtime + 5;

  g_setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", FALSE);
  g_setenv ("WAYLAND_DISPLAY", "wayland-1", FALSE);

  tc = tcase_create ("onevideostream");
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc, test_stream_NV12_1920x1080_30fps, start, end);

  tc = tcase_create ("onevideo+jpeg");
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc,
      test_streams_NV12_1280x720_30fps_JPEG_1920x1080, start, end);

  tc = tcase_create ("onevideo+jpeg+raw");
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc,
      test_streams_NV12_1280x720_30fps_JPEG_1920x1080_RAW_, start, end);

  tc = tcase_create ("twovideostreams");
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc,
      test_streams_NV12_1280x70_30fps_NV12_1920x1080_30fps, start, end);

  // TCase for two video streams.
  tc = tcase_create ("twovideo+jpeg");
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_NV12_1920x1080_30FPS_NV12_1280x720_60fps_JPEG_1920x1080,
      start, end);

  tc = tcase_create ("twovideo+jepg+raw");
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_NV12_1920x1080_30FPS_NV12_1280x720_60fps_JPEG_1920x1080_RAW,
      start, end);

  tc = tcase_create ("display+encode");
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_NV12_1920x1080_DISPLAY_NV12_1280x720_60fps_ENCODE,
      start, end);

  tc = tcase_create ("vtrans+display");
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_NV12_1920x1080_VTRANS_BGRA_1280x720_30fps_R90_DISPLAY,
      start, end);

  return s;
}

void gst_plugin_get_camera_suite(GstPluginSuite* psuite)
{
  if (psuite == NULL)
    return;

  psuite->name = "camera";
  psuite->suite = camera_suite (psuite->iteration, psuite->duration);
}

