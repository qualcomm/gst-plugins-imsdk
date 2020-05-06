/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qmmf_source_context.h"

#include <gst/allocators/allocators.h>
#include <qmmf-sdk/qmmf_recorder.h>
#include <qmmf-sdk/qmmf_recorder_extra_param_tags.h>
#include <qmmf-sdk/qmmf_recorder_extra_param.h>
#include <camera/VendorTagDescriptor.h>
#include <camera/CameraMetadata.h>

#include "qmmf_source_utils.h"
#include "qmmf_source_image_pad.h"
#include "qmmf_source_video_pad.h"

#define GST_QMMF_CONTEXT_GET_LOCK(obj) (&GST_QMMF_CONTEXT_CAST(obj)->lock)
#define GST_QMMF_CONTEXT_LOCK(obj) \
  g_mutex_lock(GST_QMMF_CONTEXT_GET_LOCK(obj))
#define GST_QMMF_CONTEXT_UNLOCK(obj) \
  g_mutex_unlock(GST_QMMF_CONTEXT_GET_LOCK(obj))

#define GST_CAT_DEFAULT qmmf_context_debug_category()
static GstDebugCategory *
qmmf_context_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qmmfsrc", 0,
        "qmmf-context object");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

struct _GstQmmfContext {
  /// Global mutex lock.
  GMutex            lock;

  /// QMMF Recorder camera device opened by this source.
  guint             camera_id;
  /// QMMF Recorder multimedia session ID.
  guint             session_id;

  /// Camera property to Enable or Disable LDC.
  gboolean          ldc;
  /// Camera property to Enable or Disable EIS.
  gboolean          eis;
  /// Camera property to Enable or Disable SHDR.
  gboolean          shdr;
  /// Camera frame effect property.
  guchar            effect;
  /// Camera scene optimization property.
  guchar            scene;
  /// Camera antibanding mode property.
  guchar            antibanding;
  /// Camera Auto Exposure compensation property.
  gint              aecompensation;
  /// Camera Exposure metering mode property.
  gint              expmetering;
  /// Camera Auto Exposure mode property.
  guchar            aemode;
  /// Camera Auto Exposure Compensation lock property.
  gboolean          aelock;
  /// Camera Manual Exposure time property.
  gint64            exptime;
  /// Camera Exposure table property.
  GstStructure      *exptable;
  /// Camera Auto White Balance mode property.
  guchar            awbmode;
  /// Camera Auto White Balance lock property.
  gboolean          awblock;
  /// Camera Auto Focus mode property.
  guchar            afmode;
  /// Camera IR mode property.
  gint              irmode;
  /// Camera ISO exposure mode property.
  gint              isomode;
  /// Camera Noise Reduction mode property.
  guchar            nrmode;
  /// Camera Zoom region property.
  GstVideoRectangle zoom;
  /// Camera Defog table property.
  GstStructure      *defogtable;
  /// Video and image pads timestamp base.
  GstClockTime      tsbase;
  /// Camera Slave mode.
  gboolean          slave;
  /// To check if Camera is opened or not
  gboolean          opened;
  /// ADRC property
  gboolean          adrc;
  /// Local tone Mapping
  GstStructure      *ltmdata;
  /// Noise Reduction Tuning
  GstStructure      *nrtuning;
};

/// Global QMMF Recorder instance.
static ::qmmf::recorder::Recorder *recorder = NULL;

/// Mutex and refcount for the QMMF recorder instance.
G_LOCK_DEFINE_STATIC (recorder);
static grefcount refcount = 0;

static G_DEFINE_QUARK(QmmfBufferQDataQuark, qmmf_buffer_qdata);


static gboolean
update_structure (GQuark id, const GValue * value, gpointer data)
{
  GstStructure *structure = GST_STRUCTURE (data);
  gst_structure_id_set_value (structure, id, value);
  return TRUE;
}

static GstClockTime
qmmfsrc_running_time (GstPad * pad)
{
  GstElement *element = GST_ELEMENT (gst_pad_get_parent (pad));
  GstClock *clock = gst_element_get_clock (element);
  GstClockTime runningtime = GST_CLOCK_TIME_NONE;

  runningtime =
      gst_clock_get_time (clock) - gst_element_get_base_time (element);

  gst_object_unref (clock);
  gst_object_unref (element);

  return runningtime;
}

static guint
get_vendor_tag_by_name (const gchar * section, const gchar * name)
{
  ::android::sp<::android::VendorTagDescriptor> vtags;
  ::android::status_t status = 0;
  guint tag_id = 0;

  vtags = ::android::VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (vtags.get() == NULL) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    return 0;
  }

  status = vtags->lookupTag(::android::String8(name),
      ::android::String8(section), &tag_id);
  if (status != 0) {
    GST_WARNING ("Unable to locate tag for '%s', section '%s'!", name, section);
    return 0;
  }

  return tag_id;
}

static void
set_vendor_tags (GstStructure * structure, ::android::CameraMetadata * meta)
{
  gint idx = 0, tag_id = 0;
  const gchar *name = NULL, *section = NULL;
  const GValue *value = NULL;

  for (idx = 0; idx < gst_structure_n_fields (structure); ++idx) {
    section = gst_structure_get_name (structure);
    name = gst_structure_nth_field_name (structure, idx);

    if ((tag_id = get_vendor_tag_by_name (section, name)) == 0)
      continue;

    value = gst_structure_get_value (structure, name);

    if (G_VALUE_HOLDS (value, G_TYPE_BOOLEAN)) {
      guchar uvalue = g_value_get_boolean (value);
      meta->update(tag_id, &uvalue, 1);
    } else if (G_VALUE_HOLDS (value, G_TYPE_UCHAR)) {
      guchar uvalue = g_value_get_uchar (value);
      meta->update(tag_id, &uvalue, 1);
    } else if (G_VALUE_HOLDS (value, G_TYPE_INT)) {
      gint ivalue = g_value_get_int (value);
      meta->update(tag_id, &ivalue, 1);
    } else if (G_VALUE_HOLDS (value, G_TYPE_DOUBLE)) {
      gfloat fvalue = g_value_get_double (value);
      meta->update(tag_id, &fvalue, 1);
    } else if (G_VALUE_HOLDS (value, GST_TYPE_INT_RANGE)) {
      gint range[2];
      range[0] = gst_value_get_int_range_min (value);
      range[1] = gst_value_get_int_range_max (value);
      meta->update(tag_id, range, 2);
    } else if (G_VALUE_HOLDS (value, GST_TYPE_DOUBLE_RANGE)) {
      gfloat range[2];
      range[0] = gst_value_get_double_range_min (value);
      range[1] = gst_value_get_double_range_max (value);
      meta->update(tag_id, range, 2);
    } else if (G_VALUE_HOLDS (value, GST_TYPE_ARRAY)) {
      guint num = 0, n_bytes = 0;
      gpointer data = NULL;

      // Due to discrepancy in CamX vendor tags with the camera_metadata
      // the count and type fields are not actually describing the contents.
      // Adding this workaround until it is fixed.
      if (g_strcmp0 (section, "org.codeaurora.qcamera3.exposuretable") == 0) {
        if (g_strcmp0 (name, "gainKneeEntries") == 0) {
          n_bytes = gst_value_array_get_size (value) * sizeof(gfloat);
          data = g_malloc0 (n_bytes);

          for (num = 0; num < gst_value_array_get_size (value); num++) {
            const GValue *val = gst_value_array_get_value (value, num);
            ((gfloat*)data)[num] = g_value_get_double (val);
          }

          meta->update(tag_id, (gfloat*)data, n_bytes / sizeof(gfloat));
          g_free (data);
        } else if (g_strcmp0 (name, "expTimeKneeEntries") == 0) {
          n_bytes = gst_value_array_get_size (value) * sizeof(gint64);
          data = g_malloc0 (n_bytes);

          for (num = 0; num < gst_value_array_get_size (value); num++) {
            const GValue *val = gst_value_array_get_value (value, num);
            ((gint64*)data)[num] = g_value_get_int (val);
          }

          meta->update(tag_id, (gint64*)data, n_bytes / sizeof(gint64));
          g_free (data);
        } else if (g_strcmp0 (name, "incrementPriorityKneeEntries") == 0) {
          n_bytes = gst_value_array_get_size (value) * sizeof(gint);
          data = g_malloc0 (n_bytes);

          for (num = 0; num < gst_value_array_get_size (value); num++) {
            const GValue *val = gst_value_array_get_value (value, num);
            ((gint*)data)[num] = g_value_get_int (val);
          }

          meta->update(tag_id, (gint*)data, n_bytes / sizeof(gint));
          g_free (data);
        } else if (g_strcmp0 (name, "expIndexKneeEntries") == 0) {
          n_bytes = gst_value_array_get_size (value) * sizeof(gfloat);
          data = g_malloc0 (n_bytes);

          for (num = 0; num < gst_value_array_get_size (value); num++) {
            const GValue *val = gst_value_array_get_value (value, num);
            ((gfloat*)data)[num] = g_value_get_double (val);
          }

          meta->update(tag_id, (gfloat*)data, n_bytes / sizeof(gfloat));
          g_free (data);
        }
      } else if (g_strcmp0 (section, "org.quic.camera.defog") == 0) {
        n_bytes = gst_value_array_get_size (value) * 4;
        data = g_malloc0 (n_bytes);

        for (num = 0; num < gst_value_array_get_size (value); num += 3) {
          const GValue *val = gst_value_array_get_value (value, num);
          ((gfloat*)data)[num] = g_value_get_double (val);

          val = gst_value_array_get_value (value, num + 1);
          ((gfloat*)data)[num + 1] = g_value_get_double (val);

          val = gst_value_array_get_value (value, num + 2);
          ((gint*)data)[num + 2] = g_value_get_int (val);
        }

        meta->update(tag_id, (guchar*)data, n_bytes);
        g_free (data);
      }

    }
  }
}

