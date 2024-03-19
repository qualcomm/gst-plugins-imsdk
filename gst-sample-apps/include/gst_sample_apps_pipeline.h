/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * This file provides pipline functions for gst applications.
 */

#ifndef GST_SAMPLE_APPS_PIPELINE_H
#define GST_SAMPLE_APPS_PIPELINE_H


#define GST_PIPELINE_2STREAM "qtivcomposer name=mixer " \
  "sink_0::position=\"<0, 0>\" sink_0::dimensions=\"<960, 1080>\" " \
  "sink_1::position=\"<960, 0>\" sink_1::dimensions=\"<960, 1080>\" " \
  "mixer. ! queue ! " \
  "waylandsink enable-last-sample=false async=false sync=true fullscreen=true " \
  "filesrc name=source0 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source1 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \

#define GST_PIPELINE_4STREAM "qtivcomposer name=mixer " \
  "sink_0::position=\"<0, 0>\" sink_0::dimensions=\"<960, 540>\" " \
  "sink_1::position=\"<960, 0>\" sink_1::dimensions=\"<960, 540>\" " \
  "sink_2::position=\"<0, 540>\" sink_2::dimensions=\"<960, 540>\" " \
  "sink_3::position=\"<960, 540>\" sink_3::dimensions=\"<960, 540>\" " \
  "mixer. ! queue ! " \
  "waylandsink enable-last-sample=false async=false sync=true fullscreen=true " \
  "filesrc name=source0 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source1 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source2 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source3 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \

#define GST_PIPELINE_8STREAM "qtivcomposer name=mixer " \
  "sink_0::position=\"<0, 0>\" sink_0::dimensions=\"<480, 540>\" " \
  "sink_1::position=\"<480, 0>\" sink_1::dimensions=\"<480, 540>\" " \
  "sink_2::position=\"<960, 0>\" sink_2::dimensions=\"<480, 540>\" " \
  "sink_3::position=\"<1440, 0>\" sink_3::dimensions=\"<480, 540>\" " \
  "sink_4::position=\"<0, 540>\" sink_4::dimensions=\"<480, 540>\" " \
  "sink_5::position=\"<480, 540>\" sink_5::dimensions=\"<480, 540>\" " \
  "sink_6::position=\"<960, 540>\" sink_6::dimensions=\"<480, 540>\" " \
  "sink_7::position=\"<1440, 540>\" sink_7::dimensions=\"<480, 540>\" " \
  "mixer. ! queue ! " \
  "waylandsink enable-last-sample=false async=false sync=true fullscreen=true " \
  "filesrc name=source0 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source1 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source2 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source3 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source4 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source5 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source6 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source7 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \

#define GST_PIPELINE_16STREAM "qtivcomposer name=mixer " \
  "sink_0::position=\"<0, 0>\" sink_0::dimensions=\"<480, 270>\" " \
  "sink_1::position=\"<480, 0>\" sink_1::dimensions=\"<480, 270>\" " \
  "sink_2::position=\"<480, 270>\" sink_2::dimensions=\"<480, 270>\" " \
  "sink_3::position=\"<0, 270>\" sink_3::dimensions=\"<480, 270>\" " \
  "sink_4::position=\"<480, 540>\" sink_4::dimensions=\"<480, 270>\" " \
  "sink_5::position=\"<0, 540>\" sink_5::dimensions=\"<480, 270>\" " \
  "sink_6::position=\"<480, 810>\" sink_6::dimensions=\"<480, 270>\" " \
  "sink_7::position=\"<0, 810>\" sink_7::dimensions=\"<480, 270>\" " \
  "sink_8::position=\"<960, 0>\" sink_8::dimensions=\"<480, 270>\" " \
  "sink_9::position=\"<1440, 0>\" sink_9::dimensions=\"<480, 270>\" " \
  "sink_10::position=\"<960, 270>\" sink_10::dimensions=\"<480, 270>\" " \
  "sink_11::position=\"<1440, 270>\" sink_11::dimensions=\"<480, 270>\" " \
  "sink_12::position=\"<960, 540>\" sink_12::dimensions=\"<480, 270>\" " \
  "sink_13::position=\"<1440, 540>\" sink_13::dimensions=\"<480, 270>\" " \
  "sink_14::position=\"<960, 810>\" sink_14::dimensions=\"<480, 270>\" " \
  "sink_15::position=\"<1440, 810>\" sink_15::dimensions=\"<480, 270>\" " \
  "mixer. ! queue ! " \
  "waylandsink enable-last-sample=false async=false sync=true fullscreen=true " \
  "filesrc name=source0 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source1 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source2 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source3 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source4 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source5 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source6 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source7 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source8 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source9 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source10 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source11 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source12 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source13 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source14 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \
  "filesrc name=source15 location=FILESOURCE ! qtdemux ! queue ! " \
  "h264parse ! v4l2h264dec capture-io-mode=5 output-io-mode=5 ! queue ! mixer. " \

#endif //GST_SAMPLE_APPS_PIPELINE_H
