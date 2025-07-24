/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include "objtracker-data.h"
#include "objtracker-algo.h"

#define GST_CAT_DEFAULT gst_objtracker_algo_debug
GST_DEBUG_CATEGORY (gst_objtracker_algo_debug);

#define OBJTRACKER_ALGO_CREATE_FUNC    "TrackerAlgoCreate"
#define OBJTRACKER_ALGO_EXECUTE_FUNC   "TrackerAlgoExecute"
#define OBJTRACKER_ALGO_DELETE_FUNC    "TrackerAlgoDelete"

/**
 * TrackerAlgoCreate:
 * @params: Paramters for objtracker algorithm.
 *
 * Create a new instance of the Objtracker algorithm structure.
 *
 * Objtracker algorithm must implement function called
 * 'TrackerAlgoCreate' with the same arguments and return types.
 *
 * return: Pointer to algorithm instance on success or NULL on failure
 */
typedef void *(*TrackerAlgoCreate) (ParameterTypeMap params);

/**
 * TrackerAlgoExecute:
 * @tracker: Pointer to algorithm instance.
 * @data: Input data for algorithm.
 *
 * Parses object detection result and generate tracking ID for each bbox.
 *
 * Objtracker algorithm must implement function called
 * 'TrackerAlgoExecute' with the same arguments.
 *
 * return: Output data from algorithm.
 */
typedef std::vector<TrackerAlgoOutputData> (*TrackerAlgoExecute)
    (void *tracker, std::vector<TrackerAlgoInputData> data);

/**
 * TrackerAlgoDelete:
 * @tracker: Pointer to algorithm instance.
 *
 * Delete algorithm instance.
 *
 * Objtracker algorithm algo must implement function called
 * 'TrackerAlgoDelete' with the same arguments.
 *
 * return: TRUE on success or FALSE on failure
 */
typedef void (*TrackerAlgoDelete) (void *tracker);

/**
 * _GstObjTrackerAlgo:
 * @handle: Library handle.
 * @name: Library (Algorithm) name.
 * @subalgo: Pointer to private algorithm structure.
 * @roiregions: Pointer to roi metadata hash table.
 * @bboxregions: Pointer to bbox hash table.
 *
 * @algocreate: Function pointer to the subalgo 'TrackerAlgoCreate' API.
 * @algoexecute: Function pointer to the subalgo 'TrackerAlgoExecute' API.
 * @algodelete: Function pointer to the subalgo 'TrackerAlgoDelete' API.
 *
 * Tracker algorithm interface for object tracker algorithm.
 */
struct _GstObjTrackerAlgo {
  gpointer                  handle;
  gchar                     *name;
  gpointer                  subalgo;
  GHashTable                *roiregions;
  GHashTable                *bboxregions;

  /// Interface functions.
  TrackerAlgoCreate         algocreate;
  TrackerAlgoExecute        algoexecute;
  TrackerAlgoDelete         algodelete;
};

typedef struct _GstRegionMetaEntry GstRegionMetaEntry;
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

GstRegionMetaEntry *
gst_region_meta_entry_new (GstVideoRegionOfInterestMeta * roimeta)
{
  GstRegionMetaEntry *region = g_slice_new0 (GstRegionMetaEntry);

  // Copy the ROI type/name and its IDs.
  region->roi_type = roimeta->roi_type;

  region->id = roimeta->id;
  region->parent_id = roimeta->parent_id;

  // Copy the ROI meta coordinates.
  region->x = roimeta->x;
  region->y = roimeta->y;
  region->w = roimeta->w;
  region->h = roimeta->h;

  region->params = (GList *) g_steal_pointer (&(roimeta->params));

  return region;
}

void
gst_region_meta_entry_free (GstRegionMetaEntry * region)
{
  if (region->params != NULL)
    g_list_free_full (region->params, (GDestroyNotify) gst_structure_free);

  g_slice_free (GstRegionMetaEntry, region);
}

static inline void
gst_objtracker_algo_init_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    GST_DEBUG_CATEGORY_INIT (gst_objtracker_algo_debug,
        "objtracker-algo", 0, "QTI object tracker Algo ");
    g_once_init_leave (&catonce, TRUE);
  }
}

static gboolean
gst_objtracker_algo_symbol (gpointer handle, const gchar * name,
    gpointer * symbol)
{
  *(symbol) = dlsym (handle, name);
  if (NULL == *(symbol)) {
    GST_ERROR ("Failed to link library method %s, error: %s!", name,
        dlerror());
    return FALSE;
  }

  return TRUE;
}

