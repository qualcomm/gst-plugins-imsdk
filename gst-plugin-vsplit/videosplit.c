/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "videosplit.h"

#include <stdio.h>

#include "videosplitpads.h"


#define GST_CAT_DEFAULT gst_video_split_debug
GST_DEBUG_CATEGORY_STATIC (gst_video_split_debug);

#define gst_video_split_parent_class parent_class
G_DEFINE_TYPE (GstVideoSplit, gst_video_split, GST_TYPE_ELEMENT);

#define GST_TYPE_VIDEO_SPLIT_MODE (gst_video_split_mode_get_type())

#define DEFAULT_PROP_MODE           GST_VIDEO_SPLIT_MODE_NORMAL

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

#undef GST_VIDEO_FPS_RANGE
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, 255 ]"

#define GST_VIDEO_FORMATS \
  "{ NV12, NV21, UYVY, YUY2, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8 }"

#define GST_CONVERTER_REQUEST(obj) ((GstConverterRequest *) obj)

enum
{
  PROP_0,
  PROP_MODE,
};

static GstStaticPadTemplate gst_video_split_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM, GST_VIDEO_FORMATS))
    );

static GstStaticPadTemplate gst_video_split_src_template =
    GST_STATIC_PAD_TEMPLATE("src_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM, GST_VIDEO_FORMATS))
    );


typedef struct _GstConverterRequest GstConverterRequest;

struct _GstConverterRequest {
  GstMiniObject parent;

  // Request ID.
  gpointer      id;

  // Input frames submitted with provided ID.
  GstVideoFrame *inframes;
  // Number of input frames.
  guint         n_inputs;

  // Output frames submitted with provided ID.
  GstVideoFrame *outframes;
  // Number of output frames.
  guint         n_outputs;

  // Time it took for this request to be processed.
  GstClockTime  time;
};

static GstConverterRequest *
gst_converter_request_new ()
{
  GstConverterRequest *request = g_new0 (GstConverterRequest, 1);

  request->id = NULL;
  request->inframes = NULL;
  request->n_inputs = 0;
  request->outframes = NULL;
  request->n_outputs = 0;
  request->time = GST_CLOCK_TIME_NONE;

  return request;
}

static void
gst_converter_request_free (GstConverterRequest * request)
{
  GstBuffer *buffer = NULL;
  guint idx = 0;

  for (idx = 0; idx < request->n_inputs; idx++) {
    buffer = request->inframes[idx].buffer;

    if (buffer != NULL) {
      gst_video_frame_unmap (&(request)->inframes[idx]);
      gst_buffer_unref (buffer);
    }
  }

  for (idx = 0; idx < request->n_outputs; idx++) {
    buffer = request->outframes[idx].buffer;

    if (buffer != NULL) {
      gst_video_frame_unmap (&(request)->outframes[idx]);
      gst_buffer_unref (buffer);
    }
  }

  g_free (request->inframes);
  g_free (request->outframes);
  g_free (request);
}

static GType
gst_video_split_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue methods[] = {
    { GST_VIDEO_SPLIT_MODE_NORMAL,
        "Normal mode. Incoming buffer is rescaled and color converted for each "
        "of the source pads in order to match the negotiated caps.", "normal"
    },
    { GST_VIDEO_SPLIT_MODE_ROI,
        "ROI mode. Incoming buffer is checked for ROI meta. For each meta "
        "entry a crop, rescale and color conversion are performed, and then "
        "sent to the corresponding source pad. Pads with no corresponding ROI "
        "meta will produce GAP buffers.", "roi"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstVideoSplitMode", methods);

  return gtype;
}

static void
gst_data_queue_free_item (gpointer userdata)
{
 GstDataQueueItem *item = userdata;
 gst_buffer_unref (GST_BUFFER (item->object));
 g_slice_free (GstDataQueueItem, item);
}

static gboolean
gst_caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
}

static gboolean
gst_video_split_srcpad_push_event (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);
  GstEvent *event = GST_EVENT (userdata);

  GST_TRACE_OBJECT (vsplit, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_push_event (pad, gst_event_ref (event));
}

