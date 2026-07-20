/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "camera_source_device.h"
#include "camera_source_utils.h"
#include <gst/gstelementfactory.h>
#include <qmmf-sdk/qmmf_recorder.h>
#include <qmmf-sdk/qmmf_recorder_params.h>
#include <qmmf-sdk/qmmf_camera_metadata.h>


namespace camera = qmmf;

#define GST_TYPE_QMMFSRC_DEVICE (gst_qmmfsrc_device_get_type())
G_DECLARE_FINAL_TYPE (GstQmmfSrcDevice, gst_qmmfsrc_device,
    GST, QMMFSRC_DEVICE, GstDevice)

struct _GstQmmfSrcDevice {
  GstDevice parent;
  guint     camera_id;
};

G_DEFINE_TYPE (GstQmmfSrcDevice, gst_qmmfsrc_device, GST_TYPE_DEVICE)

struct _GstQmmfSrcDeviceProvider {
  GstDeviceProvider parent;
};

G_DEFINE_TYPE (GstQmmfSrcDeviceProvider, gst_qmmfsrc_device_provider,
    GST_TYPE_DEVICE_PROVIDER)

#define GST_CAT_DEFAULT qmmfsrc_device_debug_category()
static GstDebugCategory *
qmmfsrc_device_debug_category (void)
{
  static gsize catgonce = 0;

  if (g_once_init_enter (&catgonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtiqmmfsrc.device", 0,
        "Camera service device enumeration");
    g_once_init_leave (&catgonce, catdone);
  }
  return (GstDebugCategory *) catgonce;
}

static void
detect_formats (gint fmt,
    gboolean *have_impldef,
    gboolean *have_yuy2,
    gboolean *have_uyvy,
    gboolean *have_jpeg,
    gboolean *have_bayer)
{
  switch (fmt) {
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
      *have_impldef = TRUE;
      break;
    case HAL_PIXEL_FORMAT_YUY2:
      *have_yuy2 = TRUE;
      break;
    case HAL_PIXEL_FORMAT_UYVY:
      *have_uyvy = TRUE;
      break;
    case HAL_PIXEL_FORMAT_BLOB:
      *have_jpeg = TRUE;
      break;
    case HAL_PIXEL_FORMAT_RAW8:
    case HAL_PIXEL_FORMAT_RAW10:
    case HAL_PIXEL_FORMAT_RAW12:
    case HAL_PIXEL_FORMAT_RAW16:
      *have_bayer = TRUE;
      break;
    default:
      break;
  }
}

guint
gst_qmmfsrc_get_num_cameras (void)
{
  ::qmmf::recorder::Recorder *recorder = NULL;
  ::qmmf::recorder::RecorderCb cbs;
  std::vector<::camera::CameraMetadata> infolist;
  gint status = 0;
  guint num_cameras = 0;

  recorder = new ::qmmf::recorder::Recorder ();
  status = recorder->Connect (cbs);
  if (status != 0) {
    GST_WARNING ("Failed to connect Recorder for camera enumeration!");
    delete recorder;
    return 0;
  }

  status = recorder->GetCamStaticInfo (infolist);
  if (status == 0)
    num_cameras = (guint) infolist.size ();
  else
    GST_WARNING ("GetCamStaticInfo failed while getting camera count!");

  recorder->Disconnect ();
  delete recorder;

  GST_DEBUG ("Number of cameras reported by Camera Service: %u", num_cameras);
  return num_cameras;
}

