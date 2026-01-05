/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_BIN_H__
#define __GST_QTI_ML_BIN_H__

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_BIN (gst_ml_bin_get_type())
#define GST_ML_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_BIN,GstMLBin))
#define GST_ML_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_BIN,GstMLBinClass))
#define GST_IS_ML_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_BIN))
#define GST_IS_ML_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_BIN))
#define GST_ML_BIN_CAST(obj) ((GstMLBin *)(obj))

#define GST_ML_BIN_GET_LOCK(obj) (&GST_ML_BIN(obj)->lock)
#define GST_ML_BIN_LOCK(obj) \
    g_mutex_lock(GST_ML_BIN_GET_LOCK(obj))
#define GST_ML_BIN_UNLOCK(obj) \
    g_mutex_unlock(GST_ML_BIN_GET_LOCK(obj))

// Define the names of the plugin structures based on the bin being compiled.
typedef struct _GstMLBin GST_ML_BIN_STRUCT_NAME;
typedef struct _GstMLBinClass GST_ML_BIN_STRUCT_CLASS_NAME;
// Convenient aliases for the plugin structures.
typedef GST_ML_BIN_STRUCT_NAME GstMLBin;
typedef GST_ML_BIN_STRUCT_CLASS_NAME GstMLBinClass;

struct _GstMLBin
{
  /// Inherited parent structure.
  GstBin       bin;

  /// Global mutex lock.
  GMutex       lock;
};

struct _GstMLBinClass
{
  // Inherited parent structure.
  GstBinClass parent_class;
};

G_GNUC_INTERNAL GType gst_ml_bin_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_BIN_H__
