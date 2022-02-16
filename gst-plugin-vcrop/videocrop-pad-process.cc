/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "videocrop-pad-process.h"

gboolean
VideoCropPadProcess::AllocateBuffers (GstVideoFormat format)
{
  GstVideoCropVideoPad *vpad = GST_VIDEOCROP_VIDEO_PAD (pad_);
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  gint size = ((vpad->width * vpad->height * 3 / 2));
  const gchar *format_srt = "NV12";
  gint size_aligned = 0;

  if (GST_VIDEO_FORMAT_RGB == format) {
    size = ((vpad->width * vpad->height * 3));
    format_srt = "RGB";
  }

  size_aligned = (size + 4096-1) & ~(4096-1);

  GstCaps *caps = gst_caps_new_simple ("video/x-raw",
        "format", G_TYPE_STRING, format_srt,
        "framerate", GST_TYPE_FRACTION, (gint)vpad->framerate, 1,
        "width", G_TYPE_INT, vpad->width,
        "height", G_TYPE_INT, vpad->height,
        NULL);

  pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
  if (pool == NULL) {
    GST_ERROR ("%s: Failed create buffer image pool", __func__);
    return FALSE;
  }

  config = gst_buffer_pool_get_config (pool);
  if (config == NULL) {
    GST_ERROR ("%s: Failed set config of the pool", __func__);
    gst_object_unref (pool);
    return FALSE;
  }

  gst_buffer_pool_config_set_params (
      config, caps, size_aligned, 3, buffers_count_);

  allocator = gst_fd_allocator_new ();
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR ("%s: Failed to set pool configuration", __func__);
    g_object_unref (pool);
    g_object_unref (allocator);
    return FALSE;
  }

  g_object_unref (allocator);
  if (GST_VIDEO_FORMAT_RGB == format)
    proc_buffers_.rgb_pool = pool;
  else
    proc_buffers_.nv12_pool = pool;

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR ("%s: Failed to activate output video buffer pool", __func__);
    if (GST_VIDEO_FORMAT_RGB == format) {
      g_object_unref (proc_buffers_.rgb_pool);
      proc_buffers_.rgb_pool = NULL;
    } else {
      g_object_unref (proc_buffers_.nv12_pool);
      proc_buffers_.nv12_pool = NULL;
    }
    return FALSE;
  }
  return TRUE;
}

gboolean
VideoCropPadProcess::Init (GstVideoInfo video_info)
{
  GST_DEBUG ("%s: Enter", __func__);
  in_video_info_ = video_info;
  gint input_width = GST_VIDEO_INFO_WIDTH(&in_video_info_);
  gint input_height = GST_VIDEO_INFO_HEIGHT(&in_video_info_);
  GstVideoFormat format = GST_VIDEO_FORMAT_NV12;
  gst_video_info_set_format (
      &in_video_info_, format, input_width, input_height);
  GstVideoCropVideoPad *vpad = GST_VIDEOCROP_VIDEO_PAD (pad_);

  // Init C2D or FastCV
  if (GST_VIDEO_CROP_TYPE_C2D == crop_type_) {
    c2dconvert_ = gst_c2d_video_converter_new ();
    if (!c2dconvert_) {
      GST_ERROR ("%s: Error creating C2D converter", __func__);
    }
  } else if (GST_VIDEO_CROP_TYPE_FASTCV == crop_type_) {
    fcvSetOperationMode(FASTCV_OP_PERFORMANCE);
  }

  // Check if input and output are same and disable scale down
  if (input_width == vpad->width && input_height == vpad->height) {
    GST_DEBUG ("%s: Disable scale down", __func__);
    do_scale_down_ = FALSE;
  }

  // Check if will do color convert
  if (GST_VIDEO_FORMAT_RGB == vpad->format) {
    GST_DEBUG ("%s: Enable color conversion", __func__);
    do_color_convert_ = TRUE;
  }

  if (!AllocateBuffers (GST_VIDEO_FORMAT_NV12)) {
    GST_ERROR ("%s: Error allocationg NV12 buffers", __func__);
    return FALSE;
  }

  if (TRUE == do_color_convert_ && !AllocateBuffers (GST_VIDEO_FORMAT_RGB)) {
    GST_ERROR ("%s: Error allocationg RGB buffers", __func__);
    Deinit ();
    return FALSE;
  }

  if (GST_VIDEO_CROP_TYPE_C2D == crop_type_ && c2dconvert_) {
    GstStructure *inopts = gst_structure_new_empty ("options");
    GValue rects = G_VALUE_INIT, entry = G_VALUE_INIT, value = G_VALUE_INIT;

    g_value_init (&rects, GST_TYPE_ARRAY);
    g_value_init (&entry, GST_TYPE_ARRAY);
    g_value_init (&value, G_TYPE_INT);

    g_value_set_int (&value, 0);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, 0);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, input_width);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, input_height);
    gst_value_array_append_value (&entry, &value);

    gst_value_array_append_value (&rects, &entry);
    g_value_reset (&entry);

    gst_structure_set_value (inopts,
        GST_C2D_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, &rects);
    g_value_reset (&rects);

    g_value_set_int (&value, 0);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, 0);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, vpad->width);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, vpad->height);
    gst_value_array_append_value (&entry, &value);

    gst_value_array_append_value (&rects, &entry);

    gst_structure_set_value (inopts,
        GST_C2D_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, &rects);

    g_value_unset (&value);
    g_value_unset (&entry);
    g_value_unset (&rects);

    gst_c2d_video_converter_set_input_opts (c2dconvert_, 0, inopts);
  }

  crop_.x = 0;
  crop_.y = 0;
  crop_.w = input_width;
  crop_.h = input_height;

  gst_segment_init (&sink_segment_, GST_FORMAT_UNDEFINED);

  GST_DEBUG ("%s: Exit", __func__);
  return TRUE;
}

