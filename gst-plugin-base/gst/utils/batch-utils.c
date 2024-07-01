/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "batch-utils.h"

static const gchar* batch_channel_names[] = {
    "batch-channel-00", "batch-channel-01", "batch-channel-02", "batch-channel-03",
    "batch-channel-04", "batch-channel-05", "batch-channel-06", "batch-channel-07",
    "batch-channel-08", "batch-channel-09", "batch-channel-10", "batch-channel-11",
    "batch-channel-12", "batch-channel-13", "batch-channel-14", "batch-channel-15",
    "batch-channel-16", "batch-channel-17", "batch-channel-18", "batch-channel-19",
    "batch-channel-20", "batch-channel-21", "batch-channel-22", "batch-channel-23",
    "batch-channel-24", "batch-channel-25", "batch-channel-26", "batch-channel-27",
    "batch-channel-28", "batch-channel-29", "batch-channel-30", "batch-channel-31",
};

const gchar *
gst_batch_channel_name (guint index)
{
  g_return_val_if_fail ((G_N_ELEMENTS (batch_channel_names) > index), NULL);
  return batch_channel_names[index];
}
