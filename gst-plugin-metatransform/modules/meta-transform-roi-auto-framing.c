/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <math.h>

#include <gst/gst.h>
#include <gst/utils/common-utils.h>
#include <gst/video/video.h>
#include <gst/video/video-utils.h>

#include "auto-framing-stabilization/include/auto-framing-alg.h"

#define GST_CAT_DEFAULT gst_meta_module_debug
GST_DEBUG_CATEGORY_STATIC (gst_meta_module_debug);

#define GST_META_SUB_MODULE_CAST(obj) ((GstMetaSubModule*)(obj))

typedef struct _GstMetaSubModule GstMetaSubModule;

struct _GstMetaSubModule {
  AutoFramingAlgo *algo;
  AutoFramingConfig config;
  guint filter_size;
  guint filter_average_size;
  guint pos_threshold;
  guint dims_threshold;
  guint pos_moving_threshold;
  guint dims_moving_threshold;
  guint movement_speed;
  guint max_move_step;
  gfloat max_crop_ratio;
  gboolean first_rect_start;
};

gpointer
gst_meta_module_open (GstStructure * settings)
{
  GstMetaSubModule *submodule = NULL;

  GST_DEBUG_CATEGORY_GET (gst_meta_module_debug, "meta-transform-module");

  submodule = g_slice_new0 (GstMetaSubModule);
  g_return_val_if_fail (submodule != NULL, NULL);

  submodule->filter_size = AFR_DEFAULT_FILTER_SIZE;
  submodule->filter_average_size = AFR_FILTER_AVERAGE_SIZE;
  submodule->pos_threshold = AFR_DEFAULT_POS_THRESHOLD;
  submodule->dims_threshold = AFR_DEFAULT_SIZE_THRESHOLD;
  submodule->pos_moving_threshold = AFR_DEFAULT_POS_MOVING_THRESHOLD_PERCENT;
  submodule->dims_moving_threshold = AFR_DEFAULT_SIZE_MOVING_THRESHOLD_PERCENT;
  submodule->movement_speed = AFR_DEFAULT_SPEED_MOVEMENT;
  submodule->max_move_step = AFR_DEFAULT_MAX_MOVE_STEP;
  submodule->max_crop_ratio = AFR_DEFAULT_MAX_CROP_RATIO;
  submodule->first_rect_start = AFR_DEFAULT_FIRST_RECT_START;

  submodule->config.in_width   = 0;
  submodule->config.in_height  = 0;
  submodule->config.out_width  = 0;
  submodule->config.out_height = 0;

  if (settings != NULL) {
    if (gst_structure_has_field (settings, "filter-size"))
      gst_structure_get_uint (settings, "filter-size", &(submodule->filter_size));

    if (gst_structure_has_field (settings, "filter-average-size"))
      gst_structure_get_uint (settings, "filter-average-size",
          &(submodule->filter_average_size));

    if (gst_structure_has_field (settings, "pos-threshold"))
      gst_structure_get_uint (settings, "pos-threshold", &(submodule->pos_threshold));

    if (gst_structure_has_field (settings, "dims-threshold"))
      gst_structure_get_uint (settings, "dims-threshold", &(submodule->dims_threshold));

    if (gst_structure_has_field (settings, "pos-moving-threshold"))
      gst_structure_get_uint (settings, "pos-moving-threshold",
          &(submodule->pos_moving_threshold));

    if (gst_structure_has_field (settings, "dims-moving-threshold"))
      gst_structure_get_uint (settings, "dims-moving-threshold",
          &(submodule->dims_moving_threshold));

    if (gst_structure_has_field (settings, "movement-speed"))
      gst_structure_get_uint (settings, "movement-speed", &(submodule->movement_speed));

    if (gst_structure_has_field (settings, "max-move-step"))
      gst_structure_get_uint (settings, "max-move-step", &(submodule->max_move_step));

    if (gst_structure_has_field (settings, "max-crop-ratio")) {
      gdouble ratio = 0.0;
      gst_structure_get_double (settings, "max-crop-ratio", &ratio);
      submodule->max_crop_ratio = (gfloat) ratio;
    }

    if (gst_structure_has_field (settings, "first-rect-start"))
      gst_structure_get_boolean (settings, "first-rect-start", &(submodule->first_rect_start));

    if (gst_structure_has_field (settings, "in-width"))
      gst_structure_get_int (settings, "in-width", &(submodule->config.in_width));

    if (gst_structure_has_field (settings, "in-height"))
      gst_structure_get_int (settings, "in-height", &(submodule->config.in_height));

    if (gst_structure_has_field (settings, "out-width"))
      gst_structure_get_int (settings, "out-width", &(submodule->config.out_width));

    if (gst_structure_has_field (settings, "out-height"))
      gst_structure_get_int (settings, "out-height", &(submodule->config.out_height));
  }

  submodule->algo = NULL;

  return (gpointer) submodule;
}

