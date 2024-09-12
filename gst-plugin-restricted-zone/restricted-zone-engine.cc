/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <map>
#include <string>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/utils/common-utils.h>
#include <gst/video/gstvideolandmarksmeta.h>

#include <opencv2/opencv.hpp>

#include "restricted-zone-engine.h"

#define DEFAULT_MAX_RECORDS 5

#define GST_RESTRICTED_ZONE_ENGINE_CAST(obj) ((GstRestrictedZoneEngine*)(obj))

typedef std::map<std::string, std::vector<cv::Point2f>> Zones;

struct _GstRestrictedZoneEngine {
  GstVideoInfo   vinfo;

  // Mapping between restricted zone name and its dimensions.
  Zones zones;

  // Mapping between ROI ID and its distance from zone values over time.
  GHashTable     *trajectories;

  /// Properties.
  guint          maxrecords;
};

static GstVideoKeypoint *
gst_video_landmarks_get_keypoint (GArray * landmarks, const gchar * name)
{
  GstVideoKeypoint *kp = NULL;
  guint idx = 0;

  for (idx = 0; idx < landmarks->len; idx++) {
    kp = &(g_array_index (landmarks, GstVideoKeypoint, idx));

    if (kp->name == g_quark_from_string (name))
      return kp;
  }

  return NULL;
}

static gboolean
gst_min_distance_records_in_zone (GArray * records)
{
  guint idx = 0, in_zone_records = 0;
  gfloat distance = 0.0;

  for (idx = 0; idx < records->len; idx++) {
    distance = g_array_index (records, gfloat, idx);
    in_zone_records += (distance >= 0.0) ? 1 : 0;
  }

  GST_LOG ("Number of distance records in the zone: %u", in_zone_records);
  return (in_zone_records >= 3) ? TRUE : FALSE;
}

static gboolean
gst_structure_extract_restricted_zones (GQuark field_id, const GValue * value,
    gpointer userdata)
{
  GstRestrictedZoneEngine *engine = GST_RESTRICTED_ZONE_ENGINE_CAST (userdata);
  const GValue *subvalue = NULL;
  guint idx = 0, size = 0, x = 0, y = 0;

  // We are iterating all fields, skip the caps and max-records fields.
  if ((field_id == g_quark_from_static_string ("caps")) ||
      (field_id == g_quark_from_static_string ("max-records")))
    return TRUE;

  // Check if the value type of this is the expected for a zone.
  if (!GST_VALUE_HOLDS_ARRAY (value)) {
    GST_ERROR ("Field '%s' has invalid type!", g_quark_to_string (field_id));
    return FALSE;
  }

  // No entries in that value array, skip it.
  if ((size = gst_value_array_get_size (value)) < 3) {
    GST_ERROR ("Field '%s' has less then the requires 3 coordinates!",
        g_quark_to_string (field_id));
    return FALSE;
  }

  std::vector<cv::Point2f> zone;

  for (idx = 0; idx < size; idx++) {
    subvalue = gst_value_array_get_value (value, idx);

    if (!GST_VALUE_HOLDS_ARRAY (subvalue) ||
        (gst_value_array_get_size (subvalue) != 2)) {
      GST_ERROR ("Field '%s' has invalid coordinate at index %u!",
          g_quark_to_string (field_id), idx);
      return FALSE;
    }

    x = g_value_get_int (gst_value_array_get_value (subvalue, 0));
    y = g_value_get_int (gst_value_array_get_value (subvalue, 1));

    zone.push_back(cv::Point2f(x, y));

    GST_INFO ("%s: Coordinate: [%u, %u]", g_quark_to_string (field_id), x, y);
  }

  if (zone.size() < 3) {
    GST_ERROR ("Invalid number of coordinates for '%s'. Expecting at least 3, "
        "but filled only %lu!", g_quark_to_string (field_id), zone.size());
    return FALSE;
  }

  engine->zones.emplace(g_quark_to_string (field_id), zone);
  return TRUE;
}

