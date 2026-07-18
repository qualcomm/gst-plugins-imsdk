/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_ML_QAIRT_ENGINE_H__
#define __GST_ML_QAIRT_ENGINE_H__

#include <gst/gst.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>

G_BEGIN_DECLS

/**
 * GstMLQairtDelegate:
 * @GST_ML_QAIRT_DELEGATE_CPU: CPU backend is used for all operations.
 * @GST_ML_QAIRT_DELEGATE_GPU: Adreno Graphics Processing Unit.
 * @GST_ML_QAIRT_DELEGATE_HTP: Hexagon Tensor Processor (HexNN v3).
 *
 * Hardware backend used to execute the network. Maps to qairt::BackendType.
 * Only CPU, GPU and HTP are supported by QAIRT 2.47.
 */
typedef enum {
  GST_ML_QAIRT_DELEGATE_CPU,
  GST_ML_QAIRT_DELEGATE_GPU,
  GST_ML_QAIRT_DELEGATE_HTP,
} GstMLQairtDelegate;

GST_API GType gst_ml_qairt_delegate_get_type (void);
#define GST_TYPE_ML_QAIRT_DELEGATE (gst_ml_qairt_delegate_get_type())

/**
 * GstMLQairtExecPriority:
 * @GST_ML_QAIRT_EXEC_PRIORITY_NORMAL: Normal priority.
 * @GST_ML_QAIRT_EXEC_PRIORITY_LOW: Lower priority.
 * @GST_ML_QAIRT_EXEC_PRIORITY_NORMAL_HIGH: Between normal and high priority.
 * @GST_ML_QAIRT_EXEC_PRIORITY_HIGH: Higher than normal priority.
 * @GST_ML_QAIRT_EXEC_PRIORITY_CRITICAL: Critical priority.
 *
 * Execution scheduling priority. Maps to Qairt_Priority_t.
 */
typedef enum {
  GST_ML_QAIRT_EXEC_PRIORITY_NORMAL,
  GST_ML_QAIRT_EXEC_PRIORITY_LOW,
  GST_ML_QAIRT_EXEC_PRIORITY_NORMAL_HIGH,
  GST_ML_QAIRT_EXEC_PRIORITY_HIGH,
  GST_ML_QAIRT_EXEC_PRIORITY_CRITICAL,
} GstMLQairtExecPriority;

GST_API GType gst_ml_qairt_exec_priority_get_type (void);
#define GST_TYPE_ML_QAIRT_EXEC_PRIORITY (gst_ml_qairt_exec_priority_get_type())

typedef struct _GstMLQairtEngine GstMLQairtEngine;
typedef struct _GstMLQairtSettings GstMLQairtSettings;

/**
 * GstMLQairtSettings:
 * @modelfile: Path to the model. Either a QAIRT/SNPE '.dlc' container or a
 *     cached context '.bin' binary.
 * @delegate: Hardware backend used to execute the network.
 * @priority: Execution scheduling priority.
 * @outputs: Optional %NULL terminated list of output tensor names. When set,
 *     the engine generates outputs in the order defined by this list.
 *
 * Configuration passed to gst_ml_qairt_engine_new().
 */
struct _GstMLQairtSettings {
  gchar                   *modelfile;
  GstMLQairtDelegate      delegate;
  GstMLQairtExecPriority  priority;
  GList                   *outputs;
};

GST_API GstMLQairtEngine *
gst_ml_qairt_engine_new               (GstMLQairtSettings * settings);

GST_API void
gst_ml_qairt_engine_free              (GstMLQairtEngine * engine);

GST_API const GstMLInfo *
gst_ml_qairt_engine_get_input_info    (GstMLQairtEngine * engine);

GST_API const GstMLInfo *
gst_ml_qairt_engine_get_output_info   (GstMLQairtEngine * engine);

GST_API gboolean
gst_ml_qairt_engine_execute           (GstMLQairtEngine * engine,
                                       GstMLFrame * inframe,
                                       GstMLFrame * outframe);

G_END_DECLS

#endif // __GST_ML_QAIRT_ENGINE_H__
