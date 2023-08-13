/* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef __GST_C2_ENGINE_H__
#define __GST_C2_ENGINE_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstC2Engine GstC2Engine;
typedef struct _GstC2Callbacks GstC2Callbacks;

enum {
  GST_C2_EVENT_UNKNOWN,
  GST_C2_EVENT_EOS,
  GST_C2_EVENT_ERROR,
  GST_C2_EVENT_DROP
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
gst_c2_engine_new (const gchar * name, GstC2Callbacks * callbacks,
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
 * @frame: GStreamer codec frame that will be queued for encoding or decoding.
 *
 * Takes a GStreamer codec frame containing a GstBuffer, translates that codec
 * frame into Codec2 buffer and submits it to the Codec2 component for encoding
 * or decoding.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_c2_engine_queue (GstC2Engine * engine, GstVideoCodecFrame * frame);

G_END_DECLS

#endif // __GST_C2_ENGINE_H__
