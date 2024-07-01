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
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 *
 * Copyright (c) 2022,2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_ML_SNPE_ENGINE_H__
#define __GST_ML_SNPE_ENGINE_H__

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>

G_BEGIN_DECLS

/**
 * GstMLSnpeDelegate:
 * @GST_ML_SNPE_DELEGATE_NONE: CPU is used for all operations
 * @GST_ML_SNPE_DELEGATE_DSP: Hexagon Digital Signal Processor
 * @GST_ML_SNPE_DELEGATE_GPU: Graphics Processing Unit
 * @GST_ML_SNPE_DELEGATE_AIP: Snapdragon AIX + HVX
 *
 * Different delegates for transferring part or all of the model execution.
 */
typedef enum {
  GST_ML_SNPE_DELEGATE_NONE,
  GST_ML_SNPE_DELEGATE_DSP,
  GST_ML_SNPE_DELEGATE_GPU,
  GST_ML_SNPE_DELEGATE_AIP,
} GstMLSnpeDelegate;

GST_API GType gst_ml_snpe_delegate_get_type (void);
#define GST_TYPE_ML_SNPE_DELEGATE (gst_ml_snpe_delegate_get_type())

/**
 * GstMLSnpePerfProfile:
 * @GST_ML_SNPE_PERF_PROFILE_DEFAULT: Run in a standard mode.
 * @GST_ML_SNPE_PERF_PROFILE_BALANCED: Run in a balanced mode.
 * @GST_ML_SNPE_PERF_PROFILE_HIGH_PERFORMANCE: Run in high performance mode.
 * @GST_ML_SNPE_PERF_PROFILE_POWER_SAVER: Run in a power sensitive mode,
 *     at the expense of performance.
 * @GST_ML_SNPE_PERF_PROFILE_SYSTEM_SETTINGS: Use system settings.
 *     SNPE makes no calls to any performance related APIs.
 * @GST_ML_SNPE_PERF_PROFILE_SUSTAINED_HIGH_PERFORMANCE:
 *     Run in sustained high performance mode.
 * @GST_ML_SNPE_PERF_PROFILE_BURST: Run in burst mode.
 * @GST_ML_SNPE_PERF_PROFILE_LOW_POWER_SAVER: Run in lower clock than POWER_SAVER,
 *     at the expense of performance.
 * @GST_ML_SNPE_PERF_PROFILE_HIGH_POWER_SAVER: Run in higher clock
 *     and provides better performance than POWER_SAVER.
 * @GST_ML_SNPE_PERF_PROFILE_LOW_BALANCED: Run in lower balanced mode.
 *
 * Different performance setting profiles.
 */
typedef enum {
  GST_ML_SNPE_PERF_PROFILE_DEFAULT,
  GST_ML_SNPE_PERF_PROFILE_BALANCED,
  GST_ML_SNPE_PERF_PROFILE_HIGH_PERFORMANCE,
  GST_ML_SNPE_PERF_PROFILE_POWER_SAVER,
  GST_ML_SNPE_PERF_PROFILE_SYSTEM_SETTINGS,
  GST_ML_SNPE_PERF_PROFILE_SUSTAINED_HIGH_PERFORMANCE,
  GST_ML_SNPE_PERF_PROFILE_BURST,
  GST_ML_SNPE_PERF_PROFILE_LOW_POWER_SAVER,
  GST_ML_SNPE_PERF_PROFILE_HIGH_POWER_SAVER,
  GST_ML_SNPE_PERF_PROFILE_LOW_BALANCED,
} GstMLSnpePerfProfile;

GST_API GType gst_ml_snpe_perf_profile_get_type (void);
#define GST_TYPE_ML_SNPE_PERF_PROFILE (gst_ml_snpe_perf_profile_get_type())

/**
 * GstMLSnpeProfilingLevel:
 * @GST_ML_SNPE_PROFILING_LEVEL_OFF:      No profiling.
 *     Collects no runtime stats in the DiagLog.
 * @GST_ML_SNPE_PROFILING_LEVEL_BASIC:    Basic profiling.
 *     Collects some runtime stats in the DiagLog.
 * @GST_ML_SNPE_PROFILING_LEVEL_DETAILED: Detailed profiling.
 *      Collects more runtime stats in the DiagLog, including per-layer statistics.
 *      Performance may be impacted.
 * @GST_ML_SNPE_PROFILING_LEVEL_MODERATE: Moderate profiling.
 *     Collects more runtime stats in the DiagLog, no per-layer statistics.
 *
 * Different profiling levels.
 */
typedef enum {
  GST_ML_SNPE_PROFILING_LEVEL_OFF,
  GST_ML_SNPE_PROFILING_LEVEL_BASIC,
  GST_ML_SNPE_PROFILING_LEVEL_DETAILED,
  GST_ML_SNPE_PROFILING_LEVEL_MODERATE,
} GstMLSnpeProfilingLevel;

GST_API GType gst_ml_snpe_profiling_level_get_type (void);
#define GST_TYPE_ML_SNPE_PROFILING_LEVEL (gst_ml_snpe_profiling_level_get_type())

/**
 * GstMLSnpeExecutionPriority:
 * @GST_ML_SNPE_EXEC_PRIORITY_NORMAL:      Normal priority.
 * @GST_ML_SNPE_EXEC_PRIORITY_HIGH:        Higher than normal priority.
 * @GST_ML_SNPE_EXEC_PRIORITY_LOW:         Lower priority.
 *
 * Different levels of execution priority.
 */
typedef enum {
  GST_ML_SNPE_EXEC_PRIORITY_NORMAL,
  GST_ML_SNPE_EXEC_PRIORITY_HIGH,
  GST_ML_SNPE_EXEC_PRIORITY_LOW,
} GstMLSnpeExecPriority;

GST_API GType gst_ml_snpe_exec_priority_get_type (void);
#define GST_TYPE_ML_SNPE_EXEC_PRIORITY (gst_ml_snpe_exec_priority_get_type())

typedef struct _GstMLSnpeEngine GstMLSnpeEngine;
typedef struct _GstMLSnpeSettings GstMLSnpeSettings;

struct _GstMLSnpeSettings {
  gchar                   *modelfile;
  GstMLSnpeDelegate       delegate;
  GstMLSnpePerfProfile    perf_profile;
  GstMLSnpeProfilingLevel profiling_level;
  GstMLSnpeExecPriority   exec_priority;
  gboolean                is_tensor;
  GList                   *outputs;
};

GST_API GstMLSnpeEngine *
gst_ml_snpe_engine_new (GstMLSnpeSettings * settings);

GST_API void
gst_ml_snpe_engine_free (GstMLSnpeEngine * engine);

GST_API GstCaps *
gst_ml_snpe_engine_get_input_caps (GstMLSnpeEngine * engine);

GST_API GstCaps *
gst_ml_snpe_engine_get_output_caps (GstMLSnpeEngine * engine);

GST_API gboolean
gst_ml_snpe_engine_update_output_caps (GstMLSnpeEngine * engine, GstCaps * caps);

GST_API gboolean
gst_ml_snpe_engine_execute (GstMLSnpeEngine * engine, GstMLFrame * inframe,
                            GstMLFrame * outframe);

G_END_DECLS

#endif /* __GST_ML_SNPE_ENGINE_H__ */
