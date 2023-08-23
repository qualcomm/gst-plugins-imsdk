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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_ML_TFLITE_ENGINE_H__
#define __GST_ML_TFLITE_ENGINE_H__

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>
#include <tensorflow/lite/version.h>

G_BEGIN_DECLS

/**
 * GST_ML_TFLITE_ENGINE_OPT_MODEL:
 *
 * #G_TYPE_STRING, neural network model file path and name
 * Default: NULL
 */
#define GST_ML_TFLITE_ENGINE_OPT_MODEL \
    "GstMLTFLiteEngine.model"

#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 10)
/**
 * GST_ML_TFLITE_ENGINE_OPT_EXT_DELEGATE_PATH:
 *
 * #G_TYPE_STRING, external delegate absolute file path and name
 * Default: NULL
 */

#define GST_ML_TFLITE_ENGINE_OPT_EXT_DELEGATE_PATH \
    "GstMLTFLiteEngine.ext-delegate-path"

/**
 * GST_ML_TFLITE_ENGINE_OPT_EXT_DELEGATE_OPTS:
 *
 * #GST_TYPE_STRUCTURE, external delegate options
 * Default: NULL
 */

#define GST_ML_TFLITE_ENGINE_OPT_EXT_DELEGATE_OPTS \
    "GstMLTFLiteEngine.ext-delegate-opts"

#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 10)

/**
 * GstMLTFLiteDelegate:
 * @GST_ML_TFLITE_DELEGATE_NONE     : CPU is used for all operations
 * @GST_ML_TFLITE_DELEGATE_NNAPI_DSP: DSP through Android NN API
 * @GST_ML_TFLITE_DELEGATE_NNAPI_GPU: GPU through Android NN API
 * @GST_ML_TFLITE_DELEGATE_NNAPI_NPU: NPU through Android NN API
 * @GST_ML_TFLITE_DELEGATE_HEXAGON  : Hexagon DSP is used for all operations
 * @GST_ML_TFLITE_DELEGATE_GPU      : GPU is used for all operations
 * @GST_ML_TFLITE_DELEGATE_XNNPACK  : Prefer to delegate nodes to XNNPACK
 * @GST_ML_TFLITE_DELEGATE_EXTERNAL : Use external delegate
 *
 * Different delegates for transferring part or all of the model execution.
 */
typedef enum {
  GST_ML_TFLITE_DELEGATE_NONE,
  GST_ML_TFLITE_DELEGATE_NNAPI_DSP,
  GST_ML_TFLITE_DELEGATE_NNAPI_GPU,
  GST_ML_TFLITE_DELEGATE_NNAPI_NPU,
  GST_ML_TFLITE_DELEGATE_HEXAGON,
  GST_ML_TFLITE_DELEGATE_GPU,
  GST_ML_TFLITE_DELEGATE_XNNPACK,
#if TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 10)
  GST_ML_TFLITE_DELEGATE_EXTERNAL,
#endif // TF_MAJOR_VERSION > 2 || (TF_MAJOR_VERSION == 2 && TF_MINOR_VERSION >= 10)
} GstMLTFLiteDelegate;

GST_API GType gst_ml_tflite_delegate_get_type (void);
#define GST_TYPE_ML_TFLITE_DELEGATE (gst_ml_tflite_delegate_get_type())

/**
 * GST_ML_TFLITE_ENGINE_OPT_DELEGATE:
 *
 * #GST_TYPE_ML_TFLITE_DELEGATE, set the delegate
 * Default: #GST_ML_TFLITE_DELEGATE_NONE.
 */
#define GST_ML_TFLITE_ENGINE_OPT_DELEGATE \
    "GstMLTFLiteEngine.delegate"

/**
 * GST_ML_TFLITE_ENGINE_OPT_THREADS:
 *
 * #G_TYPE_UINT, number of theads available to the interpreter
 * Default: 1
 */
#define GST_ML_TFLITE_ENGINE_OPT_THREADS \
    "GstMLTFLiteEngine.threads"

typedef struct _GstMLTFLiteEngine GstMLTFLiteEngine;

GST_API GstMLTFLiteEngine *
gst_ml_tflite_engine_new              (GstStructure * settings);

GST_API void
gst_ml_tflite_engine_free             (GstMLTFLiteEngine * engine);

GST_API const GstMLInfo *
gst_ml_tflite_engine_get_input_info   (GstMLTFLiteEngine * engine);

GST_API const GstMLInfo *
gst_ml_tflite_engine_get_output_info  (GstMLTFLiteEngine * engine);

GST_API gboolean
gst_ml_tflite_engine_execute          (GstMLTFLiteEngine * engine,
                                       GstMLFrame * inframe,
                                       GstMLFrame * outframe);

G_END_DECLS

#endif /* __GST_ML_TFLITE_ENGINE_H__ */
