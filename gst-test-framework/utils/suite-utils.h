/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_SUITE_UTILS_H__
#define __GST_SUITE_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_VIDEO_CODEC_H264           "H.264"
#define GST_VIDEO_CODEC_H265           "H.265"

typedef struct _GstCapsParameters GstCapsParameters;

struct _GstCapsParameters {
  const gchar *format;
  gint        width;
  gint        height;
  gint        fps;
};

/**
 * gst_mp4_verification:
 * @location: File location.
 * @width: Mp4 expected width if none zero.
 * @height: Mp4 expected height if none zero.
 * @framerate: Mp4 expected height if none zero.
 * @diff: The tolerable deviation between expected and actual FPS.
 * @induration: Mp4 expected playing time if none zero.
 * @codec: if it is MP4 file, return video-codec type by tag.
 *
 * Function for verify Mp4 info with expected parameters.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_mp4_verification (gchar *location, gint width, gint height,
    gdouble framerate, gdouble diff, guint induration, gchar **codec);

/**
 * gst_element_on_pad_added:
 * @element: GStreamer source element.
 * @pad: GStreamer source element pad.
 * @data: GStreamer sink element.
 *
 * Function for verify Mp4 info with expected parameters.
 *
 * return: None
 */
GST_API void
gst_element_on_pad_added (GstElement *element, GstPad *pad, gpointer data);

/**
 * gst_element_send_eos:
 * @element: GStreamer element.
 *
 * Function for send eos event and check return message.
 *
 * return: TRUE on success or FALSE on failure.
 */
GST_API gboolean
gst_element_send_eos (GstElement *element);

G_END_DECLS

#endif /* __GST_SUITE_UTILS_H__ */
