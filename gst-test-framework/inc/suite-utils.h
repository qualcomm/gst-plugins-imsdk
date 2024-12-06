/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_SUITE_UTILS_H__
#define __GST_SUITE_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstCapsParameters GstCapsParameters;

struct _GstCapsParameters {
  const gchar *format;
  gint        width;
  gint        height;
  gint        fps;
};

G_END_DECLS

#endif /* __GST_SUITE_UTILS_H__ */
