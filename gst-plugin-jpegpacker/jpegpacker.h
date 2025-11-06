/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_JPEG_PACKER_H__
#define __GST_JPEG_PACKER_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

G_BEGIN_DECLS

// Classic macro defination
#define GST_TYPE_JPEG_PACKER (gst_jpeg_packer_get_type())
#define GST_JPEG_PACKER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),\
  GST_TYPE_JPEG_PACKER,GstJpegPacker))
#define GST_JPEG_PACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),\
  GST_TYPE_JPEG_PACKER,GstJpegPackerClass))
#define GST_IS_JPEG_PACKER_(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JPEG_PACKER))
#define GST_IS_JPEG_PACKER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_JPEG_PACKER))

typedef struct _GstJpegPacker      GstJpegPacker;
typedef struct _GstJpegPackerClass GstJpegPackerClass;

typedef enum {
  PACK_TYPE_EXIF,
  PACK_TYPE_JFIF,
} GstPackerType;

struct _GstJpegPacker {
  /// Inherited parent structure.
  GstElement     parent;

  /// SinkPads
  GstCollectPads *collectpad;

  /// SrcPad
  GstPad         *srcpad;

  /// Internal queue to save a bundle of input buffers
  GAsyncQueue    *buffers;

  /// Parse jpeg images to sections
  GList          *sections;

  /// Scan data (primary)
  const guint8   *primary_data;
  guint          primary_size;

  /// Scan data (thumbnail)
  const guint8   *thumbnail_data;
  guint          thumbnail_size;

  /// Output jpeg interchange format
  GstPackerType  pack_type;
};

struct _GstJpegPackerClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_jpeg_packer_get_type (void);

G_END_DECLS

#endif // __GST_JPEG_PACKER_H__
