/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_PARSER_MODULE_H__
#define __GST_PARSER_MODULE_H__

#include <gst/ml/ml-info.h>
#include <gst/ml/ml-frame.h>
#include <gst/ml/gstmlmeta.h>

G_BEGIN_DECLS

#define GST_PARSER_MODULE_CAST(obj) ((GstParserModule*)(obj))

/**
 * GST_PARSER_MODULE_OPT_CAPS
 *
 * #GST_TYPE_CAPS: A fixated set of ML caps. Submodule will expect to receive
 *                 ML frames with the fixated caps layout for processing.
 * Default: NULL
 *
 * To be used as a mandatory option for 'gst_parser_module_configure'.
 */
#define GST_PARSER_MODULE_OPT_CAPS "GstParserModule.caps"

/**
 * GST_PARSER_MODULE_OPT_LABELS
 *
 * #G_TYPE_STRING: Path and name of the file containing the ML labels.
 * Default: NULL
 *
 * To be used as a possible option for 'gst_parser_module_configure'.
 */
#define GST_PARSER_MODULE_OPT_LABELS "GstParserModule.labels"

/**
 * GST_PARSER_MODULE_OPT_THRESHOLD
 *
 * #G_TYPE_DOUBLE: The confidence threshold value in the range of 0.0 to 100.0,
 *                 below which prediction results will be discarded.
 * Default: 0.0
 *
 * To be used as a possible option for 'gst_parser_module_configure'.
 */
#define GST_PARSER_MODULE_OPT_THRESHOLD "GstParserModule.threshold"

/**
 * GST_PARSER_MODULE_OPT_CONSTANTS
 *
 * #GST_TYPE_STRUCTURE: A structure containing module and caps specific
 *                      constants, offsets and/or coefficients used for
 *                      processing incoming tensors.
 *                      May not be applicable for all modules.
 * Default: NULL
 *
 * To be used as a possible option for 'gst_parser_module_configure'.
 */
#define GST_PARSER_MODULE_OPT_CONSTANTS "GstParserModule.constants"

/**
 * GST_PARSER_MODULE_OPT_XTRA_OPERATION
 *
 * #G_TYPE_ENUM: Operation enum value contains extra operations to perform
 *               on the data.
 *               May not be applicable for all modules.
 * Default: NULL
 *
 * To be used as a possible option for 'gst_parser_module_configure'.
 */
#define GST_PARSER_MODULE_OPT_XTRA_OPERATION "GstParserModule.xtra-operation"

typedef struct _GstParserModule GstParserModule;
typedef struct _GstMLLabel GstMLLabel;

/**
 * GstParserModuleOpen:
 *
 * Create a new instance of the private ML post-processing module structure.
 *
 * Post-processing module must implement function called 'gst_parser_module_open'
 * with the same arguments and return types.
 *
 * return: Pointer to private module instance on success or NULL on failure
 */
typedef gpointer  (*GstParserModuleOpen)      (void);

/**
 * GstParserModuleClose:
 * @submodule: Pointer to the private ML post-processing module instance.
 *
 * Deinitialize and free the private ML post-processing module instance.
 *
 * Post-processing module must implement function called 'gst_parser_module_close'
 * with the same arguments.
 *
 * return: NONE
 */
typedef void      (*GstParserModuleClose)     (gpointer submodule);

/**
 * GstParserModuleConfigure:
 * @submodule: Pointer to the private ML post-processing module instance.
 * @settings: Pointer to GStreamer structure containing the settings.
 *
 * Configure the module with a set of options.
 *
 * Post-processing module must implement function called 'gst_parser_module_configure'
 * with the same arguments.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef gboolean  (*GstParserModuleConfigure) (gpointer submodule,
                                           GstStructure * settings);

/**
 * GstParserModuleProcess:
 * @submodule: Pointer to the private ML post-processing module instance.
 * @frame: Frame containing mapped tensor memory blocks that need processing.
 * @output: Plugin specific output, refer to the module header of that plugin.
 *
 * Parses incoming buffer containing result tensors and converts that
 * information into a plugin specific output.
 *
 * Post-processing module must implement function called 'gst_parser_module_process'
 * with the same arguments.
 *
 * The the type the 'output' argument is plugin specific. Refer to the plugin
 * module header for detailed information.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef gboolean  (*GstParserModuleProcess) (gpointer submodule,
                                            GstBuffer * inbuffer,
                                            gchar ** output);

/**
 * GstMLLabel:
 * @name: The label name.
 * @color: Color of the label is present, otherwise is set to 0x00000000.
 *
 * Machine learning label used for post-processing.
 */
struct _GstMLLabel {
  gchar *name;
  guint color;
};

/**
 * gst_parser_module_new:
 * @name: Name of the module.
 *
 * Allocate an instance of ML post-processing module.
 * Usable only at plugin level.
 *
 * return: Pointer to ML post-processing module on success or NULL on failure
 */
GST_API GstParserModule *
gst_parser_module_new      (const gchar * name);

/**
 * gst_parser_module_free:
 * @module: Pointer to the ML post-processing module.
 *
 * De-initialize and free the memory associated with the module.
 * Usable only at plugin level.
 *
 * return: NONE
 */
GST_API void
gst_parser_module_free     (GstParserModule * module);

/**
 * gst_parser_module_init:
 * @module: Pointer to ML post-processing module.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_parser_module_open' API.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_parser_module_init     (GstParserModule * module);

/**
 * gst_parser_module_set_opts:
 * @module: Pointer to ML post-processing module.
 * @options: Pointer to GStreamer structure containing the settings.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_parser_module_configure' API to set various options.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_parser_module_set_opts (GstParserModule * module, GstStructure * options);

/**
 * gst_parser_module_execute:
 * @module: Pointer to ML post-processing module.
 * @inbuffer: Buffer of tensor memory blocks that need processing.
 * @output: Plugin specific output, refer to the module header of that plugin.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_parser_module_process' API in order to process input tensors and produce
 * a plugin specific output.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_parser_module_execute  (GstParserModule * module, GstBuffer * inbuffer,
                            gchar ** output);

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

#endif /* __GST_PARSER_MODULE_H__ */
