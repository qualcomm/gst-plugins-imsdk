/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>

#if defined(NOT_GST_BUILD)
#include <android/log.h>
#else
#include <gst/gstinfo.h>
#endif /* defined(NOT_GST_BUILD) */

#define LOG_TAG_QMOT "QMOT"
//#define ENABLE_QMOT_DEBUG

#if defined(NOT_GST_BUILD)
#ifdef ENABLE_QMOT_DEBUG
#define QMOT_LOG_DEBUG(...) { \
  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG_QMOT, __VA_ARGS__); \
  printf(__VA_ARGS__); \
  printf("\n"); \
}
#else
#define QMOT_LOG_DEBUG(...)
#endif /* ENABLE_QMOT_DEBUG */

#define QMOT_LOG_INFO(...) { \
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG_QMOT, __VA_ARGS__); \
  printf(__VA_ARGS__); \
  printf("\n"); \
}
#define QMOT_LOG_ERROR(...) { \
  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG_QMOT, __VA_ARGS__); \
  printf(__VA_ARGS__); \
  printf("\n"); \
}
#else
#define QMOT_LOG_DEBUG(...)
// #define QMOT_LOG_DEBUG(...) { GST_ERROR(__VA_ARGS__); }
#define QMOT_LOG_INFO(...)  { GST_ERROR(__VA_ARGS__); }
#define QMOT_LOG_ERROR(...) { GST_ERROR(__VA_ARGS__); }
#endif /* defined(NOT_GST_BUILD) */