GstObjTrackerAlgo *
gst_objtracker_algo_new (const gchar * name)
{
  GstObjTrackerAlgo *algo = NULL;
  gchar *location = NULL;
  gboolean success = TRUE;

  // Initialize the debug category.
  gst_objtracker_algo_init_debug_category ();

  algo = g_new0 (GstObjTrackerAlgo, 1);
  location = g_strdup_printf ("%s/libobjtracker-%s.so",
      GST_QTI_OBJTRACKER_ALGORITHM, name);

  algo->name = g_strdup (name);
  algo->handle = dlopen (location, RTLD_NOW);

  g_free (location);

  if (algo->handle == NULL) {
    GST_ERROR ("Failed to open %s library, error: %s!", algo->name, dlerror());
    gst_objtracker_algo_free (algo);
    return NULL;
  }

  success &= gst_objtracker_algo_symbol (algo->handle,
      OBJTRACKER_ALGO_CREATE_FUNC,
      (gpointer *) &(algo)->algocreate);
  success &= gst_objtracker_algo_symbol (algo->handle,
      OBJTRACKER_ALGO_EXECUTE_FUNC,
      (gpointer *) &(algo)->algoexecute);
  success &= gst_objtracker_algo_symbol (algo->handle,
      OBJTRACKER_ALGO_DELETE_FUNC,
      (gpointer *) &(algo)->algodelete);

  if (!success) {
    gst_objtracker_algo_free (algo);
    return NULL;
  }

  GST_INFO ("Created %s algo: %p", algo->name, algo);
  return algo;
}

void
gst_objtracker_algo_free (GstObjTrackerAlgo * algo)
{
  if (NULL == algo)
    return;

  if (algo->subalgo != NULL)
    algo->algodelete (algo->subalgo);

  if (algo->roiregions != NULL)
    g_hash_table_destroy (algo->roiregions);

  if (algo->bboxregions != NULL)
    g_hash_table_destroy (algo->bboxregions);

  if (algo->handle != NULL)
    dlclose (algo->handle);

  GST_INFO ("Destroyed %s algorithm: %p", algo->name, algo);

  if (algo->name != NULL)
    g_free (algo->name);

  g_free (algo);

  return;
}

gboolean
gst_objtracker_algo_init (GstObjTrackerAlgo * algo)
{
  g_return_val_if_fail (algo != NULL, FALSE);

  algo->roiregions = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gst_region_meta_entry_free);

  algo->bboxregions = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) gst_structure_free);

  return TRUE;
}

