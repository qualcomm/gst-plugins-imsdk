/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

#include "byte-tracker/BYTETracker.h"

#include "objtracker.h"

#include <gst/gst.h>
#include <gst/utils/common-utils.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoclassificationmeta.h>
#include <gst/video/gstvideolandmarksmeta.h>

#define GST_CAT_DEFAULT gst_objtracker_debug
GST_DEBUG_CATEGORY_STATIC (gst_objtracker_debug);

#define DEFAULT_MAX_OBJECTS  100

#define gst_objtracker_parent_class parent_class
G_DEFINE_TYPE (GstObjTracker, gst_objtracker, GST_TYPE_BASE_TRANSFORM);

#define GST_OBJ_TRACKER_SINK_CAPS \
    "video/x-raw(ANY)"

#define GST_OBJ_TRACKER_SRC_CAPS \
    "video/x-raw(ANY)"

typedef struct _GstRegionMetaEntry GstRegionMetaEntry;

static GstStaticPadTemplate gst_objtracker_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_OBJ_TRACKER_SINK_CAPS)
    );
static GstStaticPadTemplate gst_objtracker_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_OBJ_TRACKER_SRC_CAPS)
    );

struct _GstRegionMetaEntry {
  // Unique ROI type/name.
  GQuark roi_type;

  // The ID and parent ID from the ROI meta.
  gint   id;
  gint   parent_id;

  // The ROI coordinates and dimensions.
  guint  x;
  guint  y;
  guint  w;
  guint  h;

  // List of GstStructure param containing derived non-ROI metas.
  GList  *params;
};

static inline const gchar *
gst_track_state_string (gint state)
{
  switch (state) {
    case TrackState::New:
      return "NEW";
    case TrackState::Tracked:
      return "TRACKED";
    case TrackState::Lost:
      return "LOST";
    case TrackState::Removed:
      return "REMOVED";
    default:
      break;
  }

  return "NEW";
}

static GstRegionMetaEntry *
gst_region_meta_entry_new (GstVideoRegionOfInterestMeta * roimeta)
{
  GstRegionMetaEntry *region = g_slice_new0 (GstRegionMetaEntry);
  GList *list = NULL;
  GstVideoRegionOfInterestMeta * rmeta = NULL;
  gpointer state = NULL;

  // Copy the ROI type/name and its IDs.
  region->roi_type = roimeta->roi_type;

  region->id = roimeta->id;
  region->parent_id = roimeta->parent_id;

  // Copy the ROI meta coordinates.
  region->x = roimeta->x;
  region->y = roimeta->y;
  region->w = roimeta->w;
  region->h = roimeta->h;

  region->params = g_steal_pointer (&(roimeta->params));
  return region;
}

static void
gst_region_meta_entry_free (GstRegionMetaEntry * region)
{
  if (region->params != NULL)
    g_list_free_full (region->params, (GDestroyNotify) gst_structure_free);

  g_slice_free (GstRegionMetaEntry, region);
}

static gboolean
gst_objtracker_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (base);

  GST_DEBUG_OBJECT (objtracker, "Output caps: %" GST_PTR_FORMAT, outcaps);
  return TRUE;
}

