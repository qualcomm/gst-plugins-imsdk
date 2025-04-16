/**
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
* Application:
* Camera per port grouping
*
* Description:
* This application demonstrates the camera per port grouping
*
* Usage:
* gst-camera-per-port-example
*
*/

#include <iostream>
#include <sstream>
#include <vector>
#include <unordered_map>

#include <glib-unix.h>
#include <gst/gst.h>

#include <qmmf-sdk/qmmf_camera_metadata.h>
#include <qmmf-sdk/qmmf_vendor_tag_descriptor.h>

namespace camera = qmmf;

#define MAXGROUPEDCAMERAS      4
#define MAXISPGROUPS           10
#define MAXCONTEXTIDPERCAMERA  1
#define MAXSTREAMCONFIGS       4

#define DASH_LINE   "----------------------------------------------------------------------"
#define SPACE       "                                                                      "
#define HASH_LINE  "##################################################"
#define EQUAL_LINE "=================================================="

#define TERMINATE_MESSAGE      "APP_TERMINATE_MSG"
#define STDIN_MESSAGE          "APP_STDIN_MSG"
#define QUIT_OPTION            "q"

#define APPEND_MENU_HEADER(string) \
  g_string_append_printf (string, "\n\n%.*s MENU %.*s\n\n", \
      37, HASH_LINE, 37, HASH_LINE);

#define APPEND_CAMERA_ON_OFF_CONTROLS_SECTION(string) \
  g_string_append_printf (string, " %.*s Camera ON/OFF Controls %.*s\n", \
      30, EQUAL_LINE, 30, EQUAL_LINE);

#define GST_PER_PORT_CONTEXT_CAST(obj)           ((GstPerPortCtx*)(obj))
#define GST_APP_CONTEXT_CAST(obj)                ((GstAppContext*)(obj))

typedef struct _GstPerPortCtx GstPerPortCtx;
typedef struct _GstAppContext GstAppContext;

// Contains pipeline context information
struct _GstPerPortCtx
{
  // Pointer to the pipeline
  GstElement *pipeline;

  // Pointer to the mainloop
  GMainLoop *mloop;

  // Camera Id
  gint camera;

  // Pipeline active status
  gint active;

  // Pipeline name
  const gchar *pipe_name;

  // Guard against potential non atomic access to members
  GMutex *lock;

  // Total instantiated pipes
  guint *refcount;

  // Reconfigure if group info is changed
  gboolean reconfig;
};

struct _GstAppContext
{
  std::vector < GstPerPortCtx > ctx;
  // Asynchronous queue thread communication.
  GAsyncQueue *messages;
};

// Structure describing the grouped Cameras for same ISP
typedef struct
{
  guint groupID;
  guint numCameras;
  guint cameraIDs[MAXGROUPEDCAMERAS];
  guint numberOfContextPerCam[MAXGROUPEDCAMERAS];
  guint contextID[MAXGROUPEDCAMERAS][MAXCONTEXTIDPERCAMERA];
  guint numberOfStreamsPerContext[MAXGROUPEDCAMERAS][MAXCONTEXTIDPERCAMERA];
  guint statsEnabledCameraID;
} ISPGroup;

// Structure describing the grouped Cameras info
typedef struct
{
  guint numGroups;
  ISPGroup group[MAXISPGROUPS];
} ISPGroupsInfo;

// Structure describing the stream config needed from fwk
typedef struct
{
  guint width;
  guint height;
  guint frameRate;
} ISPCameraStreamConfig;

// Structure describing the camera config needed from fwk
typedef struct
{
  guint cameraID;
  guint numStreams;
  ISPCameraStreamConfig streamConfig[MAXSTREAMCONFIGS];
  guint operationMode;
  guint isStatsNeeded;
  guint remosaicType;
  guint HDRMode;
  guint numHDRExposure;
  guint isHDRVideoMode;
  guint reserved[6];
} ISPCameraConfig;

// Structure describing all camera configs for a given group
typedef struct
{
  guint groupID;
  guint numCameras;
  ISPCameraConfig cameraConfig[MAXGROUPEDCAMERAS];
} ISPGroupCameraConfigs;

// Structure describing the group configs for all groups
typedef struct
{
  guint numGroups;
  ISPGroupCameraConfigs group[MAXISPGROUPS];
} ISPGroupsConfig;

/* Per Port Static capablities*/
ISPGroupsInfo mCameraGroupsInfo;
ISPGroupsConfig mGroupedCameraInfo;

