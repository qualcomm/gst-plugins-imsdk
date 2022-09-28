/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_C2_COMMON_H__
#define __GST_C2_COMMON_H__

#include <stdio.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "c2-config.h"

typedef enum {
    EVENT_OUTPUTS_DONE = 0,
    EVENT_TRIPPED,
    EVENT_ERROR
} EVENT_TYPE;

typedef enum {
    BLOCK_MODE_DONT_BLOCK = 0,
    BLOCK_MODE_MAY_BLOCK
} BLOCK_MODE_TYPE;

typedef enum {
    BUFFER_POOL_BASIC_LINEAR = 0,
    BUFFER_POOL_BASIC_GRAPHIC
} BUFFER_POOL_TYPE;

typedef enum {
    FLAG_TYPE_DROP_FRAME = 1 << 0,
    // For input frames: no output frame shall be generated when
    // processing this frame.
    FLAG_TYPE_END_OF_STREAM = 1 << 1,
    // For output frames: this frame shall be discarded.
    // This frame shall be discarded with its metadata.
    FLAG_TYPE_DISCARD_FRAME = 1 << 2,
    // This frame is not the last frame produced for the input
    FLAG_TYPE_INCOMPLETE = 1 << 3,
    // Frame contains only codec-specific configuration data,
    // and no actual access unit
    FLAG_TYPE_CODEC_CONFIG = 1 << 4
} FLAG_TYPE;

typedef struct {
    gint32 fd;
    guint8* data;
    guint32 size;
    guint64 timestamp;
    guint64 index;
    guint32 width;
    guint32 height;
    guint32 stride;
    GstVideoFormat format;
    FLAG_TYPE flag;
    BUFFER_POOL_TYPE pool_type;
    guint8* config_data; // codec config data
    guint32 config_size; // size of codec config data
    guint32 ubwc_flag;
} BufferDescriptor;

#endif // __GST_C2_COMMON_H__