static gboolean
gst_video_split_prepare_output_frame (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (pad);
  GstVideoFrame *frames = GST_CONVERTER_REQUEST (userdata)->outframes;
  GstBufferPool *pool = NULL;
  GstBuffer *inbuffer = NULL, *outbuffer = NULL;
  guint idx = 0, n_entries = 0;

  GST_OBJECT_LOCK (vsplit);
  idx = g_list_index (element->srcpads, pad);
  GST_OBJECT_UNLOCK (vsplit);

  inbuffer = GST_CONVERTER_REQUEST (userdata)->inframes[0].buffer;

  // Fetch the number of ROI entries.
  n_entries = gst_buffer_get_n_meta (inbuffer,
      GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

  // Skip this pad as there is no corresponding ROI meta in ROI mode.
  if ((vsplit->mode == GST_VIDEO_SPLIT_MODE_ROI) && (idx >= n_entries))
    return TRUE;

  pool = srcpad->pool;

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (pad, "Failed to activate buffer pool!");
    return FALSE;
  }

  // Retrieve new output buffer from the pool.
  if (gst_buffer_pool_acquire_buffer (pool, &outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (pad, "Failed to acquire buffer!");
    return FALSE;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  if (!gst_video_frame_map (&frames[idx], srcpad->info, outbuffer,
          GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (srcpad, "Failed to map buffer!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_video_split_push_output_buffer (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (pad);
  GstConverterRequest *request = GST_CONVERTER_REQUEST (userdata);
  GstBuffer *inbuffer = NULL, *outbuffer = NULL;
  GstDataQueueItem *item = NULL;
  guint idx = 0;

  GST_OBJECT_LOCK (vsplit);
  idx = g_list_index (element->srcpads, pad);
  GST_OBJECT_UNLOCK (vsplit);

  inbuffer = request->inframes[0].buffer;
  outbuffer = request->outframes[idx].buffer;

  if (outbuffer != NULL) {
    gst_video_frame_unmap (&(request)->outframes[idx]);
    request->outframes[idx].buffer = NULL;
  } else {
    outbuffer = gst_buffer_new ();

    // Copy the flags and timestamps from the input buffer.
    gst_buffer_copy_into (outbuffer, inbuffer,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

    // Mark this buffer as GAP.
    GST_BUFFER_FLAG_SET (outbuffer, GST_BUFFER_FLAG_GAP);
  }

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (outbuffer);
  item->size = gst_buffer_get_size (outbuffer);
  item->duration = GST_BUFFER_DURATION (outbuffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (srcpad->buffers, item))
    item->destroy (item);

  return TRUE;
}

static void
gst_video_split_update_params (GstVideoSplit * vsplit,
    GstVideoFrame * inframe, GstVideoFrame * outframes, guint n_outputs)
{
  GstStructure *structure = NULL;
  GValue srcrects = G_VALUE_INIT, dstrects = G_VALUE_INIT;
  GValue entry = G_VALUE_INIT, value = G_VALUE_INIT;
  GstVideoRectangle inrect = {0,0,0,0}, outrect = {0,0,0,0};
  guint idx = 0;
  gint par_n = 0, par_d = 0, sar_n = 0, sar_d = 0, num = 0, den = 0;

  g_value_init (&srcrects, GST_TYPE_ARRAY);
  g_value_init (&dstrects, GST_TYPE_ARRAY);

  g_value_init (&entry, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_INT);

  // Iterate over all output frames.
  for (idx = 0; idx < n_outputs; idx++) {
    GstVideoFrame *frame = &outframes[idx];
    GstVideoRegionOfInterestMeta *roimeta = NULL;

    // There is no buffer for this frame, no need to update params for it.
    if (frame->buffer == NULL)
      continue;

    roimeta = (vsplit->mode != GST_VIDEO_SPLIT_MODE_ROI) ? NULL :
        gst_buffer_get_video_region_of_interest_meta_id (inframe->buffer, idx);

    // If available extract coordinates and dimensions from ROI meta.
    inrect.x = roimeta ? (gint) roimeta->x : 0;
    inrect.y = roimeta ? (gint) roimeta->y : 0;
    inrect.w = roimeta ? (gint) roimeta->w : GST_VIDEO_FRAME_WIDTH (inframe);
    inrect.h = roimeta ? (gint) roimeta->h : GST_VIDEO_FRAME_HEIGHT (inframe);

    g_value_set_int (&value, inrect.x);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, inrect.y);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, inrect.w);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, inrect.h);
    gst_value_array_append_value (&entry, &value);

    gst_value_array_append_value (&srcrects, &entry);
    g_value_reset (&entry);

    // Fill output PAR (Pixel Aspect Ratio), will be used to calculations.
    par_n = GST_VIDEO_INFO_PAR_N (&(frame)->info);
    par_d = GST_VIDEO_INFO_PAR_D (&(frame)->info);

    // Calculate input SAR (Source Aspect Ratio) value.
    if (!gst_util_fraction_multiply (inrect.w, inrect.h, par_n, par_d,
            &sar_n, &sar_d))
      sar_n = sar_d = 1;

    outrect.x = 0;
    outrect.y = 0;
    outrect.w = GST_VIDEO_FRAME_WIDTH (frame);
    outrect.h = GST_VIDEO_FRAME_HEIGHT (frame);

    // Adjust destination dimensions to preserve SAR.
    gst_util_fraction_multiply (sar_n, sar_d, par_d, par_n, &num, &den);

    if (num > den) {
      outrect.h = gst_util_uint64_scale_int (outrect.w, den, num);

      // Clip height if outside the limit and recalculate width.
      if (outrect.h > GST_VIDEO_FRAME_HEIGHT (frame)) {
        outrect.h = GST_VIDEO_FRAME_HEIGHT (frame);
        outrect.w = gst_util_uint64_scale_int (outrect.h, num, den);
        outrect.x = (GST_VIDEO_FRAME_WIDTH (frame) - outrect.w) / 2;
      }

      outrect.y = (GST_VIDEO_FRAME_HEIGHT (frame) - outrect.h) / 2;
    } else if (num < den) {
      outrect.w = gst_util_uint64_scale_int (outrect.h, num, den);

      // Clip width if outside the limit and recalculate height.
      if (outrect.w > GST_VIDEO_FRAME_WIDTH (frame)) {
        outrect.w = GST_VIDEO_FRAME_WIDTH (frame);
        outrect.h = gst_util_uint64_scale_int (outrect.w, den, num);
        outrect.y = (GST_VIDEO_FRAME_HEIGHT (frame) - outrect.h) / 2;
      }

      outrect.x = (GST_VIDEO_FRAME_WIDTH (frame) - outrect.w) / 2;
    }

    g_value_set_int (&value, outrect.x);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, outrect.y);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, outrect.w);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, outrect.h);
    gst_value_array_append_value (&entry, &value);

    gst_value_array_append_value (&dstrects, &entry);
    g_value_reset (&entry);

    // Add ROI meta with the actual part of the buffer filled with image data.
    gst_buffer_add_video_region_of_interest_meta (frame->buffer,
        "ImageRegion", outrect.x, outrect.y, outrect.w, outrect.h);

    GST_TRACE_OBJECT (vsplit, "Rectangles [%u] SAR[%d/%d]: [%d %d %d %d]"
        " -> [%d %d %d %d]", idx, sar_n, sar_d, inrect.x, inrect.y, inrect.w,
        inrect.h, outrect.x, outrect.y, outrect.w, outrect.h);
  }

  structure = gst_structure_new_empty ("options");

#ifdef USE_C2D_CONVERTER
  gst_structure_set_value (structure,
      GST_C2D_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, &srcrects);
  gst_structure_set_value (structure,
      GST_C2D_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, &dstrects);

  gst_c2d_video_converter_set_input_opts (vsplit->c2dconvert, 0, structure);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  gst_structure_set_value (structure,
      GST_GLES_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, &srcrects);
  gst_structure_set_value (structure,
      GST_GLES_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, &dstrects);

  gst_gles_video_converter_set_input_opts (vsplit->glesconvert, 0, structure);
#endif // USE_GLES_CONVERTER

  g_value_unset (&value);
  g_value_unset (&entry);

  g_value_unset (&dstrects);
  g_value_unset (&srcrects);
}