static void
get_vendor_tags (const gchar * section, const gchar * names[], guint n_names,
    GstStructure * structure, ::android::CameraMetadata * meta)
{
  guint idx = 0, num = 0, tag_id = 0;
  const gchar *name = NULL;
  GValue value = G_VALUE_INIT;

  for (idx = 0; idx < n_names; ++idx) {
    name = names[idx];

    if ((tag_id = get_vendor_tag_by_name (section, name)) == 0)
      continue;

    camera_metadata_entry e = meta->find(tag_id);

    if (e.count == 0) {
      GST_WARNING ("No entries in the retrieved tag with name '%s', "
          "section '%s'", name, section);
      continue;
    }

    if (e.count == 2 && (e.type == TYPE_FLOAT || e.type == TYPE_DOUBLE)) {
      g_value_init (&value, GST_TYPE_DOUBLE_RANGE);
      gst_value_set_double_range (&value, e.data.f[0], e.data.f[1]);
    } else if (e.count == 2 && e.type == TYPE_INT32) {
      g_value_init (&value, GST_TYPE_INT_RANGE);
      gst_value_set_double_range (&value, e.data.i32[0], e.data.i32[1]);
    } else if (e.count > 2) {
      g_value_init (&value, GST_TYPE_ARRAY);

      // Due to discrepancy in CamX vendor tags with the camera_metadata
      // the count and type fields are not actually describing the contents.
      // Adding this workaround until it is fixed.
      if (g_strcmp0 (section, "org.quic.camera.defog") == 0) {
        GValue val = G_VALUE_INIT;

        for (num = 0; num < (e.count / 4); num += 3) {
          g_value_init (&val, G_TYPE_DOUBLE);

          g_value_set_double (&val, (gdouble)e.data.f[num]);
          gst_value_array_append_value (&value, &val);
          g_value_unset (&val);

          g_value_init (&val, G_TYPE_DOUBLE);

          g_value_set_double (&val, (gdouble)e.data.f[num + 1]);
          gst_value_array_append_value (&value, &val);
          g_value_unset (&val);

          g_value_init (&val, G_TYPE_DOUBLE);

          g_value_set_double (&val, e.data.i32[num + 2]);
          gst_value_array_append_value (&value, &val);
          g_value_unset (&val);
        }
      } else {
        for (num = 0; num < e.count; ++num) {
          GValue val = G_VALUE_INIT;

          if (e.type == TYPE_INT32) {
            g_value_init (&val, G_TYPE_INT);
            g_value_set_int (&val, e.data.i32[num]);
          } else if (e.type == TYPE_INT64) {
            g_value_init (&val, G_TYPE_INT64);
            g_value_set_int64 (&val, e.data.i64[num]);
          } else if (e.type == TYPE_BYTE) {
            g_value_init (&val, G_TYPE_UCHAR);
            g_value_set_uchar (&val, e.data.u8[num]);
          } else if (e.type == TYPE_FLOAT || e.type == TYPE_DOUBLE) {
            g_value_init (&val, G_TYPE_DOUBLE);
            g_value_set_double (&val, (gdouble)e.data.f[num]);
          }

          gst_value_array_append_value (&value, &val);
        }
      }
    } else if (e.type == TYPE_FLOAT || e.type == TYPE_DOUBLE) {
      g_value_init (&value, G_TYPE_DOUBLE);
      g_value_set_double (&value, (gdouble)e.data.f[0]);
    } else if (e.type == TYPE_INT32) {
      g_value_init (&value, G_TYPE_INT);
      g_value_set_int (&value, e.data.i32[0]);
    } else if (e.type == TYPE_BYTE) {
      g_value_init (&value, G_TYPE_BOOLEAN);
      g_value_set_boolean (&value, e.data.u8[0]);
    }

    gst_structure_set_value (structure, name, &value);
    g_value_unset (&value);
  }
}

static gboolean
initialize_camera_param (GstQmmfContext * context)
{
  ::android::CameraMetadata meta;
  gint tag_id = 0;
  guchar numvalue = 0;
  gint status = 0;

  G_LOCK(recorder);

  status = recorder->GetCameraParam(context->camera_id, meta);

  G_UNLOCK(recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder GetCameraParam Failed!");

  numvalue = gst_qmmfsrc_effect_mode_android_value(context->effect);
  meta.update(ANDROID_CONTROL_EFFECT_MODE, &numvalue, 1);

  numvalue = gst_qmmfsrc_scene_mode_android_value(context->scene);
  meta.update(ANDROID_CONTROL_SCENE_MODE, &numvalue, 1);

  numvalue = gst_qmmfsrc_antibanding_android_value(context->antibanding);
  meta.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &numvalue, 1);

  meta.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
              &(context)->aecompensation, 1);

  numvalue = gst_qmmfsrc_ae_mode_android_value(context->aemode);
  meta.update(ANDROID_CONTROL_AE_MODE, &numvalue, 1);

  numvalue = context->aelock;
  meta.update(ANDROID_CONTROL_AE_LOCK, &numvalue, 1);

  meta.update(ANDROID_SENSOR_EXPOSURE_TIME, &(context)->exptime, 1);

  numvalue = gst_qmmfsrc_awb_mode_android_value(context->awbmode);
  meta.update(ANDROID_CONTROL_AWB_MODE, &numvalue, 1);

  numvalue = context->awblock;
  meta.update(ANDROID_CONTROL_AWB_LOCK, &numvalue, 1);

  numvalue = gst_qmmfsrc_af_mode_android_value(context->afmode);
  meta.update(ANDROID_CONTROL_AF_MODE, &numvalue, 1);

  numvalue = gst_qmmfsrc_noise_reduction_android_value(context->nrmode);
  meta.update(ANDROID_NOISE_REDUCTION_MODE, &numvalue, 1);

  numvalue = context->adrc;
  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.adrc", "disable");
  if (tag_id != 0)
    meta.update(tag_id, &numvalue, 1);

  if (context->zoom.w > 0 && context->zoom.h > 0) {
    gint32 crop[] = { context->zoom.x, context->zoom.y, context->zoom.w,
        context->zoom.h };
    meta.update(ANDROID_SCALER_CROP_REGION, crop, 4);
  }

  tag_id = get_vendor_tag_by_name("org.codeaurora.qcamera3.ir_led", "mode");
  if (tag_id != 0)
    meta.update(tag_id, &(context)->irmode, 1);

  tag_id = get_vendor_tag_by_name("org.codeaurora.qcamera3.iso_exp_priority",
                                  "select_priority");
  if (tag_id != 0)
    meta.update(tag_id, &(context)->isomode, 1);

  tag_id = get_vendor_tag_by_name("org.codeaurora.qcamera3.exposure_metering",
                                  "exposure_metering_mode");
  if (tag_id != 0)
    meta.update(tag_id, &(context)->expmetering, 1);

  set_vendor_tags(context->defogtable, &meta);
  set_vendor_tags(context->exptable, &meta);
  set_vendor_tags(context->ltmdata, &meta);
  set_vendor_tags(context->nrtuning, &meta);

  if (!context->slave) {
    G_LOCK(recorder);

    status = recorder->SetCameraParam(context->camera_id, meta);

    G_UNLOCK(recorder);

    QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
        "QMMF Recorder SetCameraParam Failed!");
  }

  return TRUE;
}

