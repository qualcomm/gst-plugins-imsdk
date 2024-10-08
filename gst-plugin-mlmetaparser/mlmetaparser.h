/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_ML_META_PARSER_H__
#define __GST_QTI_ML_META_PARSER_H__

#include "parsermodule.h"

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_META_PARSER  (gst_ml_meta_parser_get_type())
#define GST_ML_META_PARSER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ML_META_PARSER,GstMlMetaParser))
#define GST_ML_META_PARSER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ML_META_PARSER,GstMlMetaParserClass))
#define GST_IS_ML_META_PARSER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ML_META_PARSER))
#define GST_IS_ML_META_PARSER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ML_META_PARSER))
#define GST_ML_META_PARSER_CAST(obj)       ((GstMlMetaParser *)(obj))

typedef struct _GstMlMetaParser GstMlMetaParser;
typedef struct _GstMlMetaParserClass GstMlMetaParserClass;

struct _GstMlMetaParser {
  GstBaseTransform parent;

  /// Parsing ML data module
  GstParserModule *module;

  /// Properties.
  gint            mdlenum;
};

struct _GstMlMetaParserClass {
  GstBaseTransformClass parent;
};

G_GNUC_INTERNAL GType gst_ml_meta_parser_get_type (void);

G_END_DECLS

#endif // __GST_QTI_ML_META_PARSER_H__
