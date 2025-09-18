/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <string.h>

#include "plugin-suite.h"
#include "suite-ml-pipeline.h"

/*
 * Default running time 300 seconds.
 * For ML case, the filesrc is video which has limited time.
*/
static gint runningtime = 300;

GstMLVideoInfo detection_videoinfo = {
  .file = TF_FILE_LOCATION (Draw_720p_180s_24FPS.mp4),
  .frameinfo[0] = {5, 0},
  .frameinfo[1] = {10, 0},
  .frameinfo[2] = {15, 0},
  .frameinfo[3] = {20, 0},
  .frameinfo[4] = {30, 1},
  .frameinfo[5] = {38, 2},
  .frameinfo[6] = {47, 2},
  .frameinfo[7] = {55, 2},
  .frameinfo[8] = {65, 2},
  .frameinfo[9] = {80, 2},
};

GstMLVideoInfo classification_videoinfo = {
  .file = TF_FILE_LOCATION (Animals_000_1080p_180s_30FPS.mp4),
  .frameinfo[0] = {5, 0},
  .frameinfo[1] = {10, 0},
  .frameinfo[2] = {15, 0},
  .frameinfo[3] = {20, 0},
  .frameinfo[4] = {30, 1},
  .frameinfo[5] = {35, 1},
  .frameinfo[6] = {45, 2},
  .frameinfo[7] = {55, 2},
  .frameinfo[8] = {65, 2},
  .frameinfo[9] = {75, 2},
};

GST_START_TEST (test_ml_video_tflite_detection_yolov8)
{
  GstMLModelInfo tflite_yolov8 = {
    .inferencetype = GST_ML_OBJECT_DETECTION,
    .type = GST_ML_MODEL_TFLITE,
    .modelpath = TF_MODEL_LOCATION (yolov8_det_quantized.tflite),
    .labelspath = TF_LABEL_LOCATION (yolov8.json),
    .settings = "{\"confidence\": 70.0}",
    .moduletype = GST_ML_MODULE_YOLO_V8,
    .results = 10,
    .delegate = GST_ML_DELEGATE_DSP
  };

  ml_video_inference_pipeline (&tflite_yolov8, &detection_videoinfo);
}
GST_END_TEST;

GST_START_TEST (test_ml_video_tflite_detection_yolox)
{
  GstMLModelInfo tflite_yolox = {
    .inferencetype = GST_ML_OBJECT_DETECTION,
    .type = GST_ML_MODEL_TFLITE,
    .modelpath = TF_MODEL_LOCATION (yolox-yolo-x-w8a8.tflite),
    .labelspath = TF_LABEL_LOCATION (yolov8.json),
    .settings = "{\"confidence\": 80.0}",
    .moduletype = GST_ML_MODULE_YOLO_V8,
    .results = 10,
    .delegate = GST_ML_DELEGATE_DSP
  };

  ml_video_inference_pipeline (&tflite_yolox, &detection_videoinfo);
}
GST_END_TEST;

GST_START_TEST (test_ml_video_qnn_detection_yolov8)
{
  GstMLModelInfo qnn_yolov8 = {
    .inferencetype = GST_ML_OBJECT_DETECTION,
    .type = GST_ML_MODEL_QNN,
    .modelpath = TF_MODEL_LOCATION (yolov8_det.bin),
    .labelspath = TF_LABEL_LOCATION (yolov8.json),
    .settings = NULL,
    .moduletype = GST_ML_MODULE_YOLO_V8,
    .results = 10,
    .delegate = GST_ML_DELEGATE_DSP
  };

  ml_video_inference_pipeline (&qnn_yolov8, &detection_videoinfo);
}
GST_END_TEST;

GST_START_TEST (test_ml_video_tflite_classification_mobilenet)
{
  GstMLModelInfo tflite_mobilenet = {
    .inferencetype = GST_ML_CLASSIFICATION,
    .type = GST_ML_MODEL_TFLITE,
    .modelpath = TF_MODEL_LOCATION (resnext101-resnext101-w8a8.tflite),
    .labelspath = TF_LABEL_LOCATION (mobilenet_old.json),
    .settings = "{\"confidence\": 51.0}",
    .moduletype = GST_ML_MODULE_MOBILENET_SOFTMAX,
    .results = 10,
    .delegate = GST_ML_DELEGATE_DSP
  };

  ml_video_inference_pipeline (&tflite_mobilenet, &classification_videoinfo);
}
GST_END_TEST;

static Suite *
ml_suite (GList **tcnames, gint iteration, gint duration)
{
  Suite *s = suite_create ("ml");
  TCase *tc;
  gchar *tcname = NULL;
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

  tcname = "tflitedetection_yolov8";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase tflite yolov8.
  tcase_add_loop_test (tc, test_ml_video_tflite_detection_yolov8, start, end);

  tcname = "tflitedetection_yolox";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase tflite yolox.
  tcase_add_loop_test (tc, test_ml_video_tflite_detection_yolox, start, end);

  tctimeout = runningtime + 35;
  tcname = "tflite_classification_mobilenet";
  tc = tcase_create (tcname);
  *tcnames = g_list_append (*tcnames, (gpointer)tcname);
  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, tctimeout);
  // Add test to TCase.
  tcase_add_loop_test (tc,
      test_ml_video_tflite_classification_mobilenet, start, end);

  return s;
}

void gst_plugin_get_ml_suite (GstPluginSuite* psuite)
{
  if (psuite == NULL)
    return;

  psuite->name = "ml";
  psuite->suite = ml_suite (&psuite->tcnames,
      psuite->iteration, psuite->duration);
}