void
qmmfsrc_free_queue_item (GstDataQueueItem * item)
{
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

void
qmmfsrc_gst_buffer_release (GstStructure * structure)
{
  guint value, track_id, session_id, camera_id;
  std::vector<::qmmf::BufferDescriptor> buffers;
  ::qmmf::BufferDescriptor buffer;

  GST_TRACE ("%s", gst_structure_to_string (structure));

  gst_structure_get_uint (structure, "camera", &camera_id);
  gst_structure_get_uint (structure, "session", &session_id);

  gst_structure_get_uint (structure, "data", &value);
  buffer.data = GUINT_TO_POINTER (value);

  gst_structure_get_int (structure, "fd", &buffer.fd);
  gst_structure_get_uint (structure, "bufid", &buffer.buf_id);
  gst_structure_get_uint (structure, "size", &buffer.size);
  gst_structure_get_uint (structure, "capacity", &buffer.capacity);
  gst_structure_get_uint (structure, "offset", &buffer.offset);
  gst_structure_get_uint64 (structure, "timestamp", &buffer.timestamp);
  gst_structure_get_uint (structure, "flag", &buffer.flag);

  buffers.push_back (buffer);

  if (gst_structure_has_field (structure, "track")) {
    gst_structure_get_uint (structure, "track", &track_id);
    recorder->ReturnTrackBuffer (session_id, track_id, buffers);
  } else {
    recorder->ReturnImageCaptureBuffer (camera_id, buffer);
  }

  gst_structure_free (structure);
}

GstBuffer *
qmmfsrc_gst_buffer_new_wrapped (GstQmmfContext * context, GstPad * pad,
                                const ::qmmf::BufferDescriptor * buffer)
{
  GstAllocator *allocator = NULL;
  GstMemory *gstmemory = NULL;
  GstBuffer *gstbuffer = NULL;
  GstStructure *structure = NULL;

  // Create a GstBuffer.
  gstbuffer = gst_buffer_new ();
  g_return_val_if_fail (gstbuffer != NULL, NULL);

  // Create a FD backed allocator.
  allocator = gst_fd_allocator_new ();
  QMMFSRC_RETURN_VAL_IF_FAIL_WITH_CLEAN (NULL, allocator != NULL,
      gst_buffer_unref (gstbuffer), NULL, "Failed to create FD allocator!");

  // Wrap our buffer memory block in FD backed memory.
  gstmemory = gst_fd_allocator_alloc (
      allocator, buffer->fd, buffer->capacity,
      GST_FD_MEMORY_FLAG_DONT_CLOSE
  );
  QMMFSRC_RETURN_VAL_IF_FAIL_WITH_CLEAN (NULL, gstmemory != NULL,
      gst_buffer_unref (gstbuffer); gst_object_unref (allocator),
      NULL, "Failed to allocate FD memory block!");

  // Set the actual size filled with data.
  gst_memory_resize (gstmemory, buffer->offset, buffer->size);

  // Append the FD backed memory to the newly created GstBuffer.
  gst_buffer_append_memory (gstbuffer, gstmemory);

  // Unreference the allocator so that it is owned only by the gstmemory.
  gst_object_unref (allocator);

  // GSreamer structure for later recreating the QMMF buffer to be returned.
  structure = gst_structure_new_empty ("QMMF_BUFFER");
  QMMFSRC_RETURN_VAL_IF_FAIL_WITH_CLEAN (NULL, structure != NULL,
      gst_buffer_unref (gstbuffer), NULL, "Failed to create buffer structure!");

  gst_structure_set (structure,
      "camera", G_TYPE_UINT, context->camera_id,
      "session", G_TYPE_UINT, context->session_id,
      NULL
  );

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad))
    gst_structure_set (structure, "track", G_TYPE_UINT,
        GST_QMMFSRC_VIDEO_PAD (pad)->id, NULL);

  gst_structure_set (structure,
      "data", G_TYPE_UINT, GPOINTER_TO_UINT (buffer->data),
      "fd", G_TYPE_INT, buffer->fd,
      "bufid", G_TYPE_UINT, buffer->buf_id,
      "size", G_TYPE_UINT, buffer->size,
      "capacity", G_TYPE_UINT, buffer->capacity,
      "offset", G_TYPE_UINT, buffer->offset,
      "timestamp", G_TYPE_UINT64, buffer->timestamp,
      "flag", G_TYPE_UINT, buffer->flag,
      NULL
  );

  // Set a notification function to signal when the buffer is no longer used.
  gst_mini_object_set_qdata (
      GST_MINI_OBJECT (gstbuffer), qmmf_buffer_qdata_quark (),
      structure, (GDestroyNotify) qmmfsrc_gst_buffer_release
  );

  GST_TRACE ("%s", gst_structure_to_string (structure));
  return gstbuffer;
}

void
video_event_callback (uint32_t track_id, ::qmmf::recorder::EventType type,
                      void * data, size_t size)
{
  GST_WARNING ("Not Implemented!");
}

void video_data_callback (GstQmmfContext * context, GstPad * pad,
                          std::vector<::qmmf::BufferDescriptor> buffers,
                          std::vector<::qmmf::recorder::MetaData> metabufs)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);

  guint idx = 0, numplanes = 0;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };
  gint  stride[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };

  GstBuffer *gstbuffer = NULL;
  GstBufferFlags gstflags;

  GstDataQueueItem *item = NULL;

  for (idx = 0; idx < buffers.size(); ++idx) {
    ::qmmf::BufferDescriptor& buffer = buffers[idx];
    ::qmmf::recorder::MetaData& meta = metabufs[idx];

    gstbuffer = qmmfsrc_gst_buffer_new_wrapped (context, pad, &buffer);
    QMMFSRC_RETURN_IF_FAIL_WITH_CLEAN (NULL, gstbuffer != NULL,
        recorder->ReturnTrackBuffer (context->session_id, vpad->id, buffers),
        "Failed to create GST buffer!");

    gstflags = (buffer.flag & (guint)::qmmf::BufferFlags::kFlagCodecConfig) ?
        GST_BUFFER_FLAG_HEADER : GST_BUFFER_FLAG_LIVE;
    GST_BUFFER_FLAG_SET (gstbuffer, gstflags);

    if (meta.meta_flag &
        static_cast<uint32_t>(::qmmf::recorder::MetaParamType::kCamBufMetaData)) {
      for (size_t i = 0; i < meta.cam_buffer_meta_data.num_planes; ++i) {
        stride[i] = meta.cam_buffer_meta_data.plane_info[i].stride;
        offset[i] = meta.cam_buffer_meta_data.plane_info[i].offset;
        numplanes++;
      }
    }

    // Set GStreamer buffer video metadata.
    gst_buffer_add_video_meta_full (
        gstbuffer, GST_VIDEO_FRAME_FLAG_NONE,
        vpad->format, vpad->width, vpad->height,
        numplanes, offset, stride
    );

    GST_QMMF_CONTEXT_LOCK (context);
    // Initialize the timestamp base value for buffer synchronization.
    context->tsbase = (GST_CLOCK_TIME_NONE == context->tsbase) ?
        buffer.timestamp - qmmfsrc_running_time (pad) : context->tsbase;

    if (GST_FORMAT_UNDEFINED == vpad->segment.format) {
      gst_segment_init (&(vpad)->segment, GST_FORMAT_TIME);
      gst_pad_push_event (pad, gst_event_new_segment (&(vpad)->segment));
    }

    GST_BUFFER_PTS (gstbuffer) = buffer.timestamp - context->tsbase;
    GST_BUFFER_DTS (gstbuffer) = GST_CLOCK_TIME_NONE;

    vpad->segment.position = GST_BUFFER_PTS (gstbuffer);
    GST_QMMF_CONTEXT_UNLOCK (context);

    GST_QMMFSRC_VIDEO_PAD_LOCK (vpad);
    GST_BUFFER_DURATION (gstbuffer) = vpad->duration;
    GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);

    item = g_slice_new0 (GstDataQueueItem);
    item->object = GST_MINI_OBJECT (gstbuffer);
    item->size = gst_buffer_get_size (gstbuffer);
    item->duration = GST_BUFFER_DURATION (gstbuffer);
    item->visible = TRUE;
    item->destroy = (GDestroyNotify) qmmfsrc_free_queue_item;

    // Push the buffer into the queue or free it on failure.
    if (!gst_data_queue_push (vpad->buffers, item))
      item->destroy (item);
  }
}

