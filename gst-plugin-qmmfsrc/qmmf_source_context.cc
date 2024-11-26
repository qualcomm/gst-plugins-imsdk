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
*
* Changes from Qualcomm Innovation Center are provided under the following license:
*
* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "qmmf_source_context.h"

#ifdef ENABLE_RUNTIME_PARSER
#include <gst/utils/runtime-flags-parser-c-api.h>
#endif // ENABLE_RUNTIME_PARSER
#include <gst/allocators/allocators.h>
#include <qmmf-sdk/qmmf_recorder.h>
#include <qmmf-sdk/qmmf_recorder_extra_param_tags.h>
#include <qmmf-sdk/qmmf_recorder_extra_param.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>
#include <qmmf-sdk/qmmf_camera_metadata.h>

#include "qmmf_source_utils.h"
#include "qmmf_source_image_pad.h"
#include "qmmf_source_video_pad.h"

namespace camera = qmmf;
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

struct _GstQmmfLogicalCamInfo {
  gboolean        is_logical_cam;
  gint            phy_cam_num;
  gchar*          phy_cam_name_list[16];
};

struct _GstQmmfCameraSwitchInfo {
  gint            phy_cam_id_for_switch;
  gint            input_req_id;
};

struct _GstQmmfContext {
  /// Global mutex lock.
  GMutex            lock;

  /// User provided callback for signalling events.
  GstCameraEventCb  eventcb;
  /// User provided callback for signalling result metadata.
  GstCameraMetaCb   metacb;
  /// User provided private data to be called with the callback.
  gpointer          userdata;

  /// QMMF Recorder camera device opened by this source.
  guint             camera_id;

  /// Keep track of internal states by reusing the GstState enum:
  /// @GST_STATE_NULL - Context created.
  /// @GST_STATE_READY - Camera opened, no session has been created yet.
  /// @GST_STATE_PAUSED - Session created but it is not yet active.
  /// @GST_STATE_PLAYING - Session is active/running.
  GstState          state;

  /// Video and image pads timestamp base.
  GstClockTime      tsbase;

  /// Camera Slave mode.
  gboolean          slave;
  /// Camera property to Enable or Disable Lens Distortion Correction.
  gboolean          ldc;
  /// Camera property to Enable or Disable Lateral Chromatic Aberration Correction.
  gboolean          lcac;
#ifndef EIS_MODES_ENABLE
  /// Camera property to Enable or Disable Electronic Image Stabilization.
  gboolean          eis;
#else
  /// Camera property to select Electronic Image Stabilization mode.
  gint              eis;
#endif // EIS_MODES_ENABLE
#ifndef VHDR_MODES_ENABLE
  /// Camera property to Enable or Disable Super High Dynamic Range.
  gboolean          shdr;
#else
  /// Camera property for Video High Dynamic Range modes.
  gint              vhdr;
#endif // VHDR_MODES_ENABLE
  /// Camera property to Enable or Disable Auto Dynamic Range Compression.
  gboolean          adrc;
  /// Overall mode of 3A
  guchar            controlmode;
  /// Camera frame effect property.
  guchar            effect;
  /// Camera scene optimization property.
  guchar            scene;
  /// Camera antibanding mode property.
  guchar            antibanding;
  /// Camera Sharpness property.
  gint              sharpness;
  /// Camera Contrast property.
  gint              contrast;
  /// Camera Saturation property.
  gint              saturation;
  /// Camera ISO exposure mode property.
  gint64            isomode;
  /// Camera Manual ISO exposure value property.
  gint32            isovalue;
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
  /// Camera IR mode property.
  gint              irmode;
  /// Camera sensor active pixels property.
  GstVideoRectangle sensorsize;
  /// Camera Sensor Mode.
  gint              sensormode;
  /// Streams frame rate control mode
  guchar            frc_mode;
  /// Camera IFE direct stream enable
  gboolean          ife_direct_stream;
  /// Multi Camera (0) Exposure value
  gint64            master_exp_time;
  /// Multi Camera (1) Exposure value
  gint64            slave_exp_time;
  /// Camera operation mode
  guint32           op_mode;
  /// Input ROI reprocess usecase enable
  gboolean          input_roi_enable;
  /// Number of Input ROI's
  gint32            input_roi_count;

  /// Logical Camera Information
  GstQmmfLogicalCamInfo logical_cam_info;
  /// Sensor Switch Information
  GstQmmfCameraSwitchInfo camera_switch_info;

  /// QMMF Recorder instance.
  ::qmmf::recorder::Recorder *recorder;
};

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
  ::camera::CameraMetadata meta;
  camera_metadata_entry entry;
  gint width = 0, height = 0, format = 0;
  gboolean supported = FALSE;
  guint idx = 0;

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
#if defined(CAMERA_METADATA_1_1) || defined(CAMERA_METADATA_1_0_NS)
    case ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_MONO:
      QMMFSRC_RETURN_VAL_IF_FAIL (NULL, format == GST_BAYER_FORMAT_MONO,
          FALSE, "Invalid bayer matrix format, expected format 'mono' !");
      break;
#endif
    default:
      GST_WARNING ("Unsupported sensor filter arrangement!");
      return FALSE;
  }

#if defined(CAMERA_METADATA_1_0_NS)
  if (meta.exists(ANDROID_SENSOR_OPAQUE_RAW_SIZE_MAXIMUM_RESOLUTION)) {
    entry = meta.find (ANDROID_SENSOR_OPAQUE_RAW_SIZE_MAXIMUM_RESOLUTION);

    for (idx = 0; !supported && (idx < entry.count); idx += 3) {
      if ((width == entry.data.i32[idx]) && (height == entry.data.i32[idx+1]))
        supported = TRUE;
    }
  }
#endif

  if ((supported != TRUE) && (!meta.exists(ANDROID_SENSOR_OPAQUE_RAW_SIZE))) {
      GST_WARNING ("There is no camera bayer size information!");
      return FALSE;
  }

  entry = meta.find (ANDROID_SENSOR_OPAQUE_RAW_SIZE);
  for (idx = 0; !supported && (idx < entry.count); idx += 3) {
    if ((width == entry.data.i32[idx]) && (height == entry.data.i32[idx+1]))
      supported = TRUE;
  }

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, supported, FALSE,
      "Invalid %dx%d bayer resolution!", width, height);

  return TRUE;
}

static guint
get_vendor_tag_by_name (const gchar * section, const gchar * name)
{
  std::shared_ptr<VendorTagDescriptor> vtags;
  status_t status = 0;
  guint tag_id = 0;

  vtags = VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (vtags.get() == NULL) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    return 0;
  }

  status = vtags->lookupTag(std::string(name),
      std::string(section), &tag_id);
  if (status != 0) {
    GST_WARNING ("Unable to locate tag for '%s', section '%s'!", name, section);
    return 0;
  }

  return tag_id;
}

