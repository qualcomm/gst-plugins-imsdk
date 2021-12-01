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

#ifndef __GST_ML_INFO_H__
#define __GST_ML_INFO_H__

#include <gst/gst.h>
#include <gst/ml/ml-type.h>

G_BEGIN_DECLS

#define GST_TYPE_ML_INFO (gst_ml_info_get_type ())

typedef struct _GstMLInfo GstMLInfo;

/**
 * GstMLInfo:
 * @type: type of the tensors
 * @n_tensors: number of tensors
 * @n_dimensions: number of dimensions for each tensor
 * @tensors: array with tensor dimensions
 *
 * Info describing ML properties. This info can be filled
 * in from GstCaps with gst_ml_info_from_caps().
 *
 * Use the provided macros to access the info in this structure.
 */
struct _GstMLInfo {
  GstMLType type;
  guint     n_tensors;
  guint     n_dimensions[GST_ML_MAX_TENSORS];
  guint     tensors[GST_ML_MAX_TENSORS][GST_ML_TENSOR_MAX_DIMENSIONS];
};

GST_API
GType gst_ml_info_get_type     (void);

GST_API
void gst_ml_info_init          (GstMLInfo * info);

GST_API
GstMLInfo * gst_ml_info_new    (void);

GST_API
GstMLInfo * gst_ml_info_copy   (const GstMLInfo * info);

GST_API
void gst_ml_info_free          (GstMLInfo * info);

GST_API
gboolean gst_ml_info_from_caps (GstMLInfo * info, const GstCaps  * caps);

GST_API
GstCaps * gst_ml_info_to_caps  (const GstMLInfo * info);

GST_API
gboolean gst_ml_info_is_equal  (const GstMLInfo * info, const GstMLInfo * other);

GST_API
gsize gst_ml_info_tensor_size  (const GstMLInfo * info, guint index);

GST_API
gsize gst_ml_info_size         (const GstMLInfo * info);


#define GST_ML_INFO_TYPE(i)           ((i)->type)
#define GST_ML_INFO_N_TENSORS(i)      ((i)->n_tensors)

G_END_DECLS

#endif /* __GST_ML_INFO_H__ */