void image_data_callback (GstQmmfContext * context, GstPad * pad,
                          ::qmmf::BufferDescriptor buffer,
                          ::qmmf::recorder::MetaData meta)
{
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);

  GstBuffer *gstbuffer = NULL;
  GstBufferFlags gstflags;

  GstDataQueueItem *item = NULL;

  gstbuffer = qmmfsrc_gst_buffer_new_wrapped (context, pad, &buffer);
  QMMFSRC_RETURN_IF_FAIL_WITH_CLEAN (NULL, gstbuffer != NULL,
      recorder->ReturnImageCaptureBuffer (context->camera_id, buffer);,
      "Failed to create GST buffer!");

  gstflags = (buffer.flag & (guint)::qmmf::BufferFlags::kFlagCodecConfig) ?
      GST_BUFFER_FLAG_HEADER : GST_BUFFER_FLAG_LIVE;
  GST_BUFFER_FLAG_SET (gstbuffer, gstflags);

  GST_QMMF_CONTEXT_LOCK (context);
  // Initialize the timestamp base value for buffer synchronization.
  context->tsbase = (GST_CLOCK_TIME_NONE == context->tsbase) ?
      buffer.timestamp - qmmfsrc_running_time (pad) : context->tsbase;

  if (GST_FORMAT_UNDEFINED == ipad->segment.format) {
    gst_segment_init (&(ipad)->segment, GST_FORMAT_TIME);
    gst_pad_push_event (pad, gst_event_new_segment (&(ipad)->segment));

    // This is the 1st capture which is used for stream configuration.
    // TODO Remove this once ConfigImageCapture is actually configuring image.
    gst_buffer_unref (gstbuffer);
    GST_QMMF_CONTEXT_UNLOCK (context);
    return;
  }

  GST_BUFFER_PTS (gstbuffer) = buffer.timestamp - context->tsbase;
  GST_BUFFER_DTS (gstbuffer) = GST_CLOCK_TIME_NONE;

  ipad->segment.position = GST_BUFFER_PTS (gstbuffer);
  GST_QMMF_CONTEXT_UNLOCK (context);

  GST_QMMFSRC_IMAGE_PAD_LOCK (ipad);
  GST_BUFFER_DURATION (gstbuffer) = ipad->duration;
  GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (gstbuffer);
  item->size = gst_buffer_get_size (gstbuffer);
  item->duration = GST_BUFFER_DURATION (gstbuffer);
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) qmmfsrc_free_queue_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (ipad->buffers, item))
    item->destroy (item);
}

GstQmmfContext *
gst_qmmf_context_new ()
{
  GstQmmfContext *context = NULL;

  context = g_slice_new0 (GstQmmfContext);
  g_return_val_if_fail (context != NULL, NULL);

  G_LOCK (recorder);

  if (NULL == recorder) {
    ::qmmf::recorder::RecorderCb cb;

    recorder = new ::qmmf::recorder::Recorder();
    if (NULL == recorder) {
      G_UNLOCK (recorder);

      g_slice_free (GstQmmfContext, context);
      GST_ERROR ("QMMF Recorder creation failed!");
      return NULL;
    }

    cb.event_cb = [] (::qmmf::recorder::EventType type, void *data, size_t size)
        {  };

    if (recorder->Connect (cb) != 0) {
      delete recorder;
      recorder = NULL;
      G_UNLOCK (recorder);

      g_slice_free (GstQmmfContext, context);
      GST_ERROR ("QMMF Recorder Connect failed!");
      return NULL;
    }

    g_ref_count_init (&refcount);
  } else {
    g_ref_count_inc (&refcount);
  }

  G_UNLOCK (recorder);

  context->defogtable = gst_structure_new_empty ("org.quic.camera.defog");
  context->exptable =
      gst_structure_new_empty ("org.codeaurora.qcamera3.exposuretable");
  context->ltmdata =
      gst_structure_new_empty ("org.quic.camera.ltmDynamicContrast");
  context->nrtuning =
      gst_structure_new_empty ("org.quic.camera.anr_tuning");

  GST_INFO ("Created QMMF context: %p", context);
  return context;
}

void
gst_qmmf_context_free (GstQmmfContext * context)
{
  G_LOCK (recorder);

  if (recorder != NULL && g_ref_count_dec (&refcount)) {
    recorder->Disconnect ();

    delete recorder;
    recorder = NULL;
  }

  G_UNLOCK (recorder);

  gst_structure_free (context->defogtable);
  gst_structure_free (context->exptable);
  gst_structure_free (context->ltmdata);
  gst_structure_free (context->nrtuning);

  GST_INFO ("Destroyed QMMF context: %p", context);
  g_slice_free (GstQmmfContext, context);
}

gboolean
gst_qmmf_context_open (GstQmmfContext * context)
{
  gint status = 0;

  GST_TRACE ("Open QMMF context");

  G_LOCK (recorder);

  qmmf::recorder::CameraExtraParam extra_param;

  // Slave Mode
  qmmf::recorder::CameraSlaveMode camera_slave_mode;
  camera_slave_mode.mode = context->slave ?
      qmmf::recorder::SlaveMode::kSlave :
      qmmf::recorder::SlaveMode::kMaster;
  extra_param.Update(::qmmf::recorder::QMMF_CAMERA_SLAVE_MODE,
      camera_slave_mode);

  // LDC
  qmmf::recorder::LDCMode ldc;
  ldc.enable = context->ldc;
  extra_param.Update(qmmf::recorder::QMMF_LDC, ldc);

  // EIS
  qmmf::recorder::EISSetup eis_mode;
  eis_mode.enable = context->eis;
  extra_param.Update(qmmf::recorder::QMMF_EIS, eis_mode);

  // SHDR
  qmmf::recorder::VideoHDRMode vid_hdr_mode;
  vid_hdr_mode.enable = context->shdr;
  extra_param.Update(qmmf::recorder::QMMF_VIDEO_HDR_MODE, vid_hdr_mode);

  status = recorder->StartCamera(context->camera_id, 30, extra_param);

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StartCamera Failed!");

  context->opened = TRUE;

  GST_TRACE ("QMMF context opened");

  return TRUE;
}

gboolean
gst_qmmf_context_close (GstQmmfContext * context)
{
  gint status = 0;

  GST_TRACE ("Closing QMMF context");

  G_LOCK (recorder);

  status = recorder->StopCamera (context->camera_id);

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StopCamera Failed!");

  context->opened = FALSE;

  GST_TRACE ("QMMF context closed");

  return TRUE;
}

gboolean
gst_qmmf_context_create_session (GstQmmfContext * context)
{
  ::qmmf::recorder::SessionCb session_cbs;
  guint session_id = -1;
  gint status = 0;

  GST_TRACE ("Create QMMF context session");

  session_cbs.event_cb =
      [] (::qmmf::recorder::EventType type, void *data, size_t size) { };

  G_LOCK (recorder);

  status = recorder->CreateSession (session_cbs, &session_id);

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder CreateSession Failed!");

  context->session_id = session_id;

  GST_TRACE ("QMMF context session created");

  return TRUE;
}

gboolean
gst_qmmf_context_delete_session (GstQmmfContext * context)
{
  gint status = 0;

  GST_TRACE ("Delete QMMF context session");

  G_LOCK (recorder);

  status = recorder->DeleteSession(context->session_id);

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder DeleteSession Failed!");

  context->session_id = 0;

  GST_TRACE ("QMMF context session deleted");

  return TRUE;
}

