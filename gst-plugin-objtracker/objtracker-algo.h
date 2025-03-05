/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_OBJTRACKER_ALGO_H__
#define __GST_OBJTRACKER_ALGO_H__

#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include <gst/allocators/allocators.h>
#include <gst/utils/common-utils.h>

G_BEGIN_DECLS
GST_DEBUG_CATEGORY_EXTERN (gst_objtracker_algo_debug);

#define GST_OBJTRACKER_ALGO_CAST(obj) ((GstObjTrackerAlgo*)(obj))

/**
 * GST_OBJTRACKER_ALGO_OPT_PARAMETERS
 *
 * #GST_TYPE_STRUCTURE: A structure containing objtracker algorithm parameters.
 *                      If NULL, will use default parameters.
 * Default: NULL
 *
 * To be used as a possible option for 'gst_objtracker_algo_configure'.
 */
#define GST_OBJTRACKER_ALGO_OPT_PARAMETERS "GstObjTrackerAlgo.parameters"

typedef struct _GstObjTrackerAlgo GstObjTrackerAlgo;


/**
 * gst_objtracker_algo_new:
 * @name: Name of the algorithm.
 *
 * Allocate an instance of Objtracker algorithm.
 * Usable only at plugin level.
 *
 * return: Pointer to Objtracker algorithm on success or NULL on failure
 */
GST_API GstObjTrackerAlgo *
gst_objtracker_algo_new (const gchar * name);

/**
 * gst_objtracker_algo_free:
 * @algo: Pointer to Objtracker algorithm.
 *
 * De-initialize and free the memory associated with the algorithm.
 * Usable only at plugin level.
 *
 * return: NONE
 */
GST_API void
gst_objtracker_algo_free (GstObjTrackerAlgo * algo);

/**
 * gst_objtracker_algo_init:
 * @algo: Pointer to Objtracker algorithm.
 *
 * Convenient wrapper function used on plugin level to call the subalgo
 * 'gst_objtracker_algo_open' API.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_objtracker_algo_init (GstObjTrackerAlgo * algo);

/**
 * gst_objtracker_algo_set_opts:
 * @algo: Pointer to Objtracker algorithm.
 * @options: Pointer to GStreamer structure containing the settings.
 *
 * Convenient wrapper function used on plugin level to call the subalgo
 * 'gst_objtracker_algo_configure' API to set various options.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_objtracker_algo_set_opts (GstObjTrackerAlgo * algo,
                              GstStructure * options);

/**
 * gst_objtracker_algo_execute_text:
 * @algo: Pointer to Objtracker algorithm.
 * @input_text: Object detection text info.
 * @output_text: Object tracker text info.
 *
 * Convenient wrapper function used on plugin level to call the subalgo
 * 'gst_objtracker_algo_process' API in order to process object detection
 * result and generate track id.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_objtracker_algo_execute_text (GstObjTrackerAlgo * algo, gchar * input_text,
    gchar ** output_text);

/**
 * gst_objtracker_algo_execute:
 * @algo: Pointer to Objtracker algorithm.
 * @buffer: Frame containing mapped tensor memory blocks that need processing.
 *
 * Convenient wrapper function used on plugin level to call the subalgo
 * 'gst_objtracker_algo_process' API in order to process object detection
 * result and generate track id.
 *
 * return: TRUE on success or FALSE on failure
 */
GST_API gboolean
gst_objtracker_algo_execute_buffer (GstObjTrackerAlgo * algo,
    GstBuffer * buffer);

G_END_DECLS

#endif // __GST_OBJTRACKER_ALGO_H__
