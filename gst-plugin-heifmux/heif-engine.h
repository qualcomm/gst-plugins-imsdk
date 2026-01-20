/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_HEIF_ENGINE_H__
#define __GST_HEIF_ENGINE_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_HEIF_ENGINE_CAST(obj)   ((GstHeifEngine*)(obj))

typedef struct _GstHeifEngine GstHeifEngine;

/**
 * gst_heif_engine_new:
 *
 * Initialize instance of heif engine.
 *
 * return: Pointer to heif engine instance on success or NULL on failure.
 */
GST_API GstHeifEngine *
gst_heif_engine_new ();

/**
 * gst_heif_engine_free:
 * @engine: Pointer to heif engine.
 *
 * Deinitialise and free the heif engine instance.
 *
 * return: NONE
 */
GST_API void
gst_heif_engine_free (GstHeifEngine * engine);

/**
 * gst_heif_engine_execute:
 * @engine: Pointer to heif engine instance.
 * @inbuf: Input buffer, heif main buffer including HEVC compressed tiles.
 * @thframes: thumbnail frames, could be empty.
 * @outbuf: Output buffer data which is encapsulated in HEIF format.
 *
 * Takes input buffer and thumbnail frames, encapsulate them in HEIF format.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_heif_engine_execute (GstHeifEngine * engine, GstBuffer * inbuf,
                         GList * thframes, GstBuffer ** outbuf);

G_END_DECLS

#endif // __GST_HEIF_ENGINE_H__