gboolean
gst_objtracker_algo_set_opts (GstObjTrackerAlgo * algo,
    GstStructure * options)
{
  gboolean success = TRUE;
  gdouble value;
  GstStructure *parameters = NULL;
  std::map<std::string, ParameterType> params;
  const GValue *frame_rate = NULL, *track_buffer = NULL,
      *wh_smooth_factor = NULL, *track_thresh = NULL,
      *high_thresh = NULL;

  g_return_val_if_fail (algo != NULL, FALSE);

  if (options != NULL) {
    success = gst_structure_has_field (options,
        GST_OBJTRACKER_ALGO_OPT_PARAMETERS);

    parameters = GST_STRUCTURE (g_value_get_boxed (
        gst_structure_get_value (options,
        GST_OBJTRACKER_ALGO_OPT_PARAMETERS)));
    if (!(success = gst_structure_has_field (parameters, "frame-rate"))) {
      GST_ERROR ("Missing bytetrack rate value paramter!");
      goto cleanup;
    } else if (!(success = gst_structure_has_field (parameters,
        "track-buffer"))) {
      GST_ERROR ("Missing bytetrack track buffer paramter!");
      goto cleanup;
    } else if (!(success = gst_structure_has_field (parameters,
        "wh-smooth-factor"))) {
      GST_ERROR ("Missing bytetrack wh smooth factor paramter!");
      goto cleanup;
    } else if (!(success = gst_structure_has_field (parameters,
        "track-thresh"))) {
      GST_ERROR ("Missing bytetrack track thresh paramter!");
      goto cleanup;
    } else if (!(success = gst_structure_has_field (parameters,
        "high-thresh"))) {
      GST_ERROR ("Missing bytetrack high thresh paramter!");
      goto cleanup;
    }

    frame_rate = gst_structure_get_value (parameters, "frame-rate");
    track_buffer = gst_structure_get_value (parameters, "track-buffer");
    wh_smooth_factor =
        gst_structure_get_value (parameters, "wh-smooth-factor");
    track_thresh = gst_structure_get_value (parameters, "track-thresh");
    high_thresh = gst_structure_get_value (parameters, "high-thresh");

    if (!(success = (gst_value_array_get_size (frame_rate) == 1))) {
      GST_ERROR ("Expecting %u frame-rate entries but received "
          "%u!", 1, gst_value_array_get_size (frame_rate));
      goto cleanup;
    } else if (!(success = (gst_value_array_get_size (track_buffer) == 1))) {
      GST_ERROR ("Expecting %u track-buffer entries but received "
          "%u!", 1, gst_value_array_get_size (track_buffer));
      goto cleanup;
    } else if (!(success =
        (gst_value_array_get_size (wh_smooth_factor) == 1))) {
      GST_ERROR ("Expecting %u wh_smooth_factor entries but received "
          "%u!", 1, gst_value_array_get_size (wh_smooth_factor));
      goto cleanup;
    } else if (!(success = (gst_value_array_get_size (track_thresh) == 1))) {
      GST_ERROR ("Expecting %u track_thresh entries but received "
          "%u!", 1, gst_value_array_get_size (track_thresh));
      goto cleanup;
    } else if (!(success = (gst_value_array_get_size (high_thresh) == 1))) {
      GST_ERROR ("Expecting %u high_thresh entries but received "
          "%u!", 1, gst_value_array_get_size (high_thresh));
      goto cleanup;
    }

    params.emplace("frame-rate",
        g_value_get_int (gst_value_array_get_value (frame_rate, 0)));
    params.emplace("track-buffer",
        g_value_get_int (gst_value_array_get_value (track_buffer, 0)));
    value = g_value_get_double (
        gst_value_array_get_value (wh_smooth_factor, 0));
    params.emplace("wh-smooth-factor", (float)value);
    value = g_value_get_double (gst_value_array_get_value (track_thresh, 0));
    params.emplace("track-thresh", (float)value);
    value = g_value_get_double (gst_value_array_get_value (high_thresh, 0));
    params.emplace("high-thresh", (float)value);
  }

  algo->subalgo = algo->algocreate(params);
  if (algo->subalgo == NULL)
    goto cleanup;

  return TRUE;

cleanup:
  return FALSE;
}

gboolean
gst_objtracker_algo_execute_text (GstObjTrackerAlgo * algo,
    gchar * input_text, gchar ** output_text)
{
  TrackerAlgoInputData item;
  gpointer key = NULL;
  std::vector<TrackerAlgoInputData> data;
  std::vector<TrackerAlgoOutputData> results;
  GValue list = G_VALUE_INIT;
  gboolean success = FALSE;
  const GValue *bboxes = NULL, *val = NULL;
  GValue array = G_VALUE_INIT, value = G_VALUE_INIT;
  GValue trackerbboxes = G_VALUE_INIT;
  GstStructure *structure = NULL, *entry = NULL;
  GstStructure *region = NULL, *trackerregion = NULL;
  gdouble confidence = 0.0;
  guint size = 0, idx = 0, id = 0;

  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&array, GST_TYPE_ARRAY);
  g_value_init (&trackerbboxes, GST_TYPE_ARRAY);

  success = gst_value_deserialize (&list, input_text);
  if (!success) {
    GST_ERROR ("Failed to deserialize input data!");
    goto cleanup;
  }

  if (gst_value_list_get_size (&list) == 0) {
    GST_ERROR ("Input contains no data!");
    return FALSE;
  }

  // TODO: Add support for more than one element in the list
  val = gst_value_list_get_value (&list, 0);
  structure = GST_STRUCTURE (g_value_get_boxed (val));

  bboxes = gst_structure_get_value (structure, "bounding-boxes");
  if ((size = gst_value_array_get_size (bboxes)) == 0) {
    GST_INFO ("There are no bounding-boxes!");
    goto cleanup;
  }

  for (idx = 0; idx < size; idx++) {
    val = gst_value_array_get_value (bboxes, idx);
    entry = GST_STRUCTURE (g_value_get_boxed (val));

    val = gst_structure_get_value (entry, "rectangle");
    item.x = g_value_get_float (gst_value_array_get_value (val, 0));
    item.y = g_value_get_float (gst_value_array_get_value (val, 1));
    item.w = g_value_get_float (gst_value_array_get_value (val, 2));
    item.h = g_value_get_float (gst_value_array_get_value (val, 3));

    gst_structure_get_uint (entry, "id", &id);
    item.detection_id = id;
    gst_structure_get_double (entry, "confidence", &confidence);
    item.prob = confidence;

    key = GUINT_TO_POINTER (id);
    region = gst_structure_copy (entry);
    g_hash_table_insert (algo->bboxregions, key, region);

    data.push_back(item);
  }

  //remove bounding-boxes
  gst_structure_remove_field (structure, "bounding-boxes");

  results = algo->algoexecute (algo->subalgo, data);

  for (size_t i = 0; i < results.size(); i++) {
    key = GUINT_TO_POINTER (results[i].matched_detection_id);
    region = (GstStructure *) g_hash_table_lookup (algo->bboxregions,
        key);

    if (region == NULL)
      continue;

    g_value_init (&value, G_TYPE_FLOAT);
    trackerregion = gst_structure_copy (region);

    gst_structure_remove_field (trackerregion, "rectangle");
    g_value_set_float (&value, results[i].x);
    gst_value_array_append_value (&array, &value);
    g_value_set_float (&value, results[i].y);
    gst_value_array_append_value (&array, &value);
    g_value_set_float (&value, results[i].w);
    gst_value_array_append_value (&array, &value);
    g_value_set_float (&value, results[i].h);
    gst_value_array_append_value (&array, &value);
    gst_structure_set_value (trackerregion, "rectangle", &array);
    g_value_reset (&array);

    gst_structure_set (trackerregion, "tracking-id", G_TYPE_UINT,
        results[i].track_id, NULL);

    g_value_unset (&value);
    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_take_boxed (&value, trackerregion);
    gst_value_array_append_value (&trackerbboxes, &value);
    g_value_unset (&value);

    g_hash_table_remove (algo->bboxregions, key);
  }

  gst_structure_set_value (structure, "bounding-boxes", &trackerbboxes);

