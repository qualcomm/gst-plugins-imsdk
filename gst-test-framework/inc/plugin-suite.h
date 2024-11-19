/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_PLUGIN_SUITE_H__
#define __GST_PLUGIN_SUITE_H__

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

G_BEGIN_DECLS

typedef struct _GstPluginSuite GstPluginSuite;

// Convinient macros for calling the functions.
#define GST_PLUGIN_GET_SUITE(sname, psuite) \
    gst_plugin_get_##sname##_suite (psuite)

typedef enum {
  GST_TEST_SUITE_ALL,
  GST_TEST_SUITE_CAMERA,
  GST_TEST_SUITE_AI,
  GST_TEST_SUITE_ML,
  GST_TEST_SUITE_CV,
  GST_TEST_SUITE_MAX
} GstPluginSuiteIdx;

struct _GstPluginSuite {
  const gchar       *name;
  GstPluginSuiteIdx idx;
  gboolean          enable;
  gint              iteration;
  gint              duration;
  Suite             *suite;
};

GST_API void
gst_plugin_get_camera_suite (GstPluginSuite* psuite);

G_END_DECLS

#endif /* __GST_PLUGIN_SUITE_H__ */