gboolean
gst_qmmf_context_create_stream (GstQmmfContext * context, GstPad * pad)
{
  ::qmmf::recorder::TrackCb track_cbs;
  ::qmmf::recorder::VideoExtraParam extraparam;
  gint status = 0;

  GST_TRACE ("Create QMMF context stream");

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
    guint bitrate, ratecontrol, qpvalue;
    const gchar *profile, *level;

    GST_QMMFSRC_VIDEO_PAD_LOCK (vpad);

    ::qmmf::VideoFormat format;
    switch (vpad->codec) {
      case GST_VIDEO_CODEC_NONE:
        format = ::qmmf::VideoFormat::kYUV;
        break;
      case GST_VIDEO_CODEC_H264:
        format = ::qmmf::VideoFormat::kAVC;
        break;
      case GST_VIDEO_CODEC_H265:
        format = ::qmmf::VideoFormat::kHEVC;
        break;
      default:
        GST_ERROR ("Unsupported video format!");
        return FALSE;
    }

    ::qmmf::recorder::VideoTrackCreateParam params (
      context->camera_id, format, vpad->width, vpad->height, vpad->framerate
    );

    if (format == ::qmmf::VideoFormat::kAVC) {
      profile = gst_structure_get_string(vpad->params, "profile");
      if (g_strcmp0(profile, "baseline") == 0) {
        params.codec_param.avc.profile = ::qmmf::AVCProfileType::kBaseline;
      } else if (g_strcmp0(profile, "main") == 0) {
        params.codec_param.avc.profile = ::qmmf::AVCProfileType::kMain;
      } else if (g_strcmp0(profile, "high") == 0) {
        params.codec_param.avc.profile = ::qmmf::AVCProfileType::kHigh;
      }

      level = gst_structure_get_string(vpad->params, "level");
      if (g_strcmp0(level, "1") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel1;
      } else if (g_strcmp0(level, "1.3") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel1_3;
      } else if (g_strcmp0(level, "2") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel2;
      } else if (g_strcmp0(level, "2.1") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel2_1;
      } else if (g_strcmp0(level, "2.2") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel2_2;
      } else if (g_strcmp0(level, "3") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel3;
      } else if (g_strcmp0(level, "3.1") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel3_1;
      } else if (g_strcmp0(level, "3.2") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel3_2;
      } else if (g_strcmp0(level, "4") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel4;
      } else if (g_strcmp0(level, "4.1") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel4_1;
      } else if (g_strcmp0(level, "4.2") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel4_2;
      } else if (g_strcmp0(level, "5") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel5;
      } else if (g_strcmp0(level, "5.1") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel5_1;
      } else if (g_strcmp0(level, "5.2") == 0) {
        params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel5_2;
      }

      gst_structure_get_uint(vpad->params, "bitrate-control", &ratecontrol);
      switch (ratecontrol) {
        case GST_VIDEO_CONTROL_RATE_DISABLE:
          params.codec_param.avc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kDisable;
          break;
        case GST_VIDEO_CONTROL_RATE_VARIABLE:
          params.codec_param.avc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kVariable;
          break;
        case GST_VIDEO_CONTROL_RATE_CONSTANT:
          params.codec_param.avc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kConstant;
          break;
        case GST_VIDEO_CONTROL_RATE_MAXBITRATE:
          params.codec_param.avc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kMaxBitrate;
          break;
        case GST_VIDEO_CONTROL_RATE_VARIABLE_SKIP_FRAMES:
          params.codec_param.avc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kVariableSkipFrames;
          break;
        case GST_VIDEO_CONTROL_RATE_CONSTANT_SKIP_FRAMES:
          params.codec_param.avc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kConstantSkipFrames;
          break;
        case GST_VIDEO_CONTROL_RATE_MAXBITRATE_SKIP_FRAMES:
          params.codec_param.avc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kMaxBitrateSkipFrames;
          break;
      }

      gst_structure_get_uint(vpad->params, "bitrate", &bitrate);
      params.codec_param.avc.bitrate = bitrate;

      gst_structure_get_uint(vpad->params, "quant-i-frames", &qpvalue);
      params.codec_param.avc.qp_params.init_qp.init_IQP = qpvalue;

      gst_structure_get_uint(vpad->params, "quant-p-frames", &qpvalue);
      params.codec_param.avc.qp_params.init_qp.init_PQP = qpvalue;

      gst_structure_get_uint(vpad->params, "quant-b-frames", &qpvalue);
      params.codec_param.avc.qp_params.init_qp.init_BQP = qpvalue;

      gst_structure_get_uint(vpad->params, "min-qp", &qpvalue);
      params.codec_param.avc.qp_params.qp_range.min_QP = qpvalue;

      gst_structure_get_uint(vpad->params, "max-qp", &qpvalue);
      params.codec_param.avc.qp_params.qp_range.max_QP = qpvalue;

      gst_structure_get_uint(vpad->params, "min-qp-i-frames", &qpvalue);
      params.codec_param.avc.qp_params.qp_IBP_range.min_IQP = qpvalue;

      gst_structure_get_uint(vpad->params, "max-qp-i-frames", &qpvalue);
      params.codec_param.avc.qp_params.qp_IBP_range.max_IQP = qpvalue;

      gst_structure_get_uint(vpad->params, "min-qp-p-frames", &qpvalue);
      params.codec_param.avc.qp_params.qp_IBP_range.min_PQP = qpvalue;

      gst_structure_get_uint(vpad->params, "max-qp-p-frames", &qpvalue);
      params.codec_param.avc.qp_params.qp_IBP_range.max_PQP = qpvalue;

      gst_structure_get_uint(vpad->params, "min-qp-b-frames", &qpvalue);
      params.codec_param.avc.qp_params.qp_IBP_range.min_BQP = qpvalue;

      gst_structure_get_uint(vpad->params, "max-qp-b-frames", &qpvalue);
      params.codec_param.avc.qp_params.qp_IBP_range.max_BQP = qpvalue;

      params.codec_param.avc.qp_params.enable_init_qp = true;
      params.codec_param.avc.qp_params.enable_qp_range = true;
      params.codec_param.avc.qp_params.enable_qp_IBP_range = true;

    } else if (format == ::qmmf::VideoFormat::kHEVC) {
      profile = gst_structure_get_string(vpad->params, "profile");
      if (g_strcmp0(profile, "main") == 0) {
        params.codec_param.hevc.profile = ::qmmf::HEVCProfileType::kMain;
      }

      level = gst_structure_get_string(vpad->params, "level");
      if (g_strcmp0(level, "3") == 0) {
        params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel3;
      } else if (g_strcmp0(level, "4") == 0) {
        params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel4;
      } else if (g_strcmp0(level, "5") == 0) {
        params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel5;
      } else if (g_strcmp0(level, "5.1") == 0) {
        params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel5_1;
      } else if (g_strcmp0(level, "5.2") == 0) {
        params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel5_2;
      }

      gst_structure_get_uint(vpad->params, "bitrate-control", &ratecontrol);
      switch (ratecontrol) {
        case GST_VIDEO_CONTROL_RATE_DISABLE:
          params.codec_param.hevc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kDisable;
          break;
        case GST_VIDEO_CONTROL_RATE_VARIABLE:
          params.codec_param.hevc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kVariable;
          break;
        case GST_VIDEO_CONTROL_RATE_CONSTANT:
          params.codec_param.hevc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kConstant;
          break;
        case GST_VIDEO_CONTROL_RATE_MAXBITRATE:
          params.codec_param.hevc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kMaxBitrate;
          break;
        case GST_VIDEO_CONTROL_RATE_VARIABLE_SKIP_FRAMES:
          params.codec_param.hevc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kVariableSkipFrames;
          break;
        case GST_VIDEO_CONTROL_RATE_CONSTANT_SKIP_FRAMES:
          params.codec_param.hevc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kConstantSkipFrames;
          break;
        case GST_VIDEO_CONTROL_RATE_MAXBITRATE_SKIP_FRAMES:
          params.codec_param.hevc.ratecontrol_type =
              ::qmmf::VideoRateControlType::kMaxBitrateSkipFrames;
          break;
      }

      gst_structure_get_uint(vpad->params, "bitrate", &bitrate);
      params.codec_param.hevc.bitrate = bitrate;

      gst_structure_get_uint(vpad->params, "quant-i-frames", &qpvalue);
      params.codec_param.hevc.qp_params.init_qp.init_IQP = qpvalue;

      gst_structure_get_uint(vpad->params, "quant-p-frames", &qpvalue);
      params.codec_param.hevc.qp_params.init_qp.init_PQP = qpvalue;

      gst_structure_get_uint(vpad->params, "quant-b-frames", &qpvalue);
      params.codec_param.hevc.qp_params.init_qp.init_BQP = qpvalue;

      gst_structure_get_uint(vpad->params, "min-qp", &qpvalue);
      params.codec_param.hevc.qp_params.qp_range.min_QP = qpvalue;

      gst_structure_get_uint(vpad->params, "max-qp", &qpvalue);
      params.codec_param.hevc.qp_params.qp_range.max_QP = qpvalue;

      gst_structure_get_uint(vpad->params, "min-qp-i-frames", &qpvalue);
      params.codec_param.hevc.qp_params.qp_IBP_range.min_IQP = qpvalue;

      gst_structure_get_uint(vpad->params, "max-qp-i-frames", &qpvalue);
      params.codec_param.hevc.qp_params.qp_IBP_range.max_IQP = qpvalue;

      gst_structure_get_uint(vpad->params, "min-qp-p-frames", &qpvalue);
      params.codec_param.hevc.qp_params.qp_IBP_range.min_PQP = qpvalue;

      gst_structure_get_uint(vpad->params, "max-qp-p-frames", &qpvalue);
      params.codec_param.hevc.qp_params.qp_IBP_range.max_PQP = qpvalue;

      gst_structure_get_uint(vpad->params, "min-qp-b-frames", &qpvalue);
      params.codec_param.hevc.qp_params.qp_IBP_range.min_BQP = qpvalue;

      gst_structure_get_uint(vpad->params, "max-qp-b-frames", &qpvalue);
      params.codec_param.hevc.qp_params.qp_IBP_range.max_BQP = qpvalue;

      params.codec_param.hevc.qp_params.enable_init_qp = true;
      params.codec_param.hevc.qp_params.enable_qp_range = true;
      params.codec_param.hevc.qp_params.enable_qp_IBP_range = true;
    }

    track_cbs.event_cb =
        [&] (uint32_t track_id, ::qmmf::recorder::EventType type,
            void *data, size_t size)
        { video_event_callback (track_id, type, data, size); };
    track_cbs.data_cb =
        [&, context, pad] (uint32_t track_id,
            std::vector<::qmmf::BufferDescriptor> buffers,
            std::vector<::qmmf::recorder::MetaData> metabufs)
        { video_data_callback (context, pad, buffers, metabufs); };

    vpad->id = vpad->index + VIDEO_TRACK_ID_OFFSET;

    if (vpad->srcidx != -1) {
      ::qmmf::recorder::SourceVideoTrack srctrack;
      srctrack.source_track_id = vpad->srcidx + VIDEO_TRACK_ID_OFFSET;
      extraparam.Update(::qmmf::recorder::QMMF_SOURCE_VIDEO_TRACK_ID, srctrack);
    } else if (context->slave) {
      ::qmmf::recorder::LinkedTrackInSlaveMode linked_track_slave_mode;
      linked_track_slave_mode.enable = true;
      extraparam.Update(::qmmf::recorder::QMMF_USE_LINKED_TRACK_IN_SLAVE_MODE, linked_track_slave_mode);
    }

    G_LOCK (recorder);

    status = recorder->CreateVideoTrack (
        context->session_id, vpad->id, params, extraparam, track_cbs);
    extraparam.Clear();

    G_UNLOCK (recorder);

    GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);

    QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
        "QMMF Recorder CreateVideoTrack Failed!");

  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);

    GST_QMMFSRC_IMAGE_PAD_LOCK (ipad);

    ::qmmf::recorder::ImageConfigParam config;

    if (ipad->codec == GST_IMAGE_CODEC_TYPE_JPEG) {
      ::qmmf::recorder::ImageThumbnail thumbnail;
      ::qmmf::recorder::ImageExif exif;
      guint width, height, quality;

      gst_structure_get_uint (ipad->params, "thumbnail-width", &width);
      gst_structure_get_uint (ipad->params, "thumbnail-height", &height);
      gst_structure_get_uint (ipad->params, "thumbnail-quality", &quality);

      if (width > 0 && height > 0) {
        thumbnail.width = width;
        thumbnail.height = height;
        thumbnail.quality = quality;
        config.Update (::qmmf::recorder::QMMF_IMAGE_THUMBNAIL, thumbnail, 0);
      }

      gst_structure_get_uint (ipad->params, "screennail-width", &width);
      gst_structure_get_uint (ipad->params, "screennail-height", &height);
      gst_structure_get_uint (ipad->params, "screennail-quality", &quality);

      if (width > 0 && height > 0) {
        thumbnail.width = width;
        thumbnail.height = height;
        thumbnail.quality = quality;
        config.Update (::qmmf::recorder::QMMF_IMAGE_THUMBNAIL, thumbnail, 1);
      }

      exif.enable = true;
      config.Update (::qmmf::recorder::QMMF_EXIF, exif, 0);
    }

    G_LOCK (recorder);

    status = recorder->ConfigImageCapture (context->camera_id, config);

    G_UNLOCK (recorder);

    GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);

    QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
        "QMMF Recorder ConfigImageCapture Failed!");
  }

  GST_TRACE ("QMMF context stream created");

  return TRUE;
}