GstCaps *
gst_qmmfsrc_get_camera_static_caps (guint camera_id)
{
  ::qmmf::recorder::Recorder *recorder = NULL;
  ::qmmf::recorder::RecorderCb cbs;
  std::vector<::camera::CameraMetadata> infolist;
  gint status = 0;
  GstCaps *caps = NULL;

  gint min_w = G_MAXINT, max_w = 0, min_h = G_MAXINT, max_h = 0;
  gint min_fps = G_MAXINT, max_fps = 0;
  gboolean have_impldef = FALSE, have_yuy2 = FALSE, have_uyvy = FALSE;
  gboolean have_jpeg = FALSE;
  gboolean have_bayer = FALSE;

  recorder = new ::qmmf::recorder::Recorder ();
  status = recorder->Connect (cbs);
  if (status != 0) {
    GST_WARNING ("Failed to connect Recorder for camera %u static caps!",
        camera_id);
    delete recorder;
    return NULL;
  }

  status = recorder->GetCamStaticInfo (infolist);
  if (status != 0 || camera_id >= infolist.size ()) {
    GST_WARNING ("No static info for camera %u (status=%d, count=%zu)!",
        camera_id, status, infolist.size ());
    recorder->Disconnect ();
    delete recorder;
    return NULL;
  }

  ::camera::CameraMetadata *meta = &infolist[camera_id];

  // Select the stream-configuration tag to use for the MAXIMUM resolution.
  // For ULTRA_HIGH_RESOLUTION sensors the maximum resolution is reported in a
  // separate MAXIMUM_RESOLUTION configuration list.
  auto max_config_tag = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS;
#if defined(HAS_ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) && \
    defined(HAS_ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION)
  if (meta->exists (ANDROID_REQUEST_AVAILABLE_CAPABILITIES)) {
    auto cap_entry = meta->find (ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
    for (guint i = 0; i < cap_entry.count; i++) {
      if (cap_entry.data.u8[i] ==
          ANDROID_REQUEST_AVAILABLE_CAPABILITIES_ULTRA_HIGH_RESOLUTION_SENSOR) {
        max_config_tag =
            ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_MAXIMUM_RESOLUTION;
        GST_DEBUG ("Camera %u: ULTRA_HIGH_RESOLUTION sensor; using "
            "MAXIMUM_RESOLUTION configurations for max resolution", camera_id);
        break;
      }
    }
  }
#endif

  // MAX resolution + format detection from the selected configuration list.
  if (meta->exists (max_config_tag)) {
    auto entry = meta->find (max_config_tag);

    for (guint i = 0; (i + 3) < entry.count; i += 4) {
      gint fmt = entry.data.i32[i + 0];
      gint w   = entry.data.i32[i + 1];
      gint h   = entry.data.i32[i + 2];
      gint dir = entry.data.i32[i + 3];

      if (dir != ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT)
        continue;

      if (w > max_w) max_w = w;
      if (h > max_h) max_h = h;

      detect_formats (fmt, &have_impldef, &have_yuy2, &have_uyvy,
          &have_jpeg, &have_bayer);
    }
  }

  // MIN resolution + format detection always from the standard list.
  if (meta->exists (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS)) {
    auto entry = meta->find (ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);

    for (guint i = 0; (i + 3) < entry.count; i += 4) {
      gint fmt = entry.data.i32[i + 0];
      gint w   = entry.data.i32[i + 1];
      gint h   = entry.data.i32[i + 2];
      gint dir = entry.data.i32[i + 3];

      if (dir != ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT)
        continue;

      if (w < min_w) min_w = w;
      if (w > max_w) max_w = w;
      if (h < min_h) min_h = h;
      if (h > max_h) max_h = h;

      detect_formats (fmt, &have_impldef, &have_yuy2, &have_uyvy,
          &have_jpeg, &have_bayer);
    }
  }

  // Framerate range.
  if (meta->exists (ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)) {
    auto entry = meta->find (ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES);

    for (guint i = 0; (i + 1) < entry.count; i += 2) {
      gint fmin = entry.data.i32[i + 0];
      gint fmax = entry.data.i32[i + 1];
      if (fmin < min_fps) min_fps = fmin;
      if (fmax > max_fps) max_fps = fmax;
    }
  }

  recorder->Disconnect ();
  delete recorder;

  // Fallbacks.
  if (max_w == 0 || max_h == 0) {
    GST_WARNING ("Camera %u reported no output resolutions!", camera_id);
    return NULL;
  }
  if (min_w == G_MAXINT) min_w = max_w;
  if (min_h == G_MAXINT) min_h = max_h;
  if (max_fps == 0) max_fps = 30;
  min_fps = 0;

  caps = gst_caps_new_empty ();

  {
    GstStructure *s = gst_structure_new_empty ("video/x-raw");
    GValue formats = G_VALUE_INIT;
    GValue v = G_VALUE_INIT;
    g_value_init (&formats, GST_TYPE_LIST);
    g_value_init (&v, G_TYPE_STRING);

    g_value_set_string (&v, "NV12");
    gst_value_list_append_value (&formats, &v);
    g_value_set_string (&v, "NV16");
    gst_value_list_append_value (&formats, &v);
    g_value_set_string (&v, "NV12_Q08C");
    gst_value_list_append_value (&formats, &v);
    g_value_set_string (&v, "RGB");
    gst_value_list_append_value (&formats, &v);

    if (have_yuy2) {
      g_value_set_string (&v, "YUY2");
      gst_value_list_append_value (&formats, &v);
    }
    if (have_uyvy) {
      g_value_set_string (&v, "UYVY");
      gst_value_list_append_value (&formats, &v);
    }
    if (have_impldef) {
      g_value_set_string (&v, "P010_10LE");
      gst_value_list_append_value (&formats, &v);
      g_value_set_string (&v, "NV12_Q10LE32C");
      gst_value_list_append_value (&formats, &v);
    }

    g_value_unset (&v);
    gst_structure_set_value (s, "format", &formats);
    g_value_unset (&formats);

    gst_structure_set (s,
        "width",     GST_TYPE_INT_RANGE, min_w, max_w,
        "height",    GST_TYPE_INT_RANGE, min_h, max_h,
        "framerate", GST_TYPE_FRACTION_RANGE, min_fps, 1, max_fps, 1,
        NULL);

    gst_caps_append_structure (caps, s);
  }

  // image/jpeg
  if (have_jpeg) {
    gst_caps_append_structure (caps, gst_structure_new ("image/jpeg",
        "width",     GST_TYPE_INT_RANGE, min_w, max_w,
        "height",    GST_TYPE_INT_RANGE, min_h, max_h,
        "framerate", GST_TYPE_FRACTION_RANGE, min_fps, 1, max_fps, 1,
        NULL));
  }

  // video/x-bayer
  if (have_bayer) {
    GstStructure *sb = gst_structure_new_empty ("video/x-bayer");

    {
      GValue fmts = G_VALUE_INIT;
      GValue v = G_VALUE_INIT;
      g_value_init (&fmts, GST_TYPE_LIST);
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, "bggr"); gst_value_list_append_value (&fmts, &v);
      g_value_set_string (&v, "rggb"); gst_value_list_append_value (&fmts, &v);
      g_value_set_string (&v, "gbrg"); gst_value_list_append_value (&fmts, &v);
      g_value_set_string (&v, "grbg"); gst_value_list_append_value (&fmts, &v);
      g_value_set_string (&v, "mono"); gst_value_list_append_value (&fmts, &v);
      g_value_unset (&v);
      gst_structure_set_value (sb, "format", &fmts);
      g_value_unset (&fmts);
    }

    {
      GValue bpps = G_VALUE_INIT;
      GValue v = G_VALUE_INIT;
      g_value_init (&bpps, GST_TYPE_LIST);
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, "8");  gst_value_list_append_value (&bpps, &v);
      g_value_set_string (&v, "10"); gst_value_list_append_value (&bpps, &v);
      g_value_set_string (&v, "12"); gst_value_list_append_value (&bpps, &v);
      g_value_set_string (&v, "16"); gst_value_list_append_value (&bpps, &v);
      g_value_unset (&v);
      gst_structure_set_value (sb, "bpp", &bpps);
      g_value_unset (&bpps);
    }

    gst_structure_set (sb,
        "width",     GST_TYPE_INT_RANGE, min_w, max_w,
        "height",    GST_TYPE_INT_RANGE, min_h, max_h,
        "framerate", GST_TYPE_FRACTION_RANGE, min_fps, 1, max_fps, 1,
        NULL);

    gst_caps_append_structure (caps, sb);
  }

  GST_DEBUG ("Camera %u caps: %" GST_PTR_FORMAT, camera_id, caps);
  return caps;
}