static void
gst_video_split_srcpad_worker_task (gpointer userdata)
{
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (srcpad->buffers, &item)) {
    GstBuffer *buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    GST_TRACE_OBJECT (srcpad, "Submitting %" GST_PTR_FORMAT, buffer);

    // Adjust the source pad segment position.
    srcpad->segment.position = GST_BUFFER_TIMESTAMP (buffer) +
        GST_BUFFER_DURATION (buffer);

    gst_pad_push (GST_PAD (srcpad), buffer);
  } else {
    GST_INFO_OBJECT (srcpad, "Pause worker task!");
    gst_pad_pause_task (GST_PAD (srcpad));
  }
}

static GstFlowReturn
gst_video_split_sinkpad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * inbuffer)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (parent);
  GstConverterRequest *request = NULL;
  gboolean success = FALSE;
  guint n_entries = 0;

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, inbuffer);

  GST_OBJECT_LOCK (vsplit);
  n_entries = g_list_length (GST_ELEMENT (vsplit)->srcpads);
  GST_OBJECT_UNLOCK (vsplit);

  // Convenient structure containing all the necessary data.
  request = gst_converter_request_new ();
  request->inframes = g_new0 (GstVideoFrame, 1);
  request->n_inputs = 1;
  request->outframes = g_new0 (GstVideoFrame, n_entries);
  request->n_outputs = n_entries;

  // Get start time for performance measurements.
  request->time = gst_util_get_timestamp ();

  success = gst_video_frame_map (&(request)->inframes[0],
      GST_VIDEO_SPLIT_SINKPAD (pad)->info, inbuffer,
      GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to map input buffer!");
    return GST_FLOW_ERROR;
  }

  // Fetch and prepare output buffers for each of the source pads.
  success = gst_element_foreach_src_pad (GST_ELEMENT_CAST (vsplit),
      gst_video_split_prepare_output_frame, request);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Failed to prepare output video frames!");
    gst_converter_request_free (request);
    return GST_FLOW_ERROR;
  }

  // Check whether there is any actual work to be done in ROI mode.
  n_entries = gst_buffer_get_n_meta (inbuffer,
      GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

  if ((vsplit->mode == GST_VIDEO_SPLIT_MODE_ROI) && (n_entries == 0)) {
    success = gst_element_foreach_src_pad (GST_ELEMENT_CAST (vsplit),
        gst_video_split_push_output_buffer, request);

    gst_converter_request_free (request);
    return success ? GST_FLOW_OK : GST_FLOW_ERROR;
  }

#ifdef USE_C2D_CONVERTER
  {
    gpointer *request_ids = g_new0 (gpointer, request->n_outputs);
    guint idx = 0;

    // Submit each output frame as seperate c2d converter request.
    for (idx = 0; idx < request->n_outputs; idx++) {
      // Update source/destination rectangles and output buffers flags/meta.
      gst_video_split_update_params (vsplit, &(request)->inframes[0],
          &(request)->outframes[idx], 1);

      request_ids[idx] = gst_c2d_video_converter_submit_request (vsplit->c2dconvert,
              request->inframes, request->n_inputs, &(request)->outframes[idx]);
    }

    // Wait for all c2d converter requests to complete.
    for (idx = 0; idx < request->n_outputs; idx++)
      success &= gst_c2d_video_converter_wait_request (vsplit->c2dconvert,
          request_ids[idx]);

    g_free (request_ids);
  }
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  // Update source/destination rectangles and output buffers flags/meta.
  gst_video_split_update_params (vsplit, &(request)->inframes[0],
      request->outframes, request->n_outputs);

  success = gst_gles_video_converter_process (vsplit->glesconvert,
      request->inframes, request->n_inputs, request->outframes,
      request->n_outputs);
#endif // USE_GLES_CONVERTER

  // Get time difference between current time and start.
  request->time = GST_CLOCK_DIFF (request->time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (vsplit, "Conversion took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (request->time),
      (GST_TIME_AS_USECONDS (request->time) % 1000));

  success = gst_element_foreach_src_pad (GST_ELEMENT_CAST (vsplit),
      gst_video_split_push_output_buffer, request);

  // Free the memory allocated by the internal request structure.
  gst_converter_request_free (request);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Failed to push output buffers!");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstCaps *
gst_video_split_sinkpad_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *caps = NULL, *intersect = NULL;

  if (!(caps = gst_pad_get_current_caps (pad)))
    caps = gst_pad_get_pad_template_caps (pad);

  GST_DEBUG_OBJECT (pad, "Current caps: %" GST_PTR_FORMAT, caps);

  if (filter != NULL) {
    GST_DEBUG_OBJECT (pad, "Filter caps: %" GST_PTR_FORMAT, caps);
    intersect = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (caps);
    caps = intersect;
  }


  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_video_split_sinkpad_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstCaps *tmplcaps = NULL;
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Caps %" GST_PTR_FORMAT, caps);

  tmplcaps = gst_pad_get_pad_template_caps (GST_PAD (pad));
  GST_DEBUG_OBJECT (pad, "Template: %" GST_PTR_FORMAT, tmplcaps);

  success &= gst_caps_can_intersect (caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Caps can't intersect with template!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_video_split_sinkpad_setcaps (GstVideoSplit * vsplit, GstPad * pad,
    GstCaps * caps)
{
  GstStructure *opts = NULL;
  GList *list = NULL;
  GstVideoInfo info = { 0, };
  gint maxwidth = 0, maxheight = 0;

  GST_DEBUG_OBJECT (vsplit, "Setting caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vsplit, "Failed to extract input video info from caps!");
    return FALSE;
  }

  // Fill video info structure from the negotiated caps.
  if (GST_VIDEO_SPLIT_SINKPAD (pad)->info != NULL)
    gst_video_info_free (GST_VIDEO_SPLIT_SINKPAD (pad)->info);

  GST_VIDEO_SPLIT_SINKPAD (pad)->info = gst_video_info_copy (&info);

  opts = gst_structure_new_empty ("options");

#ifdef USE_C2D_CONVERTER
  gst_structure_set (opts,
      GST_C2D_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN,
          gst_caps_has_compression (caps, "ubwc"),
      NULL);

  // Configure the input parameters of the GLES converter.
  gst_c2d_video_converter_set_input_opts (vsplit->c2dconvert, 0, opts);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  // TODO Workaround due to single thread limitation in GLES.
  if (vsplit->glesconvert != NULL)
    gst_gles_video_converter_free (vsplit->glesconvert);

  vsplit->glesconvert = gst_gles_video_converter_new ();

  gst_structure_set (opts,
      GST_GLES_VIDEO_CONVERTER_OPT_UBWC_FORMAT, G_TYPE_BOOLEAN,
          gst_caps_has_compression (caps, "ubwc"),
      NULL);

  // Configure the input parameters of the GLES converter.
  gst_gles_video_converter_set_input_opts (vsplit->glesconvert, 0, opts);
#endif // USE_GLES_CONVERTER

  GST_VIDEO_SPLIT_LOCK (vsplit);

  for (list = vsplit->srcpads; list != NULL; list = g_list_next (list)) {
    GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (list->data);

    if (!gst_video_split_srcpad_setcaps (srcpad, caps)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (vsplit), CORE, NEGOTIATION, (NULL),
          ("Failed to set caps to %s!", GST_PAD_NAME (srcpad)));

      GST_VIDEO_SPLIT_UNLOCK (vsplit);
      return FALSE;
    }

    // Find the maximum width and height.
    if (GST_VIDEO_INFO_WIDTH (srcpad->info) > maxwidth)
      maxwidth = GST_VIDEO_INFO_WIDTH (srcpad->info);

    if (GST_VIDEO_INFO_HEIGHT (srcpad->info) > maxheight)
      maxheight = GST_VIDEO_INFO_HEIGHT (srcpad->info);
  }

  GST_VIDEO_SPLIT_UNLOCK (vsplit);

#ifdef USE_GLES_CONVERTER
  opts = gst_structure_new ("options",
      GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_WIDTH, G_TYPE_UINT, maxwidth,
      GST_GLES_VIDEO_CONVERTER_OPT_OUTPUT_HEIGHT, G_TYPE_UINT, maxheight,
      NULL);

  // Configure the processing pipeline of the GLES converter.
  gst_gles_video_converter_set_output_opts (vsplit->glesconvert, opts);
#endif // USE_GLES_CONVERTER

  return TRUE;
}

static gboolean
gst_video_split_sinkpad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GST_LOG_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_video_split_sinkpad_getcaps (pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_video_split_sinkpad_acceptcaps (pad, caps);

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_video_split_sinkpad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (parent);
  gboolean success = FALSE;

  GST_LOG_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      success = gst_video_split_sinkpad_setcaps (vsplit, pad, caps);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstVideoSplitSinkPad *sinkpad = GST_VIDEO_SPLIT_SINKPAD (pad);
      GList *list = NULL;
      GstSegment segment;

      gst_event_copy_segment (event, &segment);
      gst_event_unref (event);

      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format == GST_FORMAT_BYTES) {
        gst_segment_init (&(sinkpad)->segment, GST_FORMAT_TIME);
        sinkpad->segment.start = segment.start;

        GST_DEBUG_OBJECT (pad, "Converted incoming segment to TIME: %"
            GST_SEGMENT_FORMAT, &(sinkpad)->segment);
      } else if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &(sinkpad)->segment);
        gst_segment_copy_into (&segment, &(sinkpad)->segment);
      } else {
        GST_ERROR_OBJECT (pad, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        return FALSE;
      }

      GST_OBJECT_LOCK (vsplit);

      for (list = GST_ELEMENT (vsplit)->srcpads; list; list = list->next) {
        GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (list->data);
        gst_segment_copy_into (&(sinkpad)->segment, &(srcpad)->segment);
      }

      GST_OBJECT_UNLOCK (vsplit);

      event = gst_event_new_segment (&(sinkpad)->segment);

      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_STREAM_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      return success;
    case GST_EVENT_FLUSH_STOP:
    {
      GstVideoSplitSinkPad *sinkpad = GST_VIDEO_SPLIT_SINKPAD (pad);
      GList *list = NULL;

      GST_OBJECT_LOCK (vsplit);

      for (list = GST_ELEMENT (vsplit)->srcpads; list; list = list->next) {
        GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (list->data);
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);
      }

      GST_OBJECT_UNLOCK (vsplit);

      gst_segment_init (&(sinkpad)->segment, GST_FORMAT_UNDEFINED);

      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      return success;
    }
    case GST_EVENT_EOS:
      success = gst_element_foreach_src_pad (GST_ELEMENT (vsplit),
          gst_video_split_srcpad_push_event, event);
      return success;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}


