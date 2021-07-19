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

#ifndef __GST_ML_TYPE_H__
#define __GST_ML_TYPE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_ML_MAX_TENSORS           4
#define GST_ML_TENSOR_MAX_DIMENSIONS 4

/**
 * GstMLType:
 * @GST_ML_TYPE_UNKNOWN:
 * @GST_ML_TYPE_UINT8: data is represented as 1 byte of unsigned integer value.
 * @GST_ML_TYPE_INT32: data is represented as 4 byte of signed integer value.
 * @GST_ML_TYPE_FLOAT32: data is represented as 4 bytes of floating point value.
 *
 * The possible values of the #GstMLType describing the tensor format.
 */
typedef enum {
  GST_ML_TYPE_UNKNOWN,
  GST_ML_TYPE_UINT8,
  GST_ML_TYPE_INT32,
  GST_ML_TYPE_FLOAT32,
} GstMLType;

GST_API
guint gst_ml_type_get_size (GstMLType type);

GST_API
GstMLType gst_ml_type_from_string (const gchar * type);

GST_API
const gchar * gst_ml_type_to_string (GstMLType type);

G_END_DECLS

#endif /* __GST_ML_TYPE_H__ */
