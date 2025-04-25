/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_PARSER_MODULE_H__
#define __GST_PARSER_MODULE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GST_PARSER_MODULE_OPT_DATA_TYPE:
 *
 * #GstDataType, The type of the data expected in the incoming buffers.
 * Default: #GST_DATA_TYPE_NONE.
 */
#define GST_PARSER_MODULE_OPT_DATA_TYPE "GstParserModule.data-type"

typedef struct _GstParserModule GstParserModule;

typedef enum {
  GST_DATA_TYPE_NONE,
  GST_DATA_TYPE_VIDEO,
  GST_DATA_TYPE_TEXT,
} GstDataType;

/**
 * GstParserModuleOpen:
 *
 * Create a new instance of the private parser module structure.
 *
 * Parser module must implement function called 'gst_parser_module_open'
 * with the same arguments and return types.
 *
 * return: Pointer to private module instance on success or NULL on failure
 */
typedef gpointer  (*GstParserModuleOpen)      (void);

/**
 * GstParserModuleClose:
 * @submodule: Pointer to the private parser module instance.
 *
 * Deinitialize and free the private parser module instance.
 *
 * Parser module must implement function called 'gst_parser_module_close'
 * with the same arguments.
 *
 * return: NONE
 */
typedef void      (*GstParserModuleClose)     (gpointer submodule);

/**
 * GstParserModuleConfigure:
 * @submodule: Pointer to the private parser module instance.
 * @settings: Pointer to GStreamer structure containing the settings.
 *
 * Configure the module with a set of options.
 *
 * Parser module must implement function called 'gst_parser_module_configure'
 * with the same arguments.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef gboolean  (*GstParserModuleConfigure) (gpointer submodule,
                                               GstStructure * settings);

/**
 * GstParserModuleProcess:
 * @submodule: Pointer to the private parser module instance.
 * @inbuffer: GstBuffer containing ml metadata.
 * @output: String containing json format parsed metadata.
 *
 * Parses incoming buffer containing ml metadata and converts that
 * information into a json format string.
 *
 * Parser module must implement function called 'gst_parser_module_process'
 * with the same arguments.
 *
 * The type of the 'output' argument is plugin specific. Refer to the plugin
 * module header for detailed information.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef gboolean  (*GstParserModuleProcess) (gpointer submodule,
                                             GstBuffer * inbuffer,
                                             gchar ** output);

/**
 * gst_parser_module_new:
 * @name: Name of the module.
 *
 * Allocate an instance of parser module.
 * Usable only at plugin level.
 *
 * return: Pointer to parser module on success or NULL on failure
 */
GST_API GstParserModule *
gst_parser_module_new      (const gchar * name);

/**
 * gst_parser_module_free:
 * @module: Pointer to the parser module.
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
 * @module: Pointer to parser module.
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
 * @module: Pointer to parser module.
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
 * @module: Pointer to the private parser module instance.
 * @inbuffer: GstBuffer containing ml metadata.
 * @output: String containing json format parsed metadata.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_parser_module_process' API in order to process input metadata and produce
 * json format output.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_parser_module_execute  (GstParserModule * module, GstBuffer * inbuffer,
                            gchar ** output);

/**
 * gst_parser_enumarate_modules:
 * @type: String containing the prefix used to identify the modules type.
 *
 * Helper function to find all modules of the given type and create an array
 * of GEnumValue from them that will be used for registering an enum GType.
 *
 * return: Pointer to Array of GEnumValue on success or NULL on failure
 */
GEnumValue *
gst_parser_enumarate_modules (const gchar * type);

G_END_DECLS

#endif /* __GST_PARSER_MODULE_H__ */