struct StreamConfig
{
  guint width;
  guint height;
  guint framerate;
};

struct CameraGroupInfo
{
  guint camera_id;
  std::vector < StreamConfig > streamconfig;
};

static ISPGroupsInfo
GetISPGroupsInfo (::camera::CameraMetadata * camInfo)
{
  ISPGroupsInfo groupInfo;

  const std::shared_ptr<::camera::VendorTagDescriptor> vtags =
      ::camera::VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (vtags.get () == NULL) {
    g_printerr ("Failed to retrieve Global Vendor Tag Descriptor!\n");
  }

  guint tag_id = 0;
  status_t ret = camInfo->getTagFromName (
      "org.codeaurora.qcamera3.AvailableISPGroupsInfo.AvailableISPGroupsInfo",
      vtags.get (), &tag_id);

  if (ret != 0) {
    g_printerr ("%s: Failed To Read Vendor Tag AvailableISPGroupsInfo\n",
        __func__);
    return groupInfo;
  }

  camera_metadata_entry_t dataEntry = camInfo->find (tag_id);

  gint offset = 0;
  groupInfo.numGroups = dataEntry.data.i32[offset++];

  if (groupInfo.numGroups <= 0 || groupInfo.numGroups > 10)
    g_printerr ("%s: Obtained invalid number of groups %d\n", __func__,
        groupInfo.numGroups);

  g_print ("%s: Obtained %d groups for ISPGroupsInfo\n", __func__,
      groupInfo.numGroups);

  // Fill capability data for each group
  for (guint groupIdx = 0; groupIdx < groupInfo.numGroups; groupIdx++) {
    auto & curGroupData = groupInfo.group[groupIdx];
    g_print ("%s: Collecting info for groupIdx: %d\n", __func__, groupIdx);
    curGroupData.groupID = dataEntry.data.i32[offset++];
    curGroupData.numCameras = dataEntry.data.i32[offset++];

    if (curGroupData.numCameras > MAXGROUPEDCAMERAS)
      g_printerr ("Group %d has more than maximum possible cameras %d\n",
          curGroupData.groupID, MAXGROUPEDCAMERAS);

    g_print ("%s: Obtained groupId: %d and number of Cameras: %d\n", __func__,
        curGroupData.groupID, curGroupData.numCameras);
    g_print ("%s: Collecting the camera ids for group: %d\n", __func__,
        curGroupData.groupID);
    for (guint camIdx = 0; camIdx < MAXGROUPEDCAMERAS; camIdx++) {
      curGroupData.cameraIDs[camIdx] = dataEntry.data.i32[offset++];
      g_print ("%s: Obtained CameraId: %d\n", __func__,
          curGroupData.cameraIDs[camIdx]);
    }

    g_print ("%s: Collecting the number of contexts per camera for group: %d\n",
        __func__, curGroupData.groupID);
    for (guint contextPerCameraIdx = 0; contextPerCameraIdx < MAXGROUPEDCAMERAS;
        contextPerCameraIdx++) {
      curGroupData.numberOfContextPerCam[contextPerCameraIdx] =
          dataEntry.data.i32[offset++];
      g_print ("%s: [C%u]: Obtained %d contexts\n", __func__,
          curGroupData.cameraIDs[contextPerCameraIdx],
          curGroupData.numberOfContextPerCam[contextPerCameraIdx]);
    }

    g_print ("%s: Collecting context ids for cameras belonging to group: %d\n",
        __func__, curGroupData.groupID);
    for (guint camIdx = 0; camIdx < MAXGROUPEDCAMERAS; camIdx++) {
      g_print ("%s: [C%u]: Collecting the context ids\n", __func__,
          curGroupData.cameraIDs[camIdx]);
      for (guint contextIdx = 0; contextIdx < MAXCONTEXTIDPERCAMERA;
          contextIdx++) {
        curGroupData.contextID[camIdx][contextIdx] =
            dataEntry.data.i32[offset++];
        g_print ("%s: [C%u]: Obtained contextId: %d\n", __func__,
            curGroupData.cameraIDs[camIdx],
            curGroupData.contextID[camIdx][contextIdx]);
      }
    }

    g_print
        ("%s: Collecting number of streams per context for cameras " \
         "belonging to group: %d\n", __func__, curGroupData.groupID);
    for (guint camIdx = 0; camIdx < MAXGROUPEDCAMERAS; camIdx++) {
      g_print ("%s: [C%u]: Collecting number of streams per context\n",
          __func__, curGroupData.cameraIDs[camIdx]);
      for (guint contextIdx = 0; contextIdx < MAXCONTEXTIDPERCAMERA;
          contextIdx++) {
        curGroupData.numberOfStreamsPerContext[camIdx][contextIdx] =
            dataEntry.data.i32[offset++];
        g_print
            ("%s: Obtained %d number of Streams per context for contextId: %d\n",
            __func__,
            curGroupData.numberOfStreamsPerContext[camIdx][contextIdx],
            curGroupData.contextID[camIdx][contextIdx]);
      }
    }

    curGroupData.statsEnabledCameraID = dataEntry.data.i32[offset++];
    g_print ("%s: Obtained stats enabled camera id: %d\n", __func__,
        curGroupData.statsEnabledCameraID);
  }

  return groupInfo;
}

