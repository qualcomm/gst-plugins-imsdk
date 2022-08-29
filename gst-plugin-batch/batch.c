/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "batch.h"

#include <stdio.h>

#include <gst/allocators/allocators.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#include "batchpads.h"


#define GST_CAT_DEFAULT gst_batch_debug
GST_DEBUG_CATEGORY_STATIC (gst_batch_debug);

#define gst_batch_parent_class parent_class
G_DEFINE_TYPE (GstBatch, gst_batch, GST_TYPE_ELEMENT);

#define GST_BATCH_CHANNEL_BASE     100

#define GST_BATCH_SINK_CAPS \
    "video/x-raw(ANY); "    \
    "audio/x-raw(ANY)"

#define GST_BATCH_SRC_CAPS \
    "video/x-raw(ANY); "   \
    "audio/x-raw(ANY)"

enum
{
  PROP_0,
};

static GstStaticPadTemplate gst_batch_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink_%u",
        GST_PAD_SINK,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_BATCH_SINK_CAPS)
    );

static GstStaticPadTemplate gst_batch_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_BATCH_SRC_CAPS)
    );


static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static void
gst_caps_extract_video_framerate (GstStructure * structure, GValue * framerate)
{
  const GValue *value = NULL;
  gdouble fps = 0.0, outfps = 0.0;

  if ((value = gst_structure_get_value (structure, "framerate")) == NULL)
    return;

  if (G_VALUE_TYPE (framerate) != GST_TYPE_FRACTION)
    g_value_init (framerate, GST_TYPE_FRACTION);

  value = gst_structure_get_value (structure, "framerate");

  gst_util_fraction_to_double (gst_value_get_fraction_numerator (value),
      gst_value_get_fraction_denominator (value), &fps);
  gst_util_fraction_to_double (gst_value_get_fraction_numerator (framerate),
      gst_value_get_fraction_denominator (framerate), &outfps);

  if (fps > outfps)
    g_value_copy (value, framerate);
}

static gboolean
gst_batch_all_sink_pads_flushing (GstBatch * batch, GstPad * pad)
{
  GList *list = NULL;
  gboolean flushing = TRUE;

  GST_OBJECT_LOCK (batch);

  // Check all whether other sink pads are in flushing state.
  for (list = GST_ELEMENT (batch)->sinkpads; list; list = list->next) {
    // Skip current sink pad as it is already in flushing state.
    if (g_strcmp0 (GST_PAD_NAME (list->data), GST_PAD_NAME (pad)) == 0)
      continue;

    GST_OBJECT_LOCK (GST_PAD (list->data));
    flushing &= GST_PAD_IS_FLUSHING (GST_PAD (list->data));
    GST_OBJECT_UNLOCK (GST_PAD (list->data));
  }

  GST_OBJECT_UNLOCK (batch);

  return flushing;
}

static gboolean
gst_batch_all_sink_pads_non_flushing (GstBatch * batch, GstPad * pad)
{
  GList *list = NULL;
  gboolean flushing = FALSE;

  GST_OBJECT_LOCK (batch);

  // Check all whether other sink pads are in non flushing state.
  for (list = GST_ELEMENT (batch)->sinkpads; list; list = list->next) {
    // Skip current sink pad as it is already in flushing state.
    if (g_strcmp0 (GST_PAD_NAME (list->data), GST_PAD_NAME (pad)) == 0)
      continue;

    GST_OBJECT_LOCK (GST_PAD (list->data));
    flushing |= GST_PAD_IS_FLUSHING (GST_PAD (list->data));
    GST_OBJECT_UNLOCK (GST_PAD (list->data));
  }

  GST_OBJECT_UNLOCK (batch);

  return !flushing;
}

