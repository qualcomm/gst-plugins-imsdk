/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_REDIS_SINK_H__
#define __GST_QTI_REDIS_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/ml/gstmlmodule.h>

#include <hiredis/hiredis.h>

G_BEGIN_DECLS

#define GST_TYPE_REDIS_SINK \
  (gst_redis_sink_get_type())
#define GST_REDIS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REDIS_SINK,GstRedisSink))
#define GST_REDIS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REDIS_SINK,GstRedisSinkClass))
#define GST_IS_REDIS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_REDIS_SINK))
#define GST_IS_REDIS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_REDIS_SINK))
#define GST_REDIS_SINK_CAST(obj)       ((GstRedisSink *)(obj))

typedef struct _GstRedisSink GstRedisSink;
typedef struct _GstRedisSinkClass GstRedisSinkClass;

struct _GstRedisSink {
  /// Inherited parent structure.
  GstBaseSink parent;

  /// Hiredis library context
  redisContext *redis;

  /// Properties.
  gchar *host;
  guint port;
  gchar *password;
  gchar *username;
  gchar *channel;
};

struct _GstRedisSinkClass {
  GstBaseSinkClass parent_class;
};

G_GNUC_INTERNAL GType gst_redis_sink_get_type (void);

G_END_DECLS

#endif // __GST_QTI_REDIS_SINK_H__
