/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_C2_ENGINE_H__
#define __GST_C2_ENGINE_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstC2Engine GstC2Engine;
typedef struct _GstC2Callbacks GstC2Callbacks;
typedef struct _GstC2QueueItem GstC2QueueItem;

enum {
  GST_C2_EVENT_UNKNOWN,
  GST_C2_EVENT_EOS,
  GST_C2_EVENT_ERROR,
  GST_C2_EVENT_DROP
};

enum
{
  GST_C2_MODE_VIDEO_ENCODE,
  GST_C2_MODE_VIDEO_DECODE,
  GST_C2_MODE_AUDIO_ENCODE,
  GST_C2_MODE_AUDIO_DECODE,
};

struct _GstC2QueueItem {
  /// Input buffer to be queued
  GstBuffer *buffer;
  /// Current index of the buffer
  guint64   index;
  /// Frame user data
  gpointer  userdata;
  /// Number of subframes in one buffer
  guint32   n_subframes;
};

struct _GstC2Callbacks {
  void (*event) (guint type, gpointer payload, gpointer userdata);
  void (*buffer) (GstBuffer * buffer, gpointer userdata);
};

/**
 * gst_c2_engine_new:
 * @name: The Codec2 component name which will be created internally.
 * @callbacks: Engine callback functions which will be called when an event
 *             occurs or an encoded/decoded output buffer is produced.
 * @userdata: Private user defined data which will be attached to the callbacks.
 *
 * Initialize instance of Codec2 engine.
 *
 * return: Pointer to Codec2 engine on success or NULL on failure.
 */
GST_API GstC2Engine *
gst_c2_engine_new (const gchar * name, guint32 mode, GstC2Callbacks * callbacks,
                   gpointer userdata);

/**
 * gst_c2_engine_free:
 * @engine: Pointer to Codec2 engine.
 *
 * Deinitialise and free the Codec2 engine instance.
 *
 * return: NONE
 */
GST_API void
gst_c2_engine_free (GstC2Engine * engine);

/**
 * gst_c2_engine_get_parameter:
 * @engine: Pointer to Codec2 engine instance.
 * @type: The type of the parameter payload.
 * @payload: Pointer to the structure or variable that corresponds to the
 *           given parameter type.
 *
 * Queries the Codec2 component for the parameter with the given type and
 * fills (packs) the provided payload engine structure or variable.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_c2_engine_get_parameter (GstC2Engine * engine, guint type, gpointer payload);

/**
 * gst_c2_engine_set_parameter:
 * @engine: Pointer to Codec2 engine instance.
 * @type: The type of the parameter payload.
 * @payload: Pointer to the structure or variable that corresponds to the
 *           given parameter type.
 *
 * Takes an engine parameter, tranlates (unpack) the payload to Codec2 component
 * parameter and submits it.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_c2_engine_set_parameter (GstC2Engine * engine, guint type, gpointer payload);

/**
 * gst_c2_engine_start:
 * @engine: Pointer to Codec2 engine instance.
 *
 * Allow the Codec2 component to process requests.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_c2_engine_start (GstC2Engine * engine);

/**
 * gst_c2_engine_stop:
 * @engine: Pointer to Codec2 engine instance.
 *
 * Stop the Codec2 component from processing any further requests.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_c2_engine_stop (GstC2Engine * engine);

/**
 * gst_c2_engine_flush:
 * @engine: Pointer to Codec2 engine instance.
 *
 * Flush all pending work in the Codec2 component and wait until it is done.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_c2_engine_flush (GstC2Engine * engine);

/**
 * gst_c2_engine_drain:
 * @engine: Pointer to Codec2 engine instance.
 * @eos: Flag to specify drain with or without EOS.
 *
 * Requests and waits all pending work in the Codec2 component to finish.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_c2_engine_drain (GstC2Engine * engine, gboolean eos);

/**
 * gst_c2_engine_queue:
 * @engine: Pointer to Codec2 engine instance.
 * @item: Buffer data that will be queued for encoding or decoding.
 *
 * Takes a Buffer data containing a GstBuffer, translates that codec
 * frame into Codec2 buffer and submits it to the Codec2 component for encoding
 * or decoding.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_c2_engine_queue (GstC2Engine * engine, GstC2QueueItem * item);

G_END_DECLS

#endif // __GST_C2_ENGINE_H__