static void
set_vendor_tags (GstStructure * structure, ::camera::CameraMetadata * meta)
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
    GstStructure * structure, ::camera::CameraMetadata * meta)
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
  ::camera::CameraMetadata meta;
  guint tag_id = 0;
  guchar numvalue = 0;
  gint ivalue = 0, status = 0;

  status = recorder->GetCameraParam (context->camera_id, meta);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder GetCameraParam Failed!");

  numvalue = gst_qmmfsrc_control_mode_android_value (context->controlmode);
  meta.update(ANDROID_CONTROL_MODE, &numvalue, 1);

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

  numvalue = !context->adrc;
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

  // Here priority is ISOPriority whose index is 0.
  gint32 priority = 0;

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.iso_exp_priority", "select_priority");
  if (tag_id != 0)
    meta.update(tag_id, &priority, 1);

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_exp_priority");
  if (tag_id != 0)
    meta.update(tag_id, &(context)->isomode, 1);

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_value");
  if (tag_id != 0)
    meta.update(tag_id, &(context)->isovalue, 1);

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.exposure_metering",
                                  "exposure_metering_mode");
  if (tag_id != 0)
    meta.update(tag_id, &(context)->expmetering, 1);

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.sharpness",
      "strength");

  if (tag_id != 0)
    meta.update (tag_id, &(context)->sharpness, 1);

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.contrast",
      "level");

  if (tag_id != 0)
    meta.update (tag_id, &(context)->contrast, 1);

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.saturation",
      "use_saturation");

  if (tag_id != 0)
    meta.update (tag_id, &(context)->saturation, 1);

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.multicam_exptime",
      "masterExpTime");

  if (tag_id != 0)
    meta.update (tag_id,
        (context->master_exp_time) > 0 ? &(context)->master_exp_time : &(context)->exptime, 1);

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.multicam_exptime",
      "slaveExpTime");

  if (tag_id != 0)
    meta.update (tag_id,
        (context->slave_exp_time) > 0 ? &(context)->slave_exp_time : &(context)->exptime, 1);

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

  QMMFSRC_TRACE_STRUCTURE (structure);

  gst_structure_get (structure, "recorder", G_TYPE_ULONG, &value, NULL);
  recorder =
      reinterpret_cast<::qmmf::recorder::Recorder*>(GSIZE_TO_POINTER (value));

  gst_structure_get_uint (structure, "camera", &camera_id);

  gst_structure_get (structure, "data", G_TYPE_ULONG, &value, NULL);
  buffer.data = GSIZE_TO_POINTER (value);

  gst_structure_get_int (structure, "fd", &buffer.fd);
  gst_structure_get_uint (structure, "bufid", &buffer.buf_id);
  gst_structure_get_uint (structure, "size", &buffer.size);
  gst_structure_get_uint (structure, "capacity", &buffer.capacity);
  gst_structure_get_uint (structure, "offset", &buffer.offset);
  gst_structure_get_uint64 (structure, "timestamp", &buffer.timestamp);
  gst_structure_get_uint64 (structure, "seqnum", &buffer.seqnum);
  gst_structure_get_uint64 (structure, "flags", &buffer.flags);

  buffers.push_back (buffer);

  if (gst_structure_has_field (structure, "track")) {
    gst_structure_get_uint (structure, "session", &session_id);
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
  GstBufferPool *pool = NULL;

  // Create or acquire a GstBuffer.
  if (GST_IS_QMMFSRC_VIDEO_PAD (pad))
    pool = GST_QMMFSRC_VIDEO_PAD (pad)->pool;
  else if (GST_IS_QMMFSRC_IMAGE_PAD (pad))
    pool = GST_QMMFSRC_IMAGE_PAD (pad)->pool;

  gst_buffer_pool_acquire_buffer (pool, &gstbuffer, NULL);
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
      NULL
  );

  if (GST_IS_QMMFSRC_VIDEO_PAD (pad)) {
    gst_structure_set (structure,
      "session", G_TYPE_UINT, GST_QMMFSRC_VIDEO_PAD (pad)->session_id,
      "track", G_TYPE_UINT, GST_QMMFSRC_VIDEO_PAD (pad)->id,
      NULL
    );
  }

  gst_structure_set (structure,
      "data", G_TYPE_ULONG, GPOINTER_TO_SIZE (buffer->data),
      "fd", G_TYPE_INT, buffer->fd,
      "bufid", G_TYPE_UINT, buffer->buf_id,
      "size", G_TYPE_UINT, buffer->size,
      "capacity", G_TYPE_UINT, buffer->capacity,
      "offset", G_TYPE_UINT, buffer->offset,
      "timestamp", G_TYPE_UINT64, buffer->timestamp,
      "seqnum", G_TYPE_UINT64, buffer->seqnum,
      "flags", G_TYPE_UINT64, buffer->flags,
      NULL
  );

  // Set a notification function to signal when the buffer is no longer used.
  gst_mini_object_set_qdata (
      GST_MINI_OBJECT (gstbuffer), qmmf_buffer_qdata_quark (),
      structure, (GDestroyNotify) qmmfsrc_gst_buffer_release
  );
  QMMFSRC_TRACE_STRUCTURE (structure);
  return gstbuffer;
}

::qmmf::recorder::Rotation
qmmfsrc_gst_get_stream_rotaion (gint rotate)
{
  switch (rotate) {
    case ROTATE_NONE:
      return ::qmmf::recorder::Rotation::kNone;
    case ROTATE_90CCW:
      return ::qmmf::recorder::Rotation::k90;
    case ROTATE_180CCW:
      return ::qmmf::recorder::Rotation::k180;
    case ROTATE_270CCW:
      return ::qmmf::recorder::Rotation::k270;
    default:
      GST_WARNING ("Rotation value %d is invalid default to no rotation",
          rotate);
      return ::qmmf::recorder::Rotation::kNone;
  }
}

::qmmf::recorder::VideoColorimetry
qmmfsrc_gst_get_stream_colorimetry (gchar *colorimetry)
{
  if (colorimetry == NULL)
    return ::qmmf::recorder::VideoColorimetry::kBT601;
#if (GST_VERSION_MAJOR >= 1) && (GST_VERSION_MINOR >= 18)
  else if (g_strcmp0(colorimetry, GST_VIDEO_COLORIMETRY_BT2100_HLG) == 0)
    return ::qmmf::recorder::VideoColorimetry::kBT2100HLG;
#endif // (GST_VERSION_MAJOR >= 1) && (GST_VERSION_MINOR >= 18)
  else {
    GST_WARNING ("Colorimetry value %s is invalid default to BT.601",
        colorimetry);
    return ::qmmf::recorder::VideoColorimetry::kBT601;
  }
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
    std::vector<::qmmf::BufferMeta> metas)
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
    ::qmmf::BufferMeta& meta = metas[idx];

    gstbuffer = qmmfsrc_gst_buffer_new_wrapped (context, pad, &buffer);
    QMMFSRC_RETURN_IF_FAIL_WITH_CLEAN (NULL, gstbuffer != NULL,
        recorder->ReturnTrackBuffer (vpad->session_id, vpad->id, buffers),
        "Failed to create GST buffer!");

    GST_BUFFER_FLAG_SET (gstbuffer, GST_BUFFER_FLAG_LIVE);

    for (size_t i = 0; i < meta.n_planes; ++i) {
      stride[i] = meta.planes[i].stride;
      offset[i] = meta.planes[i].offset;
      numplanes++;
    }

    // Set GStreamer buffer video metadata.
    gst_buffer_add_video_meta_full (gstbuffer, GST_VIDEO_FRAME_FLAG_NONE,
        (GstVideoFormat)vpad->format, vpad->width, vpad->height,
        numplanes, offset, stride);

    // Propagate original camera timestamp in media dependent OFFSET_END field.
    GST_BUFFER_OFFSET_END (gstbuffer) = buffer.timestamp;

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
    ::qmmf::BufferDescriptor buffer, ::qmmf::BufferMeta meta)
{
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;

  guint numplanes = 0;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };
  gint stride[GST_VIDEO_MAX_PLANES] = { 0, 0, 0, 0 };

  GstBuffer *gstbuffer = NULL;
  GstDataQueueItem *item = NULL;

  gstbuffer = qmmfsrc_gst_buffer_new_wrapped (context, pad, &buffer);
  QMMFSRC_RETURN_IF_FAIL_WITH_CLEAN (NULL, gstbuffer != NULL,
      recorder->ReturnImageCaptureBuffer (context->camera_id, buffer);,
      "Failed to create GST buffer!");

  GST_BUFFER_FLAG_SET (gstbuffer, GST_BUFFER_FLAG_LIVE);

  for (size_t i = 0; i < meta.n_planes; ++i) {
    stride[i] = meta.planes[i].stride;
    offset[i] = meta.planes[i].offset;
    numplanes++;
  }

  // Set GStreamer buffer video metadata.
  gst_buffer_add_video_meta_full (gstbuffer, GST_VIDEO_FRAME_FLAG_NONE,
      (GstVideoFormat)ipad->format, ipad->width, ipad->height,
      numplanes, offset, stride);

  // Propagate original camera timestamp in media dependent OFFSET_END field.
  GST_BUFFER_OFFSET_END (gstbuffer) = buffer.timestamp;

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
    ::qmmf::recorder::EventType type, void * payload, size_t size)
{
  gint event = EVENT_UNKNOWN;

  switch (type) {
    case ::qmmf::recorder::EventType::kServerDied:
      event = EVENT_SERVICE_DIED;
      break;
    case ::qmmf::recorder::EventType::kCameraError:
    {
      g_assert (size == sizeof (guint));
      uint32_t camera_id = *(static_cast<uint32_t*>(payload));

      // Ignore event if it is not for this camera id.
      if (camera_id != context->camera_id)
        return;

      event = EVENT_CAMERA_ERROR;
      break;
    }
    case ::qmmf::recorder::EventType::kCameraOpened:
    {
      g_assert (size == sizeof (guint));
      uint32_t camera_id = *(static_cast<uint32_t*>(payload));

      // Ignore event if it is not for this camera id.
      if (camera_id == context->camera_id)
        return;

      event = EVENT_CAMERA_OPENED;
      break;
    }
    case ::qmmf::recorder::EventType::kCameraClosing:
    {
      g_assert (size == sizeof (guint));
      uint32_t camera_id = *(static_cast<uint32_t*>(payload));

      // Ignore event if it's not for this camera id or it's in master mode.
      if ((camera_id != context->camera_id) || !context->slave)
        return;

      event = EVENT_CAMERA_CLOSING;
      break;
    }
    case ::qmmf::recorder::EventType::kCameraClosed:
    {
      g_assert (size == sizeof (guint));
      uint32_t camera_id = *(static_cast<uint32_t*>(payload));

      // Ignore event if it is not for this camera id.
      if (camera_id == context->camera_id)
        return;

      event = EVENT_CAMERA_CLOSED;
      break;
    }
    case  ::qmmf::recorder::EventType::kFrameError:
    {
      g_assert (size == sizeof (guint));

      uint32_t camera_id = *(static_cast<uint32_t*>(payload));
      // Ignore event if it is not for this camera id.
      if (camera_id == context->camera_id)
        return;

      event = EVENT_CAMERA_CLOSED;
      break;
    }
    case  ::qmmf::recorder::EventType::kMetadataError:
    {
      g_assert (size == sizeof (guint));
      uint32_t camera_id = *(static_cast<uint32_t*>(payload));

      // Ignore event if it is not for this camera id.
      if (camera_id == context->camera_id)
        return;

      event = EVENT_CAMERA_CLOSED;
      break;
    }
    default:
      event = EVENT_UNKNOWN;
      break;
  }

  context->eventcb (event, context->userdata);
}

