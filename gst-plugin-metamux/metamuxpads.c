/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "metamuxpads.h"

G_DEFINE_TYPE(GstMetaMuxDataPad, gst_meta_mux_data_pad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_STATIC (gst_meta_mux_debug);
#define GST_CAT_DEFAULT gst_meta_mux_debug

static void
gst_meta_mux_data_pad_finalize (GObject * object)
{
  GstMetaMuxDataPad *pad = GST_META_MUX_DATA_PAD (object);

  g_queue_free (pad->queue);

  G_OBJECT_CLASS (gst_meta_mux_data_pad_parent_class)->finalize(object);
}

void
gst_meta_mux_data_pad_class_init (GstMetaMuxDataPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_meta_mux_data_pad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_meta_mux_debug, "qtimetamux", 0,
      "QTI Meta muxer pads");
}

void
gst_meta_mux_data_pad_init (GstMetaMuxDataPad * pad)
{
  pad->type = GST_DATA_TYPE_UNKNOWN;
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->stash = NULL;
  pad->queue = g_queue_new ();
}

