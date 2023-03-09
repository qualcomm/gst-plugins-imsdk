/* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_C2_WRAPPER_H__
#define __GST_C2_WRAPPER_H__

#include <gst/gst.h>

#include "common.h"
#include "c2-component.h"

typedef struct _GstC2Wrapper GstC2Wrapper;

GST_API GstC2Wrapper *
gst_c2_wrapper_new ();

GST_API void
gst_c2_wrapper_free (GstC2Wrapper * wrapper);

GST_API gboolean
gst_c2_wrapper_create_component (GstC2Wrapper * wrapper,
    const gchar * name, event_handler_cb callback, gpointer userdata);

GST_API gboolean
gst_c2_wrapper_delete_component (GstC2Wrapper * wrapper);

GST_API gboolean
gst_c2_wrapper_init_block_pool (GstC2Wrapper * wrapper,
    gchar* comp, guint32 width, guint32 height, GstVideoFormat format);

GST_API gint
gst_c2_wrapper_get_block_pool_id (GstC2Wrapper * wrapper);

GST_API gboolean
gst_c2_wrapper_config_component (GstC2Wrapper * wrapper,
    GPtrArray * config);

GST_API gboolean
gst_c2_wrapper_component_start (GstC2Wrapper * wrapper);

GST_API gboolean
gst_c2_wrapper_component_stop (GstC2Wrapper * wrapper);

GST_API gboolean
gst_c2_wrapper_component_queue (GstC2Wrapper * wrapper,
    BufferDescriptor * buffer);

GST_API gboolean
gst_c2_wrapper_free_output_buffer (GstC2Wrapper * wrapper,
    uint64_t bufferIdx);

#endif // __GST_C2_WRAPPER_H__