GstQmmfContext *
gst_qmmf_context_new (GstCameraEventCb eventcb, GstCameraMetaCb metacb,
    gpointer userdata)
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

  context->state = GST_STATE_NULL;
  context->eventcb = eventcb;
  context->metacb = metacb;
  context->userdata = userdata;

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

  // logical camera and sensor switch info init
  context->logical_cam_info.is_logical_cam = FALSE;
  context->logical_cam_info.phy_cam_num = 0;
  context->camera_switch_info.input_req_id = -1;

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

  if (context->logical_cam_info.is_logical_cam == TRUE) {
    for (int i = 0; i < context->logical_cam_info.phy_cam_num; i++)
      g_free (context->logical_cam_info.phy_cam_name_list[i]);
  }

  GST_INFO ("Destroyed QMMF context: %p", context);
  g_slice_free (GstQmmfContext, context);
}

void
gst_qmmf_context_parse_logical_cam_info (GstQmmfContext *context,
    ::camera::CameraMetadata meta)
{
  camera_metadata_entry entry;
  GstQmmfLogicalCamInfo *pinfo = &context->logical_cam_info;

  entry = meta.find (ANDROID_REQUEST_AVAILABLE_CAPABILITIES);

  if (entry.count != 0) {
    guint8 *cap_req_keys = entry.data.u8;
    size_t i = 0;

    GST_INFO ("Found request available caps tag");

    for (i = 0; i < entry.count; i++) {
      if (ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA ==
          cap_req_keys[i]) {
        pinfo->is_logical_cam = TRUE;
        break;
      }
    }
  }

  if (pinfo->is_logical_cam == TRUE) {
    entry = meta.find (ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS);

    if (entry.count != 0) {
      size_t i = 0;
      guchar *pids = entry.data.u8;
      gchar  *pname = (gchar *)pids;

      for (i = 0; i < entry.count; i++) {
        // data format example:
        // '0''\0''1''\0''2''\0'
        if (pids[i] == '\0') {
          pinfo->phy_cam_name_list[pinfo->phy_cam_num] = g_strdup (pname);
          pinfo->phy_cam_num++;
          pname = (gchar *)&pids[i+1];

          GST_INFO ("Get physical camera %s in logical camera (%d)",
              pinfo->phy_cam_name_list[pinfo->phy_cam_num - 1],
              context->camera_id);
        }
      }

      GST_INFO ("Found %d physical camera in logical camera %d",
          pinfo->phy_cam_num, context->camera_id);
    }
  }
}

gboolean
gst_qmmf_context_open (GstQmmfContext * context)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;
  uint32_t op_mode = context->op_mode;

  GST_TRACE ("Open QMMF context");

  ::qmmf::recorder::CameraExtraParam xtraparam;

  // Slave Mode
  ::qmmf::recorder::CameraSlaveMode camera_slave_mode;
  camera_slave_mode.mode = context->slave ?
      ::qmmf::recorder::SlaveMode::kSlave :
      ::qmmf::recorder::SlaveMode::kMaster;
  xtraparam.Update (::qmmf::recorder::QMMF_CAMERA_SLAVE_MODE,
      camera_slave_mode);

  // LDC
  ::qmmf::recorder::LDCMode ldc;
  ldc.enable = context->ldc;
  xtraparam.Update (::qmmf::recorder::QMMF_LDC, ldc);

  // LCAC
  ::qmmf::recorder::LCACMode lcac;
  lcac.enable = context->lcac;
  xtraparam.Update (::qmmf::recorder::QMMF_LCAC, lcac);

  // EIS
#ifndef EIS_MODES_ENABLE
  ::qmmf::recorder::EISSetup eis;
  eis.enable = context->eis;
  xtraparam.Update (::qmmf::recorder::QMMF_EIS, eis);
#else
  ::qmmf::recorder::EISModeSetup eis;
  if (context->eis == EIS_OFF) {
    eis.mode = ::qmmf::recorder::EisMode::kEisOff;
  } else if (context->eis == EIS_ON_SINGLE_STREAM) {
    eis.mode = ::qmmf::recorder::EisMode::kEisSingleStream;
  } else {
    eis.mode = ::qmmf::recorder::EisMode::kEisDualStream;
  }
  xtraparam.Update (::qmmf::recorder::QMMF_EIS_MODE, eis);
#endif // EIS_MODES_ENABLE

  // SHDR
  ::qmmf::recorder::VideoHDRMode hdr;
#ifndef VHDR_MODES_ENABLE
  hdr.enable = context->shdr;
#else
  switch (context->vhdr) {
    case VHDR_OFF:
      hdr.mode = ::qmmf::recorder::VHDRMode::kVHDROff;
      break;
    case SHDR_MODE_RAW:
      hdr.mode = ::qmmf::recorder::VHDRMode::kSHDRRaw;
      break;
    case SHDR_MODE_YUV:
      hdr.mode = ::qmmf::recorder::VHDRMode::kSHDRYuv;
      break;
    case SHDR_RAW_SWITCH_ENABLE:
      hdr.mode = ::qmmf::recorder::VHDRMode::kSHDRRawSwitchEnable;
      break;
    case SHDR_YUV_SWITCH_ENABLE:
      hdr.mode = ::qmmf::recorder::VHDRMode::kSHDRYUVSwitchEnable;
      break;
    case QBC_HDR_MODE_VIDEO:
      hdr.mode = ::qmmf::recorder::VHDRMode::kQBCHDRVideo;
      break;
    case QBC_HDR_MODE_SNAPSHOT:
      hdr.mode = ::qmmf::recorder::VHDRMode::kQBCHDRSnapshot;
      break;
  }
