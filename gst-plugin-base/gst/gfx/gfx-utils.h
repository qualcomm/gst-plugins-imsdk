/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_GFX_UTILS_H__
#define __GST_QTI_GFX_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * gst_gfx_adreno_get_alignment:
 *
 * Helper function for retrieving the alignment requirements of Adreno GPU.
 *
 * return: Alignment requirement in bytes.
 */
GST_API gint
gst_gfx_adreno_get_alignment (void);

G_END_DECLS

#endif // __GST_QTI_GFX_UTILS_H__