gboolean
gst_qmmf_context_delete_stream (GstQmmfContext * context, GstPad * pad)
{
  gint status = 0;

  GST_TRACE ("Delete QMMF context stream");

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);

    G_LOCK (recorder);

    status = recorder->DeleteVideoTrack (context->session_id, vpad->id);

    G_UNLOCK (recorder);

    QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
        "QMMF Recorder DeleteVideoTrack Failed!");
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    G_LOCK (recorder);

    status = recorder->CancelCaptureImage (context->camera_id);

    G_UNLOCK (recorder);

    QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
        "QMMF Recorder CancelCaptureImage Failed!");
  }

  GST_TRACE ("QMMF context stream deleted");

  return TRUE;
}

gboolean
gst_qmmf_context_start_session (GstQmmfContext * context)
{
  gint status = 0;
  gboolean success = FALSE;

  context->tsbase = GST_CLOCK_TIME_NONE;

  success = initialize_camera_param(context);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, success, FALSE,
      "Failed to initialize camera parameters!");

  GST_TRACE ("Starting QMMF context session");

  G_LOCK (recorder);

  status = recorder->StartSession (context->session_id);

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StartSession Failed!");

  GST_TRACE ("QMMF context session started");

  return TRUE;
}

gboolean
gst_qmmf_context_stop_session (GstQmmfContext * context)
{
  gint status = 0;

  GST_TRACE ("Stopping QMMF context session");

  G_LOCK (recorder);

  status = recorder->StopSession (context->session_id, false);

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StopSession Failed!");

  GST_TRACE ("QMMF context session stopped");

  context->tsbase = GST_CLOCK_TIME_NONE;

  return TRUE;
}

gboolean
gst_qmmf_context_pause_session (GstQmmfContext * context)
{
  gint status = 0;

  GST_TRACE ("Pausing QMMF context session");

  G_LOCK (recorder);

  status = recorder->PauseSession (context->session_id);

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder PauseSession Failed!");

  GST_TRACE ("QMMF context session paused");

  return TRUE;
}

gboolean
gst_qmmf_context_capture_image (GstQmmfContext * context, GstPad * pad)
{
  gint status = 0;
  ::qmmf::recorder::ImageCaptureCb imagecb;
  std::vector<::android::CameraMetadata> metadata;
  ::android::CameraMetadata meta;

  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);

  GST_QMMFSRC_IMAGE_PAD_LOCK (ipad);

  ::qmmf::recorder::ImageParam imgparam;
  imgparam.width = ipad->width;
  imgparam.height = ipad->height;

  if (ipad->codec == GST_IMAGE_CODEC_TYPE_JPEG) {
    imgparam.image_format = ::qmmf::ImageFormat::kJPEG;
    gst_structure_get_uint (ipad->params, "quality", &imgparam.image_quality);
  } else if (ipad->codec == GST_IMAGE_CODEC_TYPE_NONE) {
    switch (ipad->format) {
      case GST_VIDEO_FORMAT_NV12:
        imgparam.image_format = ::qmmf::ImageFormat::kNV12;
        break;
      case GST_VIDEO_FORMAT_NV21:
        imgparam.image_format = ::qmmf::ImageFormat::kNV21;
        break;
      case GST_VIDEO_FORMAT_UNKNOWN: {
        switch (ipad->bayer) {
          case GST_IMAGE_FORMAT_RAW8:
            imgparam.image_format = ::qmmf::ImageFormat::kBayerRDI8BIT;
            break;
          case GST_IMAGE_FORMAT_RAW10:
            imgparam.image_format = ::qmmf::ImageFormat::kBayerRDI10BIT;
            break;
          case GST_IMAGE_FORMAT_RAW12:
            imgparam.image_format = ::qmmf::ImageFormat::kBayerRDI12BIT;
            break;
          case GST_IMAGE_FORMAT_RAW16:
            imgparam.image_format = ::qmmf::ImageFormat::kBayerRDI16BIT;
            break;
          default:
            GST_ERROR ("Unsupported format %d", ipad->bayer);
            GST_QMMFSRC_IMAGE_PAD_UNLOCK(ipad);
            return FALSE;
        }
        break;
      }
      default:
        GST_ERROR ("Unsupported format %d", ipad->format);
        GST_QMMFSRC_IMAGE_PAD_UNLOCK(ipad);
        return FALSE;
    }
  }

  imagecb = [&, context, pad] (uint32_t camera_id, uint32_t imgcount,
      ::qmmf::BufferDescriptor buffer, ::qmmf::recorder::MetaData meta)
      { image_data_callback (context, pad, buffer, meta); };

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);

  G_LOCK (recorder);

  status = recorder->GetDefaultCaptureParam (context->camera_id, meta);

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder GetDefaultCaptureParam Failed!");

  metadata.push_back (meta);

  G_LOCK (recorder);

  status = recorder->CaptureImage (
      context->camera_id, imgparam, 1, metadata, imagecb);

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder CaptureImage Failed!");

  return TRUE;
}