#endif // VHDR_MODES_ENABLE
  xtraparam.Update (::qmmf::recorder::QMMF_VIDEO_HDR_MODE, hdr);

  // ForceSensorMode
  ::qmmf::recorder::ForceSensorMode forcesensormode;
  forcesensormode.mode = context->sensormode;
  xtraparam.Update (::qmmf::recorder::QMMF_FORCE_SENSOR_MODE, forcesensormode);

  // FrameRateControl
  ::qmmf::recorder::FrameRateControl frc;
  if (context->frc_mode == FRAME_SKIP) {
    frc.mode = ::qmmf::recorder::FrameRateControlMode::kFrameSkip;
  } else {
    frc.mode = ::qmmf::recorder::FrameRateControlMode::kCaptureRequest;
  }
  xtraparam.Update (::qmmf::recorder::QMMF_FRAME_RATE_CONTROL, frc);

  // IFE Direct Stream
  ::qmmf::recorder::IFEDirectStream qmmf_ife_direct_stream;
  qmmf_ife_direct_stream.enable = context->ife_direct_stream;
  xtraparam.Update (::qmmf::recorder::QMMF_IFE_DIRECT_STREAM, qmmf_ife_direct_stream);

  // Input ROI
  ::qmmf::recorder::InputROISetup qmmf_input_roi;
  qmmf_input_roi.enable = context->input_roi_enable;
  xtraparam.Update (::qmmf::recorder::QMMF_INPUT_ROI, qmmf_input_roi);

  // Camera Operation Mode
  ::qmmf::recorder::CamOpModeControl cam_opmode;
  gint extra_param_entry = 0;

  while (op_mode) {
    if (op_mode & CAM_OPMODE_NONE) {
      cam_opmode.mode =
        ::qmmf::recorder::CamOpMode::kNone;
      op_mode &= (~CAM_OPMODE_NONE);
    } else if (op_mode & CAM_OPMODE_FRAMESELECTION) {
      cam_opmode.mode =
        ::qmmf::recorder::CamOpMode::kFrameSelection;
      op_mode &= (~CAM_OPMODE_FRAMESELECTION);
    } else if (op_mode & CAM_OPMODE_FASTSWITCH) {
      cam_opmode.mode =
        ::qmmf::recorder::CamOpMode::kFastSwitch;
      op_mode &= (~CAM_OPMODE_FASTSWITCH);
    }

    if (xtraparam.Update(::qmmf::recorder::QMMF_CAM_OP_MODE_CONTROL,
          cam_opmode, extra_param_entry) < 0)
      GST_ERROR ("operation mode (%d) idx (%d) update failed",
        (int32_t)cam_opmode.mode, extra_param_entry);
    else
      GST_DEBUG ("operation mode (%d) idx (%d) update OK",
        (int32_t)cam_opmode.mode, extra_param_entry);

    extra_param_entry++;
  }

  qmmf::recorder::CameraResultCb result_cb = [&, context](uint32_t camera_id,
      const ::camera::CameraMetadata& result) {

    // Timestamp cannot exist in urgent metadata because at time urgent meta
    // is created frame is not exposed. This is why we use that to detect
    // result callback is for urgent or full metadata.
    gboolean isurgent = !result.exists (ANDROID_SENSOR_TIMESTAMP);

    context->metacb (camera_id, &result, isurgent, context->userdata);
  };

  status = recorder->StartCamera (context->camera_id, 30, xtraparam, result_cb);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StartCamera Failed!");

  ::camera::CameraMetadata meta;
  recorder->GetCameraCharacteristics (context->camera_id, meta);

  if (meta.exists(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE)) {
    context->sensorsize.x =
        meta.find (ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[0];
    context->sensorsize.y =
        meta.find (ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[1];
    context->sensorsize.w =
        meta.find (ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[2];
    context->sensorsize.h =
        meta.find (ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE).data.i32[3];
  }

  gst_qmmf_context_parse_logical_cam_info(context, meta);

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
gst_qmmf_context_create_video_stream (GstQmmfContext * context, GstPad * pad)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::qmmf::recorder::TrackCb track_cbs;
  ::qmmf::recorder::VideoExtraParam extraparam;
  ::qmmf::recorder::SessionCb session_cbs;
  ::qmmf::recorder::Rotation rotate;
  ::qmmf::recorder::VideoColorimetry colorimetry;
  gint status = 0;
#if (GST_VERSION_MAJOR >= 1) && (GST_VERSION_MINOR >= 18)
  guchar streamhdrmode = 0;
#endif // (GST_VERSION_MAJOR >= 1) && (GST_VERSION_MINOR >= 18)

  GST_TRACE ("Create QMMF context session");

  session_cbs.event_cb =
      [] (::qmmf::recorder::EventType type, void *data, size_t size) { };

  status = recorder->CreateSession (session_cbs, &vpad->session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder CreateSession Failed!");

  GST_TRACE ("QMMF context session created");

  context->state = GST_STATE_PAUSED;

  GST_TRACE ("Create QMMF context video stream");

  GST_QMMFSRC_VIDEO_PAD_LOCK (vpad);

  ::qmmf::recorder::VideoFormat format;
  switch (vpad->codec) {
    case GST_VIDEO_CODEC_JPEG:
      format = ::qmmf::recorder::VideoFormat::kJPEG;
      break;
    case GST_VIDEO_CODEC_NONE:
      // Not an encoded stream.
      break;
    default:
      GST_ERROR ("Unsupported video codec!");
      GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);
      return FALSE;
  }

  if (vpad->compression != GST_VIDEO_COMPRESSION_NONE &&
      vpad->format != GST_VIDEO_FORMAT_NV12 &&
      vpad->format != GST_VIDEO_FORMAT_NV12_10LE32) {
    GST_ERROR ("Compresion is not supported for %s format!",
        gst_qmmf_video_format_to_string (vpad->format));
    GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);
    return FALSE;
  }

  switch (vpad->format) {
    case GST_VIDEO_FORMAT_NV12:
      format = (vpad->compression == GST_VIDEO_COMPRESSION_UBWC) ?
          ::qmmf::recorder::VideoFormat::kNV12UBWC :
          ::qmmf::recorder::VideoFormat::kNV12;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      format = ::qmmf::recorder::VideoFormat::kP010;
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      if (vpad->compression != GST_VIDEO_COMPRESSION_UBWC) {
        GST_ERROR ("Only UBWC commpresion is supported for %s format!",
            gst_qmmf_video_format_to_string (vpad->format));
        GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);
        return FALSE;
      }
      format = ::qmmf::recorder::VideoFormat::kTP10UBWC;
      break;
    case GST_VIDEO_FORMAT_NV16:
      format = ::qmmf::recorder::VideoFormat::kNV16;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      format = ::qmmf::recorder::VideoFormat::kYUY2;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      format = ::qmmf::recorder::VideoFormat::kUYVY;
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
        format = ::qmmf::recorder::VideoFormat::kBayerRDI8BIT;
      } else if (vpad->bpp == 10) {
        format = ::qmmf::recorder::VideoFormat::kBayerRDI10BIT;
      } else if (vpad->bpp == 12) {
        format = ::qmmf::recorder::VideoFormat::kBayerRDI12BIT;
      } else if (vpad->bpp == 16) {
        format = ::qmmf::recorder::VideoFormat::kBayerRDI16BIT;
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
      GST_ERROR ("Unsupported %s format!",
          gst_qmmf_video_format_to_string (vpad->format));
      GST_QMMFSRC_VIDEO_PAD_UNLOCK (vpad);
      return FALSE;
  }

  rotate = qmmfsrc_gst_get_stream_rotaion (vpad->rotate);
  colorimetry = qmmfsrc_gst_get_stream_colorimetry (vpad->colorimetry);
  ::qmmf::recorder::VideoTrackParam params (
      context->camera_id, vpad->width, vpad->height, vpad->framerate, format,
      colorimetry, rotate, vpad->xtrabufs
  );

#ifdef ENABLE_RUNTIME_PARSER
  void* qmmfsrc_parser = get_qmmfsrc_parser ();

  gboolean gst_video_type_support = get_flag_as_bool (qmmfsrc_parser,
      "GST_VIDEO_TYPE_SUPPORT");

  if (gst_video_type_support && (vpad->type == VIDEO_TYPE_PREVIEW))
    params.flags |= ::qmmf::recorder::VideoFlags::kPreview;

#else
#ifdef GST_VIDEO_TYPE_SUPPORT
  if (vpad->type == VIDEO_TYPE_PREVIEW)
    params.flags |= ::qmmf::recorder::VideoFlags::kPreview;
#endif // GST_VIDEO_TYPE_SUPPORT
#endif // ENABLE_RUNTIME_PARSER

  if (vpad->reprocess_enable)
    params.flags |= ::qmmf::recorder::VideoFlags::kReproc;

#ifdef FEATURE_LOGICAL_CAMERA_SUPPORT
  if (!context->logical_cam_info.is_logical_cam) {
    GST_WARNING ("Non logical multi camera(%u), logical-stream-type makes no "
        "sense.", context->camera_id);
  } else {
    ::qmmf::recorder::StreamCameraId cam_id;
    ::qmmf::recorder::StitchLayoutSelect layout;
    GstQmmfLogicalCamInfo *pinfo = &context->logical_cam_info;
    gchar *info_name = NULL;

    if (vpad->log_stream_type < GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MIN) {
      GST_ERROR ("Invalid logical stream type.");
    } else if (vpad->log_stream_type <=
        GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MAX) {
      info_name = pinfo->phy_cam_name_list[vpad->log_stream_type -
          GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MIN];

      if (!info_name) {
        GST_ERROR ("Physical camera name is null.");
      } else {
        GST_DEBUG ("Physical camera name: %s", info_name);
      }

      g_strlcpy (cam_id.stream_camera_id, info_name, MAX_CAM_NAME_SIZE);
      extraparam.Update(::qmmf::recorder::QMMF_STREAM_CAMERA_ID, cam_id);
    } else if (vpad->log_stream_type < GST_PAD_LOGICAL_STREAM_TYPE_NONE) {
      switch (vpad->log_stream_type) {
        case GST_PAD_LOGICAL_STREAM_TYPE_SIDEBYSIDE:
          GST_DEBUG ("Stitch layout is selected: SideBySide.");
          layout.stitch_layout = ::qmmf::recorder::StitchLayout::kSideBySide;
          break;
        case GST_PAD_LOGICAL_STREAM_TYPE_PANORAMA:
          GST_DEBUG ("Stitch layout is selected: Panorama.");
          layout.stitch_layout = ::qmmf::recorder::StitchLayout::kPanorama;
          break;
        default:
          break;
      }
      extraparam.Update(::qmmf::recorder::QMMF_STITCH_LAYOUT, layout);
    } else {
      GST_ERROR ("Unknown logical-stream-type(%ld) of stream.",
          vpad->log_stream_type);
    }
  }
#endif // FEATURE_LOGICAL_CAMERA_SUPPORT

  if (context->input_roi_enable && !vpad->reprocess_enable)
    context->input_roi_count++;

  track_cbs.event_cb =
      [&] (uint32_t track_id, ::qmmf::recorder::EventType type,
          void *data, size_t size)
      { video_event_callback (track_id, type, data, size); };
  track_cbs.data_cb =
      [&, context, pad] (uint32_t track_id,
          std::vector<::qmmf::BufferDescriptor> buffers,
          std::vector<::qmmf::BufferMeta> metas)
      { video_data_callback (context, pad, buffers, metas); };

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
      vpad->session_id, vpad->id, params, extraparam, track_cbs);

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
    ::camera::CameraMetadata meta;
    guint tag_id = 0;
    gint32 ivalue = 0;

    recorder->GetCameraParam (context->camera_id, meta);
