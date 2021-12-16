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

#ifndef __GST_ION_POOL_H__
#define __GST_ION_POOL_H__

#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

#define GST_TYPE_ION_BUFFER_POOL (gst_ion_buffer_pool_get_type ())
#define GST_ION_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ION_BUFFER_POOL, \
      GstIonBufferPool))
#define GST_ION_BUFFER_POOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ION_BUFFER_POOL, \
      GstIonBufferPoolClass))
#define GST_IS_ION_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ION_BUFFER_POOL))
#define GST_IS_ION_BUFFER_POOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ION_BUFFER_POOL))
#define GST_ION_BUFFER_POOL_CAST(obj) ((GstIonBufferPool*)(obj))

typedef struct _GstIonBufferPool GstIonBufferPool;
typedef struct _GstIonBufferPoolClass GstIonBufferPoolClass;
typedef struct _GstIonBufferPoolPrivate GstIonBufferPoolPrivate;

struct _GstIonBufferPool
{
  GstBufferPool parent;

  GstIonBufferPoolPrivate *priv;
};

struct _GstIonBufferPoolClass
{
  GstBufferPoolClass parent;
};

GType gst_ion_buffer_pool_get_type (void);

/// Creates a buffer pool for managing video frames.
GstBufferPool * gst_ion_buffer_pool_new ();

G_END_DECLS

#endif /* __GST_ION_POOL_H__ */
