/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __GST_HEXAGONNN_H__
#define __GST_HEXAGONNN_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <ml-meta/ml_meta.h>

#include "deeplabengine.h"

G_BEGIN_DECLS

#define GST_TYPE_HEXAGONNN \
  (gst_hexagonnn_get_type())
#define GST_HEXAGONNN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HEXAGONNN,GstHexagonNN))
#define GST_HEXAGONNN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HEXAGONNN,GstHexagonNNClass))
#define GST_IS_HEXAGONNN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HEXAGONNN))
#define GST_IS_HEXAGONNN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HEXAGONNN))

typedef struct _GstHexagonNN      GstHexagonNN;
typedef struct _GstHexagonNNClass GstHexagonNNClass;

struct _GstHexagonNN {
  GstVideoFilter element;
  GstPad *sinkpad, *srcpad;
  NNSourceInfo source_info;
  gchar* model;
  gchar* mode;
  guint frame_skip_count;
  gchar* data_folder;
  gchar* label_file;
  NNEngine *engine;
};

struct _GstHexagonNNClass {
  GstVideoFilterClass parent_class;
};

GType gst_hexagonnn_get_type (void);

G_END_DECLS

#endif /* __GST_HEXAGONNN_H__ */
