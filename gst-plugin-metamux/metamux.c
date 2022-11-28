/*
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "metamux.h"

#include <stdio.h>
#include <string.h>

#include <gst/allocators/allocators.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/cvp/gstcvpmeta.h>

#define GST_CAT_DEFAULT gst_metamux_debug
GST_DEBUG_CATEGORY_STATIC (gst_metamux_debug);

#define gst_metamux_parent_class parent_class
G_DEFINE_TYPE (GstMetaMux, gst_metamux, GST_TYPE_ELEMENT);

#define CAST_TO_GUINT32(data) ((guint32*) data)
#define EXTRACT_DATA_VALUE(data, offset, bits) \
    (CAST_TO_GUINT32 (data)[offset / 32] >> (offset - ((offset / 32) * 32))) & ((1 << bits) - 1)

#define EXTRACT_FIELD_PARAMS(structure, name, offset, size, isunsigned) \
{\
  const GValue *value = gst_structure_get_value (structure, name);       \
  g_return_val_if_fail (value != NULL, FALSE);                           \
                                                                         \
  offset = g_value_get_uchar (gst_value_array_get_value (value, 0));     \
  size = g_value_get_uchar (gst_value_array_get_value (value, 1));       \
  isunsigned = g_value_get_uchar (gst_value_array_get_value (value, 2)); \
}


#define GST_TYPE_METAMUX_MODE       (gst_metamux_mode_get_type())

#define TIMESTAMP_DELTA_THRESHOLD   1000000

#define DEFAULT_PROP_MODE           GST_METAMUX_MODE_ASYNC
#define DEFAULT_PROP_LATENCY        0

#define GST_METAMUX_MEDIA_CAPS \
    "video/x-raw(ANY); "       \
    "audio/x-raw(ANY)"

#define GST_METAMUX_DATA_CAPS     \
    "text/x-raw, format = utf8; " \
    "cvp/x-optical-flow"

enum
{
  PROP_0,
  PROP_MODE,
  PROP_LATENCY,
};


static GstStaticPadTemplate gst_metamux_media_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_METAMUX_MEDIA_CAPS)
    );

static GstStaticPadTemplate gst_metamux_data_sink_template =
    GST_STATIC_PAD_TEMPLATE("data_%u",
        GST_PAD_SINK,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_METAMUX_DATA_CAPS)
    );

static GstStaticPadTemplate gst_metamux_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_METAMUX_MEDIA_CAPS)
    );


static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static gboolean
gst_caps_is_media_type (const GstCaps * caps, const gchar * mediatype)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);

  return (g_ascii_strcasecmp (gst_structure_get_name (s), mediatype) == 0) ?
      TRUE : FALSE;
}

static GstMetaEntry *
gst_metadata_entry_new ()
{
  GstMetaEntry *entry = g_new0 (GstMetaEntry, 1);
  g_return_val_if_fail (entry != NULL, NULL);

  g_value_init (&(entry)->value, GST_TYPE_LIST);
  entry->timestamp = GST_CLOCK_TIME_NONE;

  return entry;
}

static void
gst_metadata_entry_free (GstMetaEntry * entry)
{
  g_value_unset (&(entry)->value);
  g_free (entry);
}

static GType
gst_metamux_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_METAMUX_MODE_ASYNC,
        "No timestamp synchronization is done between the media buffers and "
        "the incoming metadata entries. When a media buffer arrives it will "
        "wait until there are metadata entries on all data pads.",
        "async"
    },
    { GST_METAMUX_MODE_SYNC,
        "Timestamp matching between media buffers & metadata entries is enabled. "
        "When a media buffer arrives it will wait a maximum of '1 / framerate' "
        "(for video caps) or '1 / rate' (for audio caps) time to receive meta "
        "entries on all pads with timestamps matching that of the buffer.",
        "sync"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMetaMuxMode", variants);

  return gtype;
}

static gboolean
gst_metamux_is_meta_available (GstMetaMux * muxer, GstClockTime timestamp)
{
  GList *list = NULL;
  GstMetaEntry *entry = NULL;
  gboolean available = TRUE, skip = FALSE;

  // Iterate ovr the data pads and check if data available on all of them.
  for (list = muxer->metapads; list != NULL; list = g_list_next (list)) {
    GstMetaMuxDataPad *dpad = GST_METAMUX_DATA_PAD (list->data);
    GstClockTimeDiff delta = GST_CLOCK_TIME_NONE;

    GST_OBJECT_LOCK (dpad);
    skip = GST_PAD_IS_EOS (dpad) || GST_PAD_IS_FLUSHING (dpad);

    // Pads which are in EOS or FLUSHING state are not included in the checks.
    if (skip && g_queue_is_empty (dpad->queue)) {
      GST_OBJECT_UNLOCK (dpad);
      continue;
    }

    GST_OBJECT_UNLOCK (dpad);

    // If there is no data available to at least one pad return immediately.
    if (!(available &= !g_queue_is_empty (dpad->queue)))
      break;

    // If timestamp is not valid, no timestamp matching will be performed.
    if (!GST_CLOCK_TIME_IS_VALID (timestamp))
      continue;

    while ((entry = g_queue_peek_head (dpad->queue)) != NULL) {
      // If the entry doesn't contain a valid timestamp we cannot do matching.
      if (!GST_CLOCK_TIME_IS_VALID (entry->timestamp)) {
        gst_metadata_entry_free (g_queue_pop_head (dpad->queue));
        continue;
      }

      delta = GST_CLOCK_DIFF (entry->timestamp, timestamp);

      // Timestamp delta is below the threshold, break and continue with next pad.
      if (ABS (delta) <= TIMESTAMP_DELTA_THRESHOLD)
        break;

      // Entry timestamp doesn't match but it's newer, keep it and return immediately.
      if (delta < 0)
        return FALSE;

      // Drop this entry as its timestamp is too old.
      gst_metadata_entry_free (g_queue_pop_head (dpad->queue));
    }

    // If there is no data left to this pad return immediately.
    if (!(available &= !g_queue_is_empty (dpad->queue)))
      break;
  }

  return available;
}

static void
gst_metamux_flush_metadata_queues (GstMetaMux * muxer)
{
  GList *list = NULL;

  GST_METAMUX_LOCK (muxer);

  for (list = muxer->metapads; list != NULL; list = g_list_next (list)) {
    GstMetaMuxDataPad *dpad = GST_METAMUX_DATA_PAD (list->data);

    while (!g_queue_is_empty (dpad->queue))
      gst_metadata_entry_free (g_queue_pop_head (dpad->queue));

    g_clear_pointer (&(dpad)->stash, g_free);
  }

  g_cond_signal (&(muxer)->wakeup);

  GST_METAMUX_UNLOCK (muxer);
  return;
}

static void
gst_metamux_process_metadata_entry (GstMetaMux * muxer, GstBuffer * buffer,
    const GValue * value, const guint index)
{
  GstStructure *structure = NULL;
  const GValue *entry = NULL;
  gint x = 0, y = 0, width = 0, height = 0;

  entry = gst_value_list_get_value (value, index);
  structure = GST_STRUCTURE (g_value_get_boxed (entry));

  // Skip the 'Parameters' structure as this is not a prediction result.
  if (gst_structure_has_name (structure, "Parameters"))
    return;

  // Fetch bounding box rectangle if it exists and fill ROI coordinates.
  entry = gst_structure_get_value (structure, "rectangle");

  if ((entry != NULL) && (gst_value_array_get_size (entry) != 4)) {
    GST_WARNING_OBJECT (muxer, "Badly formed ROI rectangle, expected 4 "
        "entries but received %u!", gst_value_array_get_size (entry));
  } else if (entry != NULL) {
    gfloat left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;

    top    = g_value_get_float (gst_value_array_get_value (entry, 0));
    left   = g_value_get_float (gst_value_array_get_value (entry, 1));
    bottom = g_value_get_float (gst_value_array_get_value (entry, 2));
    right  = g_value_get_float (gst_value_array_get_value (entry, 3));

    x      = ABS (left) * GST_VIDEO_INFO_WIDTH (muxer->vinfo);
    y      = ABS (top) * GST_VIDEO_INFO_HEIGHT (muxer->vinfo);
    width  = ABS (right - left) * GST_VIDEO_INFO_WIDTH (muxer->vinfo);
    height = ABS (bottom - top) * GST_VIDEO_INFO_HEIGHT (muxer->vinfo);

    // Clip width and height if it outside the frame limits.
    width = ((x + width) > GST_VIDEO_INFO_WIDTH (muxer->vinfo)) ?
        (GST_VIDEO_INFO_WIDTH (muxer->vinfo) - x) : width;
    height = ((y + height) > GST_VIDEO_INFO_HEIGHT (muxer->vinfo)) ?
        (GST_VIDEO_INFO_HEIGHT (muxer->vinfo) - y) : height;
  }

  // Remove the rectangle field if it exists as that data is no longer needed.
  gst_structure_remove_field (structure, "rectangle");

  if (gst_structure_has_name (structure, "OpticalFlow")) {
    GArray *mvectors = NULL, *mvstats = NULL;
    GstCvpOptclFlowMeta *meta = NULL;

    gst_structure_get (structure,
        "mvectors", G_TYPE_ARRAY, &mvectors,
        "mvstats", G_TYPE_ARRAY, &mvstats,
        NULL);

    meta = gst_buffer_add_cvp_optclflow_meta (buffer, mvectors, mvstats);
    meta->id = index;
  } else {
    GstVideoRegionOfInterestMeta *meta = NULL;

    meta = gst_buffer_add_video_region_of_interest_meta (buffer,
        gst_structure_get_name (structure), x, y, width, height);
    meta->id = index;

    gst_video_region_of_interest_meta_add_param (meta,
        gst_structure_copy (structure));
  }
}

static void
gst_metamux_worker_task (gpointer userdata)
{
  GstMetaMux *muxer = GST_METAMUX (userdata);
  GList *list = NULL;
  GstDataQueueItem *item = NULL;
  GstBuffer *buffer = NULL;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;

  if (!gst_data_queue_pop (muxer->sinkpad->buffers, &item))
    return;

  buffer = gst_buffer_ref (GST_BUFFER (item->object));
  item->destroy (item);

  GST_TRACE_OBJECT (muxer, "Processing %" GST_PTR_FORMAT, buffer);

  GST_METAMUX_LOCK (muxer);

  switch (muxer->mode) {
    case GST_METAMUX_MODE_ASYNC:
      timestamp = GST_CLOCK_TIME_NONE;

      while (muxer->active && !gst_metamux_is_meta_available (muxer, timestamp))
        g_cond_wait (&(muxer)->wakeup, GST_METAMUX_GET_LOCK (muxer));

      break;
    case GST_METAMUX_MODE_SYNC:
      timestamp = GST_BUFFER_TIMESTAMP (buffer);

      // Initialize the timeout variable when the 1st buffer arrives.
      if (muxer->timeout == (gint64) GST_CLOCK_TIME_NONE)
        muxer->timeout = g_get_monotonic_time ();

      // Increase the timeout with buffer duration and any additional latency.
      muxer->timeout += GST_TIME_AS_USECONDS (muxer->latency) +
          GST_TIME_AS_USECONDS (GST_BUFFER_DURATION (buffer));

      while (muxer->active && !gst_metamux_is_meta_available (muxer, timestamp)) {
        if (!g_cond_wait_until (&(muxer)->wakeup, GST_METAMUX_GET_LOCK (muxer),
                muxer->timeout))
          break;
      }
      break;
    default:
      GST_ERROR_OBJECT (muxer, "Unsupported mode '%d'!", muxer->mode);
      GST_METAMUX_UNLOCK (muxer);

      gst_buffer_unref (buffer);
      return;
  }

  if (!muxer->active) {
    GST_INFO_OBJECT (muxer, "Task has been deactivated");
    GST_METAMUX_UNLOCK (muxer);

    gst_buffer_unref (buffer);
    return;
  }

  // Iterate over all of the data pad queues and extract available data.
  for (list = muxer->metapads; list != NULL; list = g_list_next (list)) {
    GstMetaMuxDataPad *dpad = GST_METAMUX_DATA_PAD (list->data);
    GstMetaEntry *entry = NULL;
    guint idx = 0, size = 0;

    if ((entry = g_queue_peek_head (dpad->queue)) == NULL)
      continue;

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      GstClockTimeDiff delta = GST_CLOCK_DIFF (entry->timestamp, timestamp);

      // Timestamp delta is above the threshold, continue with next pad.
      if (ABS (delta) > TIMESTAMP_DELTA_THRESHOLD)
        continue;
    }

    if (!gst_buffer_is_writable (buffer)) {
      GST_WARNING_OBJECT (muxer, "Unable to attach metadata to buffer %p, "
          "not writable!", buffer);
      break;
    }

    entry = g_queue_pop_head (dpad->queue);
    size = gst_value_list_get_size (&(entry)->value);

    for (idx = 0; idx < size; idx++)
      gst_metamux_process_metadata_entry (muxer, buffer, &(entry)->value, idx);

    GST_TRACE_OBJECT (dpad, "Attached metadata with timestamp %" GST_TIME_FORMAT
        " to buffer %p", GST_TIME_ARGS (entry->timestamp), buffer);
    gst_metadata_entry_free (entry);
  }

  GST_METAMUX_UNLOCK (muxer);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  GST_TRACE_OBJECT (muxer, "Submitting %" GST_PTR_FORMAT, buffer);

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (muxer->srcpad->buffers, item))
    item->destroy (item);

  return;
}

static gboolean
gst_metamux_start_worker_task (GstMetaMux * muxer)
{
  GST_METAMUX_LOCK (muxer);

  if (muxer->active) {
    GST_METAMUX_UNLOCK (muxer);
    return TRUE;
  }

  muxer->worktask = gst_task_new (gst_metamux_worker_task, muxer, NULL);
  gst_task_set_lock (muxer->worktask, &muxer->worklock);

  GST_INFO_OBJECT (muxer, "Created task %p", muxer->worktask);

  muxer->active = TRUE;
  GST_METAMUX_UNLOCK (muxer);

  if (!gst_task_start (muxer->worktask)) {
    GST_ERROR_OBJECT (muxer, "Failed to start worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (muxer, "Started task %p", muxer->worktask);
  return TRUE;
}

static gboolean
gst_metamux_stop_worker_task (GstMetaMux * muxer)
{
  GST_METAMUX_LOCK (muxer);

  if (!muxer->active) {
    GST_METAMUX_UNLOCK (muxer);
    return TRUE;
  }

  GST_INFO_OBJECT (muxer, "Stopping task %p", muxer->worktask);

  if (!gst_task_stop (muxer->worktask))
    GST_WARNING_OBJECT (muxer, "Failed to stop worker task!");

  muxer->active = FALSE;
  g_cond_signal (&(muxer)->wakeup);

  GST_METAMUX_UNLOCK (muxer);

  if (!gst_task_join (muxer->worktask)) {
    GST_ERROR_OBJECT (muxer, "Failed to join worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (muxer, "Removing task %p", muxer->worktask);

  gst_object_unref (muxer->worktask);

  muxer->worktask = NULL;
  muxer->timeout = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean
gst_metamux_parse_string_metadata (GstMetaMux * muxer,
    GstMetaMuxDataPad * dpad, GstBuffer * buffer)
{
  GstMapInfo memmap = {};
  gchar *data = NULL, *token = NULL, *next = NULL, *ctx = NULL, *string = NULL;
  gboolean success = FALSE;

  if (!gst_buffer_map (buffer, &memmap, GST_MAP_READ)) {
    GST_ERROR_OBJECT (dpad, "Failed to map buffer %p!", buffer);
    return FALSE;
  }

  // Make sure that the last character is '\0'.
  data = g_strndup ((const gchar *) memmap.data, memmap.size);

  // Split the data into separate serialized GValue token for parsing.
  next = strtok_r (data, "\n", &ctx);

  // Iterate over the serialized strings and turn them into GstValueList.
  while ((token = next) != NULL) {
    GstMetaEntry *entry = gst_metadata_entry_new ();
    guint idx = 0, size = 0;

    // Grab the next token from the mapped buffer data.
    next = strtok_r (NULL, "\n", &ctx);

    // If deserialize fails it mangles the string so work with local copy.
    string = (dpad->stash != NULL) ?
        g_strconcat (dpad->stash, token, NULL) : g_strdup (token);

    success = gst_value_deserialize (&(entry)->value, string);
    g_free (string);

    if (!success) {
      GST_TRACE_OBJECT (dpad, "Failed to deserialize data!");

      // Could be a partial string (e.g. when reading from a file). Stash the
      // string, combine it with the 1st string from next buffer and try again.
      string = (dpad->stash != NULL) ?
          g_strconcat (dpad->stash, token, NULL) : g_strdup (token);

      g_free (dpad->stash);
      gst_metadata_entry_free (entry);

      dpad->stash = string;
      continue;
    }

    g_clear_pointer (&(dpad)->stash, g_free);

    // Take the buffer timestamp if it is valid.
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      entry->timestamp = GST_BUFFER_TIMESTAMP (buffer);

    size = gst_value_list_get_size (&(entry)->value);

    // Try to extract the timestamp from the parsed GValue if not set already.
    while (!GST_CLOCK_TIME_IS_VALID (entry->timestamp) && (idx < size)) {
      const GValue *value = gst_value_list_get_value (&(entry)->value, idx++);
      GstStructure *structure = GST_STRUCTURE (g_value_get_boxed (value));

      if (!gst_structure_has_name (structure, "Parameters"))
        continue;

      gst_structure_get_uint64 (structure, "timestamp", &(entry)->timestamp);
    }

    GST_METAMUX_LOCK (muxer);

    g_queue_push_tail (dpad->queue, entry);
    g_cond_signal (&(muxer)->wakeup);

    GST_METAMUX_UNLOCK (muxer);
  }

  g_free (data);
  gst_buffer_unmap (buffer, &memmap);

  return TRUE;
}

static gboolean
gst_metamux_parse_optical_flow_metadata (GstMetaMux * muxer,
    GstMetaMuxDataPad * dpad, GstBuffer * buffer)
{
  GstProtectionMeta *pmeta = NULL;
  GstStructure *structure = NULL;
  GArray *mvectors = NULL, *mvstats = NULL;
  GstMapInfo memmap = { 0, };
  gint idx = 0, length = 0, n_vectors = 0, n_stats = 0;
  guint pxlwidth = 0, pxlheight = 0, n_rowpxls = 0, n_clmnpxls = 0;
  guchar offsets[3] = { 0, }, sizes[3] = { 0, }, isunsigned[3] = { 0, };
  gdouble xscale = 1.0, yscale = 1.0;

  if ((pmeta = gst_buffer_get_protection_meta (buffer)) == NULL) {
    GST_ERROR_OBJECT (dpad, "Buffer %p does not contain CVP meta!", buffer);
    return FALSE;
  } else if (!gst_structure_has_name (pmeta->info, "CvpOpticalFlow")) {
    GST_ERROR_OBJECT (dpad, "Invalid CVP meta in buffer %p!", buffer);
    return FALSE;
  }

  gst_structure_get (pmeta->info,
      "motion-vector-params", GST_TYPE_STRUCTURE, &structure,
      "mv-paxel-width", G_TYPE_UINT, &pxlwidth,
      "mv-paxel-height", G_TYPE_UINT, &pxlheight,
      "mv-paxels-row-length", G_TYPE_UINT, &n_rowpxls,
      "mv-paxels-column-length", G_TYPE_UINT, &n_clmnpxls,
      NULL);

  if (structure == NULL) {
    GST_ERROR_OBJECT (dpad, "CVP protection meta in buffer %p does not contain"
        " the CVP motion vector information necessary for decryption!", buffer);
    return FALSE;
  }

  // Calculate the scale factor for the coordinates.
  gst_util_fraction_to_double (GST_VIDEO_INFO_WIDTH (muxer->vinfo),
      (n_rowpxls * pxlwidth), &xscale);
  gst_util_fraction_to_double (GST_VIDEO_INFO_HEIGHT (muxer->vinfo),
      (n_clmnpxls * pxlheight), &yscale);

  // Map the 1st memory block which will contain raw motion vector data.
  if (!gst_buffer_map_range (buffer, 0, 1, &memmap, GST_MAP_READ)) {
    GST_ERROR_OBJECT (dpad, "Failed to map buffer %p!", buffer);

    gst_structure_free (structure);
    return FALSE;
  }

  // Fill the X field offsets and sizes in arrays for faster access.
  EXTRACT_FIELD_PARAMS (structure, "X", offsets[0], sizes[0], isunsigned[0]);
  // Fill the Y field offsets and sizes in arrays for faster access.
  EXTRACT_FIELD_PARAMS (structure, "Y", offsets[1], sizes[1], isunsigned[1]);
  // Fill the confidence field offsets and sizes in arrays for faster access.
  EXTRACT_FIELD_PARAMS (structure, "confidence", offsets[2], sizes[2], isunsigned[2]);

  // Calculate the length of one motion vector entry in bits.
  for (idx = 0; idx < gst_structure_n_fields (structure); idx++) {
    const gchar *name = gst_structure_nth_field_name (structure, idx);
    const GValue *value = gst_structure_get_value (structure, name);

    // Size in bits is given as the 2nd value in the list.
    length += g_value_get_uchar (gst_value_array_get_value (value, 1));
  }

  // Convert length from bits to bytes.
  length = length / CHAR_BIT;
  // Number of motion vector entries.
  n_vectors = memmap.size / length;

  // Sanity check, number of motion vectors must be equal to the number of paxels.
  g_return_val_if_fail (((guint) n_vectors) == (n_rowpxls * n_clmnpxls), FALSE);

  // Iterate over the raw data in reverse, parse and add it to the list.
  mvectors = g_array_sized_new (FALSE, FALSE, sizeof (GstCvpMotionVector),
      n_vectors);
  g_array_set_size (mvectors, n_vectors);

  for (idx = 0; idx < n_vectors; idx++) {
    guint32 *data = CAST_TO_GUINT32 (&(memmap).data[idx * length]);
    GstCvpMotionVector *mvector =
        &g_array_index (mvectors, GstCvpMotionVector, idx);

    mvector->dx = EXTRACT_DATA_VALUE (data, offsets[0], sizes[0]);
    mvector->dy = EXTRACT_DATA_VALUE (data, offsets[1], sizes[1]);
    mvector->confidence = EXTRACT_DATA_VALUE (data, offsets[2], sizes[2]);

    if (!isunsigned[0] && (mvector->dx & (1 << (sizes[0] - 1))))
      mvector->dx |= ~((1 << sizes[0]) - 1) & 0xFFFF;

    if (!isunsigned[1] && (mvector->dy & (1 << (sizes[1] - 1))))
      mvector->dy |= ~((1 << sizes[1]) - 1) & 0xFFFF;

    if (!isunsigned[2] && (mvector->confidence & (1 << (sizes[2] - 1))))
      mvector->confidence |= ~((1 << sizes[2]) - 1) & 0xFFFF;

    mvector->x = ((idx % n_rowpxls) * pxlwidth) * xscale;
    mvector->y = ((idx / n_rowpxls) * pxlheight) * yscale;

    mvector->dx *= xscale;
    mvector->dy *= yscale;
  }

  gst_structure_free (structure);
  gst_buffer_unmap (buffer, &memmap);

  // A 2nd memory block indicates the presents of statistics information.
  if (gst_buffer_n_memory (buffer) == 2) {
    guint sad = 0, variance = 0;

    gst_structure_get (pmeta->info,
        "statistics-params", GST_TYPE_STRUCTURE, &structure,
        "stats-variance-threshold", G_TYPE_UINT, &variance,
        "stats-sad-threshold", G_TYPE_UINT, &sad, NULL);

    if (structure == NULL) {
      GST_ERROR_OBJECT (dpad, "CVP protection meta in buffer %p does not contain"
          " the CVP statistics information necessary for decryption!", buffer);

      g_array_free (mvectors, TRUE);
      return FALSE;
    }

    // Map the 2nd memory block which will contain raw statistics data.
    if (!gst_buffer_map_range (buffer, 1, 1, &memmap, GST_MAP_READ)) {
      GST_ERROR_OBJECT (dpad, "Failed to map buffer %p!", buffer);

      gst_structure_free (structure);
      g_array_free (mvectors, TRUE);

      return FALSE;
    }

    // Fill the variance field offsets and sizes in arrays for faster access.
    EXTRACT_FIELD_PARAMS (structure, "variance", offsets[0], sizes[0], isunsigned[0]);
    // Fill the mean field offsets and sizes in arrays for faster access.
    EXTRACT_FIELD_PARAMS (structure, "mean", offsets[1], sizes[1], isunsigned[1]);
    // Fill the SAD field offsets and sizes in arrays for faster access.
    EXTRACT_FIELD_PARAMS (structure, "SAD", offsets[2], sizes[2], isunsigned[2]);

    length = 0;

    // Calculate the length of one entry in bits.
    for (idx = 0; idx < gst_structure_n_fields (structure); idx++) {
      const gchar *name = gst_structure_nth_field_name (structure, idx);
      const GValue *value = gst_structure_get_value (structure, name);

      // Size in bits is given as the 2nd value in the list.
      length += g_value_get_uchar (gst_value_array_get_value (value, 1));
    }

    // Convert length from bits to bytes.
    length = length / CHAR_BIT;
    // Number of statistics entries.
    n_stats = memmap.size / length;

    // Sanity check, number of statistics must be equal to the motion vectors.
    g_return_val_if_fail (n_stats == n_vectors, FALSE);

    // Iterate over the raw data in reverse, parse and add it to the list.
    mvstats = g_array_new (FALSE, FALSE, sizeof (GstCvpOptclFlowStats));
    g_array_set_size (mvstats, n_stats);

    for (idx = 0; idx < n_stats; idx++) {
      guint32 *data = CAST_TO_GUINT32 (&(memmap).data[idx * length]);
      GstCvpOptclFlowStats *stats =
          &g_array_index (mvstats, GstCvpOptclFlowStats, idx);

      stats->variance = EXTRACT_DATA_VALUE (data, offsets[0], sizes[0]);
      stats->mean = EXTRACT_DATA_VALUE (data, offsets[1], sizes[1]);
      stats->sad = EXTRACT_DATA_VALUE (data, offsets[2], sizes[2]);

      if (!isunsigned[0] && (stats->variance & (1 << (sizes[0] - 1))))
        stats->variance |= ~((1 << sizes[0]) - 1) & 0xFFFF;

      if (!isunsigned[1] && (stats->mean & (1 << (sizes[1] - 1))))
        stats->mean |= ~((1 << sizes[1]) - 1) & 0xFFFF;

      if (!isunsigned[2] && (stats->sad & (1 << (sizes[2] - 1))))
        stats->sad |= ~((1 << sizes[2]) - 1) & 0xFFFF;

      // If variance or SAD are below the thresholds clear the stats variables.
      if ((stats->variance < variance) || (stats->sad < sad)) {
        stats->variance = stats->sad = 0;
        stats->mean = 0;
      }
    }

    gst_structure_free (structure);
    gst_buffer_unmap (buffer, &memmap);
  }

  {
    // Add the parsed information to a GValue container.
    GstMetaEntry *entry = NULL;
    GValue value = G_VALUE_INIT;

    entry = gst_metadata_entry_new ();

    g_value_init (&value, GST_TYPE_STRUCTURE);

    structure = gst_structure_new ("OpticalFlow",
        "mvectors", G_TYPE_ARRAY, mvectors,
        "mvstats", G_TYPE_ARRAY, mvstats,
        NULL);

    g_value_take_boxed (&value, structure);
    gst_value_list_append_value (&(entry)->value, &value);
    g_value_unset (&value);

    // Take the buffer timestamp if it is valid.
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      entry->timestamp = GST_BUFFER_TIMESTAMP (buffer);

    GST_METAMUX_LOCK (muxer);

    g_queue_push_tail (dpad->queue, entry);
    g_cond_signal (&(muxer)->wakeup);

    GST_METAMUX_UNLOCK (muxer);
  }

  return TRUE;
}

static GstCaps *
gst_metamux_main_sink_pad_getcaps (GstMetaMux * muxer, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *srccaps = NULL, *templcaps = NULL, *sinkcaps = NULL;

  templcaps = gst_pad_get_pad_template_caps (GST_PAD (muxer->srcpad));

  // Query the source pad peer with the transformed filter.
  srccaps = gst_pad_peer_query_caps (GST_PAD (muxer->srcpad), templcaps);
  gst_caps_unref (templcaps);

  GST_DEBUG_OBJECT (pad, "Src caps %" GST_PTR_FORMAT, srccaps);

  templcaps = gst_pad_get_pad_template_caps (pad);
  sinkcaps = gst_caps_intersect (templcaps, srccaps);

  gst_caps_unref (srccaps);
  gst_caps_unref (templcaps);

  GST_DEBUG_OBJECT (pad, "Filter caps  %" GST_PTR_FORMAT, filter);

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, sinkcaps, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersection);

    gst_caps_unref (sinkcaps);
    sinkcaps = intersection;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, sinkcaps);
  return sinkcaps;
}

static gboolean
gst_metamux_main_sink_pad_setcaps (GstMetaMux * muxer, GstPad * pad,
    GstCaps * caps)
{
  GstCaps *srccaps = NULL, *intersect = NULL;

  GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  // Get the negotiated caps between the srcpad and its peer.
  srccaps = gst_pad_get_allowed_caps (GST_PAD (muxer->srcpad));
  GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

  intersect = gst_caps_intersect (srccaps, caps);
  GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

  gst_caps_unref (srccaps);

  if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
    GST_ERROR_OBJECT (pad, "Source and sink caps do not intersect!");

    if (intersect != NULL)
      gst_caps_unref (intersect);

    return FALSE;
  }

  if (gst_pad_has_current_caps (GST_PAD (muxer->srcpad))) {
    srccaps = gst_pad_get_current_caps (GST_PAD (muxer->srcpad));

    if (!gst_caps_is_equal (srccaps, intersect))
      gst_pad_mark_reconfigure (GST_PAD (muxer->srcpad));

    gst_caps_unref (srccaps);
  }

  gst_caps_unref (intersect);

  // Extract audio/video information from caps.
  if (gst_caps_is_media_type (caps, "video/x-raw")) {
    if (muxer->vinfo != NULL)
      gst_video_info_free (muxer->vinfo);

    muxer->vinfo = gst_video_info_new ();

    if (!gst_video_info_from_caps (muxer->vinfo, caps)) {
      GST_ERROR_OBJECT (pad, "Invalid caps %" GST_PTR_FORMAT, caps);
      gst_caps_unref (caps);
      return FALSE;
    }
  } else {
    if (muxer->ainfo != NULL)
      gst_audio_info_free (muxer->ainfo);

    muxer->ainfo = gst_audio_info_new ();

    if (!gst_audio_info_from_caps (muxer->ainfo, caps)) {
      GST_ERROR_OBJECT (pad, "Invalid caps %" GST_PTR_FORMAT, caps);
      gst_caps_unref (caps);
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (pad, "Negotiated caps %" GST_PTR_FORMAT, caps);
  return gst_pad_push_event (GST_PAD (muxer->srcpad), gst_event_new_caps (caps));
}

static gboolean
gst_metamux_main_sink_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMetaMux *muxer = GST_METAMUX (parent);

  GST_TRACE_OBJECT (muxer, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_event_parse_caps (event, &caps);
      success = gst_metamux_main_sink_pad_setcaps (muxer, pad, caps);

      gst_event_unref (event);
      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstMetaMuxSrcPad *srcpad = muxer->srcpad;
      GstSegment segment;

      gst_event_copy_segment (event, &segment);

      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      GST_METAMUX_SRC_LOCK (srcpad);

      if (segment.format == GST_FORMAT_BYTES) {
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);

        srcpad->segment.start = segment.start;

        GST_DEBUG_OBJECT (pad, "Converted incoming segment to TIME: %"
            GST_SEGMENT_FORMAT, &(srcpad)->segment);
      } else if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &(srcpad)->segment);
        gst_segment_copy_into (&segment, &srcpad->segment);
      } else {
        GST_ERROR_OBJECT (pad, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        GST_METAMUX_SRC_UNLOCK (srcpad);
        return FALSE;
      }

      gst_event_unref (event);
      event = gst_event_new_segment (&(srcpad)->segment);

      GST_METAMUX_SRC_UNLOCK (srcpad);

      return gst_pad_push_event (GST_PAD (srcpad), event);
    }
    case GST_EVENT_FLUSH_START:
      gst_data_queue_set_flushing (GST_METAMUX_SINK_PAD (pad)->buffers, TRUE);
      gst_data_queue_flush (GST_METAMUX_SINK_PAD (pad)->buffers);

      gst_metamux_stop_worker_task (muxer);
      gst_metamux_flush_metadata_queues (muxer);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_data_queue_set_flushing (GST_METAMUX_SINK_PAD (pad)->buffers, FALSE);
      gst_metamux_start_worker_task (muxer);
      break;
    case GST_EVENT_EOS:
      gst_data_queue_set_flushing (GST_METAMUX_SINK_PAD (pad)->buffers, TRUE);
      gst_data_queue_flush (GST_METAMUX_SINK_PAD (pad)->buffers);

      gst_metamux_stop_worker_task (muxer);
      gst_metamux_flush_metadata_queues (muxer);
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_metamux_main_sink_pad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstMetaMux *muxer = GST_METAMUX (parent);

  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_metamux_main_sink_pad_getcaps (muxer, pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      GST_DEBUG_OBJECT (pad, "Accept caps: %" GST_PTR_FORMAT, caps);

      if (gst_caps_is_fixed (caps)) {
        GstCaps *tmplcaps = gst_pad_get_pad_template_caps (pad);
        GST_DEBUG_OBJECT (pad, "Template caps: %" GST_PTR_FORMAT, tmplcaps);

        success = gst_caps_can_intersect (tmplcaps, caps);
        gst_caps_unref (tmplcaps);
      }

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static GstFlowReturn
gst_metamux_main_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstMetaMuxSinkPad *sinkpad = GST_METAMUX_SINK_PAD (pad);
  GstMetaMux *muxer = GST_METAMUX (parent);
  GstDataQueueItem *item = NULL;

  if (!gst_pad_has_current_caps (GST_PAD (muxer->srcpad))) {
    if (GST_PAD_IS_FLUSHING (muxer->srcpad)) {
      gst_buffer_unref (buffer);
      return GST_FLOW_FLUSHING;
    }

    GST_ELEMENT_ERROR (muxer, STREAM, DECODE, ("No caps set!"), (NULL));
    return GST_FLOW_ERROR;
  }

  GST_TRACE_OBJECT (sinkpad, "Received %" GST_PTR_FORMAT, buffer);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (sinkpad->buffers, item))
    item->destroy (item);

  return GST_FLOW_OK;
}

static gboolean
gst_metamux_data_sink_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL, *tmplcaps = NULL, *intersect = NULL;

      gst_event_parse_caps (event, &caps);
      gst_event_unref (event);

      GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

      // Get the negotiated caps between the srcpad and its peer.
      tmplcaps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (pad, "Template caps %" GST_PTR_FORMAT, tmplcaps);

      intersect = gst_caps_intersect (tmplcaps, caps);
      GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

      gst_caps_unref (tmplcaps);

      if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
        GST_ERROR_OBJECT (pad, "Template and sink caps do not intersect!");

        if (intersect != NULL)
          gst_caps_unref (intersect);

        return FALSE;
      }

      if (gst_caps_is_media_type (caps, "text/x-raw"))
        GST_METAMUX_DATA_PAD (pad)->type = GST_DATA_TYPE_TEXT;
      else if (gst_caps_is_media_type (caps, "cvp/x-optical-flow"))
        GST_METAMUX_DATA_PAD (pad)->type = GST_DATA_TYPE_OPTICAL_FLOW;
      else
        GST_METAMUX_DATA_PAD (pad)->type = GST_DATA_TYPE_UNKNOWN;

      return TRUE;
    }
    case GST_EVENT_FLUSH_START:
    {
      GstMetaMux *muxer = GST_METAMUX (parent);

      GST_METAMUX_LOCK (muxer);
      // Flushing flag has been already set, just notify the worker task.
      g_cond_signal (&(muxer)->wakeup);
      GST_METAMUX_UNLOCK (muxer);

      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_EOS:
    {
      GstMetaMux *muxer = GST_METAMUX (parent);

      GST_OBJECT_LOCK (pad);
      GST_OBJECT_FLAG_SET (pad, GST_PAD_FLAG_EOS);
      GST_OBJECT_UNLOCK (pad);

      GST_METAMUX_LOCK (muxer);
      g_cond_signal (&(muxer)->wakeup);
      GST_METAMUX_UNLOCK (muxer);

      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_SEGMENT:
    case GST_EVENT_GAP:
    case GST_EVENT_STREAM_START:
      // Drop the event, those events are forwarded by the main sink pad.
      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_metamux_data_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstMetaMux *muxer = GST_METAMUX (parent);
  GstMetaMuxDataPad *dpad = GST_METAMUX_DATA_PAD (pad);
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  if (GST_PAD_IS_FLUSHING (muxer->srcpad)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }

  // If the main sink pad has reached EOS return EOS for data(meta) pads.
  if (GST_PAD_IS_EOS (muxer->sinkpad)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }

  // Buffer is marked as GAP, nothing to process. Just consume it.
  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, buffer);

  time = gst_util_get_timestamp ();

  if (dpad->type == GST_DATA_TYPE_TEXT)
    success = gst_metamux_parse_string_metadata (muxer, dpad, buffer);
  else if (dpad->type == GST_DATA_TYPE_OPTICAL_FLOW)
    success = gst_metamux_parse_optical_flow_metadata (muxer, dpad, buffer);

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (pad, "Parse took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  gst_buffer_unref (buffer);
  return success ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static GstPad*
gst_metamux_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstMetaMux *muxer = GST_METAMUX (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_METAMUX_LOCK (muxer);

  if (reqname && sscanf (reqname, "data_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= muxer->nextidx) ? index + 1 : muxer->nextidx;
  } else {
    index = muxer->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  name = g_strdup_printf ("data_%u", index);

  pad = g_object_new (GST_TYPE_METAMUX_DATA_PAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (muxer, "Failed to create sink pad!");
    return NULL;
  }

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_metamux_data_sink_pad_event));
  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_metamux_data_sink_pad_chain));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (muxer, "Failed to add sink pad!");
    gst_object_unref (pad);
    return NULL;
  }

  muxer->metapads = g_list_append (muxer->metapads, pad);
  muxer->nextidx = nextindex;

  GST_METAMUX_UNLOCK (muxer);

  GST_DEBUG_OBJECT (muxer, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_metamux_release_pad (GstElement * element, GstPad * pad)
{
  GstMetaMux *muxer = GST_METAMUX (element);

  GST_DEBUG_OBJECT (muxer, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_METAMUX_LOCK (muxer);
  muxer->metapads = g_list_remove (muxer->metapads, pad);
  GST_METAMUX_UNLOCK (muxer);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_metamux_change_state (GstElement * element, GstStateChange transition)
{
  GstMetaMux *muxer = GST_METAMUX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_data_queue_set_flushing (muxer->sinkpad->buffers, FALSE);
      gst_metamux_start_worker_task (muxer);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_data_queue_set_flushing (muxer->sinkpad->buffers, TRUE);
      gst_data_queue_flush (muxer->sinkpad->buffers);

      gst_metamux_stop_worker_task (muxer);
      gst_metamux_flush_metadata_queues (muxer);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_metamux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMetaMux *muxer = GST_METAMUX (object);

  switch (prop_id) {
    case PROP_MODE:
      muxer->mode = g_value_get_enum (value);
      break;
    case PROP_LATENCY:
      muxer->latency = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_metamux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMetaMux *muxer = GST_METAMUX (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, muxer->mode);
      break;
    case PROP_LATENCY:
      g_value_set_uint64 (value, muxer->latency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_metamux_finalize (GObject * object)
{
  GstMetaMux *muxer = GST_METAMUX (object);

  if (muxer->ainfo != NULL)
    gst_audio_info_free (muxer->ainfo);

  if (muxer->vinfo != NULL)
    gst_video_info_free (muxer->vinfo);

  g_rec_mutex_clear (&muxer->worklock);
  g_cond_clear (&muxer->wakeup);

  g_mutex_clear (&muxer->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (muxer));
}

static void
gst_metamux_class_init (GstMetaMuxClass *klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_metamux_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_metamux_get_property);
  object->finalize     = GST_DEBUG_FUNCPTR (gst_metamux_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_metamux_media_sink_template, GST_TYPE_METAMUX_SINK_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_metamux_data_sink_template, GST_TYPE_METAMUX_DATA_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_metamux_src_template, GST_TYPE_METAMUX_SRC_PAD);

  g_object_class_install_property (object, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "Operational mode",
          GST_TYPE_METAMUX_MODE, DEFAULT_PROP_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (object, PROP_LATENCY,
      g_param_spec_uint64 ("latency", "Latency",
          "Additional latency to allow more time for upstream to produce "
          "metadata entries for the current position (in nanoseconds).",
          0, G_MAXUINT64, DEFAULT_PROP_LATENCY,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (element,
      "Meta muxer", "Video/Audio/Text/Muxer",
      "Muxes data stream as GstMeta with raw audio or video stream", "QTI"
  );

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_metamux_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_metamux_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_metamux_change_state);

  // Initializes a new muxer GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_metamux_debug, "qtimetamux", 0, "QTI Meta Muxer");
}

static void
gst_metamux_init (GstMetaMux * muxer)
{
  GstPadTemplate *template = NULL;

  g_mutex_init (&muxer->lock);

  muxer->nextidx = 0;
  muxer->metapads = NULL;

  muxer->vinfo = NULL;
  muxer->ainfo = NULL;

  muxer->active = FALSE;
  muxer->worktask = NULL;
  muxer->timeout = GST_CLOCK_TIME_NONE;

  g_rec_mutex_init (&muxer->worklock);
  g_cond_init (&muxer->wakeup);

  muxer->mode = DEFAULT_PROP_MODE;
  muxer->latency = DEFAULT_PROP_LATENCY;

  template = gst_static_pad_template_get (&gst_metamux_media_sink_template);
  muxer->sinkpad = g_object_new (GST_TYPE_METAMUX_SINK_PAD, "name", "sink",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_event_function (GST_PAD (muxer->sinkpad),
      GST_DEBUG_FUNCPTR (gst_metamux_main_sink_pad_event));
  gst_pad_set_query_function (GST_PAD (muxer->sinkpad),
      GST_DEBUG_FUNCPTR (gst_metamux_main_sink_pad_query));
  gst_pad_set_chain_function (GST_PAD (muxer->sinkpad),
      GST_DEBUG_FUNCPTR (gst_metamux_main_sink_pad_chain));
  gst_element_add_pad (GST_ELEMENT (muxer), GST_PAD (muxer->sinkpad));

  template = gst_static_pad_template_get (&gst_metamux_src_template);
  muxer->srcpad = g_object_new (GST_TYPE_METAMUX_SRC_PAD, "name", "src",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_event_function (GST_PAD (muxer->srcpad),
      GST_DEBUG_FUNCPTR (gst_metamux_src_pad_event));
  gst_pad_set_query_function (GST_PAD (muxer->srcpad),
      GST_DEBUG_FUNCPTR (gst_metamux_src_pad_query));
  gst_pad_set_activatemode_function (GST_PAD (muxer->srcpad),
      GST_DEBUG_FUNCPTR (gst_metamux_src_pad_activate_mode));
  gst_element_add_pad (GST_ELEMENT (muxer), GST_PAD (muxer->srcpad));
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimetamux", GST_RANK_NONE,
      GST_TYPE_METAMUX);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimetamux,
    "QTI Meta Muxer",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