cleanup:
  *output_text = gst_value_serialize (&list);

  g_value_unset (&list);
  g_value_unset (&array);
  g_value_unset (&trackerbboxes);

  g_hash_table_remove_all (algo->bboxregions);

  return (*output_text != NULL) ? TRUE : FALSE;
}

gboolean
gst_objtracker_remove_roimeta (GstBuffer * buffer, GstMeta ** meta,
    gpointer user_data)
{
  if ((*meta)->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE)
    *meta = NULL;

  return TRUE;
}

gboolean
gst_objtracker_algo_execute_buffer (GstObjTrackerAlgo * algo,
    GstBuffer * buffer)
{
  GstVideoRegionOfInterestMeta *roimeta = NULL;
  gpointer state = NULL, key = NULL;
  TrackerAlgoInputData item;
  std::vector<TrackerAlgoInputData> data;
  std::vector<TrackerAlgoOutputData> results;
  GstStructure *param = NULL;
  GstRegionMetaEntry *region = NULL;
  gdouble confidence = 0.0;

  while ((roimeta = GST_BUFFER_ITERATE_ROI_METAS (buffer, state)) != NULL) {
    item.x = roimeta->x;
    item.y = roimeta->y;
    item.w = roimeta->w;
    item.h = roimeta->h;
    item.detection_id = roimeta->id;

    param = gst_video_region_of_interest_meta_get_param (roimeta,
        "ObjectDetection");
    gst_structure_get_double (param, "confidence", &confidence);
    item.prob = confidence;

    key = GUINT_TO_POINTER (roimeta->id);
    region = gst_region_meta_entry_new (roimeta);
    g_hash_table_insert (algo->roiregions, key, region);

    data.push_back(item);
  }

  //remove origin ROI metas
  gst_buffer_foreach_meta (buffer, gst_objtracker_remove_roimeta, NULL);

  results = algo->algoexecute (algo->subalgo, data);

  for (size_t i = 0; i < results.size(); i++) {
    key = GUINT_TO_POINTER (results[i].matched_detection_id);
    region = (GstRegionMetaEntry *) g_hash_table_lookup (algo->roiregions, key);

    if (region == NULL)
      continue;

    roimeta = gst_buffer_add_video_region_of_interest_meta_id (buffer,
        region->roi_type, region->x, region->y, region->w, region->h);

    roimeta->x = results[i].x;
    roimeta->y = results[i].y;
    roimeta->w = results[i].w;
    roimeta->h = results[i].h;
    roimeta->id = region->id;
    roimeta->parent_id = region->parent_id;
    roimeta->params = (GList *) g_steal_pointer (&(region->params));
    g_hash_table_remove (algo->roiregions, key);

    param = gst_video_region_of_interest_meta_get_param (roimeta,
        "ObjectDetection");
    gst_structure_set (param, "tracking-id", G_TYPE_UINT, results[i].track_id,
        NULL);
  }

  g_hash_table_remove_all (algo->roiregions);

  return TRUE;
}