static gboolean
gst_batch_all_sink_pads_eos (GstBatch * batch, GstPad * pad)
{
  GList *list = NULL;
  gboolean eos = TRUE;

  GST_OBJECT_LOCK (batch);

  // Check all whether other sink pads are in EOS state.
  for (list = GST_ELEMENT (batch)->sinkpads; list; list = list->next) {
    // Skip current sink pad as it is already in EOS state.
    if (g_strcmp0 (GST_PAD_NAME (list->data), GST_PAD_NAME (pad)) == 0)
      continue;

    GST_OBJECT_LOCK (GST_PAD (list->data));
    eos &= GST_PAD_IS_EOS (GST_PAD (list->data));
    GST_OBJECT_UNLOCK (GST_PAD (list->data));
  }

  GST_OBJECT_UNLOCK (batch);

  return eos;
}

static gboolean
gst_batch_sink_caps_negotiated (GstBatch * batch)
{
  GList *list = NULL;
  gboolean negotiated = TRUE;

  GST_OBJECT_LOCK (batch);

  for (list = GST_ELEMENT (batch)->sinkpads; list; list = list->next)
    negotiated &= gst_pad_has_current_caps (GST_PAD (list->data));

  GST_OBJECT_UNLOCK (batch);

  return negotiated;
}

static gboolean
gst_batch_sink_buffers_available (GstBatch * batch)
{
  GList *list = NULL;
  gboolean available = TRUE;

  for (list = batch->sinkpads; list != NULL; list = g_list_next (list)) {
    GstBatchSinkPad *sinkpad = GST_BATCH_SINK_PAD (list->data);

    GST_OBJECT_LOCK (sinkpad);

    // Pads which are in EOS or FLUSHING state are not included in the checks.
    if (!GST_PAD_IS_EOS (list->data) && !GST_PAD_IS_FLUSHING (list->data)) {
      GST_BATCH_SINK_LOCK (sinkpad);
      available &= !g_queue_is_empty (sinkpad->queue);
      GST_BATCH_SINK_UNLOCK (sinkpad);
    }

    GST_OBJECT_UNLOCK (sinkpad);
  }

  return available;
}