static void
GroupCameraInfo (std::unordered_map < guint, std::vector < guint >> groupedInfo,
    std::vector < CameraGroupInfo > &group_cam_info)
{
  mGroupedCameraInfo.numGroups = groupedInfo.size ();
  guint groupIdx = 0;

  for (auto &[groupId, cameraList]:groupedInfo) {
    mGroupedCameraInfo.group[groupIdx].groupID = groupId;
    mGroupedCameraInfo.group[groupIdx].numCameras = cameraList.size ();

    /* for each camera fill in config data */
    for (guint camIdx = 0; camIdx < cameraList.size (); camIdx++) {
      guint cameraId = cameraList[camIdx];
      auto & cameraInfo =
          mGroupedCameraInfo.group[groupIdx].cameraConfig[camIdx];

      cameraInfo.cameraID = cameraId;
      for (guint i = 0; i < group_cam_info.size (); i++) {
        if (cameraId == group_cam_info[i].camera_id) {
          g_print ("%s: groupcaminfo[i].camera_id is %u\n", __func__,
              group_cam_info[i].camera_id);
          cameraInfo.numStreams = group_cam_info[i].streamconfig.size ();
          /* for each stream fill the stream config */
          for (guint streamIdx = 0; streamIdx < cameraInfo.numStreams;
              streamIdx++) {
            auto & streamInfoToSet = cameraInfo.streamConfig[streamIdx];
            streamInfoToSet.height =
                group_cam_info[i].streamconfig[streamIdx].height;
            streamInfoToSet.width =
                group_cam_info[i].streamconfig[streamIdx].width;
            streamInfoToSet.frameRate =
                group_cam_info[i].streamconfig[streamIdx].framerate;
            g_print ("%s: stream config width %u, height %u, framerate %u\n",
                __func__, group_cam_info[i].streamconfig[streamIdx].width,
                group_cam_info[i].streamconfig[streamIdx].height,
                group_cam_info[i].streamconfig[streamIdx].framerate);
          }
        }
      }
      cameraInfo.operationMode = 0;
    }

    groupIdx++;
  }
}

static void
CreateCameraGroupInfo (std::vector < CameraGroupInfo > &group_cam_info,
    ::camera::CameraMetadata * camInfo)
{
  g_print ("%s: camInfo entry count is %ld", __func__, camInfo->entryCount ());
  mCameraGroupsInfo = GetISPGroupsInfo (camInfo);

  auto FetchGroupIdForCamera =[&](guint cameraId)->guint {
    guint groupId = UINT_MAX;
    for (guint groupIdx = 0; groupIdx < mCameraGroupsInfo.numGroups; groupIdx++) {
      auto & groupData = mCameraGroupsInfo.group[groupIdx];
      guint numCameras = groupData.numCameras;

      for (guint camIdx = 0; camIdx < numCameras; camIdx++) {
        if (groupData.cameraIDs[camIdx] == cameraId) {
          groupId = groupData.groupID;
          return groupId;
        }
      }
    }
    return groupId;
  };

  /* key: groupId, value: list of cameras to be grouped*/
  std::unordered_map<guint, std::vector<guint>> groups;

  for (guint i = 0; i < group_cam_info.size (); i++) {
    // group the camId into one of the available groups
    guint groupId = FetchGroupIdForCamera (group_cam_info[i].camera_id);
    if (groupId == UINT_MAX) {
      g_printerr ("[C%u]: Provided camera is not part of any group\n",
          group_cam_info[i].camera_id);
      return;
    }
    groups[groupId].push_back (group_cam_info[i].camera_id);
    g_print ("%s: camera ID %u is being added to the group\n", __func__,
        group_cam_info[i].camera_id);
  }

  GroupCameraInfo (groups, group_cam_info);
}

