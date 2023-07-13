/*
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

#ifndef __GST_ML_MODULE_H__
#define __GST_ML_MODULE_H__

#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>
#include <gst/ml/gstmlmeta.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_ml_module_debug);

#define GST_ML_MODULE_CAST(obj) ((GstMLModule*)(obj))

/**
 * GST_ML_MODULE_OPT_CAPS
 *
 * #GST_TYPE_CAPS: A fixated set of ML caps. Submodule will expect to receive
 *                 ML frames with the fixated caps layout for processing.
 * Default: NULL
 *
 * To be used as a mandatory option for 'gst_ml_module_configure'.
 */
#define GST_ML_MODULE_OPT_CAPS "GstMLModule.caps"

/**
 * GST_ML_MODULE_OPT_LABELS
 *
 * #G_TYPE_STRING: Path and name of the file containing the ML labels.
 * Default: NULL
 *
 * To be used as a possible option for 'gst_ml_module_configure'.
 */
#define GST_ML_MODULE_OPT_LABELS "GstMLModule.labels"

/**
 * GST_ML_MODULE_OPT_THRESHOLD
 *
 * #G_TYPE_DOUBLE: The confidence threshold value in the range of 0.0 to 100.0,
 *                 below which prediction results will be discarded.
 * Default: 0.0
 *
 * To be used as a possible option for 'gst_ml_module_configure'.
 */
#define GST_ML_MODULE_OPT_THRESHOLD "GstMLModule.threshold"

/**
 * GST_ML_MODULE_OPT_CONSTANTS
 *
 * #GST_TYPE_STRUCTURE: A structure containing module and caps specific
 *                      constants, offsets and/or coefficients used for
 *                      processing incoming tensors.
 *                      May not be applicable for all modules.
 * Default: NULL
 *
 * To be used as a possible option for 'gst_ml_module_configure'.
 */
#define GST_ML_MODULE_OPT_CONSTANTS "GstMLModule.constants"

typedef struct _GstMLModule GstMLModule;
typedef struct _GstLabel GstLabel;

/**
 * GstMLModuleOpen:
 *
 * Create a new instance of the private ML post-processing module structure.
 *
 * Post-processing module must implement function called 'gst_ml_module_open'
 * with the same arguments and return types.
 *
 * return: Pointer to private module instance on success or NULL on failure
 */
typedef gpointer  (*GstMLModuleOpen)      (void);

/**
 * GstMLModuleClose:
 * @submodule: Pointer to the private ML post-processing module instance.
 *
 * Deinitialize and free the private ML post-processing module instance.
 *
 * Post-processing module must implement function called 'gst_ml_module_close'
 * with the same arguments.
 *
 * return: NONE
 */
typedef void      (*GstMLModuleClose)     (gpointer submodule);

/**
 * GstMLModuleCaps:
 *
 * Retrieve the capabilities supported by this module.
 *
 * Post-processing module must implement function called 'gst_ml_module_caps'
 * with the same arguments.
 *
 * return: Pointer to GStreamer caps on success or NULL on failure
 */
typedef GstCaps * (*GstMLModuleCaps)      (void);

/**
 * GstMLModuleConfigure:
 * @submodule: Pointer to the private ML post-processing module instance.
 * @settings: Pointer to GStreamer structure containing the settings.
 *
 * Configure the module with a set of options.
 *
 * Post-processing module must implement function called 'gst_ml_module_configure'
 * with the same arguments.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef gboolean  (*GstMLModuleConfigure) (gpointer submodule,
                                           GstStructure * settings);

/**
 * GstMLModuleProcess:
 * @submodule: Pointer to the private ML post-processing module instance.
 * @frame: Frame containing mapped tensor memory blocks that need processing.
 * @output: Plugin specific output, refer to the module header of that plugin.
 *
 * Parses incoming buffer containing result tensors and converts that
 * information into a plugin specific output.
 *
 * Post-processing module must implement function called 'gst_ml_module_process'
 * with the same arguments.
 *
 * The the type the 'output' argument is plugin specific. Refer to the plugin
 * module header for detailed information.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef gboolean  (*GstMLModuleProcess)   (gpointer submodule,
                                           GstMLFrame * mlframe,
                                           gpointer output);

/**
 * GstLabel:
 * @name: The label name.
 * @color: Color of the label is present, otherwise is set to 0x00000000.
 *
 * Machine learning label used for post-processing.
 */