static GstElement *
gst_qmmfsrc_device_create_element (GstDevice * device, const gchar * name)
{
  GstQmmfSrcDevice *self = GST_QMMFSRC_DEVICE (device);
  GstElement *element = NULL;

  element = gst_element_factory_make ("qticamsrc", name);
  if (element != NULL)
    g_object_set (element, "camera", self->camera_id, NULL);

  return element;
}

static gboolean
gst_qmmfsrc_device_reconfigure_element (GstDevice * device,
    GstElement * element)
{
  GstQmmfSrcDevice *self = GST_QMMFSRC_DEVICE (device);

  if (!GST_IS_ELEMENT (element))
    return FALSE;

  g_object_set (element, "camera", self->camera_id, NULL);
  return TRUE;
}

static void
gst_qmmfsrc_device_class_init (GstQmmfSrcDeviceClass * klass)
{
  GstDeviceClass *device_class = GST_DEVICE_CLASS (klass);

  device_class->create_element = gst_qmmfsrc_device_create_element;
  device_class->reconfigure_element = gst_qmmfsrc_device_reconfigure_element;
}

static void
gst_qmmfsrc_device_init (GstQmmfSrcDevice * self)
{
  self->camera_id = 0;
}

static GstDevice *
gst_qmmfsrc_device_new (guint camera_id, GstCaps * caps)
{
  GstQmmfSrcDevice *device = NULL;
  GstStructure *props = NULL;
  gchar *display_name = NULL;

  display_name = g_strdup_printf ("Camera %u", camera_id);

  props = gst_structure_new ("camera-properties",
      "device.api", G_TYPE_STRING, "qmmf",
      "camera-id", G_TYPE_UINT, camera_id,
      NULL);

  device = GST_QMMFSRC_DEVICE (g_object_new (GST_TYPE_QMMFSRC_DEVICE,
      "display-name", display_name,
      "device-class", "Video/Source",
      "caps", caps,
      "properties", props,
      NULL));

  device->camera_id = camera_id;

  g_free (display_name);
  gst_structure_free (props);

  return GST_DEVICE (device);
}

