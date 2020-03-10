/*
* Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
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

#ifndef __GST_QMMFSRC_H__
#define __GST_QMMFSRC_H__

#include <gst/gst.h>
#include <gobject/gtype.h>
#include <glib/gtypes.h>

#include "qmmf_source_context.h"

G_BEGIN_DECLS

/// Boilerplate cast macros and type check macros.
#define GST_TYPE_QMMFSRC (qmmfsrc_get_type())
#define GST_QMMFSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QMMFSRC,GstQmmfSrc))
#define GST_QMMFSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QMMFSRC,GstQmmfSrcClass))
#define GST_IS_QMMFSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QMMFSRC))
#define GST_IS_QMMFSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QMMFSRC))

#define GST_QMMFSRC_GET_LOCK(obj)   (&GST_QMMFSRC(obj)->lock)
#define GST_QMMFSRC_LOCK(obj)       g_mutex_lock(GST_QMMFSRC_GET_LOCK(obj))
#define GST_QMMFSRC_UNLOCK(obj)     g_mutex_unlock(GST_QMMFSRC_GET_LOCK(obj))

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

typedef struct _GstQmmfSrc      GstQmmfSrc;
typedef struct _GstQmmfSrcClass GstQmmfSrcClass;

struct _GstQmmfSrc {
  /// Inherited parent structure.
  GstElement     parent;

  /// Global mutex lock.
  GMutex         lock;

  /// List containing the existing source pads.
  GHashTable     *srcpads;
  /// Next available index for the source pads.
  guint          nextidx;

  /// List containing the indexes of existing video source pads.
  GList          *vidindexes;
  /// List containing the indexes of existing video source pads.
  GList          *audindexes;
  /// List containing the indexes of existing video source pads.
  GList          *imgindexes;

  /// QMMF context.
  GstQmmfContext *context;
};

struct _GstQmmfSrcClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType qmmfsrc_get_type(void);

G_END_DECLS

#endif // __GST_QMMFSRC_H__
