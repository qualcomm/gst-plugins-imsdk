/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_HEXAGON_MODULE_H__
#define __GST_QTI_HEXAGON_MODULE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdint.h>

G_BEGIN_DECLS

typedef enum {
  GST_HEXAGON_MODULE_NONE,
  GST_HEXAGON_MODULE_UBWC_DMA,
} GstHexagonModules;

GST_API GType gst_hexagon_modules_get_type (void);
#define GST_TYPE_HEXAGON_MODULES (gst_hexagon_modules_get_type())

typedef struct _GstHexagonModule GstHexagonModule;

/**
 * GstHexagonModuleOpen:
 *
 * Create a new instance of the private Hexagon submodule structure.
 *
 * Hexagon submodule must implement function called 'gst_hexagon_submodule_open'
 * with the same arguments and return types.
 *
 * return: Pointer to private module instance on success or NULL on failure
 */
typedef gpointer  (*GstHexagonModuleOpen)      (void);

/**
 * GstHexagonModuleClose:
 * @submodule: Pointer to the private Hexagon submodule instance.
 *
 * Deinitialize and free the private Hexagon submodule instance.
 *
 * Hexagon submodule must implement function called 'gst_hexagon_submodule_close'
 * with the same arguments.
 *
 * return: NONE
 */
typedef void      (*GstHexagonModuleClose)     (gpointer submodule);

/**
 * GstHexagonModuleInit:
 * @submodule: Pointer to the private Hexagon submodule instance.
 *
 * Do the necessary Initialization before processing.
 *
 * Hexagon module must implement function called 'gst_hexagon_submodule_init'
 * with the same arguments.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef gboolean (*GstHexagonModuleInit)       (gpointer submodule);

/**
 * GstHexagonModuleCaps:
 *
 * Retrieve the capabilities supported by this module.
 *
 * Hexagon module must implement function called 'gst_hexagon_module_caps'
 * with the same arguments.
 *
 * return: Pointer to GStreamer caps on success or NULL on failure
 */
typedef GstCaps * (*GstHexagonModuleCaps)      (void);

/**
 * GstHexagonModuleProcess:
 * @submodule: Pointer to the private Hexagon submodule instance.
 * @inbuffer: Input Gst buffer from the upstream plugin.
 * @outbuffer: Output Gst buffer from the upstream plugin.
 *
 * Parses incoming buffer and converts into the corresponding output.
 *
 * Hexagon module must implement function called 'gst_hexagon_submodule_process'
 * with the same arguments.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef gboolean  (*GstHexagonModuleProcess)   (gpointer      submodule,
                                                GstBuffer     * inbuffer,
                                                GstBuffer     * outbuffer);

/**
 * gst_hexagon_module_new:
 * @type: Prefix used to identify the modules type.
 * @name: Name of the module.
 *
 * Allocate an instance of Hexagon submodule.
 * Usable only at plugin level.
 *
 * return: Pointer to Hexagon submodule on success or NULL on failure
 */
GST_API GstHexagonModule *
gst_hexagon_module_new      (const gchar * type,
                             const gchar * name);

/**
 * gst_hexagon_module_close:
 * @module: Pointer to the Hexagon submodule.
 *
 * De-initialize and free the memory associated with the module.
 * Usable only at plugin level.
 *
 * return: NONE
 */
GST_API void
gst_hexagon_module_close    (GstHexagonModule * module);

/**
 * gst_hexagon_module_init:
 * @module: Pointer to Hexagon submodule.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_hexagon_submodule_open' API.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_hexagon_module_init     (GstHexagonModule * module);

/**
 * gst_hexagon_module_get_caps:
 * @module: Pointer to Hexagon submodule.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_hexagon_submodule_caps' API to get its capabilities.
 *
 * return: Pointer to GstCaps on success or NULL on failure
 */
GST_API GstCaps *
gst_hexagon_module_get_caps (GstHexagonModule * module);

/**
 * gst_hexagon_module_process:
 * @module: Pointer to Hexagon submodule.
 * @inbuffer: Input Gst buffer from the upstream plugin.
 * @outbuffer: Output Gst buffer from the upstream plugin.
 *
 * Convenient wrapper function used on plugin level to call the submodule
 * 'gst_hexagon_submodule_process' API Parses incoming buffer and
 * converts into the corresponding output.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_hexagon_module_process  (GstHexagonModule * module,
                             GstBuffer        * inbuffer,
                             GstBuffer        * outbuffer);

G_END_DECLS

#endif // __GST_QTI_HEXAGON_MODULE_H__