void
gst_meta_module_close (gpointer instance)
{
  GstMetaSubModule *submodule = GST_META_SUB_MODULE_CAST (instance);

  if (NULL == submodule)
    return;

  if (submodule->algo != NULL) {
    auto_framing_algo_free (submodule->algo);
    submodule->algo = NULL;
  }

  g_slice_free (GstMetaSubModule, submodule);
}

static gboolean
gst_meta_module_init_algo (GstMetaSubModule * submodule, GstBuffer * buffer)
{
  GstVideoMeta *vmeta = gst_buffer_get_video_meta (buffer);

  if (vmeta != NULL) {
    if (submodule->config.in_width == 0)
      submodule->config.in_width = (gint) vmeta->width;
    if (submodule->config.in_height == 0)
      submodule->config.in_height = (gint) vmeta->height;
    if (submodule->config.out_width == 0)
      submodule->config.out_width = (gint) vmeta->width;
    if (submodule->config.out_height == 0)
      submodule->config.out_height = (gint) vmeta->height;
  } else if (submodule->config.in_width == 0 || submodule->config.in_height == 0) {
    GST_DEBUG ("Buffer carries no GstVideoMeta and no dimensions configured "
        "— deferring algorithm initialisation to next frame.");
    return FALSE;
  }

  if (submodule->config.out_width == 0)
    submodule->config.out_width = submodule->config.in_width;
  if (submodule->config.out_height == 0)
    submodule->config.out_height = submodule->config.in_height;

  submodule->algo = auto_framing_algo_new (submodule->config);
  if (submodule->algo == NULL) {
    GST_ERROR ("Failed to create auto-framing algorithm instance");
    return FALSE;
  }

  auto_framing_algo_set_filter_size (submodule->algo, submodule->filter_size);
  auto_framing_algo_set_filter_average_size (submodule->algo,
      submodule->filter_average_size);
  auto_framing_algo_set_position_threshold (submodule->algo,
      submodule->pos_threshold);
  auto_framing_algo_set_dims_threshold (submodule->algo,
      submodule->dims_threshold);
  auto_framing_algo_set_position_moving_threshold (submodule->algo,
      submodule->pos_moving_threshold);
  auto_framing_algo_set_dims_moving_threshold (submodule->algo,
      submodule->dims_moving_threshold);
  auto_framing_algo_set_movement_speed (submodule->algo,
      submodule->movement_speed);
  auto_framing_algo_set_max_move_step (submodule->algo,
      submodule->max_move_step);
  auto_framing_algo_set_max_crop (submodule->algo, submodule->max_crop_ratio);
  auto_framing_algo_set_first_rect_start (submodule->algo,
      submodule->first_rect_start);

  GST_DEBUG ("Auto-framing algorithm initialised: in=%dx%d  out=%dx%d",
      submodule->config.in_width, submodule->config.in_height,
      submodule->config.out_width, submodule->config.out_height);

  return TRUE;
}