static gboolean
gst_batch_update_src_caps (GstBatch * batch)
{
  GstCaps *srccaps = NULL, *sinkcaps = NULL, *filter = NULL, *intersect = NULL;
  GstStructure *structure = NULL;
  GList *list = NULL;
  GValue framerate = G_VALUE_INIT;
  guint idx = 0, length = 0;

  // In case the RECONFIGURE flag was not set just return immediately.
  if (!gst_pad_check_reconfigure (batch->srcpad))
    return TRUE;

  // Get the negotiated caps between the source pad and its peer.
  srccaps = gst_pad_get_allowed_caps (batch->srcpad);

  srccaps = gst_caps_make_writable (srccaps);
  length = gst_caps_get_size (srccaps);

  // Extract and remove the framerate field for video caps.
  for (idx = 0; idx < length; idx++) {
    structure = gst_caps_get_structure (srccaps, idx);
    gst_caps_extract_video_framerate (structure, &framerate);
    gst_structure_remove_field (structure, "framerate");
  }

  GST_OBJECT_LOCK (batch);

  // Iterate over all of the sink pads and verify their caps.
  for (list = GST_ELEMENT (batch)->sinkpads; list; list = list->next) {
    GstPad *pad = GST_PAD (list->data);

    // Use currently set caps if they are set otherwise use template caps.
    sinkcaps = gst_pad_has_current_caps (pad) ?
        gst_pad_get_current_caps (pad) : gst_pad_get_pad_template_caps (pad);

    GST_DEBUG_OBJECT (batch, "Sink caps %" GST_PTR_FORMAT, sinkcaps);

    sinkcaps = gst_caps_make_writable (sinkcaps);
    length = gst_caps_get_size (sinkcaps);

    // Extract and remove the framerate field for video caps.
    for (idx = 0; idx < length; idx++) {
      structure = gst_caps_get_structure (sinkcaps, idx);
      gst_caps_extract_video_framerate (structure, &framerate);
      gst_structure_remove_field (structure, "framerate");
    }

    if (filter != NULL) {
      // Intersect this sink pad caps with the previous sink pad caps.
      intersect = gst_caps_intersect (sinkcaps, filter);

      gst_caps_unref (filter);
      filter = intersect;
    } else {
      // Use current sink pad caps as filter for next sink pad.
      filter = gst_caps_ref (sinkcaps);
    }

    gst_caps_unref (sinkcaps);
  }

  GST_OBJECT_UNLOCK (batch);

  GST_DEBUG_OBJECT (batch, "Update source caps based on caps %"
      GST_PTR_FORMAT, filter);

  intersect = gst_caps_intersect (srccaps, filter);
  GST_DEBUG_OBJECT (batch, "Intersected caps %" GST_PTR_FORMAT, intersect);

  gst_caps_unref (filter);
  gst_caps_unref (srccaps);

  srccaps = intersect;

  if (srccaps == NULL || gst_caps_is_empty (srccaps)) {
    GST_ELEMENT_ERROR (batch, CORE, NEGOTIATION, (NULL),
        ("The sink and source caps do not intersect!"));
    return FALSE;
  }

  length = gst_caps_get_size (srccaps);

  // Update the framerate field for video caps.
  for (idx = 0; idx < length; idx++) {
    structure = gst_caps_get_structure (srccaps, idx);

    if (!gst_structure_has_name (structure, "video/x-raw"))
      continue;

    if (G_VALUE_TYPE (&framerate) == GST_TYPE_FRACTION)
      gst_structure_set_value (structure, "framerate", &framerate);
  }

  if (!gst_caps_is_fixed (srccaps))
    srccaps = gst_caps_fixate (srccaps);

  GST_DEBUG_OBJECT (batch, "Caps fixated to: %" GST_PTR_FORMAT, srccaps);

  // Extract the frame duration from the caps.
  if (G_VALUE_TYPE (&framerate) == GST_TYPE_FRACTION) {
    GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (batch->srcpad);

    if (G_VALUE_TYPE (&framerate) != GST_TYPE_FRACTION) {
      structure = gst_caps_get_structure (srccaps, 0);
      gst_caps_extract_video_framerate (structure, &framerate);
    }

    srcpad->duration = gst_util_uint64_scale_int (GST_SECOND,
        gst_value_get_fraction_denominator (&framerate),
        gst_value_get_fraction_numerator (&framerate));
  } else {
    // TODO Add equivalent for Audio.
  }

  // Send stream start event if not sent, before setting the source caps.
  if (!GST_BATCH_SRC_PAD (batch->srcpad)->stmstart) {
    gchar stm_id[32] = { 0, };

    GST_INFO_OBJECT (batch, "Pushing stream start event");

    // TODO: create id based on input ids.
    g_snprintf (stm_id, sizeof (stm_id), "batch-%08x", g_random_int ());
    gst_pad_push_event (batch->srcpad, gst_event_new_stream_start (stm_id));

    GST_BATCH_SRC_PAD (batch->srcpad)->stmstart = TRUE;
  }

  // Propagate fixates caps to the peer of the source pad.
  return gst_pad_set_caps (batch->srcpad, srccaps);
}

