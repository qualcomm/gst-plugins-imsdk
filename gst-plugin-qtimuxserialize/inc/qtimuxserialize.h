/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
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

#ifndef __GST_QTIMUXSERIALIZE_H__
#define __GST_QTIMUXSERIALIZE_H__

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/base/gstaggregator.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_QTIMUXSERIALIZE            (gst_qtimuxserialize_get_type())
#define GST_QTIMUXSERIALIZE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QTIMUXSERIALIZE,Qtimuxserialize))
#define GST_QTIMUXSERIALIZE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QTIMUXSERIALIZE,QtimuxserializeClass))
#define GST_QTIMUXSERIALIZE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QTIMUXSERIALIZE, QtimuxserializeClass))
#define GST_IS_QTIMUXSERIALIZE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QTIMUXSERIALIZE))
#define GST_IS_QTIMUXSERIALIZE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QTIMUXSERIALIZE))

#define INPUT_META_API_TYPE           GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE    // depend on upstream ???

typedef struct _Qtimuxserialize      Qtimuxserialize;
typedef struct _QtimuxserializeClass QtimuxserializeClass;

typedef struct {
  GstMeta meta;
  guint channel_id;     // camera channel index
  guint frame_id;       // frame index in one channel
  guint obj_id;         // object index of ROI
} GstVideoFrameMeta;

struct _Qtimuxserialize
{
  GstAggregator parent;

  guint64 timestamp;
  guint64 timeout_v;
  gboolean gap_expected;
  gboolean silent;
};

struct _QtimuxserializeClass
{
  GstAggregatorClass parent_class;
};

extern guint g_total_memory;
extern gboolean g_save;

guint append_to_outbuf (GstBuffer *inbuf, GstBuffer *outbuf);
void verify_outbuf(GstBuffer * outbuf);
void dump_binary(void *buff, guint size, const gchar *name, guint no);
void dump_gst_memory(GstBuffer *buf, gboolean first_mem_only, const gchar *name);
GType gst_qtimuxserialize_get_type (void);

G_END_DECLS

#endif // __GST_QTIMUXSERIALIZE_H__