// Decrements total pipelines counter and if 0 tries to quit loop
static void
request_end_loop (GstPerPortCtx * ctx)
{
  // Lock mutex to prevent non atomic modification to refcount
  g_mutex_lock (ctx->lock);
  // Decrement reference counter
  // and if reached 0 request exit main loop
  if (--*ctx->refcount == 0) {
    g_main_loop_quit (ctx->mloop);
  }
  // Unlock mutex
  g_mutex_unlock (ctx->lock);
}

static GstElement *
get_element_from_pipeline (GstElement * pipeline, const gchar * factory_name)
{
  GstElement *element = NULL;
  GstElementFactory *elem_factory = gst_element_factory_find (factory_name);
  GstIterator *it = NULL;
  GValue value = G_VALUE_INIT;

  // Iterate the pipeline and check factory of each element.
  for (it = gst_bin_iterate_elements (GST_BIN (pipeline));
      gst_iterator_next (it, &value) == GST_ITERATOR_OK;
      g_value_reset (&value)) {
    element = GST_ELEMENT (g_value_get_object (&value));

    if (gst_element_get_factory (element) == elem_factory)
      goto free;
  }
  g_value_reset (&value);
  element = NULL;

free:
  gst_iterator_free (it);
  gst_object_unref (elem_factory);

  return element;
}

static gboolean
update_pipeline_state (GstElement * pipeline, GstState state)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstState current, pending;

  // First check current and pending states of the pipeline.
  ret = gst_element_get_state (pipeline, &current, &pending, 0);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("ERROR: Failed to retrieve pipeline state!\n");
    return TRUE;
  }

  if (state == current) {
    g_print ("Already in %s state\n", gst_element_state_get_name (state));
    return TRUE;
  } else if (state == pending) {
    g_print ("Pending %s state\n", gst_element_state_get_name (state));
    return TRUE;
  }

  g_print ("Setting pipeline to %s\n", gst_element_state_get_name (state));
  ret = gst_element_set_state (pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to %s state!\n",
          gst_element_state_get_name (state));

      return TRUE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      ret = gst_element_get_state (pipeline, NULL,NULL,
          GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("ERROR: Pipeline failed to PREROLL!\n");
        return TRUE;
      }

      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  GstState currstate;
  while (currstate != state) {
    ret = gst_element_get_state (pipeline, &currstate, NULL,
        GST_CLOCK_TIME_NONE);
  }

  return TRUE;
}