static gboolean
gst_batch_extract_sink_buffer (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstBatch *batch = GST_BATCH (element);
  GstBatchSinkPad *sinkpad = GST_BATCH_SINK_PAD (pad);
  GstBuffer *outbuffer = NULL, *inbuffer = NULL;
  GstVideoMeta *vmeta = NULL;
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  GstStructure *structure = NULL;
  gchar *name = NULL;
  guint idx = 0, id = 0;

  outbuffer = GST_BUFFER (userdata);

  GST_BATCH_SINK_LOCK (sinkpad);
  inbuffer = g_queue_pop_head (sinkpad->queue);
  GST_BATCH_SINK_UNLOCK (sinkpad);

  if (NULL == inbuffer)
    return TRUE;

  GST_TRACE_OBJECT (batch, "Taking %" GST_PTR_FORMAT, inbuffer);

  // Get the index of current sink pad.
  idx = g_list_index (element->sinkpads, sinkpad);

  // Construct the name for the protection meta structure.
  name = g_strdup_printf ("channel-%u", idx);

  // Create a structure that will contain information for decryption.
  structure = gst_structure_new (name,
    "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (inbuffer),
    "duration", G_TYPE_UINT64, GST_BUFFER_DURATION (inbuffer),
    "flags", G_TYPE_UINT, GST_BUFFER_FLAGS (inbuffer),
    NULL);
  g_free (name);

  // Add meta containing information for tensor decryption downstream.
  gst_buffer_add_protection_meta (outbuffer, structure);

  // Set the corresponding channel bit in the buffer universal offset field.
  GST_BUFFER_OFFSET (outbuffer) |= (1 << idx);

  // GAP buffer, nothing further to do.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
    gst_buffer_unref (inbuffer);
    return TRUE;
  }

  // Append the memory block from input buffer into the new buffer.
  gst_buffer_append_memory (outbuffer, gst_buffer_get_memory (inbuffer, 0));

  // Add parent meta, input buffer won't be released until new buffer is freed.
  gst_buffer_add_parent_buffer_meta (outbuffer, inbuffer);

  // If present transfer video metadata into the new buffer wrapper.
  if ((vmeta = gst_buffer_get_video_meta (inbuffer)) != NULL) {
    vmeta = gst_buffer_add_video_meta_full (outbuffer, vmeta->flags,
        vmeta->format, vmeta->width, vmeta->height, vmeta->n_planes,
        vmeta->offset, vmeta->stride);
    vmeta->id = gst_buffer_n_memory (outbuffer) - 1;
  }

  // TODO add equivalent operation for GstAudioMeta.


  // Transfer all ROI meta if present in the input buffer.
  roimeta = gst_buffer_get_video_region_of_interest_meta_id (inbuffer, id);

  // Copy ROI metadata for current memory block into the new buffer.
  while (roimeta != NULL) {
    roimeta = gst_buffer_add_video_region_of_interest_meta_id (outbuffer,
        roimeta->roi_type, roimeta->x, roimeta->y, roimeta->w, roimeta->h);
    roimeta->id = (idx * GST_BATCH_CHANNEL_BASE) + id;

    roimeta = gst_buffer_get_video_region_of_interest_meta_id (inbuffer, ++id);
  }

  // Reduce the reference count of the input buffer, it is no longer needed.
  gst_buffer_unref (inbuffer);

  return TRUE;
}

