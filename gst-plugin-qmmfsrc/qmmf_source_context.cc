/*
* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
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
    gsize catdone = (gsize) _gst_debug_category_new ("qtiqmmfsrc", 0,
        "QMMF context");
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

  /// Keep track of internal states by reusing the GstState enum:
  /// @GST_STATE_NULL - Context created.
  /// @GST_STATE_READY - Camera opened, no session has been created yet.
  /// @GST_STATE_PAUSED - Session created but it is not yet active.
  /// @GST_STATE_PLAYING - Session is active/running.
  GstState          state;

  /// List with video and image pads which have active streams.
  GList             *pads;

  /// Video and image pads timestamp base.
  GstClockTime      tsbase;

  /// Camera Slave mode.
  gboolean          slave;
  /// Camera property to Enable or Disable Lens Distortion Correction.
  gboolean          ldc;
  /// Camera property to Enable or Disable Electronic Image Stabilization.
  gboolean          eis;
  /// Camera property to Enable or Disable Super High Dynamic Range.
  gboolean          shdr;
  /// Camera property to Enable or Disable Auto Dynamic Range Compression.
  gboolean          adrc;
  /// Camera frame effect property.
  guchar            effect;
  /// Camera scene optimization property.
  guchar            scene;
  /// Camera antibanding mode property.
  guchar            antibanding;
  /// Camera Exposure mode property.
  guchar            expmode;
  /// Camera Exposure routine lock property.
  gboolean          explock;
  /// Camera Exposure metering mode property.
  gint              expmetering;
  /// Camera Exposure compensation property.
  gint              expcompensation;
  /// Camera Manual Exposure time property.
  gint64            exptime;
  /// Camera Exposure table property.
  GstStructure      *exptable;
  /// Camera White Balance mode property.
  guchar            wbmode;
  /// Camera White Balance lock property.
  gboolean          wblock;
  /// Camera manual White Balance settings property.
  GstStructure      *mwbsettings;
  /// Camera Auto Focus mode property.
  guchar            afmode;
  /// Camera IR mode property.
  gint              irmode;
  /// Camera ISO exposure mode property.
  gint64            isomode;
  /// Camera Noise Reduction mode property.
  guchar            nrmode;
  /// Camera Noise Reduction Tuning
  GstStructure      *nrtuning;
  /// Camera Zoom region property.
  GstVideoRectangle zoom;
  /// Camera Defog table property.
  GstStructure      *defogtable;
  /// Camera Local Tone Mapping property.
  GstStructure      *ltmdata;
  /// Camera Sharpness property.
  gint              sharpness;

  /// QMMF Recorder instance.
  ::qmmf::recorder::Recorder *recorder;
};

static G_DEFINE_QUARK(QmmfBufferQDataQuark, qmmf_buffer_qdata);


static gboolean
update_structure (GQuark id, const GValue * value, gpointer data)
{
  GstStructure *structure = GST_STRUCTURE (data);
  gst_structure_id_set_value (structure, id, value);
  return TRUE;
}

static GstClockTime
running_time (GstPad * pad)
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

static gboolean
validate_bayer_params (GstQmmfContext * context, GstPad * pad)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::android::CameraMetadata meta;
  camera_metadata_entry entry;
  gint width = 0, height = 0, format = 0;
  gboolean supported = FALSE;

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    width = GST_QMMFSRC_VIDEO_PAD (pad)->width;
    height = GST_QMMFSRC_VIDEO_PAD (pad)->height;
    format = GST_QMMFSRC_VIDEO_PAD (pad)->format;
  } else if (GST_IS_QMMFSRC_IMAGE_PAD (pad)) {
    width = GST_QMMFSRC_IMAGE_PAD (pad)->width;
    height = GST_QMMFSRC_IMAGE_PAD (pad)->height;
    format = GST_QMMFSRC_IMAGE_PAD (pad)->format;
  } else {
    GST_WARNING ("Unsupported pad '%s'!", GST_PAD_NAME (pad));
    return FALSE;
  }

    recorder->GetCameraCharacteristics (context->camera_id, meta);

  if (!meta.exists (ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT)) {
    GST_WARNING ("There is no sensor filter information!");
    return FALSE;
  }

  entry = meta.find (ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT);

  switch (entry.data.u8[0]) {
    case ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_BGGR:
      QMMFSRC_RETURN_VAL_IF_FAIL (NULL, format == GST_BAYER_FORMAT_BGGR,
          FALSE, "Invalid bayer matrix format, expected format 'bggr' !");
      break;
    case ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_GRBG:
      QMMFSRC_RETURN_VAL_IF_FAIL (NULL, format == GST_BAYER_FORMAT_GRBG,
          FALSE, "Invalid bayer matrix format, expected format 'grbg' !");
      break;
    case ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_GBRG:
      QMMFSRC_RETURN_VAL_IF_FAIL (NULL, format == GST_BAYER_FORMAT_GBRG,
          FALSE, "Invalid bayer matrix format, expected format 'gbrg' !");
      break;
    case ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_RGGB:
      QMMFSRC_RETURN_VAL_IF_FAIL (NULL, format == GST_BAYER_FORMAT_RGGB,
          FALSE, "Invalid bayer matrix format, expected format 'rggb' !");
      break;
#if defined(CAMERA_METADATA_1_1)
    case ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_MONO:
      QMMFSRC_RETURN_VAL_IF_FAIL (NULL, format == GST_BAYER_FORMAT_MONO,
          FALSE, "Invalid bayer matrix format, expected format 'mono' !");
      break;
#endif
    default:
      GST_WARNING ("Unsupported sensor filter arrangement!");
      return FALSE;
  }

  if (!meta.exists (ANDROID_SENSOR_OPAQUE_RAW_SIZE)) {
    GST_WARNING ("There is no camera bayer size information!");
    return FALSE;
  }

  entry = meta.find (ANDROID_SENSOR_OPAQUE_RAW_SIZE);

  supported = (width == entry.data.i32[0]) && (height == entry.data.i32[1]);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, supported, FALSE,
      "Invalid bayer resolution, expected %dx%d !", entry.data.i32[0],
      entry.data.i32[1]);

  return TRUE;
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
  gint idx = 0;
  guint tag_id = 0;
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
      } else if (g_strcmp0 (section, "org.codeaurora.qcamera3.manualWB") == 0) {
        if (g_strcmp0 (name, "gains") == 0) {
          n_bytes = gst_value_array_get_size (value) * sizeof(gfloat);
          data = g_malloc0 (n_bytes);

          for (num = 0; num < gst_value_array_get_size (value); num++) {
            const GValue *val = gst_value_array_get_value (value, num);
            ((gfloat*)data)[num] = g_value_get_double (val);
          }

          meta->update(tag_id, (gfloat*)data, n_bytes / sizeof(gfloat));
          g_free (data);
        }
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
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::android::CameraMetadata meta;
  guint tag_id = 0;
  guchar numvalue = 0;
  gint ivalue = 0, status = 0;

  status = recorder->GetCameraParam (context->camera_id, meta);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder GetCameraParam Failed!");

  numvalue = gst_qmmfsrc_effect_mode_android_value (context->effect);
  meta.update(ANDROID_CONTROL_EFFECT_MODE, &numvalue, 1);

  numvalue = gst_qmmfsrc_scene_mode_android_value (context->scene);
  meta.update(ANDROID_CONTROL_SCENE_MODE, &numvalue, 1);

  numvalue = gst_qmmfsrc_antibanding_android_value (context->antibanding);
  meta.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &numvalue, 1);

  meta.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
              &(context)->expcompensation, 1);

  numvalue = gst_qmmfsrc_exposure_mode_android_value (context->expmode);
  meta.update(ANDROID_CONTROL_AE_MODE, &numvalue, 1);

  numvalue = context->explock;
  meta.update(ANDROID_CONTROL_AE_LOCK, &numvalue, 1);

  meta.update(ANDROID_SENSOR_EXPOSURE_TIME, &(context)->exptime, 1);

  numvalue = gst_qmmfsrc_white_balance_mode_android_value (context->wbmode);

  // If the returned value is not UCHAR_MAX then we have an Android enum.
  if (numvalue != UCHAR_MAX)
    meta.update(ANDROID_CONTROL_AWB_MODE, &numvalue, 1);

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.manualWB", "partial_mwb_mode");

  // If the returned value is UCHAR_MAX, we have manual WB mode so set
  // that value for the vendor tag, otherwise disable manual WB mode.
  ivalue = (numvalue == UCHAR_MAX) ? context->wbmode : 0;
  meta.update(tag_id, &ivalue, 1);

  numvalue = context->wblock;
  meta.update(ANDROID_CONTROL_AWB_LOCK, &numvalue, 1);

  numvalue = gst_qmmfsrc_focus_mode_android_value (context->afmode);
  meta.update(ANDROID_CONTROL_AF_MODE, &numvalue, 1);

  numvalue = gst_qmmfsrc_noise_reduction_android_value (context->nrmode);
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

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.ir_led", "mode");
  if (tag_id != 0)
    meta.update(tag_id, &(context)->irmode, 1);

  // Here select_priority is ISOPriority whose index is 0.
  gint select_iso_priority = 0;
  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.iso_exp_priority", "select_priority");
  if (tag_id != 0)
    meta.update(tag_id, &select_iso_priority, 1);

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_exp_priority");
    if (tag_id != 0)
      meta.update(tag_id, &(context)->isomode, 1);

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.exposure_metering",
                                  "exposure_metering_mode");
  if (tag_id != 0)
    meta.update(tag_id, &(context)->expmetering, 1);

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.sharpness",
      "strength");

  if (tag_id != 0)
    meta.update (tag_id, &(context)->sharpness, 1);

  set_vendor_tags (context->defogtable, &meta);
  set_vendor_tags (context->exptable, &meta);
  set_vendor_tags (context->ltmdata, &meta);
  set_vendor_tags (context->nrtuning, &meta);
  set_vendor_tags (context->mwbsettings, &meta);

  status = recorder->SetCameraParam (context->camera_id, meta);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder SetCameraParam Failed!");

  return TRUE;
}

static void
qmmfsrc_free_queue_item (GstDataQueueItem * item)
{
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static void
qmmfsrc_gst_buffer_release (GstStructure * structure)
{
  gsize value;
  guint track_id, session_id, camera_id;
  std::vector<::qmmf::BufferDescriptor> buffers;
  ::qmmf::recorder::Recorder *recorder = NULL;
  ::qmmf::BufferDescriptor buffer;

  GST_TRACE (" %s", gst_structure_to_string (structure));

  gst_structure_get (structure, "recorder", G_TYPE_ULONG, &value, NULL);
  recorder =
      reinterpret_cast<::qmmf::recorder::Recorder*>(GSIZE_TO_POINTER (value));

  gst_structure_get_uint (structure, "camera", &camera_id);
  gst_structure_get_uint (structure, "session", &session_id);

  gst_structure_get (structure, "data", G_TYPE_ULONG, &value, NULL);
  buffer.data = GSIZE_TO_POINTER (value);

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

static GstBuffer *
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
      "recorder", G_TYPE_ULONG, GPOINTER_TO_SIZE (context->recorder),
      "camera", G_TYPE_UINT, context->camera_id,
      "session", G_TYPE_UINT, context->session_id,
      NULL
  );

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad))
    gst_structure_set (structure, "track", G_TYPE_UINT,
        GST_QMMFSRC_VIDEO_PAD (pad)->id, NULL);

  gst_structure_set (structure,
      "data", G_TYPE_ULONG, GPOINTER_TO_SIZE (buffer->data),
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

  GST_TRACE (" %s", gst_structure_to_string (structure));
  return gstbuffer;
}

static void
video_event_callback (uint32_t track_id, ::qmmf::recorder::EventType type,
    void * data, size_t size)
{
  GST_WARNING ("Not Implemented!");
}

static void
video_data_callback (GstQmmfContext * context, GstPad * pad,
    std::vector<::qmmf::BufferDescriptor> buffers,
    std::vector<::qmmf::recorder::MetaData> metabufs)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;

  guint idx = 0, numplanes = 0;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };
  gint  stride[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };

  GstBuffer *gstbuffer = NULL;
  GstDataQueueItem *item = NULL;

  for (idx = 0; idx < buffers.size(); ++idx) {
    ::qmmf::BufferDescriptor& buffer = buffers[idx];
    ::qmmf::recorder::MetaData& meta = metabufs[idx];

    gstbuffer = qmmfsrc_gst_buffer_new_wrapped (context, pad, &buffer);
    QMMFSRC_RETURN_IF_FAIL_WITH_CLEAN (NULL, gstbuffer != NULL,
        recorder->ReturnTrackBuffer (context->session_id, vpad->id, buffers),
        "Failed to create GST buffer!");

    GST_BUFFER_FLAG_SET (gstbuffer, GST_BUFFER_FLAG_LIVE);

    if (buffer.flag & (guint)::qmmf::BufferFlags::kFlagCodecConfig)
      GST_BUFFER_FLAG_SET (gstbuffer, GST_BUFFER_FLAG_HEADER);

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
        (GstVideoFormat)vpad->format, vpad->width, vpad->height,
        numplanes, offset, stride
    );

    GST_QMMF_CONTEXT_LOCK (context);
    // Initialize the timestamp base value for buffer synchronization.
    context->tsbase = (GST_CLOCK_TIME_NONE == context->tsbase) ?
        buffer.timestamp - running_time (pad) : context->tsbase;

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

static void
image_data_callback (GstQmmfContext * context, GstPad * pad,
    ::qmmf::BufferDescriptor buffer, ::qmmf::recorder::MetaData meta)
{
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;

  GstBuffer *gstbuffer = NULL;
  GstDataQueueItem *item = NULL;

  gstbuffer = qmmfsrc_gst_buffer_new_wrapped (context, pad, &buffer);
  QMMFSRC_RETURN_IF_FAIL_WITH_CLEAN (NULL, gstbuffer != NULL,
      recorder->ReturnImageCaptureBuffer (context->camera_id, buffer);,
      "Failed to create GST buffer!");

  GST_BUFFER_FLAG_SET (gstbuffer, GST_BUFFER_FLAG_LIVE);

  if (buffer.flag & (guint)::qmmf::BufferFlags::kFlagCodecConfig)
    GST_BUFFER_FLAG_SET (gstbuffer, GST_BUFFER_FLAG_HEADER);

  GST_QMMF_CONTEXT_LOCK (context);
  // Initialize the timestamp base value for buffer synchronization.
  context->tsbase = (GST_CLOCK_TIME_NONE == context->tsbase) ?
      buffer.timestamp - running_time (pad) : context->tsbase;

  if (GST_FORMAT_UNDEFINED == ipad->segment.format) {
    gst_segment_init (&(ipad)->segment, GST_FORMAT_TIME);
    gst_pad_push_event (pad, gst_event_new_segment (&(ipad)->segment));
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

static void
camera_event_callback (GstQmmfContext * context,
    ::qmmf::recorder::EventType type, void *data, size_t size)
{
  gboolean eos = FALSE;

  switch (type) {
    case ::qmmf::recorder::EventType::kServerDied:
      GST_ERROR ("Camera server has died !");
      eos = TRUE;
      break;
    case ::qmmf::recorder::EventType::kCameraError:
    {
      ::qmmf::recorder::RecorderErrorData *error =
          static_cast<::qmmf::recorder::RecorderErrorData *>(data);

      GST_ERROR ("Camera %u encountered an error with code %d !",
          error->camera_id, error->error_code);

      switch ((GstCameraError) error->error_code) {
        case GST_ERROR_CAMERA_INVALID_ERROR:
        case GST_ERROR_CAMERA_DISCONNECTED:
        case GST_ERROR_CAMERA_DEVICE:
        case GST_ERROR_CAMERA_SERVICE:
          eos = (context->camera_id == error->camera_id) ? TRUE : FALSE;
          break;
        default:
          GST_WARNING ("Camera has encounter a non-fatal error");
      }
    }
      break;
    case ::qmmf::recorder::EventType::kCameraOpened:
    {
      guint camera_id = *(static_cast<guint*>(data));
      GST_LOG ("Camera %u has been opened", camera_id);
    }
      break;
    case ::qmmf::recorder::EventType::kCameraClosing:
    {
      guint camera_id = *(static_cast<guint*>(data));
      GST_LOG ("Closing camera %u ...", camera_id);

      eos = (context->camera_id == camera_id) ? TRUE : FALSE;
    }
      break;
    case ::qmmf::recorder::EventType::kCameraClosed:
    {
      guint camera_id = *(static_cast<guint*>(data));
      GST_LOG ("Camera %u has been closed", camera_id);
    }
      break;
  }

  if (eos && (context->state == GST_STATE_PLAYING)) {
    GList *list = NULL;

    // Send EOS event for all pads which have active streams.
    for (list = context->pads; list != NULL; list = list->next)
      gst_pad_push_event (GST_PAD (list->data), gst_event_new_eos ());
  }
}

GstQmmfContext *
gst_qmmf_context_new ()
{
  GstQmmfContext *context = NULL;
  ::qmmf::recorder::RecorderCb cbs;
  gint status = 0;

  context = g_slice_new0 (GstQmmfContext);
  g_return_val_if_fail (context != NULL, NULL);

  context->recorder = new ::qmmf::recorder::Recorder();
  QMMFSRC_RETURN_VAL_IF_FAIL_WITH_CLEAN (NULL, context->recorder != NULL,
      g_slice_free (GstQmmfContext, context);,
      NULL, "QMMF Recorder creation failed!");

  // Register a events function which will call the EOS callback if necessary.
  cbs.event_cb =
      [&, context] (::qmmf::recorder::EventType type, void *data, size_t size)
      { camera_event_callback (context, type, data, size); };

  status = context->recorder->Connect (cbs);
  QMMFSRC_RETURN_VAL_IF_FAIL_WITH_CLEAN (NULL, status == 0,
      delete context->recorder; g_slice_free (GstQmmfContext, context);,
      NULL, "QMMF Recorder Connect failed!");

  context->defogtable = gst_structure_new_empty ("org.quic.camera.defog");
  context->exptable =
      gst_structure_new_empty ("org.codeaurora.qcamera3.exposuretable");
  context->ltmdata =
      gst_structure_new_empty ("org.quic.camera.ltmDynamicContrast");
  context->nrtuning =
      gst_structure_new_empty ("org.quic.camera.anr_tuning");
  context->mwbsettings =
      gst_structure_new_empty ("org.codeaurora.qcamera3.manualWB");

  context->state = GST_STATE_NULL;

  GST_INFO ("Created QMMF context: %p", context);
  return context;
}

void
gst_qmmf_context_free (GstQmmfContext * context)
{
  context->recorder->Disconnect ();
  delete context->recorder;

  gst_structure_free (context->defogtable);
  gst_structure_free (context->exptable);
  gst_structure_free (context->ltmdata);
  gst_structure_free (context->nrtuning);
  gst_structure_free (context->mwbsettings);

  GST_INFO ("Destroyed QMMF context: %p", context);
  g_slice_free (GstQmmfContext, context);
}

gboolean
gst_qmmf_context_open (GstQmmfContext * context)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  GST_TRACE ("Open QMMF context");

  ::qmmf::recorder::CameraExtraParam extra_param;

  // Slave Mode
  ::qmmf::recorder::CameraSlaveMode camera_slave_mode;
  camera_slave_mode.mode = context->slave ?
      ::qmmf::recorder::SlaveMode::kSlave :
      ::qmmf::recorder::SlaveMode::kMaster;
  extra_param.Update(::qmmf::recorder::QMMF_CAMERA_SLAVE_MODE,
      camera_slave_mode);

  // LDC
  ::qmmf::recorder::LDCMode ldc;
  ldc.enable = context->ldc;
  extra_param.Update(::qmmf::recorder::QMMF_LDC, ldc);

  // EIS
  ::qmmf::recorder::EISSetup eis_mode;
  eis_mode.enable = context->eis;
  extra_param.Update(::qmmf::recorder::QMMF_EIS, eis_mode);

  // SHDR
  ::qmmf::recorder::VideoHDRMode vid_hdr_mode;
  vid_hdr_mode.enable = context->shdr;
  extra_param.Update(::qmmf::recorder::QMMF_VIDEO_HDR_MODE, vid_hdr_mode);

  status = recorder->StartCamera(context->camera_id, 30, extra_param);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StartCamera Failed!");

  context->state = GST_STATE_READY;

  GST_TRACE ("QMMF context opened");

  return TRUE;
}

gboolean
gst_qmmf_context_close (GstQmmfContext * context)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  GST_TRACE ("Closing QMMF context");

  status = recorder->StopCamera (context->camera_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StopCamera Failed!");

  context->state = GST_STATE_NULL;

  GST_TRACE ("QMMF context closed");

  return TRUE;
}

gboolean
gst_qmmf_context_create_session (GstQmmfContext * context)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::qmmf::recorder::SessionCb session_cbs;
  guint session_id = -1;
  gint status = 0;

  GST_TRACE ("Create QMMF context session");

  session_cbs.event_cb =
      [] (::qmmf::recorder::EventType type, void *data, size_t size) { };

  status = recorder->CreateSession (session_cbs, &session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder CreateSession Failed!");

  context->session_id = session_id;
  context->state = GST_STATE_PAUSED;

  GST_TRACE ("QMMF context session created");

  return TRUE;
}

gboolean
gst_qmmf_context_delete_session (GstQmmfContext * context)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  GST_TRACE ("Delete QMMF context session");

  status = recorder->DeleteSession (context->session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder DeleteSession Failed!");

  context->session_id = 0;
  context->state = GST_STATE_READY;

  GST_TRACE ("QMMF context session deleted");

  return TRUE;
}

gboolean
gst_qmmf_context_create_video_stream (GstQmmfContext * context, GstPad * pad)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::qmmf::recorder::TrackCb track_cbs;
  ::qmmf::recorder::VideoExtraParam extraparam;
  gint status = 0;

  GST_TRACE ("Create QMMF context video stream");

  GST_QMMFSRC_VIDEO_PAD_LOCK (vpad);

  ::qmmf::VideoFormat format;
  switch (vpad->codec) {
    case GST_VIDEO_CODEC_H264:
      format = ::qmmf::VideoFormat::kAVC;
      break;
    case GST_VIDEO_CODEC_H265:
      format = ::qmmf::VideoFormat::kHEVC;
      break;
    case GST_VIDEO_CODEC_JPEG:
      format = ::qmmf::VideoFormat::kJPEG;
      break;
    case GST_VIDEO_CODEC_NONE:
      // Not an encoded stream.
      break;
    default:
      GST_ERROR ("Unsupported video codec!");
      GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);
      return FALSE;
  }

  switch (vpad->format) {
    case GST_VIDEO_FORMAT_NV12:
      format = (vpad->compression == GST_VIDEO_COMPRESSION_UBWC) ?
          ::qmmf::VideoFormat::kNV12UBWC : ::qmmf::VideoFormat::kNV12;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      format = ::qmmf::VideoFormat::kYUY2;
      break;
    case GST_VIDEO_FORMAT_NV16:
      format = ::qmmf::VideoFormat::kNV16;
      break;
    case GST_BAYER_FORMAT_BGGR:
    case GST_BAYER_FORMAT_RGGB:
    case GST_BAYER_FORMAT_GBRG:
    case GST_BAYER_FORMAT_GRBG:
    case GST_BAYER_FORMAT_MONO:
      if (!validate_bayer_params (context, pad)) {
        GST_ERROR ("Invalid bayer format or resolution!");
        GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);
        return FALSE;
      } else if (vpad->bpp == 8) {
        format = ::qmmf::VideoFormat::kBayerRDI8BIT;
      } else if (vpad->bpp == 10) {
        format = ::qmmf::VideoFormat::kBayerRDI10BIT;
      } else if (vpad->bpp == 12) {
        format = ::qmmf::VideoFormat::kBayerRDI12BIT;
      } else {
        GST_ERROR ("Unsupported bits per pixel for bayer format!");
        GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);
        return FALSE;
      }
      break;
    case GST_VIDEO_FORMAT_ENCODED:
      // Encoded stream.
      break;
    default:
      GST_ERROR ("Unsupported video format!");
      GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);
      return FALSE;
  }

  ::qmmf::recorder::VideoTrackCreateParam params (
    context->camera_id, format, vpad->width, vpad->height, vpad->framerate,
    vpad->xtrabufs
  );

  if (format == ::qmmf::VideoFormat::kAVC) {
    guint bitrate, qpvalue, idr_interval;
    const gchar *profile, *level;
    gint ratecontrol;

    profile = gst_structure_get_string (vpad->params, "profile");
    if (g_strcmp0 (profile, "baseline") == 0) {
      params.codec_param.avc.profile = ::qmmf::AVCProfileType::kBaseline;
    } else if (g_strcmp0 (profile, "main") == 0) {
      params.codec_param.avc.profile = ::qmmf::AVCProfileType::kMain;
    } else if (g_strcmp0 (profile, "high") == 0) {
      params.codec_param.avc.profile = ::qmmf::AVCProfileType::kHigh;
    }

    level = gst_structure_get_string (vpad->params, "level");
    if (g_strcmp0 (level, "1") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel1;
    } else if (g_strcmp0 (level, "1.3") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel1_3;
    } else if (g_strcmp0 (level, "2") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel2;
    } else if (g_strcmp0 (level, "2.1") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel2_1;
    } else if (g_strcmp0 (level, "2.2") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel2_2;
    } else if (g_strcmp0 (level, "3") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel3;
    } else if (g_strcmp0 (level, "3.1") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel3_1;
    } else if (g_strcmp0 (level, "3.2") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel3_2;
    } else if (g_strcmp0 (level, "4") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel4;
    } else if (g_strcmp0 (level, "4.1") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel4_1;
    } else if (g_strcmp0 (level, "4.2") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel4_2;
    } else if (g_strcmp0 (level, "5") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel5;
    } else if (g_strcmp0 (level, "5.1") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel5_1;
    } else if (g_strcmp0 (level, "5.2") == 0) {
      params.codec_param.avc.level = ::qmmf::AVCLevelType::kLevel5_2;
    }

    gst_structure_get_enum (vpad->params, "bitrate-control",
        GST_TYPE_VIDEO_PAD_CONTROL_RATE, &ratecontrol);
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

    gst_structure_get_uint (vpad->params, "bitrate", &bitrate);
    params.codec_param.avc.bitrate = bitrate;

    gst_structure_get_uint (vpad->params, "quant-i-frames", &qpvalue);
    params.codec_param.avc.qp_params.init_qp.init_IQP = qpvalue;

    gst_structure_get_uint (vpad->params, "quant-p-frames", &qpvalue);
    params.codec_param.avc.qp_params.init_qp.init_PQP = qpvalue;

    gst_structure_get_uint (vpad->params, "quant-b-frames", &qpvalue);
    params.codec_param.avc.qp_params.init_qp.init_BQP = qpvalue;

    gst_structure_get_uint (vpad->params, "min-qp", &qpvalue);
    params.codec_param.avc.qp_params.qp_range.min_QP = qpvalue;

    gst_structure_get_uint (vpad->params, "max-qp", &qpvalue);
    params.codec_param.avc.qp_params.qp_range.max_QP = qpvalue;

    gst_structure_get_uint (vpad->params, "min-qp-i-frames", &qpvalue);
    params.codec_param.avc.qp_params.qp_IBP_range.min_IQP = qpvalue;

    gst_structure_get_uint (vpad->params, "max-qp-i-frames", &qpvalue);
    params.codec_param.avc.qp_params.qp_IBP_range.max_IQP = qpvalue;

    gst_structure_get_uint (vpad->params, "min-qp-p-frames", &qpvalue);
    params.codec_param.avc.qp_params.qp_IBP_range.min_PQP = qpvalue;

    gst_structure_get_uint (vpad->params, "max-qp-p-frames", &qpvalue);
    params.codec_param.avc.qp_params.qp_IBP_range.max_PQP = qpvalue;

    gst_structure_get_uint (vpad->params, "min-qp-b-frames", &qpvalue);
    params.codec_param.avc.qp_params.qp_IBP_range.min_BQP = qpvalue;

    gst_structure_get_uint (vpad->params, "max-qp-b-frames", &qpvalue);
    params.codec_param.avc.qp_params.qp_IBP_range.max_BQP = qpvalue;

    params.codec_param.avc.qp_params.enable_init_qp = true;
    params.codec_param.avc.qp_params.enable_qp_range = true;
    params.codec_param.avc.qp_params.enable_qp_IBP_range = true;

    gst_structure_get_uint(vpad->params, "idr-interval", &idr_interval);
    params.codec_param.avc.idr_interval = idr_interval;

  } else if (format == ::qmmf::VideoFormat::kHEVC) {
    guint bitrate, qpvalue, idr_interval;
    const gchar *profile, *level;
    gint ratecontrol;

    profile = gst_structure_get_string(vpad->params, "profile");
    if (g_strcmp0 (profile, "main") == 0) {
      params.codec_param.hevc.profile = ::qmmf::HEVCProfileType::kMain;
    }

    level = gst_structure_get_string (vpad->params, "level");
    if (g_strcmp0 (level, "3") == 0) {
      params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel3;
    } else if (g_strcmp0 (level, "4") == 0) {
      params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel4;
    } else if (g_strcmp0 (level, "5") == 0) {
      params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel5;
    } else if (g_strcmp0 (level, "5.1") == 0) {
      params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel5_1;
    } else if (g_strcmp0 (level, "5.2") == 0) {
      params.codec_param.hevc.level = ::qmmf::HEVCLevelType::kLevel5_2;
    }

    gst_structure_get_enum (vpad->params, "bitrate-control",
        GST_TYPE_VIDEO_PAD_CONTROL_RATE, &ratecontrol);
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

    gst_structure_get_uint (vpad->params, "bitrate", &bitrate);
    params.codec_param.hevc.bitrate = bitrate;

    gst_structure_get_uint (vpad->params, "quant-i-frames", &qpvalue);
    params.codec_param.hevc.qp_params.init_qp.init_IQP = qpvalue;

    gst_structure_get_uint (vpad->params, "quant-p-frames", &qpvalue);
    params.codec_param.hevc.qp_params.init_qp.init_PQP = qpvalue;

    gst_structure_get_uint (vpad->params, "quant-b-frames", &qpvalue);
    params.codec_param.hevc.qp_params.init_qp.init_BQP = qpvalue;

    gst_structure_get_uint (vpad->params, "min-qp", &qpvalue);
    params.codec_param.hevc.qp_params.qp_range.min_QP = qpvalue;

    gst_structure_get_uint (vpad->params, "max-qp", &qpvalue);
    params.codec_param.hevc.qp_params.qp_range.max_QP = qpvalue;

    gst_structure_get_uint (vpad->params, "min-qp-i-frames", &qpvalue);
    params.codec_param.hevc.qp_params.qp_IBP_range.min_IQP = qpvalue;

    gst_structure_get_uint (vpad->params, "max-qp-i-frames", &qpvalue);
    params.codec_param.hevc.qp_params.qp_IBP_range.max_IQP = qpvalue;

    gst_structure_get_uint (vpad->params, "min-qp-p-frames", &qpvalue);
    params.codec_param.hevc.qp_params.qp_IBP_range.min_PQP = qpvalue;

    gst_structure_get_uint (vpad->params, "max-qp-p-frames", &qpvalue);
    params.codec_param.hevc.qp_params.qp_IBP_range.max_PQP = qpvalue;

    gst_structure_get_uint (vpad->params, "min-qp-b-frames", &qpvalue);
    params.codec_param.hevc.qp_params.qp_IBP_range.min_BQP = qpvalue;

    gst_structure_get_uint (vpad->params, "max-qp-b-frames", &qpvalue);
    params.codec_param.hevc.qp_params.qp_IBP_range.max_BQP = qpvalue;

    params.codec_param.hevc.qp_params.enable_init_qp = true;
    params.codec_param.hevc.qp_params.enable_qp_range = true;
    params.codec_param.hevc.qp_params.enable_qp_IBP_range = true;

    gst_structure_get_uint(vpad->params, "idr-interval", &idr_interval);
    params.codec_param.hevc.idr_interval = idr_interval;
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
    extraparam.Update(::qmmf::recorder::QMMF_USE_LINKED_TRACK_IN_SLAVE_MODE,
        linked_track_slave_mode);
  }

  status = recorder->CreateVideoTrack (
      context->session_id, vpad->id, params, extraparam, track_cbs);

  GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder CreateVideoTrack Failed!");

  GST_TRACE ("QMMF context video stream created");

  // Update crop metadata parameters.
  if (vpad->crop.x < 0 || vpad->crop.x > vpad->width) {
    GST_WARNING ("Cannot apply crop, X axis value outside stream width!");
  } else if (vpad->crop.y < 0 || vpad->crop.y > vpad->height) {
    GST_WARNING ("Cannot apply crop, Y axis value outside stream height!");
  } else if (vpad->crop.w < 0 || vpad->crop.w > (vpad->width - vpad->crop.x)) {
    GST_WARNING ("Cannot apply crop, width value outside stream width!");
  } else if (vpad->crop.h < 0 || vpad->crop.h > (vpad->height - vpad->crop.y)) {
    GST_WARNING ("Cannot apply crop, height value outside stream height!");
  } else if ((vpad->crop.w == 0 && vpad->crop.h != 0) ||
      (vpad->crop.w != 0 && vpad->crop.h == 0)) {
    GST_WARNING ("Cannot apply crop, width and height must either both be 0 "
        "or both be positive values !");
  } else if ((vpad->crop.w == 0 && vpad->crop.h == 0) &&
      (vpad->crop.x != 0 || vpad->crop.y != 0)) {
    GST_WARNING ("Cannot apply crop, width and height values are 0 but "
        "X and/or Y are not 0!");
  } else {
    ::android::CameraMetadata meta;
    guint tag_id = 0;
    gint32 ivalue = 0;

    recorder->GetCameraParam (context->camera_id, meta);

    tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.c2dCropParam", "c2dCropX");
    ivalue = vpad->crop.x;

    if (meta.update (tag_id, &ivalue, 1) != 0)
      GST_WARNING ("Failed to update X axis crop value");

    tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.c2dCropParam", "c2dCropY");
    if (meta.update (tag_id, &vpad->crop.y, 1) != 0)
      GST_WARNING ("Failed to update Y axis crop value");

    tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.c2dCropParam", "c2dCropWidth");
    if (meta.update (tag_id, &vpad->crop.w, 1) != 0)
      GST_WARNING ("Failed to update crop width");

    tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.c2dCropParam", "c2dCropHeight");
    if (meta.update (tag_id, &vpad->crop.h, 1) != 0)
      GST_WARNING ("Failed to update crop height");

    recorder->SetCameraParam (context->camera_id, meta);
  }

  context->pads = g_list_append (context->pads, pad);
  return TRUE;
}

gboolean
gst_qmmf_context_delete_video_stream (GstQmmfContext * context, GstPad * pad)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  GST_TRACE ("Delete QMMF context video stream");

  status = recorder->DeleteVideoTrack (context->session_id, vpad->id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder DeleteVideoTrack Failed!");

  GST_TRACE ("QMMF context video stream deleted");

  context->pads = g_list_remove (context->pads, pad);
  return TRUE;
}

gboolean
gst_qmmf_context_create_image_stream (GstQmmfContext * context, GstPad * pad,
    GstPad * bayerpad)
{
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::qmmf::recorder::SnapshotType snapshotparam;
  gint status = 0;

  GST_TRACE ("Create QMMF context image stream");

  if ((bayerpad != NULL) && GST_IS_QMMFSRC_IMAGE_PAD (bayerpad)) {
    GstQmmfSrcImagePad *bpad = GST_QMMFSRC_IMAGE_PAD (bayerpad);

    GST_QMMFSRC_IMAGE_PAD_LOCK (bpad);

    snapshotparam.type = ::qmmf::recorder::SnapshotMode::kVideoPlusRaw;
    switch (bpad->format) {
      case GST_BAYER_FORMAT_BGGR:
      case GST_BAYER_FORMAT_RGGB:
      case GST_BAYER_FORMAT_GBRG:
      case GST_BAYER_FORMAT_GRBG:
      case GST_BAYER_FORMAT_MONO:
        if (!validate_bayer_params (context, bayerpad)) {
          GST_ERROR ("Invalid bayer format or resolution!");
          GST_QMMFSRC_IMAGE_PAD_UNLOCK (bpad);
          return FALSE;
        } else if (bpad->bpp == 8) {
          snapshotparam.raw_format = ::qmmf::ImageFormat::kBayerRDI8BIT;
        } else if (bpad->bpp == 10) {
          snapshotparam.raw_format = ::qmmf::ImageFormat::kBayerRDI10BIT;
        } else if (bpad->bpp == 12) {
          snapshotparam.raw_format = ::qmmf::ImageFormat::kBayerRDI12BIT;
        } else if (bpad->bpp == 16) {
          snapshotparam.raw_format = ::qmmf::ImageFormat::kBayerRDI16BIT;
        } else {
          GST_ERROR ("Unsupported bits per pixel for bayer format!");
          GST_QMMFSRC_IMAGE_PAD_UNLOCK (bpad);
          return FALSE;
        }
        break;
      default:
        GST_ERROR ("Unsupported format: %d", bpad->format);
        GST_QMMFSRC_IMAGE_PAD_UNLOCK (bpad);
        return FALSE;
    }

    GST_QMMFSRC_IMAGE_PAD_UNLOCK (bpad);
  }

  GST_QMMFSRC_IMAGE_PAD_LOCK (ipad);

  ::qmmf::recorder::ImageConfigParam config;
  ::qmmf::recorder::ImageParam imgparam;

  imgparam.width = ipad->width;
  imgparam.height = ipad->height;

  if (ipad->codec == GST_IMAGE_CODEC_JPEG) {
    imgparam.image_format = ::qmmf::ImageFormat::kJPEG;
    gst_structure_get_uint (ipad->params, "quality", &imgparam.image_quality);

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

    if (bayerpad != NULL) {
      config.Update (::qmmf::recorder::QMMF_SNAPSHOT_TYPE, snapshotparam, 0);
      GST_WARNING ("JPEG and RAW has enabled. Image Mode is ignored.");
    } else {
      switch (ipad->mode) {
        case GST_IMAGE_MODE_VIDEO:
          snapshotparam.type = ::qmmf::recorder::SnapshotMode::kVideo;
          break;
        case GST_IMAGE_MODE_CONTINUOUS:
          snapshotparam.type = ::qmmf::recorder::SnapshotMode::kContinuous;
          break;
        default:
          GST_ERROR ("Unsupported mode %d", ipad->mode);
          GST_QMMFSRC_IMAGE_PAD_UNLOCK(ipad);
          return FALSE;
      }
      config.Update (::qmmf::recorder::QMMF_SNAPSHOT_TYPE, snapshotparam, 0);
    }
  } else if (ipad->codec == GST_IMAGE_CODEC_NONE) {
    switch (ipad->format) {
      case GST_VIDEO_FORMAT_NV12:
        imgparam.image_format = ::qmmf::ImageFormat::kNV12;
        break;
      case GST_VIDEO_FORMAT_NV21:
        imgparam.image_format = ::qmmf::ImageFormat::kNV21;
        break;
      case GST_BAYER_FORMAT_BGGR:
      case GST_BAYER_FORMAT_RGGB:
      case GST_BAYER_FORMAT_GBRG:
      case GST_BAYER_FORMAT_GRBG:
      case GST_BAYER_FORMAT_MONO:
        if (!validate_bayer_params (context, pad)) {
          GST_ERROR ("Invalid bayer format or resolution!");
          GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);
          return FALSE;
        } else if (ipad->bpp == 8) {
          imgparam.image_format = ::qmmf::ImageFormat::kBayerRDI8BIT;
        } else if (ipad->bpp == 10) {
          imgparam.image_format = ::qmmf::ImageFormat::kBayerRDI10BIT;
        } else if (ipad->bpp == 12) {
          imgparam.image_format = ::qmmf::ImageFormat::kBayerRDI12BIT;
        } else if (ipad->bpp == 16) {
          imgparam.image_format = ::qmmf::ImageFormat::kBayerRDI16BIT;
        } else {
         GST_ERROR ("Unsupported bits per pixel for bayer format!");
         GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);
         return FALSE;
        }
        break;
      default:
        GST_ERROR ("Unsupported format %d", ipad->format);
        GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);
        return FALSE;
    }
  }

  status = recorder->ConfigImageCapture (context->camera_id, imgparam, config);

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder ConfigImageCapture Failed!");

  GST_TRACE ("QMMF context image stream created");

  context->pads = g_list_append (context->pads, pad);
  return TRUE;
}

gboolean
gst_qmmf_context_delete_image_stream (GstQmmfContext * context, GstPad * pad)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  GST_TRACE ("Delete QMMF context image stream");

  status = recorder->CancelCaptureImage (context->camera_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder CancelCaptureImage Failed!");

  GST_TRACE ("QMMF context image stream deleted");

  context->pads = g_list_remove (context->pads, pad);
  return TRUE;
}

gboolean
gst_qmmf_context_start_session (GstQmmfContext * context)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  context->tsbase = GST_CLOCK_TIME_NONE;

  if (!context->slave) {
    gboolean success = initialize_camera_param(context);
    QMMFSRC_RETURN_VAL_IF_FAIL (NULL, success, FALSE,
        "Failed to initialize camera parameters!");
  }

  GST_TRACE ("Starting QMMF context session");

  status = recorder->StartSession (context->session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StartSession Failed!");

  context->state = GST_STATE_PLAYING;

  GST_TRACE ("QMMF context session started");

  return TRUE;
}

gboolean
gst_qmmf_context_stop_session (GstQmmfContext * context)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  GST_TRACE ("Stopping QMMF context session");

  status = recorder->StopSession (context->session_id, false);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StopSession Failed!");

  GST_TRACE ("QMMF context session stopped");

  context->state = GST_STATE_PAUSED;
  context->tsbase = GST_CLOCK_TIME_NONE;

  return TRUE;
}

gboolean
gst_qmmf_context_pause_session (GstQmmfContext * context)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  GST_TRACE ("Pausing QMMF context session");

  status = recorder->PauseSession (context->session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder PauseSession Failed!");

  GST_TRACE ("QMMF context session paused");

  return TRUE;
}

gboolean
gst_qmmf_context_capture_image (GstQmmfContext * context, GstPad * pad,
                                GstPad *bayerpad)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::qmmf::recorder::ImageCaptureCb imagecb;
  std::vector<::android::CameraMetadata> metadata;
  ::android::CameraMetadata meta;
  gint status = 0;

  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);

  GST_QMMFSRC_IMAGE_PAD_LOCK (ipad);

  imagecb = [&, context, pad, bayerpad] (uint32_t camera_id, uint32_t imgcount,
      ::qmmf::BufferDescriptor buffer, ::qmmf::recorder::MetaData meta)
      {
        if (bayerpad == NULL)
          image_data_callback (context, pad, buffer, meta);
        else {
          qmmf::CameraBufferMetaData &cam_buf_meta = meta.cam_buffer_meta_data;
          if (cam_buf_meta.format == qmmf::BufferFormat::kBLOB)
            image_data_callback (context, pad, buffer, meta);
          else
            image_data_callback (context, bayerpad, buffer, meta);
        }
      };

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);

  status = recorder->GetDefaultCaptureParam (context->camera_id, meta);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder GetDefaultCaptureParam Failed!");

  metadata.push_back (meta);

  status = recorder->CaptureImage (
      context->camera_id, 1, metadata, imagecb);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder CaptureImage Failed!");

  return TRUE;
}

void
gst_qmmf_context_set_camera_param (GstQmmfContext * context, guint param_id,
    const GValue * value)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::android::CameraMetadata meta;

  if (context->state >= GST_STATE_READY)
    recorder->GetCameraParam (context->camera_id, meta);

  switch (param_id) {
    case PARAM_CAMERA_ID:
      context->camera_id = g_value_get_uint (value);
      break;
    case PARAM_CAMERA_SLAVE:
      context->slave = g_value_get_boolean (value);
      break;
    case PARAM_CAMERA_LDC:
      context->ldc = g_value_get_boolean (value);
      break;
    case PARAM_CAMERA_EIS:
      context->eis = g_value_get_boolean (value);
      break;
    case PARAM_CAMERA_SHDR:
      context->shdr = g_value_get_boolean (value);
      break;
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
    case PARAM_CAMERA_EXPOSURE_MODE:
    {
      guchar mode;
      context->expmode = g_value_get_enum (value);

      mode = gst_qmmfsrc_exposure_mode_android_value (context->expmode);
      meta.update(ANDROID_CONTROL_AE_MODE, &mode, 1);
      break;
    }
    case PARAM_CAMERA_EXPOSURE_LOCK:
    {
      guchar lock;
      context->explock = g_value_get_boolean (value);

      lock = context->explock;
      meta.update(ANDROID_CONTROL_AE_LOCK, &lock, 1);
      break;
    }
    case PARAM_CAMERA_EXPOSURE_METERING:
    {
      guint tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.exposure_metering", "exposure_metering_mode");

      context->expmetering = g_value_get_enum (value);
      meta.update(tag_id, &(context)->expmetering, 1);
      break;
    }
    case PARAM_CAMERA_EXPOSURE_COMPENSATION:
    {
      gint compensation;
      context->expcompensation = g_value_get_int (value);

      compensation = context->expcompensation;
      meta.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &compensation, 1);
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
    case PARAM_CAMERA_WHITE_BALANCE_MODE:
    {
      guint tag_id = 0;
      gint mode = UCHAR_MAX;

      context->wbmode = g_value_get_enum (value);
      mode = gst_qmmfsrc_white_balance_mode_android_value (context->wbmode);

      // If the returned value is not UCHAR_MAX then we have an Android enum.
      if (mode != UCHAR_MAX)
        meta.update(ANDROID_CONTROL_AWB_MODE, (guchar*)&mode, 1);

      tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.manualWB", "partial_mwb_mode");

      // If the returned value is UCHAR_MAX, we have manual WB mode so set
      // that value for the vendor tag, otherwise disable manual WB mode.
      mode = (mode == UCHAR_MAX) ? context->wbmode : 0;
      meta.update(tag_id, &mode, 1);
      break;
    }
    case PARAM_CAMERA_WHITE_BALANCE_LOCK:
    {
      guchar lock;
      context->wblock = g_value_get_boolean (value);

      lock = context->wblock;
      meta.update(ANDROID_CONTROL_AWB_LOCK, &lock, 1);
      break;
    }
    case PARAM_CAMERA_MANUAL_WB_SETTINGS:
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
          GST_WARNING ("Failed to get manual WB file contents, error: %s!",
              GST_STR_NULL (error->message));
          g_clear_error (&error);
          break;
        }

        // Remove trailing space and replace new lines with a comma delimiter.
        contents = g_strstrip (contents);
        contents = g_strdelimit (contents, "\n", ',');

        success = gst_value_deserialize (&gvalue, contents);
        g_free (contents);

        if (!success) {
          GST_WARNING ("Failed to deserialize manual WB file contents!");
          break;
        }
      } else if (!gst_value_deserialize (&gvalue, input)) {
        GST_WARNING ("Failed to deserialize manual WB input!");
        break;
      }

      structure = GST_STRUCTURE (g_value_dup_boxed (&gvalue));
      g_value_unset (&gvalue);

      gst_structure_foreach (structure, update_structure, context->mwbsettings);
      gst_structure_free (structure);

      set_vendor_tags (context->mwbsettings, &meta);
      break;
    }
    case PARAM_CAMERA_FOCUS_MODE:
    {
      guchar mode;
      context->afmode = g_value_get_enum (value);

      mode = gst_qmmfsrc_focus_mode_android_value (context->afmode);
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
    case PARAM_CAMERA_ISO_MODE:
    {
      // Here select_priority is ISOPriority whose index is 0.
      gint select_iso_priority = 0;
      guint tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.iso_exp_priority", "select_priority");
      if (tag_id != 0)
        meta.update(tag_id, &select_iso_priority, 1);

      tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_exp_priority");

      context->isomode = g_value_get_enum (value);
      meta.update(tag_id, &(context)->isomode, 1);
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

      structure = GST_STRUCTURE (g_value_dup_boxed(&gvalue));
      g_value_unset (&gvalue);

      gst_structure_foreach (structure, update_structure, context->nrtuning);
      gst_structure_free (structure);

      set_vendor_tags (context->nrtuning, &meta);
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

        // Remove trailing space and replace new lines with a comma delimiter.
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

        // Remove trailing space and replace new lines with a comma delimiter.
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

      structure = GST_STRUCTURE (g_value_dup_boxed(&gvalue));
      g_value_unset (&gvalue);

      gst_structure_foreach (structure, update_structure, context->ltmdata);
      gst_structure_free (structure);

      set_vendor_tags(context->ltmdata, &meta);
      break;
    }
    case PARAM_CAMERA_SHARPNESS_STRENGTH:
    {
      guint tag_id = get_vendor_tag_by_name (
          "org.codeaurora.qcamera3.sharpness", "strength");

      context->sharpness = g_value_get_int (value);
      meta.update(tag_id, &(context)->sharpness, 1);
      break;
    }
  }

  if (!context->slave && (context->state >= GST_STATE_READY))
    recorder->SetCameraParam (context->camera_id, meta);
}

void
gst_qmmf_context_get_camera_param (GstQmmfContext * context, guint param_id,
    GValue * value)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::android::CameraMetadata meta;

  if (context->state >= GST_STATE_READY)
    recorder->GetCameraParam (context->camera_id, meta);

  switch (param_id) {
    case PARAM_CAMERA_ID:
      g_value_set_uint (value, context->camera_id);
      break;
    case PARAM_CAMERA_SLAVE:
      g_value_set_boolean (value, context->slave);
      break;
    case PARAM_CAMERA_LDC:
      g_value_set_boolean (value, context->ldc);
      break;
    case PARAM_CAMERA_EIS:
      g_value_set_boolean (value, context->eis);
      break;
    case PARAM_CAMERA_SHDR:
      g_value_set_boolean (value, context->shdr);
      break;
    case PARAM_CAMERA_ADRC:
      g_value_set_boolean (value, context->adrc);
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
    case PARAM_CAMERA_EXPOSURE_MODE:
      g_value_set_enum (value, context->expmode);
      break;
    case PARAM_CAMERA_EXPOSURE_LOCK:
      g_value_set_boolean (value, context->explock);
      break;
    case PARAM_CAMERA_EXPOSURE_METERING:
      g_value_set_enum (value, context->expmetering);
      break;
    case PARAM_CAMERA_EXPOSURE_COMPENSATION:
      g_value_set_int (value, context->expcompensation);
      break;
    case PARAM_CAMERA_EXPOSURE_TIME:
      g_value_set_int64 (value, context->exptime);
      break;
    case PARAM_CAMERA_WHITE_BALANCE_MODE:
      g_value_set_enum (value, context->wbmode);
      break;
    case PARAM_CAMERA_WHITE_BALANCE_LOCK:
      g_value_set_boolean (value, context->wblock);
      break;
    case PARAM_CAMERA_MANUAL_WB_SETTINGS:
    {
      gchar *string = NULL;

      get_vendor_tags ("org.codeaurora.qcamera3.manualWB",
          gst_camera_manual_wb_settings,
          G_N_ELEMENTS (gst_camera_manual_wb_settings),
          context->mwbsettings, &meta);
      string = gst_structure_to_string (context->mwbsettings);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    case PARAM_CAMERA_FOCUS_MODE:
      g_value_set_enum (value, context->afmode);
      break;
    case PARAM_CAMERA_IR_MODE:
      g_value_set_enum (value, context->irmode);
      break;
    case PARAM_CAMERA_ISO_MODE:
      g_value_set_enum (value, context->isomode);
      break;
    case PARAM_CAMERA_NOISE_REDUCTION:
      g_value_set_enum (value, context->nrmode);
      break;
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
    case PARAM_CAMERA_SHARPNESS_STRENGTH:
      g_value_set_int (value, context->sharpness);
      break;
  }
}

void
gst_qmmf_context_update_video_param (GstPad * pad, GParamSpec * pspec,
    GstQmmfContext * context)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
  guint track_id = vpad->id, session_id = context->session_id;
  const gchar *pname = g_param_spec_get_name (pspec);
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  GValue value = G_VALUE_INIT;
  gint status = 0;

  GST_DEBUG ("Received update for %s property", pname);

  if (context->state < GST_STATE_PAUSED) {
    GST_DEBUG ("Stream not yet created, skip property update.");
    return;
  }

  g_value_init (&value, pspec->value_type);
  g_object_get_property (G_OBJECT (vpad), pname, &value);

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
  } else if (g_strcmp0 (pname, "idr-interval") == 0) {
    guint idr_interval = g_value_get_uint (&value);
    ::qmmf::VideoEncIdrInterval param;

    param.num_pframes = (idr_interval == 0) ? 0 :
        ((idr_interval * vpad->framerate) - 1);
    param.num_bframes = 0;
    param.idr_period = (idr_interval == 0) ? 0 : 1;
    status = recorder->SetVideoTrackParam (session_id, track_id,
        ::qmmf::CodecParamType::kIDRIntervalType, &param, sizeof (param)
    );
  } else if (g_strcmp0 (pname, "crop") == 0) {
    ::android::CameraMetadata meta;
    gint32 x = -1, y = -1, width = -1, height = -1;
    guint tag_id = 0;

    g_return_if_fail (gst_value_array_get_size (&value) == 4);

    x = g_value_get_int (gst_value_array_get_value (&value, 0));
    y = g_value_get_int (gst_value_array_get_value (&value, 1));
    width = g_value_get_int (gst_value_array_get_value (&value, 2));
    height = g_value_get_int (gst_value_array_get_value (&value, 3));

    if (x < 0 || x > vpad->width) {
      GST_WARNING ("Cannot apply crop, X axis value outside stream width!");
      return;
    } else if (y < 0 || y > vpad->height) {
      GST_WARNING ("Cannot apply crop, Y axis value outside stream height!");
      return;
    } else if (width < 0 || width > (vpad->width - x)) {
      GST_WARNING ("Cannot apply crop, width value outside stream width!");
      return;
    } else if (height < 0 || height > (vpad->height - y)) {
      GST_WARNING ("Cannot apply crop, height value outside stream height!");
      return;
    } else if ((vpad->crop.w == 0 && vpad->crop.h != 0) ||
        (vpad->crop.w != 0 && vpad->crop.h == 0)) {
      GST_WARNING ("Cannot apply crop, width and height must either both be 0 "
          "or both be positive values!");
      return;
    } else if ((vpad->crop.w == 0 && vpad->crop.h == 0) &&
        (vpad->crop.x != 0 || vpad->crop.y != 0)) {
      GST_WARNING ("Cannot apply crop, width and height values are 0 but "
          "X and/or Y are not 0!");
      return;
    }

    recorder->GetCameraParam (context->camera_id, meta);

    tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.c2dCropParam", "c2dCropX");
    if (meta.update (tag_id, &x, 1) != 0)
      GST_WARNING ("Failed to update X axis crop value");

    tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.c2dCropParam", "c2dCropY");
    if (meta.update (tag_id, &y, 1) != 0)
      GST_WARNING ("Failed to update Y axis crop value");

    tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.c2dCropParam", "c2dCropWidth");
    if (meta.update (tag_id, &width, 1) != 0)
      GST_WARNING ("Failed to update crop width");

    tag_id = get_vendor_tag_by_name (
        "org.codeaurora.qcamera3.c2dCropParam", "c2dCropHeight");
    if (meta.update (tag_id, &height, 1) != 0)
      GST_WARNING ("Failed to update crop height");

    status = recorder->SetCameraParam (context->camera_id, meta);
  } else {
    GST_WARNING ("Unsupported parameter '%s'!", pname);
    status = -1;
  }

  QMMFSRC_RETURN_IF_FAIL (NULL, status == 0,
      "QMMF Recorder SetVideoTrackParam/SetCameraParam Failed!");
}
