/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <gst/video/video.h>

gboolean
gst_adreno_utils_compute_alignment(guint width, guint height,
                                    GstVideoFormat format, gint *stride,
                                    gint *scanline);

gboolean
gst_is_gbm_supported (void);