gboolean
gst_video_split_srcpad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (pad);

  GST_LOG_OBJECT (srcpad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_video_split_srcpad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstVideoSplitSrcPad *srcpad = GST_VIDEO_SPLIT_SRCPAD (pad);

  GST_LOG_OBJECT (srcpad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstSegment *segment = &(srcpad)->segment;
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (srcpad, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));
      return TRUE;
    }
    case GST_QUERY_SEGMENT:
    {
      GstSegment *segment = &(srcpad)->segment;
      gint64 start = 0, stop = 0;

      start = gst_segment_to_stream_time (segment, segment->format,
          segment->start);

      stop = (segment->stop == GST_CLOCK_TIME_NONE) ? segment->duration :
          gst_segment_to_stream_time (segment, segment->format, segment->stop);

      gst_query_set_segment (query, segment->rate, segment->format, start, stop);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

gboolean
gst_video_split_srcpad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean success = TRUE;

  GST_INFO_OBJECT (pad, "%s worker task", active ? "Activating" : "Deactivating");

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        // Disable requests queue in flushing state to enable normal work.
        gst_data_queue_set_flushing (GST_VIDEO_SPLIT_SRCPAD (pad)->buffers, FALSE);
        gst_data_queue_flush (GST_VIDEO_SPLIT_SRCPAD (pad)->buffers);

        success = gst_pad_start_task (pad, gst_video_split_srcpad_worker_task,
            pad, NULL);
      } else {
        gst_data_queue_set_flushing (GST_VIDEO_SPLIT_SRCPAD (pad)->buffers, TRUE);
        // TODO wait for all requests.
        success = gst_pad_stop_task (pad);
      }
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to %s worker task!",
        active ? "activate" : "deactivate");
    return FALSE;
  }

  GST_INFO_OBJECT (pad, "Worker task %s", active ? "activated" : "deactivated");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static GstPad*