static GstCaps *
gst_qmmfsrc_device_provider_template_caps (void)
{
  return gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", GST_TYPE_INT_RANGE, 1, 4096,
      "height", GST_TYPE_INT_RANGE, 1, 4096,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 120, 1,
      NULL);
}

static GList *
gst_qmmfsrc_device_provider_probe (GstDeviceProvider * provider)
{
  GList *devices = NULL;
  GstCaps *caps = NULL;
  GstDevice *device = NULL;
  guint num_cameras = 0;
  guint i = 0;

  GST_DEBUG_OBJECT (provider, "Probing camera service devices");

  /* Query the real number of cameras from the camera service. */
  num_cameras = gst_qmmfsrc_get_num_cameras ();
  GST_DEBUG_OBJECT (provider, "Camera service reports %u camera(s)",
      num_cameras);

  for (i = 0; i < num_cameras; i++) {
    /* Get this camera's real supported formats and min/max resolution. */
    caps = gst_qmmfsrc_get_camera_static_caps (i);
    if (caps == NULL) {
      GST_WARNING_OBJECT (provider,
          "Falling back to template caps for camera %u", i);
      caps = gst_qmmfsrc_device_provider_template_caps ();
    }

    device = gst_qmmfsrc_device_new (i, caps);
    gst_caps_unref (caps);

    devices = g_list_append (devices, device);
  }

  /* Ultimate fallback: if the service is unavailable or returned no cameras,
   * still advertise a single device so basic detection keeps working. */
  if (devices == NULL) {
    GST_WARNING_OBJECT (provider,
        "No cameras reported; advertising a single template device");
    caps = gst_qmmfsrc_device_provider_template_caps ();
    device = gst_qmmfsrc_device_new (0, caps);
    gst_caps_unref (caps);
    devices = g_list_append (devices, device);
  }

  return devices;
}

static void
gst_qmmfsrc_device_provider_class_init (GstQmmfSrcDeviceProviderClass * klass)
{
  GstDeviceProviderClass *provider_class = GST_DEVICE_PROVIDER_CLASS (klass);

  provider_class->probe = gst_qmmfsrc_device_provider_probe;

  gst_device_provider_class_set_static_metadata (provider_class,
      "Camera Service Device Provider", "Source/Video",
      "Enumerates cameras available via the Camera Service",
      "Qualcomm Technologies, Inc.");
}

static void
gst_qmmfsrc_device_provider_init (GstQmmfSrcDeviceProvider * self)
{
}