static void
gst_batch_worker_task (gpointer userdata)
{
  GstBatch *batch = GST_BATCH (userdata);
  GstBatchSrcPad *srcpad = GST_BATCH_SRC_PAD (batch->srcpad);
  GstBuffer *buffer = NULL;
  GstDataQueueItem *item = NULL;
  gint64 endtime = 0;

  GST_BATCH_LOCK (batch);

  // Initial block until all sink pads have negotiated their caps and
  // 1st buffers have arrived on all sink pads or until signaled to stop.
  while (batch->active && (!gst_batch_sink_caps_negotiated (batch) ||
      !gst_batch_sink_buffers_available (batch)) && !srcpad->stmstart)
    g_cond_wait (&batch->wakeup, &(batch)->lock);

  GST_BATCH_UNLOCK (batch);

  if (!gst_batch_update_src_caps (batch)) {
    GST_ELEMENT_ERROR (batch, CORE, NEGOTIATION, (NULL),
        ("Output format not negotiated"));
    return;
  }

  GST_BATCH_SRC_LOCK (srcpad);

  // Initialize and send the source segment for synchronization.
  if (GST_FORMAT_UNDEFINED == srcpad->segment.format) {
    gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);
    gst_pad_push_event (GST_PAD (srcpad),
        gst_event_new_segment (&(srcpad)->segment));
  }

  srcpad->basetime = (srcpad->basetime == (-1)) ?
      g_get_monotonic_time () : srcpad->basetime;

  endtime = srcpad->basetime;
  endtime += gst_util_uint64_scale (srcpad->segment.position + srcpad->duration,
      G_TIME_SPAN_SECOND, GST_SECOND);

  GST_BATCH_SRC_UNLOCK (srcpad);

  GST_BATCH_LOCK (batch);

  // Wait for data from all pads a maximum of average duration seconds.
  while (batch->active && !gst_batch_sink_buffers_available (batch)) {
    if (!g_cond_wait_until (&batch->wakeup, &(batch)->lock, endtime)) {
      GST_DEBUG_OBJECT (batch, "Clock to reached %" GST_TIME_FORMAT
          ", not all pads have buffers!", GST_TIME_ARGS (endtime));
      break;
    }
  }

  // Immediately exit the worker task if signaled to stop.
  if (!batch->active) {
    GST_BATCH_UNLOCK (batch);
    return;
  }

  GST_BATCH_UNLOCK (batch);

  // Create a new buffer wrapper to hold a reference to input buffer.
  buffer = gst_buffer_new ();
  // Reset the offset field as it will be used to store the channels mask.
  GST_BUFFER_OFFSET (buffer) = 0;

  gst_element_foreach_sink_pad (GST_ELEMENT_CAST (batch),
      gst_batch_extract_sink_buffer, buffer);

  GST_BATCH_SRC_LOCK (srcpad);

  // Set buffer duration and timestamp.
  GST_BUFFER_DURATION (buffer) = srcpad->duration;
  GST_BUFFER_TIMESTAMP (buffer) = srcpad->segment.position;

  // Adjust the segment position.
  srcpad->segment.position += GST_BUFFER_DURATION (buffer);

  GST_BATCH_SRC_UNLOCK (srcpad);

  // In case there is no channel data loop back and wait again.
  if (GST_BUFFER_OFFSET (buffer) == 0) {
    gst_buffer_unref (buffer);
    return;
  }

  // If buffer is empty and channel mask is not null, mark this buffer as GAP.
  if (gst_buffer_get_size (buffer) == 0)
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_GAP);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (GST_BATCH_SRC_PAD (batch->srcpad)->buffers, item))
    item->destroy (item);

  return;
}