struct _GstLabel {
  gchar *name;
  guint color;
};

/**
 * gst_ml_module_new:
 * @name: Name of the module.
 *
 * Allocate an instance of ML post-processing module.
 * Usable only at plugin level.
 *
 * return: Pointer to ML post-processing module on success or NULL on failure
 */
GST_API GstMLModule *
gst_ml_module_new      (const gchar * name);

/**
 * gst_ml_module_free:
 * @module: Pointer to the ML post-processing module.
 *
 * De-initialize and free the memory associated with the module.
 * Usable only at plugin level.
 *
 * return: NONE
 */
GST_API void
gst_ml_module_free     (GstMLModule * module);

/**
 * gst_ml_module_init:
 * @module: Pointer to ML post-processing module.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_ml_module_open' API.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_module_init     (GstMLModule * module);

/**
 * gst_ml_module_get_caps:
 * @module: Pointer to ML post-processing module.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_ml_module_caps' API to get its capabilities.
 *
 * return: Pointer to GstCaps on success or NULL on failure
 */
GST_API GstCaps *
gst_ml_module_get_caps (GstMLModule * module);

/**
 * gst_ml_module_set_opts:
 * @module: Pointer to ML post-processing module.
 * @options: Pointer to GStreamer structure containing the settings.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_ml_module_configure' API to set various options.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_module_set_opts (GstMLModule * module, GstStructure * options);

/**
 * gst_ml_module_execute:
 * @module: Pointer to ML post-processing module.
 * @mlframe: Frame containing mapped tensor memory blocks that need processing.
 * @output: Plugin specific output, refer to the module header of that plugin.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_ml_module_process' API in order to process input tensors and produce
 * a plugin specific output.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_ml_module_execute  (GstMLModule * module, GstMLFrame * mlframe,
                        gpointer output);


/**
 * gst_ml_label_new:
 *
 * Allocate and initialize instance of ML label.
 *
 * return: Pointer to ML label on success or NULL on failure
 */
GST_API GstLabel *
gst_ml_label_new (void);

/**
 * gst_ml_module_free:
 * @label: Pointer to ML label instance
 *
 * Deinitialize and free the label instance.
 *
 * return: NONE
 */
GST_API void
gst_ml_label_free (GstLabel * label);

/**
 * gst_ml_parse_labels:
 * @input: String containing either file location or a GValue string.
 * @list: GValue list which will be filled with label information.
 *
 * Helper function to parse either a file containing labels in GValue format
 * or a directly raw GValue formated string.
 *
 * return: TRUE on success or FALSE on failure
 */
gboolean
gst_ml_parse_labels (const gchar * input, GValue * list);

/**
 * gst_ml_load_labels:
 * @list: GValue list containing label information.
 *
 * Helper function to load labels information from GValue list into hash table
 * comprised from GstLabel.
 *
 * return: Pointer to hash table of GstLabel on success or NULL on failure
 */
GHashTable *
gst_ml_load_labels (GValue * list);

/**
 * gst_ml_enumarate_modules:
 * @type: String containing the prefix used to identify the modules type.
 *
 * Helper function to find all modules of the given type and create an array
 * of GEnumValue from them that will be used for registering an enum GType.
 *
 * return: Pointer to Array of GEnumValue on success or NULL on failure
 */
GEnumValue *
gst_ml_enumarate_modules (const gchar * type);

G_END_DECLS

#endif /* __GST_ML_MODULE_H__ */