gboolean
gst_meta_module_process (gpointer instance, GstBuffer * buffer)
{
  GstMetaSubModule *submodule = GST_META_SUB_MODULE_CAST (instance);
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  GstStructure *objparam = NULL;
  VideoRectangle input_rect = {0};
  gpointer state = NULL;
  gboolean found_person = FALSE;
  gint afr_sx = 0, afr_sy = 0;
  gint afr_sw, afr_sh;
  gint clamp_w, clamp_h;

  if (NULL == submodule)
    return FALSE;

  if (submodule->algo == NULL)
    gst_meta_module_init_algo (submodule, buffer);

  afr_sw = submodule->config.in_width;
  afr_sh = submodule->config.in_height;

  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    if (roimeta->roi_type != g_quark_from_static_string ("person"))
      continue;

    input_rect.x = (gint) roimeta->x;
    input_rect.y = (gint) roimeta->y;
    input_rect.w = (gint) roimeta->w;
    input_rect.h = (gint) roimeta->h;
    found_person = TRUE;
    break;
  }

  if (!found_person) {
    /* No person detected — resolve frame dimensions for the
     * full-FOV fallback */
    gint fov_w = submodule->config.in_width;
    gint fov_h = submodule->config.in_height;

    if (fov_w <= 0 || fov_h <= 0) {
      GstVideoMeta *vmeta = gst_buffer_get_video_meta (buffer);
      if (vmeta != NULL) {
        fov_w = (gint) vmeta->width;
        fov_h = (gint) vmeta->height;
      }
    }

    if (fov_w <= 0 || fov_h <= 0) {
      GST_DEBUG ("No person ROI and frame dimensions unknown — "
          "relying on qtivsplit no-ROI fallback for full-FOV display.");
      return TRUE;
    }

    input_rect.x = 0;
    input_rect.y = 0;
    input_rect.w = fov_w;
    input_rect.h = fov_h;
  }

  if (submodule->algo == NULL) {
    GST_DEBUG ("Auto-framing algorithm not yet initialised — deferring frame.");
    return TRUE;
  }

  {
    VideoRectangleF output_rectf;

    clamp_w = found_person ? submodule->config.in_width  : input_rect.w;
    clamp_h = found_person ? submodule->config.in_height : input_rect.h;

    GST_TRACE ("Auto-framing input ROI: x=%d, y=%d, w=%d, h=%d",
        input_rect.x, input_rect.y, input_rect.w, input_rect.h);

    output_rectf = auto_framing_algo_float_process (submodule->algo,
        &input_rect);

    afr_sx = (gint) roundf (output_rectf.x);
    afr_sy = (gint) roundf (output_rectf.y);
    afr_sw = (gint) roundf (output_rectf.w);
    afr_sh = (gint) roundf (output_rectf.h);

    afr_sx = CLAMP (afr_sx, 0, clamp_w - 1);
    afr_sy = CLAMP (afr_sy, 0, clamp_h - 1);
    afr_sw = CLAMP (afr_sw, 1, clamp_w - afr_sx);
    afr_sh = CLAMP (afr_sh, 1, clamp_h - afr_sy);

    GST_TRACE ("Auto-framing output ROI: x=%d, y=%d, w=%d, h=%d "
        "(float: x=%.2f, y=%.2f, w=%.2f, h=%.2f)",
        afr_sx, afr_sy, afr_sw, afr_sh,
        output_rectf.x, output_rectf.y, output_rectf.w, output_rectf.h);
  }

  {
    GstVideoRegionOfInterestMeta *afr_meta =
        gst_buffer_add_video_region_of_interest_meta (buffer,
            "auto-framing",
            (guint) afr_sx, (guint) afr_sy,
            (guint) afr_sw, (guint) afr_sh);

    if (afr_meta != NULL) {
      afr_meta->parent_id = -1;

      objparam = gst_structure_new ("ObjectDetection",
          "label",        G_TYPE_STRING, "auto-framing",
          "stabilized-x", G_TYPE_INT,    afr_sx,
          "stabilized-y", G_TYPE_INT,    afr_sy,
          "stabilized-w", G_TYPE_INT,    afr_sw,
          "stabilized-h", G_TYPE_INT,    afr_sh,
          NULL);
      gst_video_region_of_interest_meta_add_param (afr_meta, objparam);
    } else {
      GST_WARNING ("Failed to add auto-framing ROI meta to buffer!");
    }
  }

  {
    GstMeta *meta = NULL;
    gpointer rm_state = NULL;
    GQuark afr_q = g_quark_from_static_string ("auto-framing");

    while ((meta = gst_buffer_iterate_meta_filtered (buffer, &rm_state,
            GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE)) != NULL) {
      GstVideoRegionOfInterestMeta *roi = (GstVideoRegionOfInterestMeta *) meta;
      if (roi->roi_type != afr_q) {
        gst_buffer_remove_meta (buffer, meta);
        rm_state = NULL;
      }
    }
  }

  return TRUE;
}