static GstFlowReturn
gst_objtracker_transform_ip (GstBaseTransform * base, GstBuffer * buffer)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (base);
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  GstRegionMetaEntry *region = NULL;
  gpointer state = NULL, key = NULL;
  GList *metas = NULL, *list = NULL;
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gdouble confidence = 0.0;

  std::vector<ByteTrackerObject> objects;

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  // Iterate over the metas available in the buffer and process them.
  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    GstStructure *param = NULL;
    ByteTrackerObject object;

    // Coordinates need to be as left, top, bottom, right format.
    object.bounding_box[0] = roimeta->x;
    object.bounding_box[1] = roimeta->y;
    object.bounding_box[2] = roimeta->x + roimeta->w;
    object.bounding_box[3] = roimeta->y + roimeta->h;

    param = gst_video_region_of_interest_meta_get_param (roimeta,
        "ObjectDetection");
    gst_structure_get_double (param, "confidence", &confidence);

    object.prob = confidence / 100.0;
    object.label = roimeta->id;

    objects.push_back(std::move(object));

    key = GUINT_TO_POINTER (roimeta->id);

    // Replace any older region meta entry in the hash table.
    region = gst_region_meta_entry_new (roimeta);
    g_hash_table_insert (objtracker->regions, key, region);

    metas = g_list_append (metas, roimeta);
  }

  // Remove all ROI metas from the buffer. They will be added after the tracker.
  for (list = metas; list != NULL; list = list->next)
    gst_buffer_remove_meta (buffer, GST_META_CAST (list->data));

  // Process objects.
  std::vector<STrack> stracks = objtracker->tracker->update(objects);

  for (const auto& strack : stracks) {
    GstStructure *param = NULL;

    GST_TRACE ("ROI ID [0x%X] with track ID [%d] in state %s", strack.matched_detection_id,
        strack.track_id, gst_track_state_string (strack.state));

    if (strack.state == TrackState::Removed)
      continue;

    key = GUINT_TO_POINTER (strack.matched_detection_id);
    region = (GstRegionMetaEntry*) g_hash_table_lookup (objtracker->regions, key);

    if (region == NULL)
      continue;

    roimeta = gst_buffer_add_video_region_of_interest_meta_id (buffer,
        region->roi_type, region->x, region->y, region->w, region->h);

    auto cx = (strack.tlbr[2] + strack.tlbr[0]) / 2;
    auto cy = (strack.tlbr[3] + strack.tlbr[1]) / 2;

    roimeta->x = cx - strack.smoothed_wh[0] / 2;
    roimeta->y = cy - strack.smoothed_wh[1] / 2;
    roimeta->w = strack.smoothed_wh[0];
    roimeta->h = strack.smoothed_wh[1];

    GST_TRACE ("ROI ID[0x%X] Adjusted Region [%d %d %d %d] --> [%d %d %d %d]",
        region->id, region->x, region->y, region->w, region->h, roimeta->x,
        roimeta->y, roimeta->w, roimeta->h);

    roimeta->id = region->id;
    roimeta->parent_id = region->parent_id;

    roimeta->params = g_steal_pointer (&(region->params));
    g_hash_table_remove (objtracker->regions, key);

    param = gst_video_region_of_interest_meta_get_param (roimeta,
        "ObjectDetection");
    gst_structure_set (param, "tracking-id", G_TYPE_UINT, strack.track_id, NULL);

    GST_TRACE ("ROI ID[0x%X] tracking ID[%u]", region->id, strack.track_id);
  }

  g_hash_table_remove_all (objtracker->regions);

  time = gst_util_get_timestamp ();

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (objtracker, "Process took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  return GST_FLOW_OK;
}

static void
gst_objtracker_finalize (GObject * object)
{
  GstObjTracker *objtracker = GST_OBJ_TRACKER (object);

  if (objtracker->regions != NULL)
    g_hash_table_destroy (objtracker->regions);

  delete objtracker->tracker;

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (objtracker));
}

static void
gst_objtracker_class_init (GstObjTrackerClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_objtracker_finalize);

  gst_element_class_set_static_metadata (element, "Object Tracker",
      "Filter/Effect/Converter",
      "Tracks objects throughout consecutive frames", "QTI");

  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_objtracker_sink_template));
  gst_element_class_add_pad_template (element,
      gst_static_pad_template_get (&gst_objtracker_src_template));

  base->set_caps = GST_DEBUG_FUNCPTR (gst_objtracker_set_caps);
  base->transform_ip = GST_DEBUG_FUNCPTR (gst_objtracker_transform_ip);
}

static void
gst_objtracker_init (GstObjTracker * objtracker)
{
  ByteTrackerConfig config;
  objtracker->tracker = new BYTETracker(config);

  objtracker->maxobjects = DEFAULT_MAX_OBJECTS;

  objtracker->regions = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gst_region_meta_entry_free);

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (objtracker), TRUE);
  // Set plugin to be always in-place.
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (objtracker), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_objtracker_debug, "qtiobjtracker", 0,
      "QTI object tracker plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtiobjtracker", GST_RANK_NONE,
      GST_TYPE_OBJ_TRACKER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtiobjtracker,
    "QTI object tracker plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