gst_video_split_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_VIDEO_SPLIT_LOCK (vsplit);

  if (reqname && sscanf (reqname, "src_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= vsplit->nextidx) ? index + 1 : vsplit->nextidx;
  } else {
    index = vsplit->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  name = g_strdup_printf ("src_%u", index);

  pad = g_object_new (GST_TYPE_VIDEO_SPLIT_SRCPAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (vsplit, "Failed to create source pad!");
    return NULL;
  }

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_split_srcpad_query));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_split_srcpad_event));
  gst_pad_set_activatemode_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_split_srcpad_activate_mode));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (vsplit, "Failed to add source pad!");
    gst_object_unref (pad);
    return NULL;
  }

  GST_VIDEO_SPLIT_LOCK (vsplit);

  vsplit->srcpads = g_list_append (vsplit->srcpads, pad);
  vsplit->nextidx = nextindex;

  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  GST_DEBUG_OBJECT (vsplit, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_video_split_release_pad (GstElement * element, GstPad * pad)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (element);

  GST_DEBUG_OBJECT (vsplit, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_VIDEO_SPLIT_LOCK (vsplit);
  vsplit->srcpads = g_list_remove (vsplit->srcpads, pad);
  GST_VIDEO_SPLIT_UNLOCK (vsplit);

  gst_element_remove_pad (element, pad);
}

static void
gst_video_split_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (object);

  switch (prop_id) {
    case PROP_MODE:
      vsplit->mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_split_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, vsplit->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_split_finalize (GObject * object)
{
  GstVideoSplit *vsplit = GST_VIDEO_SPLIT (object);

#ifdef USE_C2D_CONVERTER
  if (vsplit->c2dconvert != NULL)
    gst_c2d_video_converter_free (vsplit->c2dconvert);
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  if (vsplit->glesconvert != NULL)
    gst_gles_video_converter_free (vsplit->glesconvert);
#endif // USE_GLES_CONVERTER

  if (vsplit->srcpads != NULL)
    g_list_free (vsplit->srcpads);

  g_mutex_clear (&(vsplit)->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (vsplit));
}

static void
gst_video_split_class_init (GstVideoSplitClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_video_split_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_video_split_get_property);
  object->finalize     = GST_DEBUG_FUNCPTR (gst_video_split_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_video_split_sink_template, GST_TYPE_VIDEO_SPLIT_SINKPAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_video_split_src_template, GST_TYPE_VIDEO_SPLIT_SRCPAD);

  g_object_class_install_property (object, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "Operational mode",
          GST_TYPE_VIDEO_SPLIT_MODE, DEFAULT_PROP_MODE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (element,
      "Video stream splitter", "Video/Demuxer",
      "Split single video stream into multiple streams", "QTI"
  );

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_video_split_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_video_split_release_pad);

  // Initializes a new vsplit GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_video_split_debug, "qtivsplit", 0,
      "QTI Video Split");
}