void
VideoCropPadProcess::Deinit ()
{
  GST_DEBUG ("%s: Enter", __func__);
  if (NULL != proc_buffers_.nv12_pool) {
    gst_buffer_pool_set_active (proc_buffers_.nv12_pool, FALSE);
    gst_object_unref (proc_buffers_.nv12_pool);
    proc_buffers_.nv12_pool = NULL;
  }
  if (NULL != proc_buffers_.rgb_pool) {
    gst_buffer_pool_set_active (proc_buffers_.rgb_pool, FALSE);
    gst_object_unref (proc_buffers_.rgb_pool);
    proc_buffers_.rgb_pool = NULL;
  }

  if (c2dconvert_)
    gst_c2d_video_converter_free (c2dconvert_);
  GST_DEBUG ("%s: Exit", __func__);
}

void
VideoCropPadProcess::SetCrop (GstVideoRectangle * crop)
{
  GstVideoCropVideoPad *vpad = GST_VIDEOCROP_VIDEO_PAD (pad_);
  gint input_width = GST_VIDEO_INFO_WIDTH(&in_video_info_);
  gint input_height = GST_VIDEO_INFO_HEIGHT(&in_video_info_);

  crop_.x = 0;
  crop_.y = 0;
  crop_.w = input_width;
  crop_.h = input_height;

  if (crop) {
    crop_.x = crop->x;
    crop_.y = crop->y;
    crop_.w = crop->w;
    crop_.h = crop->h;
  }

  if (GST_VIDEO_CROP_TYPE_C2D == crop_type_ && c2dconvert_) {
    GstStructure *inopts = gst_structure_new_empty ("options");
    GValue rects = G_VALUE_INIT, entry = G_VALUE_INIT, value = G_VALUE_INIT;

    g_value_init (&rects, GST_TYPE_ARRAY);
    g_value_init (&entry, GST_TYPE_ARRAY);
    g_value_init (&value, G_TYPE_INT);

    g_value_set_int (&value, crop_.x);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, crop_.y);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, crop_.w);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, crop_.h);
    gst_value_array_append_value (&entry, &value);

    gst_value_array_append_value (&rects, &entry);
    g_value_reset (&entry);

    gst_structure_set_value (inopts,
        GST_C2D_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, &rects);
    g_value_reset (&rects);

    g_value_set_int (&value, 0);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, 0);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, vpad->width);
    gst_value_array_append_value (&entry, &value);
    g_value_set_int (&value, vpad->height);
    gst_value_array_append_value (&entry, &value);

    gst_value_array_append_value (&rects, &entry);

    gst_structure_set_value (inopts,
        GST_C2D_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, &rects);

    g_value_unset (&value);
    g_value_unset (&entry);
    g_value_unset (&rects);

    gst_c2d_video_converter_set_input_opts (c2dconvert_, 0, inopts);
  }
}