static gboolean
gst_batch_start_worker_task (GstBatch * batch)
{
  if (batch->worktask != NULL)
    return TRUE;

  batch->worktask = gst_task_new (gst_batch_worker_task, batch, NULL);
  gst_task_set_lock (batch->worktask, &batch->worklock);

  GST_INFO_OBJECT (batch, "Created task %p", batch->worktask);

  GST_BATCH_LOCK (batch);

  batch->active = TRUE;

  GST_BATCH_UNLOCK (batch);

  if (!gst_task_start (batch->worktask)) {
    GST_ERROR_OBJECT (batch, "Failed to start worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (batch, "Started task %p", batch->worktask);
  return TRUE;
}

static gboolean
gst_batch_stop_worker_task (GstBatch * batch)
{
  if (NULL == batch->worktask)
    return TRUE;

  GST_INFO_OBJECT (batch, "Stopping task %p", batch->worktask);

  if (!gst_task_stop (batch->worktask))
    GST_WARNING_OBJECT (batch, "Failed to stop worker task!");

  GST_BATCH_LOCK (batch);

  batch->active = FALSE;
  g_cond_signal (&(batch)->wakeup);

  GST_BATCH_UNLOCK (batch);

  // Make sure task is not running.
  g_rec_mutex_lock (&batch->worklock);
  g_rec_mutex_unlock (&batch->worklock);

  if (!gst_task_join (batch->worktask)) {
    GST_ERROR_OBJECT (batch, "Failed to join worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (batch, "Removing task %p", batch->worktask);

  gst_object_unref (batch->worktask);
  batch->worktask = NULL;

  return TRUE;
}

static GstCaps *
gst_batch_sink_getcaps (GstBatch * batch, GstPad * pad, GstCaps * filter)
{
  GstCaps *srccaps = NULL, *tmplcaps = NULL, *sinkcaps = NULL, *intersect = NULL;
  guint idx = 0, length = 0;

  tmplcaps = gst_pad_get_pad_template_caps (batch->srcpad);

  // Query the source pad peer with its template caps as filter.
  srccaps = gst_pad_peer_query_caps (batch->srcpad, tmplcaps);
  gst_caps_unref (tmplcaps);

  GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

  length = gst_caps_get_size (srccaps);
  srccaps = gst_caps_make_writable (srccaps);

  // Some adjustments to source caps for the negotiation with the sink caps.
  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = gst_caps_get_structure (srccaps, idx);

    if (gst_structure_has_name (structure, "video/x-raw")) {
      // Set the multiview-mode field to mono for sink caps negotiation.
      gst_structure_set (structure, "multiview-mode", G_TYPE_STRING,
          "mono", NULL);

      // Set the multiview-flags field to none for sink caps negotiation.
      gst_structure_set (structure, "multiview-flags",
          GST_TYPE_VIDEO_MULTIVIEW_FLAGSET, GST_VIDEO_MULTIVIEW_FLAGS_NONE,
          GST_FLAG_SET_MASK_EXACT, NULL);

      // Remove the framerate field for video caps.
      gst_structure_remove_field (structure, "framerate");
    }
  }

  tmplcaps = gst_pad_get_pad_template_caps (pad);
  sinkcaps = gst_caps_intersect (tmplcaps, srccaps);

  GST_DEBUG_OBJECT (pad, "Sink caps %" GST_PTR_FORMAT, sinkcaps);

  gst_caps_unref (srccaps);
  gst_caps_unref (tmplcaps);

  if (filter != NULL) {
    GST_DEBUG_OBJECT (pad, "Filter caps %" GST_PTR_FORMAT, filter);

    intersect =
        gst_caps_intersect_full (filter, sinkcaps, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

    gst_caps_unref (sinkcaps);
    sinkcaps = intersect;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, sinkcaps);
  return sinkcaps;
}

static gboolean
gst_batch_sink_acceptcaps (GstBatch * batch, GstPad * pad, GstCaps * caps)
{
  GstCaps *tmplcaps = NULL, *srccaps = NULL;
  guint idx = 0, length = 0;
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Caps %" GST_PTR_FORMAT, caps);

  tmplcaps = gst_pad_get_pad_template_caps (pad);

  // Query the source pad peer with its template caps as filter.
  srccaps = gst_pad_peer_query_caps (batch->srcpad, tmplcaps);
  gst_caps_unref (tmplcaps);

  GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

  length = gst_caps_get_size (srccaps);
  srccaps = gst_caps_make_writable (srccaps);

  // Remove all fields and leave only the caps type and features.
  for (idx = 0; idx < length; idx++)
    gst_structure_remove_all_fields (gst_caps_get_structure (srccaps, idx));

  success &= gst_caps_can_intersect (caps, srccaps);
  gst_caps_unref (srccaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Caps can't intersect with source!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_batch_sink_setcaps (GstBatch * batch, GstPad * pad, GstCaps * caps)
{
  GstCaps *srccaps = NULL, *intersect = NULL;
  GstStructure *structure = NULL;
  guint idx = 0, length = 0;

  GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  // Get the negotiated caps between the srcpad and its peer.
  srccaps = gst_pad_get_allowed_caps (batch->srcpad);
  GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

  srccaps = gst_caps_make_writable (srccaps);
  length = gst_caps_get_size (srccaps);

  // Extract and remove the framerate field for video caps.
  for (idx = 0; idx < length; idx++) {
    structure = gst_caps_get_structure (srccaps, idx);
    gst_structure_remove_field (structure, "framerate");
  }

  intersect = gst_caps_intersect (srccaps, caps);
  GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

  gst_caps_unref (srccaps);

  if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
    GST_ERROR_OBJECT (pad, "Source and sink caps do not intersect!");

    if (intersect != NULL)
      gst_caps_unref (intersect);

    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_batch_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstBatch *batch = GST_BATCH (parent);

  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_batch_sink_getcaps (batch, pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_batch_sink_acceptcaps (batch, pad, caps);

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    case GST_QUERY_DRAIN:
    {
      // When upstream elements query for drain state
      // let sinkpad flush the buffers in internal queue
      gst_batch_sink_pad_flush_queue (pad);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_batch_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstBatch *batch = GST_BATCH (parent);

  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = TRUE;

      gst_event_parse_caps (event, &caps);
      success = gst_batch_sink_setcaps (batch, pad, caps);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstBatchSinkPad *sinkpad = GST_BATCH_SINK_PAD (pad);
      GstSegment *segment = &GST_BATCH_SRC_PAD (batch->srcpad)->segment;

      gst_event_copy_segment (event, &sinkpad->segment);
      gst_event_unref (event);

      GST_DEBUG_OBJECT (pad, "Received segment %" GST_SEGMENT_FORMAT
          " on %s pad", &sinkpad->segment, GST_PAD_NAME (pad));

      if (sinkpad->segment.format != GST_FORMAT_TIME) {
        GST_WARNING_OBJECT (batch, "Can only handle time segments!");
        return TRUE;
      }

      if ((segment->format == GST_FORMAT_TIME) &&
          (sinkpad->segment.rate != segment->rate)) {
        GST_ERROR_OBJECT (batch, "Got segment event with wrong rate %lf, "
            "expected %lf", sinkpad->segment.rate, segment->rate);
        return FALSE;
      }

      return TRUE;
    }
    case GST_EVENT_FLUSH_START:
      // Flush the sink pad buffer queue.
      gst_batch_sink_pad_flush_queue (pad);

      // When all other sink pads are in flushing state push event to source.
      if (gst_batch_all_sink_pads_flushing (batch, pad)) {
        gst_pad_push_event (batch->srcpad, event);
        gst_batch_src_pad_activate_task (batch->srcpad, FALSE);

        gst_batch_stop_worker_task (batch);
        return TRUE;
      }

      // Drop the event until all sink pads are in flushing state.
      gst_event_unref (event);
      return TRUE;
    case GST_EVENT_FLUSH_STOP:
      // Reset the sink pad segment element.
      gst_segment_init (&GST_BATCH_SINK_PAD (pad)->segment,
          GST_FORMAT_UNDEFINED);

      // When all other sink pads are in non flushing state push event to source.
      if (gst_batch_all_sink_pads_non_flushing (batch, pad)) {
        gst_pad_push_event (batch->srcpad, event);
        gst_batch_src_pad_activate_task (batch->srcpad, TRUE);

        gst_batch_start_worker_task (batch);
        return TRUE;
      }

      // Drop the event until all sink pads are in non flushing state.
      gst_event_unref (event);
      return TRUE;
    case GST_EVENT_EOS:
      // Flush the sink pad buffer queue.
      gst_batch_sink_pad_flush_queue (pad);

      // When all other sink pads are in EOS state push event to the source.
      if (gst_batch_all_sink_pads_eos (batch, pad)) {
        gst_pad_push_event (batch->srcpad, event);
        gst_batch_src_pad_activate_task (batch->srcpad, FALSE);

        gst_batch_stop_worker_task (batch);
        return TRUE;
      }

      // Drop the event until all sink pads are in EOS state.
      gst_event_unref (event);
      return TRUE;
    case GST_EVENT_STREAM_START:
      // Drop the event, element will create its own start event.
      gst_event_unref (event);
      return TRUE;
    case GST_EVENT_TAG:
      // Drop the event, won't be propagated downstream.
      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_batch_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstBatch *batch = GST_BATCH (parent);
  GstBatchSinkPad *sinkpad = GST_BATCH_SINK_PAD (pad);

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, buffer);

  GST_BATCH_SINK_LOCK (sinkpad);
  g_queue_push_tail (sinkpad->queue, buffer);
  GST_BATCH_SINK_UNLOCK (sinkpad);

  g_cond_signal (&(batch)->wakeup);
  return GST_FLOW_OK;
}

static GstPad*
gst_batch_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstBatch *batch = GST_BATCH (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_BATCH_LOCK (batch);

  if (reqname && sscanf (reqname, "sink_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= batch->nextidx) ? index + 1 : batch->nextidx;
  } else {
    index = batch->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  GST_BATCH_UNLOCK (batch);

  name = g_strdup_printf ("sink_%u", index);

  pad = g_object_new (GST_TYPE_BATCH_SINK_PAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (batch, "Failed to create sink pad!");
    return NULL;
  }

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_batch_sink_query));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_batch_sink_event));
  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_batch_sink_chain));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (batch, "Failed to add sink pad!");
    gst_object_unref (pad);
    return NULL;
  }

  GST_BATCH_LOCK (batch);

  batch->sinkpads = g_list_append (batch->sinkpads, pad);
  batch->nextidx = nextindex;

  GST_BATCH_UNLOCK (batch);

  GST_DEBUG_OBJECT (batch, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_batch_release_pad (GstElement * element, GstPad * pad)
{
  GstBatch *batch = GST_BATCH (element);

  GST_DEBUG_OBJECT (batch, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_BATCH_LOCK (batch);

  batch->sinkpads = g_list_remove (batch->sinkpads, pad);

  GST_BATCH_UNLOCK (batch);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_batch_change_state (GstElement * element, GstStateChange transition)
{
  GstBatch *batch = GST_BATCH (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_batch_start_worker_task (batch);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      GList *list = NULL;

      GST_OBJECT_LOCK (batch);

      for (list = GST_ELEMENT (batch)->sinkpads; list; list = list->next)
        gst_batch_sink_pad_flush_queue (GST_PAD (list->data));

      GST_OBJECT_UNLOCK (batch);

      gst_batch_stop_worker_task (batch);

      gst_segment_init (&GST_BATCH_SRC_PAD (batch->srcpad)->segment,
          GST_FORMAT_UNDEFINED);
      GST_BATCH_SRC_PAD (batch->srcpad)->stmstart = FALSE;
      break;
    }
    default:
      break;
  }

  return ret;
}

static void
gst_batch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_batch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_batch_finalize (GObject * object)
{
  GstBatch *batch = GST_BATCH (object);

  g_rec_mutex_clear (&batch->worklock);
  g_cond_clear (&batch->wakeup);

  g_mutex_clear (&batch->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (batch));
}

static void
gst_batch_class_init (GstBatchClass *klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_batch_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_batch_get_property);
  object->finalize     = GST_DEBUG_FUNCPTR (gst_batch_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_batch_sink_template, GST_TYPE_BATCH_SINK_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_batch_src_template, GST_TYPE_BATCH_SRC_PAD);

  gst_element_class_set_static_metadata (element,
      "Batching stream buffers", "Video/Audio/Muxer",
      "Batch buffers from multiple streams into one output buffer", "QTI"
  );

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_batch_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_batch_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_batch_change_state);

  // Initializes a new batch GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_batch_debug, "qtibatch", 0, "QTI Batch");
}

static void
gst_batch_init (GstBatch * batch)
{
  GstPadTemplate *template = NULL;

  g_mutex_init (&batch->lock);

  batch->nextidx = 0;
  batch->sinkpads = NULL;

  batch->active = FALSE;
  batch->worktask = NULL;

  g_rec_mutex_init (&batch->worklock);
  g_cond_init (&batch->wakeup);

  template = gst_static_pad_template_get (&gst_batch_src_template);
  batch->srcpad = g_object_new (GST_TYPE_BATCH_SRC_PAD, "name", "src",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_event_function (batch->srcpad,
      GST_DEBUG_FUNCPTR (gst_batch_src_pad_event));
  gst_pad_set_query_function (batch->srcpad,
      GST_DEBUG_FUNCPTR (gst_batch_src_pad_query));
  gst_pad_set_activatemode_function (batch->srcpad,
      GST_DEBUG_FUNCPTR (gst_batch_src_pad_activate_mode));

  gst_element_add_pad (GST_ELEMENT (batch), batch->srcpad);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtibatch", GST_RANK_NONE,
      GST_TYPE_BATCH);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtibatch,
    "QTI Batch",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