static void
gst_video_split_init (GstVideoSplit * vsplit)
{
  GstPadTemplate *template = NULL;

  g_mutex_init (&(vsplit)->lock);

  vsplit->mode = GST_VIDEO_SPLIT_MODE_NORMAL;

  vsplit->nextidx = 0;
  vsplit->srcpads = NULL;

#ifdef USE_C2D_CONVERTER
  vsplit->c2dconvert = gst_c2d_video_converter_new ();
#endif // USE_C2D_CONVERTER

#ifdef USE_GLES_CONVERTER
  vsplit->glesconvert = NULL;
#endif // USE_GLES_CONVERTER

  template = gst_static_pad_template_get (&gst_video_split_sink_template);
  vsplit->sinkpad = g_object_new (GST_TYPE_VIDEO_SPLIT_SINKPAD, "name", "sink",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_chain_function (vsplit->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_split_sinkpad_chain));
  gst_pad_set_query_function (vsplit->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_split_sinkpad_query));
  gst_pad_set_event_function (vsplit->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_split_sinkpad_event));

  gst_element_add_pad (GST_ELEMENT (vsplit), vsplit->sinkpad);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtivsplit", GST_RANK_NONE,
      GST_TYPE_VIDEO_SPLIT);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtivsplit,
    "QTI video stream splitter",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