GstRestrictedZoneEngine *
gst_restricted_zone_engine_new (GstStructure * settings)
{
  GstRestrictedZoneEngine *engine = NULL;
  GstCaps *caps = NULL;
  gboolean success = FALSE;

  engine = new GstRestrictedZoneEngine();

  engine->maxrecords = DEFAULT_MAX_RECORDS;

  if (settings == NULL) {
    GST_ERROR ("No parameters have been set!");
    goto error;
  }

  gst_structure_get (settings, "caps", GST_TYPE_CAPS, &caps, NULL);
  gst_video_info_from_caps (&(engine->vinfo), caps);
  gst_caps_unref (caps);

  if (gst_structure_has_field (settings, "max-records"))
    gst_structure_get_uint (settings, "max-records", &(engine->maxrecords));

  success = gst_structure_foreach (settings,
      gst_structure_extract_restricted_zones, engine);

  if (!success) {
    GST_ERROR ("Failed to extract restricted zones, invalid parameters!");
    goto error;
  }

  engine->trajectories = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) g_array_unref);

  return engine;

error:
  delete engine;

  return NULL;
}

void
gst_restricted_zone_engine_free (GstRestrictedZoneEngine * engine)
{

  if (NULL == engine)
    return;

  if (engine->trajectories != NULL)
    g_hash_table_destroy (engine->trajectories);

  delete engine;
}

gboolean
gst_restricted_zone_engine_process (GstRestrictedZoneEngine * engine, GstBuffer * buffer)
{
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  GList *list = NULL;
  gpointer state = NULL;

  // Iterate over the metas available in the buffer and process them.
  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    GstStructure *param = NULL;
    GArray *landmarks = NULL, *records = NULL;
    GstVideoKeypoint *l_ankle = NULL, *r_ankle = NULL;
    gpointer key = NULL;
    gfloat distance = -G_MAXFLOAT;

    if (roimeta->roi_type != g_quark_from_static_string ("person"))
      continue;

    GST_TRACE ("Received ROI meta %s and ID[0x%X]",
        g_quark_to_string (roimeta->roi_type), roimeta->id);

    param = gst_video_region_of_interest_meta_get_param (roimeta,
        "ObjectDetection");

    // If there are no ROI landmarks do not process this meta.
    if (!gst_structure_has_field (param, "landmarks"))
      continue;

    gst_structure_get (param, "landmarks", G_TYPE_ARRAY, &landmarks, NULL);

    // We require both left and right ankle landmarks.
    l_ankle = gst_video_landmarks_get_keypoint (landmarks, "left_ankle");
    r_ankle = gst_video_landmarks_get_keypoint (landmarks, "right_ankle");

    if ((l_ankle == NULL) || (r_ankle == NULL))
      continue;

    cv::Point2f l_foot(roimeta->x + l_ankle->x, roimeta->y + l_ankle->y);
    cv::Point2f r_foot(roimeta->x + r_ankle->x, roimeta->y + r_ankle->y);

    GST_TRACE ("ROI '%s' with ID[0x%X]: Left Foot [%f %f] Right Foot [%f %f]",
        g_quark_to_string (roimeta->roi_type), roimeta->id, l_foot.x, l_foot.y,
        r_foot.x, r_foot.y);

    for (auto& zone : engine->zones) {
      auto name = zone.first.c_str();
      auto polygon = zone.second;

      auto l_distance = cv::pointPolygonTest(polygon, l_foot, true);
      auto r_distance = cv::pointPolygonTest(polygon, r_foot, true);

      GST_LOG ("Distance of ROI '%s' with ID[0x%X] from '%s': Left Foot [%f] "
          "Right Foot [%f]", g_quark_to_string (roimeta->roi_type), roimeta->id,
          name, l_distance, r_distance);

      distance = MAX (distance, MAX (l_distance, r_distance));

      GST_DEBUG ("Distance of ROI '%s' with ID[0x%X] from '%s': %f",
          g_quark_to_string (roimeta->roi_type), roimeta->id, name, distance);
    }

    // Fetch the distance from zone records for this ROI meta.
    key = GUINT_TO_POINTER (roimeta->id);
    records = (GArray*) g_hash_table_lookup (engine->trajectories, key);

    // No distance records for this ROI meta, create a new records list.
    if (records == NULL) {
      records = g_array_new (FALSE, FALSE, sizeof (gfloat));
      g_hash_table_insert (engine->trajectories, key, records);
    }

    records = g_array_append_val (records, distance);

    // Records exceed maximum depth, discard old entires.
    while (records->len > engine->maxrecords)
      records = g_array_remove_index (records, 0);

    if (gst_min_distance_records_in_zone (records))
      gst_structure_set (param, "color", G_TYPE_UINT, 0xFF0000FF, NULL);
  }

  return TRUE;
}