#ifdef C2D_ENABLE
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
#endif

#if (GST_VERSION_MAJOR >= 1) && (GST_VERSION_MINOR >= 18)
    tag_id = get_vendor_tag_by_name (
        "org.quic.camera2.streamconfigs", "HDRVideoMode");
    if (g_strcmp0(vpad->colorimetry , GST_VIDEO_COLORIMETRY_BT2100_HLG) == 0)
      streamhdrmode = 1;
    else
      streamhdrmode = 0;
    if (meta.update (tag_id, &streamhdrmode, 1) != 0)
      GST_WARNING ("Failed to update stream HDR mode");
#endif // (GST_VERSION_MAJOR >= 1) && (GST_VERSION_MINOR >= 18)

    recorder->SetCameraParam (context->camera_id, meta);
  }

  return TRUE;
}

gboolean
gst_qmmf_context_delete_video_stream (GstQmmfContext * context, GstPad * pad)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  GST_TRACE ("Delete QMMF context video stream");

  status = recorder->DeleteVideoTrack (vpad->session_id, vpad->id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder DeleteVideoTrack Failed!");

  vpad->id = 0;

  GST_TRACE ("QMMF context video stream deleted");

  GST_TRACE ("Delete QMMF context session");
  status = recorder->DeleteSession (vpad->session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder DeleteSession Failed!");

  context->state = GST_STATE_READY;

  GST_TRACE ("QMMF context session deleted");

  return TRUE;
}

gboolean
gst_qmmf_context_create_image_stream (GstQmmfContext * context, GstPad * pad)
{
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::qmmf::recorder::ImageParam imgparam;
  ::qmmf::recorder::ImageExtraParam xtraparam;
  gint status = 0;

  GST_TRACE ("Create QMMF context image stream");

  GST_QMMFSRC_IMAGE_PAD_LOCK (ipad);
  imgparam.mode = ::qmmf::recorder::ImageMode::kSnapshot;
  imgparam.width = ipad->width;
  imgparam.height = ipad->height;
  imgparam.rotation = qmmfsrc_gst_get_stream_rotaion (ipad->rotate);

  if (ipad->codec == GST_IMAGE_CODEC_JPEG) {
    imgparam.format = ::qmmf::recorder::ImageFormat::kJPEG;
    gst_structure_get_uint (ipad->params, "quality", &imgparam.quality);
  } else if (ipad->codec == GST_IMAGE_CODEC_NONE) {
    switch (ipad->format) {
      case GST_VIDEO_FORMAT_NV12:
        imgparam.format = (ipad->subformat == GST_IMAGE_SUBFORMAT_HEIF) ?
            ::qmmf::recorder::ImageFormat::kNV12HEIF :
            ::qmmf::recorder::ImageFormat::kNV12;
        break;
      case GST_VIDEO_FORMAT_NV21:
        imgparam.format = ::qmmf::recorder::ImageFormat::kNV21;
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
          imgparam.format = ::qmmf::recorder::ImageFormat::kBayerRDI8BIT;
        } else if (ipad->bpp == 10) {
          imgparam.format = ::qmmf::recorder::ImageFormat::kBayerRDI10BIT;
        } else if (ipad->bpp == 12) {
          imgparam.format = ::qmmf::recorder::ImageFormat::kBayerRDI12BIT;
        } else if (ipad->bpp == 16) {
          imgparam.format = ::qmmf::recorder::ImageFormat::kBayerRDI16BIT;
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

#ifdef FEATURE_LOGICAL_CAMERA_SUPPORT
  if (!context->logical_cam_info.is_logical_cam) {
    GST_WARNING ("Non logical multi camera(%u), logical-stream-type makes no "
        "sense.", context->camera_id);
  } else {
    ::qmmf::recorder::StreamCameraId cam_id;
    ::qmmf::recorder::StitchLayoutSelect layout;
    GstQmmfLogicalCamInfo *pinfo = &context->logical_cam_info;
    gchar *info_name = NULL;

    if (ipad->log_stream_type < GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MIN) {
      GST_ERROR ("Invalid logical stream type.");
    } else if (ipad->log_stream_type <=
        GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MAX) {
      info_name = pinfo->phy_cam_name_list[ipad->log_stream_type -
          GST_PAD_LOGICAL_STREAM_TYPE_CAMERA_INDEX_MIN];

      if (!info_name) {
        GST_ERROR ("Physical camera name is null.");
      } else {
        GST_DEBUG ("Physical camera name: %s", info_name);
      }

      g_strlcpy (cam_id.stream_camera_id, info_name, MAX_CAM_NAME_SIZE);
      xtraparam.Update(::qmmf::recorder::QMMF_STREAM_CAMERA_ID, cam_id);
    } else if (ipad->log_stream_type < GST_PAD_LOGICAL_STREAM_TYPE_NONE) {
      switch (ipad->log_stream_type) {
        case GST_PAD_LOGICAL_STREAM_TYPE_SIDEBYSIDE:
          GST_DEBUG ("Stitch layout is selected: SideBySide.");
          layout.stitch_layout = ::qmmf::recorder::StitchLayout::kSideBySide;
          break;
        case GST_PAD_LOGICAL_STREAM_TYPE_PANORAMA:
          GST_DEBUG ("Stitch layout is selected: Panorama.");
          layout.stitch_layout = ::qmmf::recorder::StitchLayout::kPanorama;
          break;
        default:
          break;
      }
      xtraparam.Update(::qmmf::recorder::QMMF_STITCH_LAYOUT, layout);
    } else {
      GST_ERROR ("Unknown logical-stream-type(%ld) of stream.",
          ipad->log_stream_type);
    }
  }
#endif // FEATURE_LOGICAL_CAMERA_SUPPORT

  status = recorder->ConfigImageCapture (context->camera_id, ipad->index,
      imgparam, xtraparam);

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);

  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder ConfigImageCapture Failed!");

  GST_TRACE ("QMMF context image stream created");
  return TRUE;
}

gboolean
gst_qmmf_context_delete_image_stream (GstQmmfContext * context, GstPad * pad,
    gboolean cache)
{
  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (pad);
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;

  GST_TRACE ("Delete QMMF context image stream");

  status = recorder->CancelCaptureImage (context->camera_id, ipad->index,
      cache ? true : false);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder CancelCaptureImage Failed!");

  GST_TRACE ("QMMF context image stream deleted");
  return TRUE;
}

gboolean
gst_qmmf_context_start_video_stream (GstQmmfContext * context, GstPad * pad)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);

  context->tsbase = GST_CLOCK_TIME_NONE;

  if (!context->slave) {
    gboolean success = initialize_camera_param(context);
    QMMFSRC_RETURN_VAL_IF_FAIL (NULL, success, FALSE,
        "Failed to initialize camera parameters!");
  }

  GST_TRACE ("Starting QMMF context session");

  status = recorder->StartSession (vpad->session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StartSession Failed!");

  context->state = GST_STATE_PLAYING;

  GST_TRACE ("QMMF context session started");

  return TRUE;
}

gboolean
gst_qmmf_context_stop_video_stream (GstQmmfContext * context, GstPad * pad)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);

  GST_TRACE ("Stopping QMMF context session");

  status = recorder->StopSession (vpad->session_id, false);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder StopSession Failed!");

  GST_TRACE ("QMMF context session stopped");

  context->state = GST_STATE_PAUSED;
  context->tsbase = GST_CLOCK_TIME_NONE;

  return TRUE;
}