void
gst_qmmf_context_set_camera_param (GstQmmfContext * context, guint param_id,
                                   const GValue * value)
{
  ::android::CameraMetadata meta;

  if (context->opened) {
    G_LOCK(recorder);
    recorder->GetCameraParam(context->camera_id, meta);
    G_UNLOCK(recorder);
  }

  switch (param_id) {
    case PARAM_CAMERA_ID:
      context->camera_id = g_value_get_uint (value);
      break;
    case PARAM_CAMERA_LDC:
      context->ldc = g_value_get_boolean (value);
      break;
    case PARAM_CAMERA_SHDR:
      context->shdr = g_value_get_boolean (value);
      break;
    case PARAM_CAMERA_EIS:
      context->eis = g_value_get_boolean (value);
      break;
    case PARAM_CAMERA_EFFECT_MODE:
    {
      guchar mode;
      context->effect = g_value_get_enum (value);

      mode = gst_qmmfsrc_effect_mode_android_value (context->effect);
      meta.update(ANDROID_CONTROL_EFFECT_MODE, &mode, 1);
      break;
    }
    case PARAM_CAMERA_SCENE_MODE:
    {
      guchar mode;
      context->scene = g_value_get_enum (value);

      mode = gst_qmmfsrc_scene_mode_android_value (context->scene);
      meta.update(ANDROID_CONTROL_SCENE_MODE, &mode, 1);
      break;
    }
    case PARAM_CAMERA_ANTIBANDING_MODE:
    {
      guchar mode;
      context->antibanding = g_value_get_enum (value);

      mode = gst_qmmfsrc_antibanding_android_value (context->antibanding);
      meta.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &mode, 1);
      break;
    }
    case PARAM_CAMERA_AE_COMPENSATION:
    {
      gint compensation;
      context->aecompensation = g_value_get_int (value);

      compensation = context->aecompensation;
      meta.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &compensation, 1);
      break;
    }
    case PARAM_CAMERA_AE_MODE:
    {
      guchar mode;
      context->aemode = g_value_get_enum (value);

      mode = gst_qmmfsrc_ae_mode_android_value (context->aemode);
      meta.update(ANDROID_CONTROL_AE_MODE, &mode, 1);
      break;
    }
    case PARAM_CAMERA_AE_LOCK:
    {
      guchar lock;
      context->aelock = g_value_get_boolean (value);

      lock = context->aelock;
      meta.update(ANDROID_CONTROL_AE_LOCK, &lock, 1);
      break;
    }
    case PARAM_CAMERA_EXPOSURE_TIME:
    {
      gint64 time;
      context->exptime = g_value_get_int64 (value);

      time = context->exptime;
      meta.update(ANDROID_SENSOR_EXPOSURE_TIME, &time, 1);
      break;
    }
    case PARAM_CAMERA_AWB_MODE:
    {
      guchar mode;
      context->awbmode = g_value_get_enum (value);

      mode = gst_qmmfsrc_awb_mode_android_value (context->awbmode);
      meta.update(ANDROID_CONTROL_AWB_MODE, &mode, 1);
      break;
    }
    case PARAM_CAMERA_AWB_LOCK:
    {
      guchar lock;
      context->awblock = g_value_get_boolean (value);

      lock = context->awblock;
      meta.update(ANDROID_CONTROL_AWB_LOCK, &lock, 1);
      break;
    }
    case PARAM_CAMERA_SLAVE:
    {
      context->slave = g_value_get_boolean (value);
      break;
    }
    case PARAM_CAMERA_AF_MODE:
    {
      guchar mode;
      context->afmode = g_value_get_enum (value);

      mode = gst_qmmfsrc_af_mode_android_value (context->afmode);
      meta.update(ANDROID_CONTROL_AF_MODE, &mode, 1);
      break;
    }
    case PARAM_CAMERA_IR_MODE:
    {
      guint tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.ir_led", "mode");

      context->irmode = g_value_get_enum (value);
      meta.update(tag_id, &(context)->irmode, 1);
      break;
    }
    case PARAM_CAMERA_ADRC:
    {
      guchar mode;
      context->adrc = g_value_get_boolean (value);
      mode = context->adrc;

      guint tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.adrc", "disable");
      meta.update(tag_id, &mode, 1);
      break;
    }
    case PARAM_CAMERA_ISO_MODE:
    {
      guint tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.iso_exp_priority", "select_priority");

      context->isomode = g_value_get_enum (value);
      meta.update(tag_id, &(context)->isomode, 1);
      break;
    }
    case PARAM_CAMERA_AE_METERING_MODE:
    {
      guint tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.exposure_metering", "exposure_metering_mode");

      context->expmetering = g_value_get_enum (value);
      meta.update(tag_id, &(context)->expmetering, 1);
      break;
    }
    case PARAM_CAMERA_NOISE_REDUCTION:
    {
      guchar mode;
      context->nrmode = g_value_get_enum (value);

      mode = gst_qmmfsrc_noise_reduction_android_value (context->nrmode);
      meta.update(ANDROID_NOISE_REDUCTION_MODE, &mode, 1);
      break;
    }
    case PARAM_CAMERA_ZOOM:
    {
      gint32 crop[4];
      g_return_if_fail (gst_value_array_get_size (value) == 4);

      context->zoom.x = g_value_get_int (gst_value_array_get_value (value, 0));
      context->zoom.y = g_value_get_int (gst_value_array_get_value (value, 1));
      context->zoom.w = g_value_get_int (gst_value_array_get_value (value, 2));
      context->zoom.h = g_value_get_int (gst_value_array_get_value (value, 3));

      crop[0] = context->zoom.x;
      crop[1] = context->zoom.y;
      crop[2] = context->zoom.w;
      crop[3] = context->zoom.h;
      meta.update(ANDROID_SCALER_CROP_REGION, crop, 4);
      break;
    }
    case PARAM_CAMERA_DEFOG_TABLE:
    {
      const gchar *input = g_value_get_string (value);
      GstStructure *structure = NULL;

      GValue gvalue = G_VALUE_INIT;
      g_value_init (&gvalue, GST_TYPE_STRUCTURE);

      if (g_file_test (input, G_FILE_TEST_IS_REGULAR)) {
        gchar *contents = NULL;
        GError *error = NULL;
        gboolean success = FALSE;

        if (!g_file_get_contents (input, &contents, NULL, &error)) {
          GST_WARNING ("Failed to get Defog Table file contents, error: %s!",
              GST_STR_NULL (error->message));
          g_clear_error (&error);
          break;
        }

        // Remove trailing space and replace new lines with a coma delimeter.
        contents = g_strstrip (contents);
        contents = g_strdelimit (contents, "\n", ',');

        success = gst_value_deserialize (&gvalue, contents);
        g_free (contents);

        if (!success) {
          GST_WARNING ("Failed to deserialize Defog Table file contents!");
          break;
        }
      } else if (!gst_value_deserialize (&gvalue, input)) {
        GST_WARNING ("Failed to deserialize Defog Table input!");
        break;
      }

      structure = GST_STRUCTURE (g_value_dup_boxed (&gvalue));
      g_value_unset (&gvalue);

      gst_structure_foreach (structure, update_structure, context->defogtable);
      gst_structure_free (structure);

      set_vendor_tags (context->defogtable, &meta);
      break;
    }
    case PARAM_CAMERA_EXPOSURE_TABLE:
    {
      const gchar *input = g_value_get_string (value);
      GstStructure *structure = NULL;

      GValue gvalue = G_VALUE_INIT;
      g_value_init (&gvalue, GST_TYPE_STRUCTURE);

      if (g_file_test (input, G_FILE_TEST_IS_REGULAR)) {
        gchar *contents = NULL;
        GError *error = NULL;
        gboolean success = FALSE;

        if (!g_file_get_contents (input, &contents, NULL, &error)) {
          GST_WARNING ("Failed to get Exposure Table file contents, error: %s!",
              GST_STR_NULL (error->message));
          g_clear_error (&error);
          break;
        }

        // Remove trailing space and replace new lines with a coma delimeter.
        contents = g_strstrip (contents);
        contents = g_strdelimit (contents, "\n", ',');

        success = gst_value_deserialize (&gvalue, contents);
        g_free (contents);

        if (!success) {
          GST_WARNING ("Failed to deserialize Exposure Table file contents!");
          break;
        }
      } else if (!gst_value_deserialize (&gvalue, input)) {
        GST_WARNING ("Failed to deserialize Exposure Table input!");
        break;
      }

      structure = GST_STRUCTURE (g_value_dup_boxed (&gvalue));
      g_value_unset (&gvalue);

      gst_structure_foreach (structure, update_structure, context->exptable);
      gst_structure_free (structure);

      set_vendor_tags (context->exptable, &meta);
      break;
    }

    case PARAM_CAMERA_LOCAL_TONE_MAPPING:
    {
      GstStructure *structure = NULL;
      const gchar *input = g_value_get_string(value);
      GValue gvalue = G_VALUE_INIT;
      g_value_init(&gvalue, GST_TYPE_STRUCTURE);

      if (!gst_value_deserialize(&gvalue, input)) {
        GST_ERROR("Failed to deserialize LTM data!");
        break;
      }

      structure = GST_STRUCTURE(g_value_dup_boxed(&gvalue));
      g_value_unset(&gvalue);

      gst_structure_foreach(structure, update_structure, context->ltmdata);
      gst_structure_free(structure);

      set_vendor_tags(context->ltmdata, &meta);
      break;
    }

    case PARAM_CAMERA_NOISE_REDUCTION_TUNING:
    {
      GstStructure *structure = NULL;
      const gchar *input = g_value_get_string(value);
      GValue gvalue = G_VALUE_INIT;
      g_value_init(&gvalue, GST_TYPE_STRUCTURE);

      if (!gst_value_deserialize(&gvalue, input)) {
        GST_ERROR("Failed to deserialize NR tuning data!");
        break;
      }

      structure = GST_STRUCTURE(g_value_dup_boxed(&gvalue));
      g_value_unset(&gvalue);

      gst_structure_foreach(structure, update_structure, context->nrtuning);
      gst_structure_free(structure);

      set_vendor_tags(context->nrtuning, &meta);
      break;
    }
  }

  if (!context->slave) {
    if (context->opened) {
      G_LOCK(recorder);
      recorder->SetCameraParam(context->camera_id, meta);
      G_UNLOCK(recorder);
    }
  }
}

