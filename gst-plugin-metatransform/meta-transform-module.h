/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_META_TRANSFORM_MODULE_H__
#define __GST_META_TRANSFORM_MODULE_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_meta_transform_module_debug);

typedef struct _GstMetaTransformModule GstMetaTransformModule;

GST_VIDEO_API GType gst_meta_transform_backend_get_type (void);
#define GST_TYPE_META_TRANSFORM_BACKEND (gst_meta_transform_backend_get_type())

/**
 * gst_meta_transform_module_new:
 * @backend: The type of the underlying converter.
 * @settings: Structure with backend specific options.
 *
 * Allocate and initialize instance of meta transform module.
 *
 * return: Pointer to module on success or NULL on failure
 */
GST_VIDEO_API GstMetaTransformModule *
gst_meta_transform_module_new (const gchar * name, GstStructure * settings);

/**
 * gst_meta_transform_module_free:
 * @module: Pointer to meta transform module.
 *
 * Deinitialise and free the meta transform module.
 *
 * return: NONE
 */
GST_VIDEO_API void
gst_meta_transform_module_free (GstMetaTransformModule * module);

/**
 * gst_meta_transform_module_process:
 * @module: Pointer to meta transform module.
 * @buffer: Buffer with possible meta which will be processed.
 *
 * Submit buffer with possible meta for processing.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_VIDEO_API gboolean
gst_meta_transform_module_process (GstMetaTransformModule * module,
                                   GstBuffer * buffer);

G_END_DECLS

#endif // __GST_META_TRANSFORM_MODULE_H__
