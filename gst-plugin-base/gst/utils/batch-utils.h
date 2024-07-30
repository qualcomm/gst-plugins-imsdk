/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_BATCH_UTILS_H__
#define __GST_QTI_BATCH_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * gst_batch_channel_name:
 * @index: The batch channel index.
 *
 * Return the string representation of the batch index for use as name of the
 * #GstProtectionMeta attached when buffers are batched.
 *
 * This is convinient in order to avoid the constant allocation of a string
 * when corresponding to the batch number when there is a need for it.
 *
 * return: Pointer to string in "batch-channel-%2d" format or NULL on failure
 */
GST_API const gchar *
gst_batch_channel_name (guint index);

G_END_DECLS

#endif /* __GST_QTI_BATCH_UTILS_H__ */