static gboolean
start_bayer_pipeline (GstPerPortCtx & ctx, GstState newstate)
{
  GstStateChangeReturn ret;

  g_print ("Setting pipeline %s for camera %d to %s\n", ctx.pipe_name,
      ctx.camera, gst_element_state_get_name (newstate));
  ret = gst_element_set_state (ctx.pipeline, newstate);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PLAYING state!\n");
      return FALSE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");

      ret = gst_element_get_state (ctx.pipeline, NULL, NULL,
          GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Pipeline failed to PREROLL!\n");
        return FALSE;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  GstState currstate;
  while (currstate != GST_STATE_PLAYING) {
    ret = gst_element_get_state (ctx.pipeline, &currstate, NULL,
        GST_CLOCK_TIME_NONE);
  }
  ctx.active = 1;

  return TRUE;
}

// Tries to change pipelines state
static gboolean
change_state_pipelines (std::vector < GstPerPortCtx > &ctx,
    std::vector < CameraGroupInfo > &camInfo, GstState newstate)
{
  GstStateChangeReturn ret;
  guint i = 1;
  ::camera::CameraMetadata * static_meta = NULL;
  ::camera::CameraMetadata * session_meta = NULL;

  for (; i < ctx.size (); ++i) {
    // Only for first camera of group, send the session param
    if (i == 1) {
      GstElement *camsrc = NULL;
      if ((camsrc =
              get_element_from_pipeline (ctx[i].pipeline,
                  "qtiqmmfsrc")) == NULL) {
        g_printerr
            ("ERROR: No camera plugin found in pipeline, can't proceed.\n");
        return FALSE;
      }

      g_print ("\nSetting pipeline %s for camera %d to %s\n", ctx[i].pipe_name,
          ctx[i].camera, gst_element_state_get_name (GST_STATE_READY));
      ret = gst_element_set_state (ctx[i].pipeline, GST_STATE_READY);
      if (ret == GST_STATE_CHANGE_SUCCESS) {
        g_print ("\nPipeline %s is Ready.\n", ctx[i].pipe_name);
      }
      g_object_get (G_OBJECT (camsrc), "static-metadata", &static_meta, NULL);

      g_print ("static metadata entry count is %ld\n",
          static_meta->entryCount ());
      session_meta = new::camera::CameraMetadata (128, 128);
      guint tag_id = -1;

      CreateCameraGroupInfo (camInfo, static_meta);

      const std::shared_ptr <::camera::VendorTagDescriptor >
          vtags =::camera::VendorTagDescriptor::getGlobalVendorTagDescriptor ();
      if (vtags.get () == NULL) {
        g_printerr ("Failed to retrieve Global Vendor Tag Descriptor!\n");
        delete session_meta;
        return FALSE;
      }

      gint res = static_meta->getTagFromName
          ("org.codeaurora.qcamera3.sessionParameters.EnabledISPGroupsConfig",
          vtags.get (), &tag_id);
      if (res == 0) {
        g_print ("%s: Setting EnabledISPGroupsConfigTag\n", __func__);
        session_meta->updateImpl (tag_id, &mGroupedCameraInfo,
            sizeof (mGroupedCameraInfo));
      } else {
        g_print
            ("%s: Failed to get session parameter EnabledISPGroupsConfigTag",
            __func__);
        delete session_meta;
        return FALSE;
      }

      g_object_set (G_OBJECT (camsrc), "session-metadata", session_meta, NULL);
    }

    g_print ("Setting pipeline %s for camera %d to %s\n", ctx[i].pipe_name,
        ctx[i].camera, gst_element_state_get_name (newstate));
    ret = gst_element_set_state (ctx[i].pipeline, newstate);

    switch (ret) {
      case GST_STATE_CHANGE_FAILURE:
        g_printerr ("ERROR: Failed to transition to %s state!\n",
            gst_element_state_get_name (newstate));
        delete session_meta;
        return FALSE;
      case GST_STATE_CHANGE_NO_PREROLL:
        g_print ("Pipeline is live and does not need PREROLL.\n");
        break;
      case GST_STATE_CHANGE_ASYNC:
        g_print ("Pipeline is PREROLLING ...\n");
        ret = gst_element_get_state (ctx[i].pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE);

        if (ret == GST_STATE_CHANGE_FAILURE) {
          g_printerr ("ERROR: Pipeline failed to PREROLL!\n");
          delete session_meta;
          return FALSE;
        }

        break;
      case GST_STATE_CHANGE_SUCCESS:
        g_print ("Pipeline state change was successful\n");
        break;
    }

    GstState currstate;
    while (currstate != GST_STATE_PLAYING) {
      ret = gst_element_get_state (ctx[i].pipeline, &currstate, NULL,
          GST_CLOCK_TIME_NONE);
    }
    ctx[i].active = 1;
  }

  return TRUE;
}

// Hangles interrupt signals like Ctrl+C etc.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  std::vector < GstPerPortCtx > *ctx =
      static_cast < std::vector < GstPerPortCtx > *>(userdata);

  guint i = 0;
  GstState state, pending;
  GstStateChangeReturn ret;

  g_print ("\n\nReceived an interrupt signal ...\n");

  for (; i < ctx->size (); ++i) {
    // Try to query current pipeline state immediately
    ret = gst_element_get_state ((*ctx)[i].pipeline, &state, &pending,
        GST_CLOCK_TIME_NONE);
    // If not able to succeed print error and ignore
    if (ret == GST_STATE_CHANGE_FAILURE) {
      g_print ("ERROR: %s get current state! %d\n", (*ctx)[i].pipe_name, ret);
      continue;
    }
    // Check if in PLAYING state and send eos
    if (state == GST_STATE_PLAYING) {
      gst_element_send_event ((*ctx)[i].pipeline, gst_event_new_eos ());
    }
  }

  return TRUE;
}

// Handle warnings
static void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

// Handle errors
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstPerPortCtx *ctx = GST_PER_PORT_CONTEXT_CAST (userdata);
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);

  // Error message should be printed below
  // via the default error handler
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  // Since there is error, set pipeline to NULL state.
  gst_element_set_state (ctx->pipeline, GST_STATE_NULL);

  // Decrease the refcount of the pipeline which got error.
  --*ctx->refcount;

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (ctx->mloop);
}