void
VideoCropPadProcess::FreeQueueItem (GstDataQueueItem * item)
{
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

GstClockTime
VideoCropPadProcess::GetRunningTime (GstPad * pad)
{
  GstElement *element = GST_ELEMENT (gst_pad_get_parent (pad));
  GstClock *clock = gst_element_get_clock (element);
  GstClockTime runningtime = GST_CLOCK_TIME_NONE;

  if (clock && element) {
    runningtime =
        gst_clock_get_time (clock) - gst_element_get_base_time (element);

  }

  if (clock)
    gst_object_unref (clock);
  if (element)
    gst_object_unref (element);

  return runningtime;
}

gboolean
VideoCropPadProcess::PushBufferToQueue (GstVideoCropVideoPad * vpad,
    GstBuffer * buffer)
{
  GstDataQueueItem *item = NULL;
  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) FreeQueueItem;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (vpad->buffers, item)) {
    item->destroy (item);
    return FALSE;
  }

  return TRUE;
}

gboolean
VideoCropPadProcess::Process (gboolean input_is_free, GstBuffer * in_buffer)
{
  gpointer request_id = NULL;
  GstVideoFrame input_video_frame;
  GstVideoFrame nv12_output_video_frame;
  GstVideoFrame rgb_output_video_frame;
  GstVideoInfo out_vinfo;
  GstBuffer *nv12_buff = NULL;
  GstBuffer *rgb_out_buff = NULL;
  GstVideoCropVideoPad *vpad = GST_VIDEOCROP_VIDEO_PAD (pad_);

  // Send the sink segment to the src pad
  if (GST_FORMAT_UNDEFINED == vpad->segment.format &&
      GST_FORMAT_TIME == sink_segment_.format) {
    vpad->segment = sink_segment_;
    gst_pad_push_event (&vpad->parent, gst_event_new_segment (&vpad->segment));
  }

  // Even if it's passthrough run the scale down
  // if the previous process is using the input buffer
  if (FALSE == do_scale_down_ && FALSE == do_color_convert_ &&
      FALSE == input_is_free)
    do_scale_down_ = TRUE;

  if (TRUE == do_scale_down_) {
    if (GST_FLOW_OK != gst_buffer_pool_acquire_buffer (proc_buffers_.nv12_pool,
        &nv12_buff, NULL)) {
      GST_ERROR ("%s: Failed to create output buffer", __func__);
      return FALSE;
    }

    // Copy the flags and timestamps from the input buffer.
    gst_buffer_copy_into (nv12_buff, in_buffer,
        (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS |
            GST_BUFFER_COPY_TIMESTAMPS), 0, -1);
  }

  if (TRUE == do_color_convert_) {
    if (GST_FLOW_OK != gst_buffer_pool_acquire_buffer (proc_buffers_.rgb_pool,
        &rgb_out_buff, NULL)) {
      GST_ERROR ("%s: Failed to create output buffer", __func__);
      return FALSE;
    }

    // Copy the flags and timestamps from the input buffer.
    gst_buffer_copy_into (rgb_out_buff, in_buffer,
      (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS |
          GST_BUFFER_COPY_TIMESTAMPS), 0, -1);
  }

  if (TRUE == do_scale_down_ || TRUE == do_color_convert_) {
    // Map input frame
    if (!gst_video_frame_map (&input_video_frame, &in_video_info_, in_buffer,
        (GstMapFlags)(GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
      GST_ERROR ("%s: ERROR: Failed to map input buffer! - %p",
          __func__, in_buffer);
      return FALSE;
    }
  }

  if (TRUE == do_scale_down_) {
    // Convert to NV12
    GstVideoFormat format = GST_VIDEO_FORMAT_NV12;
    gst_video_info_set_format (
        &out_vinfo, format, vpad->width, vpad->height);
    if (!gst_video_frame_map (&nv12_output_video_frame, &out_vinfo, nv12_buff,
        (GstMapFlags)(GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
      GST_ERROR ("%s: ERROR: Failed to map output NV12 buffer!", __func__);
      return FALSE;
    }

    if (GST_VIDEO_CROP_TYPE_C2D == crop_type_) {
      request_id = gst_c2d_video_converter_submit_request (
          c2dconvert_, &input_video_frame, 1, &nv12_output_video_frame);
      gst_c2d_video_converter_wait_request (c2dconvert_, request_id);
    } else if (GST_VIDEO_CROP_TYPE_FASTCV == crop_type_) {
      gint input_data_stride =
          GST_VIDEO_FRAME_PLANE_STRIDE(&input_video_frame, 0);
      uint8_t *input_data_plane0 =
          (uint8_t *)GST_VIDEO_FRAME_PLANE_DATA (&input_video_frame, 0);
      uint8_t *input_data_plane1 =
          (uint8_t *)GST_VIDEO_FRAME_PLANE_DATA (&input_video_frame, 1);
      uint8_t *output_data_plane0 =
          (uint8_t *)GST_VIDEO_FRAME_PLANE_DATA (&nv12_output_video_frame, 0);
      uint8_t *output_data_plane1 =
          (uint8_t *)GST_VIDEO_FRAME_PLANE_DATA (&nv12_output_video_frame, 1);

      fcvScaleDownMNu8(input_data_plane0 +
                          (crop_.y * input_data_stride + crop_.x),
                       crop_.w,
                       crop_.h,
                       input_data_stride,
                       (uint8_t*)output_data_plane0,
                       vpad->width,
                       vpad->height,
                       0);

      fcvScaleDownMNInterleaveu8(input_data_plane1 +
                           ((crop_.y/2) * input_data_stride + crop_.x),
                       crop_.w/2,
                       crop_.h/2,
                       input_data_stride,
                       (uint8_t*)output_data_plane1,
                       vpad->width/2,
                       vpad->height/2,
                       0);
    }
  }

  if (TRUE == do_color_convert_) {
    // Convert to RGB
    GstVideoFormat format = GST_VIDEO_FORMAT_RGB;
    gst_video_info_set_format (
        &out_vinfo, format, vpad->width, vpad->height);
    if (!gst_video_frame_map (&rgb_output_video_frame, &out_vinfo, rgb_out_buff,
        (GstMapFlags)(GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
      GST_ERROR ("%s: ERROR: Failed to map output RGB buffer!", __func__);
      return FALSE;
    }

    GstVideoFrame *inputFrame = NULL;
    if (TRUE == do_scale_down_) {
      inputFrame = &nv12_output_video_frame;
    } else {
      inputFrame = &input_video_frame;
    }

    if (GST_VIDEO_CROP_TYPE_C2D == crop_type_) {
      // Disable crop for RGB conversion
      GstVideoRectangle crop;
      crop.x = 0;
      crop.y = 0;
      crop.w = vpad->width;
      crop.h = vpad->height;
      SetCrop (&crop);

      request_id = gst_c2d_video_converter_submit_request (
          c2dconvert_, inputFrame, 1, &rgb_output_video_frame);
      gst_c2d_video_converter_wait_request (c2dconvert_, request_id);

      // Reset crop
      SetCrop (NULL);
    } else if (GST_VIDEO_CROP_TYPE_FASTCV == crop_type_) {
      gpointer input_data_plane0 =
          GST_VIDEO_FRAME_PLANE_DATA (inputFrame, 0);
      gpointer input_data_plane1 =
          GST_VIDEO_FRAME_PLANE_DATA (inputFrame, 1);
      gpointer output_data_plane0 =
          GST_VIDEO_FRAME_PLANE_DATA (&rgb_output_video_frame, 0);

      fcvColorYCbCr420PseudoPlanarToRGB888u8((const uint8_t*)input_data_plane0,
                                             (const uint8_t*)input_data_plane1,
                                             vpad->width,
                                             vpad->height,
                                             0,
                                             0,
                                             (uint8_t*)output_data_plane0,
                                             0);
    }
  }

  if (TRUE == do_scale_down_ || TRUE == do_color_convert_)
    gst_video_frame_unmap (&input_video_frame);

  if (TRUE == do_scale_down_)
    gst_video_frame_unmap (&nv12_output_video_frame);

  if (TRUE == do_color_convert_)
    gst_video_frame_unmap (&rgb_output_video_frame);

  // If there is no scale down, use the input buffer for next process
  if (FALSE == do_scale_down_) {
    // In this case we use the input buffer as work buffer
    nv12_buff = in_buffer;
  } else if (input_is_free) {
    // In this case the scale down is enabled and for sure the
    // input buffer should be released
    gst_buffer_unref (in_buffer);
  }
  // Else input buffer is used for this port

  gboolean is_nv12_buffer_free = (TRUE == do_color_convert_);
  // Execute the next pad process
  if (next_process_) {
    gboolean res = next_process_->Process (is_nv12_buffer_free, nv12_buff);
    if (!res) {
      GST_ERROR ("%s: ERROR: Previous Pad process failed!" ,__func__);
      return FALSE;
    }
  } else if (is_nv12_buffer_free) {
    // If no next processing and it's not used for this port output,
    // release the scale buffer
    gst_buffer_unref (nv12_buff);
  }

  // Push buffer to pad
  if (is_nv12_buffer_free)
    PushBufferToQueue (vpad, rgb_out_buff);
  else
    PushBufferToQueue (vpad, nv12_buff);

  return TRUE;
}
