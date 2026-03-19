/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_SYNC_H__
#define __GST_SYNC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SYNC (gst_sync_get_type())
#define GST_SYNC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SYNC, GstSync))
#define GST_SYNC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SYNC, GstSyncClass))
#define GST_IS_SYNC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SYNC))
#define GST_IS_SYNC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SYNC))
#define GST_SYNC_CAST(obj) ((GstSync *)(obj))

typedef struct _GstSync GstSync;
typedef struct _GstSyncClass GstSyncClass;

struct _GstSync
{
  GstElement parent;

  GstPad *srcpad;
  GstPad *sinkpad;

  GstClock *clock;
  gboolean have_start_time;
  GstClockTimeDiff stream_start_real_time;
};

struct _GstSyncClass
{
  GstElementClass   parent;
};

G_GNUC_INTERNAL GType gst_sync_get_type (void);

G_END_DECLS

#endif //__GST_SYNC_H__