// End of stream callback function
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstPerPortCtx *ctx = GST_PER_PORT_CONTEXT_CAST (userdata);

  g_print ("\n%s for camera %d Received End-of-Stream...\n",
      ctx->pipe_name, ctx->camera);

  if (update_pipeline_state (ctx->pipeline, GST_STATE_NULL))
    ctx->active = 0;

  request_end_loop (ctx);
}

static gint
create_yuv_camera_pipeline (GstPerPortCtx * ctx, std::string yuv_pipeline)
{
  GError *error = NULL;
  gint status = -1;
  ctx->pipeline = NULL;

  ctx->pipeline = gst_parse_launch (yuv_pipeline.c_str (), &error);

  // Check for errors on pipe creation.
  if ((NULL == ctx->pipeline) && (error != NULL)) {
    g_printerr ("\nERROR: Failed to create pipeline, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);

    return status;
  } else if ((NULL == ctx->pipeline) && (NULL == error)) {
    g_printerr ("\nERROR: Failed to create pipeline, unknown error!\n");

    return status;
  } else if ((ctx->pipeline != NULL) && (error != NULL)) {
    g_printerr ("\nERROR: Erroneous pipeline, error: %s!\n",
        GST_STR_NULL (error->message));

    g_clear_error (&error);
    return status;
  }

  status = 0;
  return status;
}


static gint
create_bayer_camera_pipeline (GstPerPortCtx * ctx, std::string bayer_pipeline)
{
  GError *error = NULL;
  gint status = -1;
  ctx->pipeline = NULL;

  ctx->pipeline = gst_parse_launch (bayer_pipeline.c_str (), &error);

  // Check for errors on pipe creation.
  if ((NULL == ctx->pipeline) && (error != NULL)) {
    g_printerr ("\nERROR: Failed to create pipeline, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);

    return status;
  } else if ((NULL == ctx->pipeline) && (NULL == error)) {
    g_printerr ("\nERROR: Failed to create pipeline, unknown error!\n");

    return status;
  } else if ((ctx->pipeline != NULL) && (error != NULL)) {
    g_printerr ("\nERROR: Erroneous pipeline, error: %s!\n",
        GST_STR_NULL (error->message));

    g_clear_error (&error);
    return status;
  }

  status = 0;
  return status;
}

static gboolean
handle_stdin_source (GIOChannel * source, GIOCondition condition,
    gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  gchar *input = NULL;
  GIOStatus status = G_IO_STATUS_NORMAL;

  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((G_IO_STATUS_ERROR == status) && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);

      return FALSE;
    } else if ((G_IO_STATUS_ERROR == status) && (NULL == error)) {
      g_printerr ("ERROR: Unknown error!\n");

      return FALSE;
    } else if ((G_IO_STATUS_AGAIN != status) && (NULL == input)) {
      g_printerr ("ERROR: Input is NULL!\n");

      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  if (strlen (input) > 1)
    input = g_strchomp (input);

  // Push stdin string into the inputs queue.
  g_async_queue_push (appctx->messages, gst_structure_new (STDIN_MESSAGE,
          "input", G_TYPE_STRING, input, NULL));
  g_free (input);

  return TRUE;
}

static gboolean
wait_stdin_message (GAsyncQueue * queue, gchar ** input)
{
  GstStructure *message = NULL;

  // Clear input from previous use.
  g_clear_pointer (input, g_free);

  // Block the thread until there's no input from the user
  // or eos/error msg occurs.
  while ((message = (GstStructure *) g_async_queue_pop (queue)) != NULL) {
    if (gst_structure_has_name (message, TERMINATE_MESSAGE)) {
      gst_structure_free (message);
      // Returning FALSE will cause menu thread to terminate.
      return FALSE;
    }

    if (gst_structure_has_name (message, STDIN_MESSAGE))
      *input = g_strdup (gst_structure_get_string (message, "input"));

    if (*input != NULL)
      break;

    // Clear message to terminate the loop after having popped the data.
    gst_structure_free (message);
  }

  gst_structure_free (message);
  return TRUE;
}

static void
print_active_camera_options (std::vector < GstPerPortCtx > &ctx)
{
  GString *options = g_string_new (NULL);

  APPEND_MENU_HEADER (options);

  APPEND_CAMERA_ON_OFF_CONTROLS_SECTION (options);

  for (auto & item:ctx) {
    if (item.active) {
      std::string cam_id = std::to_string (item.camera);
      g_string_append_printf (options, "   (%s) : %s\n", cam_id.c_str (),
          "Stop the camera");
    }
  }

  for (auto & item:ctx) {
    if (!item.active) {
      std::string cam_id = std::to_string (item.camera);
      g_string_append_printf (options, "   (%s) : %s\n", cam_id.c_str (),
          "Start the camera");
    }
  }

  g_string_append_printf (options, "   (%s) : %s\n", "q",
      "Exit the application");

  g_print ("%s", options->str);
  g_string_free (options, TRUE);
}

static gboolean
gst_active_cameras_menu (std::vector < GstPerPortCtx > &ctx,
    GAsyncQueue * messages)
{
  gchar *input = NULL;
  gboolean active = TRUE;
  gint camera_id;
  GstState state, pending;
  GstStateChangeReturn ret;

  print_active_camera_options (ctx);

  g_print ("\n\nChoose an Option : ");

  // If FALSE is returned termination signal has been issued.
  active = wait_stdin_message (messages, &input);

  if (g_str_equal (input, QUIT_OPTION)) {
    g_print ("\nQuit pressed!!\n");
    active = FALSE;
    for (auto & item:ctx) {
      ret = gst_element_get_state (item.pipeline, &state, &pending,
          GST_CLOCK_TIME_NONE);
      // If not able to succeed print error and ignore
      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_print ("ERROR: %s get current state! %d\n", item.pipe_name, ret);
        continue;
      }
      // Check if in PLAYING state and send eos
      if (state == GST_STATE_PLAYING) {
        gst_element_send_event (item.pipeline, gst_event_new_eos ());
      }
    }
  } else {
    camera_id = atoi (input);
  }

  if (active) {
    for (auto &item : ctx) {
      if (item.camera == camera_id) {
        if (item.active) {
          if (update_pipeline_state (item.pipeline, GST_STATE_NULL)) {
            item.active = 0;
            (*item.refcount)--;
            g_print ("Cam %d is now Stopped\n", item.camera);
          } else {
            g_print ("Cam %d failed to Stop\n", item.camera);
          }
        } else {
          if (update_pipeline_state (item.pipeline, GST_STATE_PLAYING)) {
            item.active = 1;
            (*item.refcount)++;
            g_print ("Cam %d is Started\n", item.camera);
          } else {
            g_print ("Cam %d failed to Start\n", item.camera);
          }
        }
      }
    }
  }

  g_free (input);

  return active;
}

static gpointer
main_menu (gpointer userdata)
{
  GstAppContext *appctx = GST_APP_CONTEXT_CAST (userdata);
  gboolean active = TRUE;

  std::vector < GstPerPortCtx > ctx = appctx->ctx;

  while (active) {
    active = gst_active_cameras_menu (ctx, appctx->messages);
  }

  return NULL;
}


gint
main (gint argc, gchar * argv[])
{
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GIOChannel *gio = NULL;
  GThread *mthread = NULL;
  GMutex lock;
  guint intrpt_watch_id = 0, stdin_watch_id = 0;
  guint refcounter = 0;
  std::vector < GstPerPortCtx > ctx;

  g_mutex_init (&lock);

  // Initialize GST library.
  gst_init (&argc, &argv);

  int bayer_cam_id;
  std::cout << "Enter the bayer camera_id: ";
  std::cin >> bayer_cam_id;
  std::cin.ignore ();

  std::string bayer_pipeline;
  std::cout << "\nEnter the bayer camera pipeline: ";
  std::getline (std::cin, bayer_pipeline);

  // Take input from user
  std::string camera_ids_str;
  g_print ("\nEnter the YUV camera ID's you want to open (space separated):");
  std::getline (std::cin, camera_ids_str);

  std::istringstream iss (camera_ids_str);
  std::vector < guint > yuv_camera_ids;
  std::string id;
  while (iss >> id) {
    yuv_camera_ids.push_back (static_cast < guint > (std::stoul (id)));
  }

  std::vector < CameraGroupInfo > camInfo (yuv_camera_ids.size ());

  for (guint i = 0; i < camInfo.size (); ++i) {
    camInfo[i].camera_id = yuv_camera_ids[i];
    guint num_streams;

    // Take input from user
    std::cout <<
        "\nEnter the number of streams for camera " << camInfo[i].
        camera_id << ": ";
    std::cin >> num_streams;
    std::cin.ignore ();
    camInfo[i].streamconfig.resize (num_streams);

    for (guint j = 0; j < num_streams; ++j) {
      std::cout << "\nEnter the WIDTH for stream " << j +
          1 << " of camera " << camInfo[i].camera_id << ": ";
      std::cin >> camInfo[i].streamconfig[j].width;
      std::cin.ignore ();
      std::cout << "\nEnter the HEIGHT for stream " << j +
          1 << " of camera " << camInfo[i].camera_id << ": ";
      std::cin >> camInfo[i].streamconfig[j].height;
      std::cin.ignore ();
      std::cout << "\nEnter the FRAMERATE for stream " << j +
          1 << " of camera " << camInfo[i].camera_id << ": ";
      std::cin >> camInfo[i].streamconfig[j].framerate;
      std::cin.ignore ();
    }
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("\nERROR: Failed to create Main loop!\n");
    return -1;
  }
  // Context for bayer camera.
  GstPerPortCtx bayerctx;
  bayerctx.camera = bayer_cam_id;
  bayerctx.refcount = &refcounter;
  bayerctx.mloop = mloop;
  bayerctx.lock = &lock;

  // Create pipes
  bayerctx.pipe_name = "gst-bayer-camera-pipeline";
  if (create_bayer_camera_pipeline (&bayerctx, bayer_pipeline) < 0) {
    g_printerr ("ERROR: Failed to create first camera pipe!\n");
    return -1;
  }

  ctx.push_back (bayerctx);
  refcounter++;

  for (guint i = 0; i < camInfo.size (); ++i) {
    GstPerPortCtx yuvctx;
    yuvctx.camera = camInfo[i].camera_id;
    yuvctx.refcount = &refcounter;
    yuvctx.mloop = mloop;
    yuvctx.lock = &lock;
    yuvctx.pipe_name = "gst-yuv-camera-pipeline";
    std::string yuv_pipeline;
    std::cout << "\nEnter the yuv camera pipeline for camera "
        << camInfo[i].camera_id << ": ";
    std::getline (std::cin, yuv_pipeline);

    if (create_yuv_camera_pipeline (&yuvctx, yuv_pipeline) < 0) {
      g_printerr ("\nERROR: Failed to create %s for camera %d\n",
          yuvctx.pipe_name, yuvctx.camera);
      return -1;
    }

    ctx.push_back (yuvctx);
    refcounter++;
  }

  GstAppContext appctx;
  appctx.ctx = ctx;
  appctx.messages =
      g_async_queue_new_full ((GDestroyNotify) gst_structure_free);

  for (guint i = 0; i < appctx.ctx.size (); i++) {
    // Retrieve reference to the pipeline's bus.
    if ((bus =
            gst_pipeline_get_bus (GST_PIPELINE (appctx.ctx[i].pipeline))) ==
        NULL) {
      g_printerr ("\nERROR: Failed to retrieve pipeline bus!\n");
      goto exit;
    }
    // Watch for messages on the pipeline's bus.
    gst_bus_add_signal_watch (bus);
    g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
    g_signal_connect (bus, "message::error", G_CALLBACK (error_cb),
        &appctx.ctx[i]);
    g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), &appctx.ctx[i]);
    gst_object_unref (bus);
  }

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &appctx.ctx);

  // Create a GIOChannel to listen to the standard input stream.
  if ((gio = g_io_channel_unix_new (fileno (stdin))) == NULL) {
    g_printerr ("ERROR: Failed to initialize I/O support! %.30s\n", SPACE);
    gst_object_unref (bus);
    goto exit;
  }
  // Watch for user's input on stdin.
  stdin_watch_id = g_io_add_watch (gio,
      GIOCondition (G_IO_PRI | G_IO_IN), handle_stdin_source, &appctx);
  g_io_channel_unref (gio);

  if (start_bayer_pipeline (appctx.ctx[0], GST_STATE_PLAYING)) {
    g_print ("Bayer pipeline started\n");
  } else {
    goto exit;
  }

  if (change_state_pipelines (appctx.ctx, camInfo, GST_STATE_PLAYING)) {
    g_print ("YUV pipelines are started\n");
  } else {
    goto exit;
  }

  // Initiate the menu thread.
  if ((mthread = g_thread_new ("MainMenu", main_menu, &appctx)) == NULL) {
    g_printerr ("ERROR: Failed to create menu thread!\n");
    goto exit;
  }

  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  // Wait until main menu thread finishes.
  g_thread_join (mthread);

  g_source_remove (intrpt_watch_id);
  g_source_remove (stdin_watch_id);

exit:
  g_main_loop_unref (mloop);

  g_mutex_clear (&lock);

  gst_deinit ();

  g_print ("Exit\n");

  return 0;
}
