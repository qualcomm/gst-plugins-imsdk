/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <string.h>

#include "plugin-suite.h"
#include "suite-camera-pipeline.h"

gint runningtime = 10; // Default running time 10 seconds.

GST_START_TEST (test_stream_NV12_1920x1080_30fps)
{
  GstCapsParameters params = { "NV12_Q08C", 1920, 1080, 30};
  camera_pipeline (&params, NULL, NULL, NULL, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_stream_NV12_1280x720_30fps)
{
  GstCapsParameters params = { "NV12_Q08C", 1280, 720, 30};
  camera_pipeline (&params, NULL, NULL, NULL, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1280x70_30fps_NV12_1920x1080_30fps)
{
  GstCapsParameters params1 = { "NV12_Q08C", 1920, 1080, 30};
  GstCapsParameters params2 = { "NV12", 1280, 720, 30};
  camera_pipeline (&params1, &params2, NULL, NULL, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1280x720_30fps_JPEG_1920x1080)
{
  GstCapsParameters params = { "NV12_Q08C", 1280, 720, 30};
  GstCapsParameters jpegparams = { "JPEG", 1920, 1080, 1};
  camera_pipeline (&params, NULL, NULL, &jpegparams, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1280x720_30fps_JPEG_1920x1080_RAW_)
{
  GstCapsParameters params = { "NV12_Q08C", 1280, 720, 30};
  GstCapsParameters rawparams= { "rggb", 4056, 3040, 1};
  GstCapsParameters jpegparams = { "JPEG", 1280, 720, 1};
  camera_pipeline (&params, NULL, &rawparams, &jpegparams, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1920x1080_30FPS_NV12_1280x720_60fps_JPEG_1920x1080)
{
  GstCapsParameters params1 = { "NV12_Q08C", 1920, 1080, 30};
  GstCapsParameters params2 = { "NV12", 1280, 720, 30};
  GstCapsParameters jpegparams = { "JPEG", 1280, 720, 1};
  camera_pipeline (&params1, &params2, NULL, &jpegparams, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1920x1080_30FPS_NV12_1280x720_60fps_JPEG_1920x1080_RAW)
{
  GstCapsParameters params1 = { "NV12_Q08C", 1920, 1080, 30};
  GstCapsParameters params2 = { "NV12", 1280, 720, 30};
  GstCapsParameters rawparams = { "rggb", 4056, 3040, 1};
  GstCapsParameters jpegparams = { "JPEG", 1280, 720, 1};
  camera_pipeline (&params1, &params2, &rawparams, &jpegparams, runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_NV12_1920x1080_DISPLAY_NV12_1280x720_60fps_ENCODE)
{
  GstCapsParameters params1 = { "NV12_Q08C", 1920, 1080, 30};
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

GST_START_TEST (test_streams_NV12_1920x1080_COMPOSE_DISPLAY)
{
  GstCapsParameters params1 = { "NV12", 1920, 1080, 30};
  camera_composer_display_pipeline (&params1, "Draw_1080p_180s_30FPS.mp4",
      runningtime);
}
GST_END_TEST;

GST_START_TEST (test_streams_1080P_NV12_DECODER_DISPLAY)
{
  camera_decoder_display_pipeline ("Draw_1080p_180s_30FPS.mp4", runningtime);
}
GST_END_TEST;

static Suite *
camera_suite (GList **tcnames, gint iteration, gint duration)
{
  Suite *s = suite_create ("camera");
  TCase *tc;
  gchar *tcname = NULL;
  int start = 0, end = 1;
  // TCase timeout in seconds.
  int tctimeout = 0;

  if (iteration > 0)
    end = iteration;

  if (duration > 0)
    runningtime = duration;

  // Add extra time for other module initialization.
  tctimeout = runningtime + 5;

  g_setenv ("XDG_RUNTIME_DIR", "/dev/socket/weston", FALSE);
  g_setenv ("WAYLAND_DISPLAY", "wayland-1", FALSE);

  tcname = "onevideostream1080P";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc, test_stream_NV12_1920x1080_30fps, start, end);

  tcname = "onevideostream720P";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc, test_stream_NV12_1280x720_30fps, start, end);

  tcname = "onevideo+jpeg";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc,
      test_streams_NV12_1280x720_30fps_JPEG_1920x1080, start, end);

  tcname = "onevideo+jpeg+raw";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc,
      test_streams_NV12_1280x720_30fps_JPEG_1920x1080_RAW_, start, end);

  tcname = "twovideostreams";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc,
      test_streams_NV12_1280x70_30fps_NV12_1920x1080_30fps, start, end);

  // TCase for two video streams.
  tcname = "twovideo+jpeg";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_NV12_1920x1080_30FPS_NV12_1280x720_60fps_JPEG_1920x1080,
      start, end);

  tcname = "twovideo+jepg+raw";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_NV12_1920x1080_30FPS_NV12_1280x720_60fps_JPEG_1920x1080_RAW,
      start, end);

  tcname = "display+encode";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_NV12_1920x1080_DISPLAY_NV12_1280x720_60fps_ENCODE,
      start, end);

  tcname = "vtrans+display";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_NV12_1920x1080_VTRANS_BGRA_1280x720_30fps_R90_DISPLAY,
      start, end);

  tcname = "vcompose+display";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_NV12_1920x1080_COMPOSE_DISPLAY, start, end);

  tcname = "decoder+display";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  tcase_add_loop_test (tc,
      test_streams_1080P_NV12_DECODER_DISPLAY, start, end);

  return s;
}

void gst_plugin_get_camera_suite(GstPluginSuite* psuite)
{
  if (psuite == NULL)
    return;

  psuite->name = "camera";
  psuite->suite = camera_suite (&psuite->tcnames,
      psuite->iteration, psuite->duration);
}