gboolean
gst_qmmf_context_pause_video_stream (GstQmmfContext * context, GstPad * pad)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  gint status = 0;
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);

  GST_TRACE ("Pausing QMMF context session");

  status = recorder->PauseSession (vpad->session_id);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder PauseSession Failed!");

  GST_TRACE ("QMMF context session paused");

  return TRUE;
}

gboolean
gst_qmmf_context_capture_image (GstQmmfContext * context, GHashTable * srcpads,
                                GList * imgindexes, guint imgtype,
                                guint n_images, GPtrArray * metas)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::qmmf::recorder::ImageCaptureCb imagecb;
  ::qmmf::recorder::SnapshotType type = ::qmmf::recorder::SnapshotType::kVideo;
  std::vector<::camera::CameraMetadata> metadata;
  gint status = 0;
  guint idx = 0;

  GstQmmfSrcImagePad *ipad = GST_QMMFSRC_IMAGE_PAD (g_hash_table_lookup
      (srcpads, imgindexes->data));

  GST_QMMFSRC_IMAGE_PAD_LOCK (ipad);

  imagecb = [&, context, srcpads, imgindexes] (uint32_t camera_id,
      uint32_t imgcount,
      ::qmmf::BufferDescriptor buffer, ::qmmf::BufferMeta meta)
      {
        gpointer key;
        GList *list = NULL;
        GstPad *pad = NULL;

        for (list = imgindexes; list != NULL; list = list->next) {
          key = list->data;
          pad = GST_PAD (g_hash_table_lookup (srcpads, key));
          if (GST_QMMFSRC_IMAGE_PAD (pad)->index == buffer.img_id) {
            image_data_callback(context, pad, buffer, meta);
            break;
          }
        }
      };

  GST_QMMFSRC_IMAGE_PAD_UNLOCK (ipad);

  // Extract the capture metadata from the input argument if set.
  while ((imgtype == STILL_CAPTURE_MODE) && (metas != NULL) && (idx < metas->len)) {
    ::camera::CameraMetadata *meta =
        reinterpret_cast<::camera::CameraMetadata*>(
            g_ptr_array_index (metas, idx++));
    metadata.push_back(*meta);
  }

  // Fill the capture metadata for each image if not set via the input arguments.
  while ((imgtype == STILL_CAPTURE_MODE) && (metadata.size() < n_images)) {
    ::camera::CameraMetadata meta;

    status = recorder->GetDefaultCaptureParam (context->camera_id, meta);
    QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
        "QMMF Recorder GetDefaultCaptureParam Failed!");

    metadata.push_back(std::move(meta));
  }

  if (imgtype == VIDEO_CAPTURE_MODE)
    type = ::qmmf::recorder::SnapshotType::kVideo;
  else if (imgtype == STILL_CAPTURE_MODE)
    type = ::qmmf::recorder::SnapshotType::kStill;

  status = recorder->CaptureImage (
      context->camera_id, type, n_images, metadata, imagecb);
  QMMFSRC_RETURN_VAL_IF_FAIL (NULL, status == 0, FALSE,
      "QMMF Recorder CaptureImage Failed!");

  return TRUE;
}

void
gst_qmmf_context_update_local_props (GstQmmfContext * context,
    ::camera::CameraMetadata *meta)
{
  gint temp = 0;
  guint tag_id = 0;

  // Update local camera parameters
  if (meta->exists(ANDROID_CONTROL_MODE)) {
    temp = meta->find(ANDROID_CONTROL_MODE).data.u8[0];
    context->controlmode = gst_qmmfsrc_android_value_control_mode (temp);
  }

  if (meta->exists(ANDROID_CONTROL_EFFECT_MODE)) {
    temp = meta->find(ANDROID_CONTROL_EFFECT_MODE).data.u8[0];
    context->effect = gst_qmmfsrc_android_value_effect_mode (temp);
  }

  if (meta->exists(ANDROID_CONTROL_SCENE_MODE)) {
    temp = meta->find(ANDROID_CONTROL_SCENE_MODE).data.u8[0];
    context->scene = gst_qmmfsrc_android_value_scene_mode (temp);
  }

  if (meta->exists(ANDROID_CONTROL_AE_ANTIBANDING_MODE)) {
    temp = meta->find(ANDROID_CONTROL_AE_ANTIBANDING_MODE).data.u8[0];
    context->antibanding = gst_qmmfsrc_android_value_antibanding (temp);
  }

  if (meta->exists(ANDROID_CONTROL_AE_MODE)) {
    temp = meta->find(ANDROID_CONTROL_AE_MODE).data.u8[0];
    context->expmode = gst_qmmfsrc_android_value_exposure_mode (temp);
  }

  if (meta->exists(ANDROID_CONTROL_AWB_MODE)) {
    temp = meta->find(ANDROID_CONTROL_AWB_MODE).data.i32[0];
    context->wbmode = gst_qmmfsrc_android_value_white_balance_mode (temp);
  }

  if (meta->exists(ANDROID_CONTROL_AF_MODE)) {
    temp = meta->find(ANDROID_CONTROL_AF_MODE).data.u8[0];
    context->afmode = gst_qmmfsrc_android_value_focus_mode (temp);
  }

  if (meta->exists(ANDROID_NOISE_REDUCTION_MODE)) {
    temp = meta->find(ANDROID_NOISE_REDUCTION_MODE).data.u8[0];
    context->nrmode = gst_qmmfsrc_android_value_noise_reduction (temp);
  }

  if (meta->exists(ANDROID_SCALER_CROP_REGION)) {
    context->zoom.x = meta->find(ANDROID_SCALER_CROP_REGION).data.i32[0];
    context->zoom.y = meta->find(ANDROID_SCALER_CROP_REGION).data.i32[1];
    context->zoom.w = meta->find(ANDROID_SCALER_CROP_REGION).data.i32[2];
    context->zoom.h = meta->find(ANDROID_SCALER_CROP_REGION).data.i32[3];
  }

  if (meta->exists(ANDROID_CONTROL_AE_LOCK)) {
    context->explock = meta->find(ANDROID_CONTROL_AE_LOCK).data.u8[0];
  }

  if (meta->exists(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION)) {
    context->expcompensation =
        meta->find(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION).data.i32[0];
  }

  if (meta->exists(ANDROID_SENSOR_EXPOSURE_TIME)) {
    context->exptime = meta->find(ANDROID_SENSOR_EXPOSURE_TIME).data.i64[0];
  }

  if (meta->exists(ANDROID_CONTROL_AWB_LOCK)) {
    context->wblock = meta->find(ANDROID_CONTROL_AWB_LOCK).data.u8[0];
  }

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.saturation", "use_saturation");
  if (meta->exists(ANDROID_CONTROL_AWB_LOCK)) {
    context->saturation = meta->find(tag_id).data.i32[0];
  }

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_exp_priority");
  if (meta->exists(tag_id)) {
    context->isomode = meta->find(tag_id).data.i64[0];
  }

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_value");
  if (meta->exists(tag_id)) {
    context->isovalue = meta->find(tag_id).data.i32[0];
  }

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.exposure_metering", "exposure_metering_mode");
  if (meta->exists(tag_id)) {
    context->expmetering = meta->find(tag_id).data.i32[0];
  }

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.ir_led", "mode");
  if (meta->exists(tag_id)) {
    context->irmode = meta->find(tag_id).data.i32[0];
  }

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.sharpness", "strength");
  if (meta->exists(tag_id)) {
    context->sharpness = meta->find(tag_id).data.i32[0];
  }

  tag_id = get_vendor_tag_by_name ("org.codeaurora.qcamera3.contrast", "level");
  if (meta->exists(tag_id)) {
    context->contrast = meta->find(tag_id).data.i32[0];
  }

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.multicam_exptime", "masterExpTime");
  if (meta->exists(tag_id)) {
    context->master_exp_time = meta->find(tag_id).data.i64[0];
  }

  tag_id = get_vendor_tag_by_name (
      "org.codeaurora.qcamera3.multicam_exptime", "slaveExpTime");
  if (meta->exists(tag_id)) {
    context->slave_exp_time = meta->find(tag_id).data.i64[0];
  }
}