void
gst_qmmf_context_get_camera_param (GstQmmfContext * context, guint param_id,
                                   GValue * value)
{
  ::android::CameraMetadata meta;

  if (context->opened) {
    G_LOCK(recorder);
    recorder->GetCameraParam(context->camera_id, meta);
    G_UNLOCK(recorder);
  }

  switch (param_id) {
    case PARAM_CAMERA_ID:
      g_value_set_uint (value, context->camera_id);
      break;
    case PARAM_CAMERA_LDC:
      g_value_set_boolean (value, context->ldc);
      break;
    case PARAM_CAMERA_SHDR:
      g_value_set_boolean (value, context->shdr);
      break;
    case PARAM_CAMERA_EIS:
      g_value_set_boolean (value, context->eis);
      break;
    case PARAM_CAMERA_EFFECT_MODE:
      g_value_set_enum (value, context->effect);
      break;
    case PARAM_CAMERA_SCENE_MODE:
      g_value_set_enum (value, context->scene);
      break;
    case PARAM_CAMERA_ANTIBANDING_MODE:
      g_value_set_enum (value, context->antibanding);
      break;
    case PARAM_CAMERA_AE_COMPENSATION:
      g_value_set_int (value, context->aecompensation);
      break;
    case PARAM_CAMERA_AE_MODE:
      g_value_set_enum (value, context->aemode);
      break;
    case PARAM_CAMERA_AE_LOCK:
      g_value_set_boolean (value, context->aelock);
      break;
    case PARAM_CAMERA_EXPOSURE_TIME:
      g_value_set_int64 (value, context->exptime);
      break;
    case PARAM_CAMERA_AWB_MODE:
      g_value_set_enum (value, context->awbmode);
      break;
    case PARAM_CAMERA_AWB_LOCK:
      g_value_set_boolean (value, context->awbmode);
      break;
    case PARAM_CAMERA_SLAVE:
      g_value_set_boolean (value, context->slave);
      break;
    case PARAM_CAMERA_AF_MODE:
      g_value_set_enum (value, context->afmode);
      break;
    case PARAM_CAMERA_IR_MODE:
      g_value_set_enum (value, context->irmode);
      break;
    case PARAM_CAMERA_ADRC:
      g_value_set_boolean (value, context->adrc);
      break;
    case PARAM_CAMERA_ISO_MODE:
      g_value_set_enum (value, context->isomode);
      break;
    case PARAM_CAMERA_AE_METERING_MODE:
      g_value_set_enum (value, context->expmetering);
      break;
    case PARAM_CAMERA_NOISE_REDUCTION:
      g_value_set_enum (value, context->nrmode);
      break;
    case PARAM_CAMERA_ZOOM:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, context->zoom.x);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, context->zoom.y);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, context->zoom.w);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, context->zoom.h);
      gst_value_array_append_value (value, &val);
      break;
    }
    case PARAM_CAMERA_DEFOG_TABLE:
    {
      gchar *string = NULL;

      get_vendor_tags ("org.quic.camera.defog",
          gst_camera_defog_table, G_N_ELEMENTS (gst_camera_defog_table),
          context->defogtable, &meta);
      string = gst_structure_to_string (context->defogtable);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    case PARAM_CAMERA_EXPOSURE_TABLE:
    {
      gchar *string = NULL;

      get_vendor_tags ("org.codeaurora.qcamera3.exposuretable",
          gst_camera_exposure_table, G_N_ELEMENTS (gst_camera_exposure_table),
          context->exptable, &meta);
      string = gst_structure_to_string (context->exptable);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    case PARAM_CAMERA_LOCAL_TONE_MAPPING:
    {
      gchar *string = NULL;

      get_vendor_tags ("org.quic.camera.ltmDynamicContrast",
          gst_camera_ltm_data, G_N_ELEMENTS (gst_camera_ltm_data),
          context->ltmdata, &meta);
      string = gst_structure_to_string (context->ltmdata);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    case PARAM_CAMERA_NOISE_REDUCTION_TUNING:
    {
      gchar *string = NULL;

      get_vendor_tags ("org.quic.camera.anr_tuning",
          gst_camera_nr_tuning_data, G_N_ELEMENTS (gst_camera_nr_tuning_data),
          context->nrtuning, &meta);
      string = gst_structure_to_string (context->nrtuning);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
  }
}

void
gst_qmmf_context_update_video_param (GstPad * pad, GParamSpec * pspec,
                                     GstQmmfContext * context)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
  guint track_id = vpad->id, session_id = context->session_id;
  const gchar *pname = g_param_spec_get_name (pspec);
  GValue value = G_VALUE_INIT;
  gint status = 0;

  GST_DEBUG ("Received update for %s property", pname);

  g_value_init (&value, pspec->value_type);
  g_object_get_property (G_OBJECT (vpad), pname, &value);

  G_LOCK (recorder);

  if (g_strcmp0 (pname, "bitrate") == 0) {
    guint bitrate = g_value_get_uint (&value);
    status = recorder->SetVideoTrackParam (session_id, track_id,
        ::qmmf::CodecParamType::kBitRateType, &bitrate, sizeof (bitrate)
    );
  } else if (g_strcmp0 (pname, "framerate") == 0) {
    gfloat fps = g_value_get_double (&value);
    status = recorder->SetVideoTrackParam (session_id, track_id,
        ::qmmf::CodecParamType::kFrameRateType, &fps, sizeof (fps)
    );
  } else {
    GST_WARNING ("Unsupported parameter '%s'!", pname);
    status = -1;
  }

  G_UNLOCK (recorder);

  QMMFSRC_RETURN_IF_FAIL (NULL, status == 0,
      "QMMF Recorder SetVideoTrackParam Failed!");
}