void
gst_qmmf_context_set_camera_param (GstQmmfContext * context, guint param_id,
    const GValue * value)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;
  ::camera::CameraMetadata meta;

  switch (param_id) {
    case PARAM_CAMERA_ID:
      context->camera_id = g_value_get_uint (value);
      return;
    case PARAM_CAMERA_SLAVE:
      context->slave = g_value_get_boolean (value);
      return;
    case PARAM_CAMERA_LDC:
      context->ldc = g_value_get_boolean (value);
      return;
    case PARAM_CAMERA_LCAC:
      context->lcac = g_value_get_boolean (value);
      return;
    case PARAM_CAMERA_EIS:
#ifndef EIS_MODES_ENABLE
      context->eis = g_value_get_boolean (value);
#else
      context->eis = g_value_get_enum (value);
#endif // EIS_MODES_ENABLE
      return;
#ifndef VHDR_MODES_ENABLE
    case PARAM_CAMERA_SHDR: {
      gboolean new_shdr = g_value_get_boolean (value);
      if (context->shdr != new_shdr) {
        context->shdr = new_shdr;
        if (context->state != GST_STATE_NULL)
          recorder->SetSHDR (context->camera_id, context->shdr);
      }
    }
#else
    case PARAM_CAMERA_VHDR: {
      gint new_vhdr = g_value_get_enum (value);
      if (context->vhdr != new_vhdr) {
        context->vhdr = new_vhdr;
       if (context->state != GST_STATE_NULL)
          recorder->SetVHDR (context->camera_id, context->vhdr);
      }
    }
#endif // VHDR_MODES_ENABLE

      return;
    case PARAM_CAMERA_SENSOR_MODE:
      context->sensormode = g_value_get_int (value);
      return;
    case PARAM_CAMERA_FRC_MODE:
      context->frc_mode = g_value_get_enum (value);
      return;
    case PARAM_CAMERA_IFE_DIRECT_STREAM:
      context->ife_direct_stream = g_value_get_boolean (value);
      return;
    case PARAM_CAMERA_OPERATION_MODE:
      context->op_mode = g_value_get_flags (value);
      return;
    case PARAM_CAMERA_INPUT_ROI:
      context->input_roi_enable = g_value_get_boolean (value);
      return;
  }

  if (context->state >= GST_STATE_READY &&
      param_id != PARAM_CAMERA_VIDEO_METADATA &&
      param_id != PARAM_CAMERA_SESSION_METADATA)
    recorder->GetCameraParam (context->camera_id, meta);

  switch (param_id) {
    case PARAM_CAMERA_ADRC:
    {
      guint8 disable;
      context->adrc = g_value_get_boolean (value);
      disable = !context->adrc;
      if (context->state >= GST_STATE_READY) {
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.adrc", "disable");
        meta.update(tag_id, &disable, 1);
      }
      break;
    }
    case PARAM_CAMERA_CONTROL_MODE:
    {
      guchar mode;
      context->controlmode = g_value_get_enum (value);

      mode = gst_qmmfsrc_control_mode_android_value (context->controlmode);
      meta.update(ANDROID_CONTROL_MODE, &mode, 1);
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
    case PARAM_CAMERA_SHARPNESS:
    {
      context->sharpness = g_value_get_int (value);
      if (context->state >= GST_STATE_READY) {
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.sharpness", "strength");
        meta.update(tag_id, &(context)->sharpness, 1);
      }
      break;
    }
    case PARAM_CAMERA_CONTRAST:
    {
      context->contrast = g_value_get_int (value);
      if (context->state >= GST_STATE_READY) {
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.contrast", "level");
        meta.update(tag_id, &(context)->contrast, 1);
      }
      break;
    }
    case PARAM_CAMERA_SATURATION:
    {
      context->saturation = g_value_get_int (value);
      if (context->state >= GST_STATE_READY) {
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.saturation", "use_saturation");
        meta.update(tag_id, &(context)->saturation, 1);
      }
      break;
    }
    case PARAM_CAMERA_ISO_MODE:
    {
      gint32 priority = 0;

      context->isomode = g_value_get_enum (value);

      if (context->state >= GST_STATE_READY) {
        // Here priority is CamX ISOPriority whose index is 0.
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.iso_exp_priority", "select_priority");
        meta.update(tag_id, &priority, 1);

        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_value");
        meta.update(tag_id, &(context)->isovalue, 1);

        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_exp_priority");
        meta.update(tag_id, &(context)->isomode, 1);
      }
      break;
    }
    case PARAM_CAMERA_ISO_VALUE:
    {
      gint32 priority = 0;

      context->isovalue = g_value_get_int (value);

      if (context->state >= GST_STATE_READY) {
        // Here priority is CamX ISOPriority whose index is 0.
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.iso_exp_priority", "select_priority");
        meta.update(tag_id, &priority, 1);

        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_exp_priority");
        meta.update(tag_id, &(context)->isomode, 1);

        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.iso_exp_priority", "use_iso_value");
        meta.update(tag_id, &(context)->isovalue, 1);
      }
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
      context->expmetering = g_value_get_enum (value);
      if (context->state >= GST_STATE_READY) {
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.exposure_metering", "exposure_metering_mode");
        meta.update(tag_id, &(context)->expmetering, 1);
      }
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

      // If the returned value is UCHAR_MAX, we have manual WB mode so set
      // that value for the vendor tag, otherwise disable manual WB mode.
      mode = (mode == UCHAR_MAX) ? context->wbmode : 0;

      if (context->state >= GST_STATE_READY) {
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.manualWB", "partial_mwb_mode");
        meta.update(tag_id, &mode, 1);
      }
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
    case PARAM_CAMERA_IR_MODE:
    {
      context->irmode = g_value_get_enum (value);
      if (context->state >= GST_STATE_READY) {
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.ir_led", "mode");
        meta.update(tag_id, &(context)->irmode, 1);
      }
      break;
    }
    case PARAM_CAMERA_MULTI_CAM_EXPOSURE_TIME:
    {
      g_return_if_fail (gst_value_array_get_size (value) == 2);

      context->master_exp_time =
          g_value_get_int (gst_value_array_get_value (value, 0));
      context->slave_exp_time =
          g_value_get_int (gst_value_array_get_value (value, 1));

      if (context->state >= GST_STATE_READY) {
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.multicam_exptime", "masterExpTime");
        meta.update(tag_id,
            (context->master_exp_time) > 0 ? &(context)->master_exp_time : &(context)->exptime, 1);

        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.multicam_exptime", "slaveExpTime");
        meta.update(tag_id,
            (context->slave_exp_time) > 0 ? &(context)->slave_exp_time : &(context)->exptime, 1);
      }
      break;
    }
    case PARAM_CAMERA_STANDBY:
    {
      guint8 standby = g_value_get_uint (value);
      if (context->state >= GST_STATE_READY) {
        guint tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.sensorwriteinput","SensorStandByFlag");
        meta.update(tag_id, &standby, 1);
      }
      break;
    }
    case PARAM_CAMERA_INPUT_ROI_INFO:
    {
      g_return_if_fail (context->input_roi_count != 0);

      gint32 roi_count = context->input_roi_count *4;
      gint32 crop[roi_count];
      g_return_if_fail (gst_value_array_get_size (value) ==
          static_cast<guint32>(roi_count));

      for (gint i = 0; i < roi_count; i++) {
        crop[i] = g_value_get_int (gst_value_array_get_value (value, i));
      }

      if (context->state >= GST_STATE_READY) {
        guint32 tag_id = get_vendor_tag_by_name (
            "com.qti.camera.multiROIinfo","streamROICount");
        meta.update (tag_id, &(context)->input_roi_count, 1);

        tag_id = get_vendor_tag_by_name (
            "com.qti.camera.multiROIinfo","streamROIInfo");
        meta.update (tag_id, crop, roi_count);
      }
      break;
    }
    case PARAM_CAMERA_PHYISICAL_CAMERA_SWITCH:
    {
      if (context->logical_cam_info.is_logical_cam == TRUE) {
        gint input, output;

        input = g_value_get_int (value);
        output = -1;

        // input is -1, select next valid phy camera id automatically
        // or input is in the range of [0, physical camera number], select
        // itself as output.
        if (input < -1) {
            GST_ERROR ("Invalid id (%d) for phy camera switch", input);
        } else if (input == -1) {
          context->camera_switch_info.phy_cam_id_for_switch++;
          if (context->camera_switch_info.phy_cam_id_for_switch >=
              context->logical_cam_info.phy_cam_num)
            context->camera_switch_info.phy_cam_id_for_switch = 0;

          output = context->camera_switch_info.phy_cam_id_for_switch;
          context->camera_switch_info.input_req_id = input;
        } else {
          if (input < context->logical_cam_info.phy_cam_num) {
            context->camera_switch_info.input_req_id = input;
            context->camera_switch_info.phy_cam_id_for_switch = input;
            output = input;
          } else {
            GST_ERROR ("id (%d) out of range for phy camera switch", input);
          }
        }

        if (output != -1) {
          GST_INFO ("phy camera switch target (%d)", output);

          guint tag_id = get_vendor_tag_by_name (
              "com.qti.chi.multicameraswitchControl", "activeCameraIndex");

          if (tag_id != 0) {
            guint8 val = (guint8) output;
            gint32 ret;

            ret = meta.update (tag_id, &val, 1);
            if (ret != 0) {
              GST_ERROR ("physical camera switch tag update error");
            } else {
              GST_INFO ("physical camera switch tag update success");
            }
          } else {
            GST_ERROR ("physical camera switch tag not found ");
          }
        }
      } else {
        GST_ERROR ("not logical camera, phy camera id switch not supported");
      }
      break;
    }
  }

  if (!context->slave && (context->state >= GST_STATE_READY)) {
    if (param_id == PARAM_CAMERA_VIDEO_METADATA) {
      ::camera::CameraMetadata *meta_ptr =
          (::camera::CameraMetadata *) g_value_get_pointer (value);
      recorder->SetCameraParam (context->camera_id, *meta_ptr);

      // Update all local props from external metadata
      gst_qmmf_context_update_local_props (context, meta_ptr);
    } else if (param_id == PARAM_CAMERA_SESSION_METADATA) {
      ::camera::CameraMetadata *meta_ptr =
          (::camera::CameraMetadata *) g_value_get_pointer (value);
      recorder->SetCameraSessionParam (context->camera_id, *meta_ptr);
    } else {
      recorder->SetCameraParam (context->camera_id, meta);
    }
  }
}

void
gst_qmmf_context_get_camera_param (GstQmmfContext * context, guint param_id,
    GValue * value)
{
  ::qmmf::recorder::Recorder *recorder = context->recorder;

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
    case PARAM_CAMERA_LCAC:
      g_value_set_boolean (value, context->lcac);
      break;
    case PARAM_CAMERA_EIS:
#ifndef EIS_MODES_ENABLE
      g_value_set_boolean (value, context->eis);
#else
      g_value_set_enum (value, context->eis);
#endif // EIS_MODES_ENABLE
      break;
#ifndef VHDR_MODES_ENABLE
    case PARAM_CAMERA_SHDR:
      g_value_set_boolean (value, context->shdr);
#else
    case PARAM_CAMERA_VHDR:
      g_value_set_enum (value, context->vhdr);
#endif // VHDR_MODES_ENABLE
      break;
    case PARAM_CAMERA_ADRC:
      g_value_set_boolean (value, context->adrc);
      break;
    case PARAM_CAMERA_CONTROL_MODE:
      g_value_set_enum (value, context->controlmode);
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
    case PARAM_CAMERA_SHARPNESS:
      g_value_set_int (value, context->sharpness);
      break;
    case PARAM_CAMERA_CONTRAST:
      g_value_set_int (value, context->contrast);
      break;
    case PARAM_CAMERA_SATURATION:
      g_value_set_int (value, context->saturation);
      break;
    case PARAM_CAMERA_ISO_MODE:
      g_value_set_enum (value, context->isomode);
      break;
    case PARAM_CAMERA_ISO_VALUE:
      g_value_set_int (value, context->isovalue);
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
    case PARAM_CAMERA_SENSOR_MODE:
      g_value_set_int (value, context->sensormode);
      break;
    case PARAM_CAMERA_FRC_MODE:
      g_value_set_enum (value, context->frc_mode);
      return;
    case PARAM_CAMERA_IFE_DIRECT_STREAM:
      g_value_set_boolean (value, context->ife_direct_stream);
      break;
    case PARAM_CAMERA_INPUT_ROI:
      g_value_set_boolean (value, context->input_roi_enable);
      break;
    case PARAM_CAMERA_MANUAL_WB_SETTINGS:
    {
      gchar *string = NULL;
      ::camera::CameraMetadata meta;

      if (context->state >= GST_STATE_READY)
        recorder->GetCameraParam (context->camera_id, meta);

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
    case PARAM_CAMERA_NOISE_REDUCTION:
      g_value_set_enum (value, context->nrmode);
      break;
    case PARAM_CAMERA_NOISE_REDUCTION_TUNING:
    {
      gchar *string = NULL;
      ::camera::CameraMetadata meta;

      if (context->state >= GST_STATE_READY)
        recorder->GetCameraParam (context->camera_id, meta);

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
      ::camera::CameraMetadata meta;

      if (context->state >= GST_STATE_READY)
        recorder->GetCameraParam (context->camera_id, meta);

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
      ::camera::CameraMetadata meta;

      if (context->state >= GST_STATE_READY)
        recorder->GetCameraParam (context->camera_id, meta);

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
      ::camera::CameraMetadata meta;

      if (context->state >= GST_STATE_READY)
        recorder->GetCameraParam (context->camera_id, meta);

      get_vendor_tags ("org.quic.camera.ltmDynamicContrast",
          gst_camera_ltm_data, G_N_ELEMENTS (gst_camera_ltm_data),
          context->ltmdata, &meta);
      string = gst_structure_to_string (context->ltmdata);

      g_value_set_string (value, string);
      g_free (string);
      break;
    }
    case PARAM_CAMERA_IR_MODE:
      g_value_set_enum (value, context->irmode);
      break;
    case PARAM_CAMERA_ACTIVE_SENSOR_SIZE:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, context->sensorsize.x);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, context->sensorsize.y);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, context->sensorsize.w);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, context->sensorsize.h);
      gst_value_array_append_value (value, &val);
      break;
    }
    case PARAM_CAMERA_VIDEO_METADATA:
    {
      ::camera::CameraMetadata *meta = new ::camera::CameraMetadata();

      if (context->state >= GST_STATE_READY)
        recorder->GetCameraParam (context->camera_id, *meta);

      g_value_set_pointer (value, meta);
      break;
    }
    case PARAM_CAMERA_IMAGE_METADATA:
    {
      ::camera::CameraMetadata *meta = new ::camera::CameraMetadata();

      if (context->state >= GST_STATE_READY)
        recorder->GetDefaultCaptureParam (context->camera_id, *meta);

      g_value_set_pointer (value, meta);
      break;
    }
    case PARAM_CAMERA_STATIC_METADATA:
    {
      ::camera::CameraMetadata *meta = new ::camera::CameraMetadata();

      if (context->state >= GST_STATE_READY)
        recorder->GetCameraCharacteristics (context->camera_id, *meta);

      g_value_set_pointer (value, meta);
      break;
    }
    case PARAM_CAMERA_MULTI_CAM_EXPOSURE_TIME:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, context->master_exp_time);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, context->slave_exp_time);
      gst_value_array_append_value (value, &val);

      break;
    }
    case PARAM_CAMERA_OPERATION_MODE:
      g_value_set_flags (value, context->op_mode);
      break;
    case PARAM_CAMERA_INPUT_ROI_INFO:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_message("Sensor active array size <X,Y,Width,Height> is <%d,%d,%d,%d>", \
          context->sensorsize.x,context->sensorsize.y, \
          context->sensorsize.w,context->sensorsize.h);
      g_message("Please align the ROI values and aspect ratio according to " \
          "Sensor active array size");

      for (int i = 0; i < context->input_roi_count * 4; i++) {
        g_value_set_int (&val, 0);
        gst_value_array_append_value (value, &val);
      }

      break;
    }
    case PARAM_CAMERA_PHYISICAL_CAMERA_SWITCH:
    {
      g_value_set_int (value, context->camera_switch_info.input_req_id);
      break;
    }
  }
}

void
gst_qmmf_context_update_video_param (GstPad * pad, GParamSpec * pspec,
    GstQmmfContext * context)
{
  GstQmmfSrcVideoPad *vpad = GST_QMMFSRC_VIDEO_PAD (pad);
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

  if (g_strcmp0 (pname, "framerate") == 0) {
    gfloat fps = g_value_get_double (&value);
    status = recorder->SetVideoTrackParam (vpad->session_id, vpad->id,
        ::qmmf::recorder::VideoParam::kFrameRate, &fps, sizeof (fps)
    );
  } else if (g_strcmp0 (pname, "crop") == 0) {
    ::camera::CameraMetadata meta;
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
#ifdef C2D_ENABLE
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
#endif
    status = recorder->SetCameraParam (context->camera_id, meta);
  } else {
    GST_WARNING ("Unsupported parameter '%s'!", pname);
    status = -1;
  }

  QMMFSRC_RETURN_IF_FAIL (NULL, status == 0,
      "QMMF Recorder SetVideoTrackParam/SetCameraParam Failed!");
}
