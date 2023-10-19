/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <evaTypes.h>
#include <evaMem.h>
#include <evaSession.h>
#include <evaOpticalFlow.h>
#include <evaUtils.h>

#include "opticalflow-engine.h"

#define GST_RETURN_VAL_IF_FAIL(expression, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return (value); \
  } \
}

#define GST_RETURN_VAL_IF_FAIL_WITH_CLEAN(expression, value, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return (value); \
  } \
}

#define GST_RETURN_IF_FAIL(expression, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return; \
  } \
}

#define GST_RETURN_IF_FAIL_WITH_CLEAN(expression, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return; \
  } \
}

#define ADD_FIELD_PARAMS(params, entries, value, name, offset, size, isunsigned) \
{ \
  g_value_set_uchar (value, offset);               \
  gst_value_array_append_value (entries, value);   \
                                                   \
  g_value_set_uchar (value, size);                 \
  gst_value_array_append_value (entries, value);   \
                                                   \
  g_value_set_uchar (value, isunsigned);           \
  gst_value_array_append_value (entries, value);   \
                                                   \
  gst_structure_set_value (params, name, entries); \
  g_value_reset (entries);                         \
}

#define REQUIRED_N_INPUTS             2

#define EVA_MV_X_FIELD_SIZE           16
#define EVA_MV_Y_FIELD_SIZE           16

#define EVA_PAXEL_WIDTH               4
#define EVA_PAXEL_HEIGHT              4

#define DEFAULT_OPT_VIDEO_WIDTH       0
#define DEFAULT_OPT_VIDEO_HEIGHT      0
#define DEFAULT_OPT_VIDEO_STRIDE      0
#define DEFAULT_OPT_VIDEO_SCANLINE    0
#define DEFAULT_OPT_VIDEO_FORMAT      GST_VIDEO_FORMAT_UNKNOWN
#define DEFAULT_OPT_VIDEO_FPS         0
#define DEFAULT_OPT_ENABLE_STATS      TRUE

#define GET_OPT_FORMAT(s) ((GstVideoFormat) get_opt_enum (s, \
    GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_FORMAT, GST_TYPE_VIDEO_FORMAT, \
    DEFAULT_OPT_VIDEO_FORMAT))
#define GET_OPT_WIDTH(s) get_opt_uint (s, \
    GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_WIDTH, DEFAULT_OPT_VIDEO_WIDTH)
#define GET_OPT_HEIGHT(s) get_opt_uint (s, \
    GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_HEIGHT, DEFAULT_OPT_VIDEO_HEIGHT)
#define GET_OPT_STRIDE(s) get_opt_uint (s, \
    GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_STRIDE, DEFAULT_OPT_VIDEO_STRIDE)
#define GET_OPT_SCANLINE(s) get_opt_uint (s, \
    GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_SCANLINE, DEFAULT_OPT_VIDEO_SCANLINE)
#define GET_OPT_FPS(s) get_opt_uint (s, \
    GST_CV_OPTCLFLOW_ENGINE_OPT_VIDEO_FPS, DEFAULT_OPT_VIDEO_FPS)

#define GST_CAT_DEFAULT gst_eva_optclflow_engine_debug_category()

struct _GstCvOptclFlowEngine
{
  GstStructure *settings;

  // EVA session handle.
  evaSession   session;
  // EVA handle for the OpticalFlow algorithm.
  evaHandle    handle;
  // Indicates whether the EVA session is active.
  gboolean     active;

  // Size requirements for the Motion Vector.
  guint        mv_size;

  // Map of buffer FDs and their corresponding EVA image.
  GHashTable   *evaimages;
};

static GstDebugCategory *
gst_eva_optclflow_engine_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("eva-opticalflow-engine",
        0, "Engine for Video Optical Flow Engine");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

static guint
get_opt_uint (GstStructure * settings, const gchar * opt, guint dval)
{
  guint result;
  return gst_structure_get_uint (settings, opt, &result) ?
    result : dval;
}

static gint
get_opt_enum (GstStructure * settings, const gchar * opt, GType type, gint dval)
{
  gint result;
  return gst_structure_get_enum (settings, opt, type, &result) ?
    result : dval;
}

static gboolean
get_opt_bool (const GstStructure * settings, const gchar * opt, gboolean value)
{
  gboolean result;
  return gst_structure_get_boolean (settings, opt, &result) ? result : value;
}

static void
gst_eva_append_custom_meta (GstCvOptclFlowEngine * engine, GstBuffer * buffer)
{
  GstStructure *info = NULL, *params = NULL;
  GValue entries = G_VALUE_INIT, value = G_VALUE_INIT;
  guchar offset = 0, size = 0;

  g_value_init (&entries, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_UCHAR);

  // Create a structure that will contain information for data decryption.
  info = gst_structure_new_empty ("CvOpticalFlow");

  // Names, offsets and sizes in bits of the fields in the motion vector.
  params = gst_structure_new_empty ("MotionVector");

  offset = 0;
  size = EVA_MV_X_FIELD_SIZE;

  // Fill offset and size of the motion vector X field in bits.
  ADD_FIELD_PARAMS (params, &entries, &value, "X", offset, size, false);

  offset += size;
  size = EVA_MV_Y_FIELD_SIZE;

  // Fill offset and size of the motion vector Y field in bits.
  ADD_FIELD_PARAMS (params, &entries, &value, "Y", offset, size, false);

  // Add the motion vector parameters to the main info structure.
  gst_structure_set (info,
      "motion-vector-params", GST_TYPE_STRUCTURE, params, NULL);
  gst_structure_free (params);

  g_value_unset (&value);
  g_value_unset (&entries);

  g_value_init (&value, G_TYPE_UINT);

  // Add the dimensions of single motion vector paxel to the info structure.
  g_value_set_uint (&value, EVA_PAXEL_WIDTH);
  gst_structure_set_value (info, "mv-paxel-width", &value);
  g_value_set_uint (&value, EVA_PAXEL_HEIGHT);
  gst_structure_set_value (info, "mv-paxel-height", &value);

  // Add the number of paxels in a single row and column to the info structure.
  g_value_set_uint (&value,
      GST_ROUND_UP_64 (GET_OPT_WIDTH (engine->settings)) / EVA_PAXEL_WIDTH);
  gst_structure_set_value (info, "mv-paxels-row-length", &value);
  g_value_set_uint (&value,
      GET_OPT_HEIGHT (engine->settings) / EVA_PAXEL_HEIGHT);
  gst_structure_set_value (info, "mv-paxels-column-length", &value);

  g_value_unset (&value);

  // Append custom meta to the output buffer for data decryption.
  gst_buffer_add_protection_meta (buffer, info);
}

static evaImage *
gst_eva_create_image (GstCvOptclFlowEngine * engine, const GstVideoFrame * frame)
{
  GstMemory *memory = NULL;
  evaImage *evaimage = NULL;
  evaImageInfo *imginfo = NULL;
  evaStatus status = EVA_SUCCESS;

  // Get the 1st (and only) memory block from the input GstBuffer.
  memory = gst_buffer_peek_memory (frame->buffer, 0);
  GST_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), NULL,
      "Input buffer %p does not have FD memory!", frame->buffer);

  evaimage = g_new0 (evaImage, 1);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (evaimage != NULL, NULL,
      g_free (evaimage), "Failed to allocate memory for EVA image!");

  evaimage->pBuffer = g_new0 (evaMem, 1);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (evaimage->pBuffer != NULL, NULL,
      g_free (evaimage->pBuffer), "Failed to allocate memory for EVA image buffer!");

  evaimage->pBuffer->eType = EVA_MEM_NON_SECURE;
  evaimage->pBuffer->nSize = gst_buffer_get_size (frame->buffer);
  evaimage->pBuffer->nFD = gst_fd_memory_get_fd (memory);
  evaimage->pBuffer->pAddress = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  evaimage->pBuffer->nOffset = GST_VIDEO_FRAME_PLANE_OFFSET (frame, 0);

  imginfo = &evaimage->sImageInfo;
  imginfo->nWidth = GST_VIDEO_FRAME_WIDTH (frame);
  imginfo->nHeight = GST_VIDEO_FRAME_HEIGHT (frame);
  imginfo->nTotalSize = gst_buffer_get_size (frame->buffer);

  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
    case GST_VIDEO_FORMAT_NV12:
      imginfo->eFormat = EVA_COLORFORMAT_NV12;
      imginfo->nPlane = 2;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      imginfo->eFormat = EVA_COLORFORMAT_GRAY_8BIT;
      imginfo->nPlane = 1;
      break;
    default:
      GST_ERROR ("Unsupported video format: %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));

      g_free (evaimage->pBuffer);
      g_free (evaimage);

      return NULL;
  }

  imginfo->nWidthStride[0] = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  imginfo->nAlignedSize[0] = (GST_VIDEO_FRAME_N_PLANES (frame) == 2) ?
      GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1) : imginfo->nTotalSize;

  if (GST_VIDEO_FRAME_N_PLANES (frame) == 2) {
    imginfo->nWidthStride[1] = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
    imginfo->nAlignedSize[1] = imginfo->nTotalSize - imginfo->nAlignedSize[0];
  }

  GST_INFO ("Fd(%d) Format(%d) Width(%u) Height(%u) Planes(%u) TotalSize(%u)",
      evaimage->pBuffer->nFD, imginfo->eFormat, imginfo->nWidth, imginfo->nHeight,
      imginfo->nPlane, imginfo->nTotalSize);

  return evaimage;
}

static void
gst_eva_delete_image (gpointer key, gpointer value, gpointer userdata)
{
  GstCvOptclFlowEngine *engine = (GstCvOptclFlowEngine*) userdata;
  evaImage *evaimage = (evaImage*) value;
  evaStatus status = EVA_SUCCESS;

  if (status != EVA_SUCCESS)
    GST_ERROR ("Failed to deregister EVA image for key %p, "
        "error: %d!", key, status);

  status = evaMemDeregister (engine->session, evaimage->pBuffer);
  if (status != EVA_SUCCESS)
    GST_ERROR ("Failed to deregister EVA image buffer for key %p, "
        "error: %d!", key, status);

  g_free (evaimage->pBuffer);
  g_free (evaimage);

  GST_DEBUG ("Deleted EVA image for key %p", key);
  return;
}

GstCvOptclFlowEngine *
gst_cv_optclflow_engine_new (GstStructure * settings)
{
  GstCvOptclFlowEngine *engine = NULL;
  evaConfigList config;
  evaOFOutBuffReq requirements;
  evaImageInfo imginfo;
  evaStatus status = EVA_SUCCESS;
  guint stride = 0, scanline = 0, width = 0, height = 0;
  guint mvpackformat = 1;
  guint actlfps = 0;
  evaOFDirection direction = EVA_OF_FORWARD_DIRECTION;
  evaOFAmFilterConfig amfconf;

  amfconf.nConfThresh = 255;
  amfconf.nStepSize = 2;
  amfconf.nUpScale = 0;
  amfconf.nOutputIntOnly = 1;
  amfconf.nOutputFormat = 0;

  engine = g_slice_new0 (GstCvOptclFlowEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  engine->evaimages = g_hash_table_new (NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->evaimages != NULL, NULL,
      gst_cv_optclflow_engine_free (engine), "Failed to create hash table "
      "for source images!");

  engine->session = evaCreateSession (NULL, NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->session != NULL, NULL,
      gst_cv_optclflow_engine_free (engine), "Failed to create EVA session!");

  width = GET_OPT_WIDTH (engine->settings);
  height = GET_OPT_HEIGHT (engine->settings);
  stride = GET_OPT_STRIDE (engine->settings);
  scanline = GET_OPT_SCANLINE (engine->settings);
  actlfps = GET_OPT_FPS (engine->settings);

  imginfo.nWidth = width;
  imginfo.nHeight = height;

  switch (GET_OPT_FORMAT (engine->settings)) {
    case GST_VIDEO_FORMAT_NV12:
      imginfo.eFormat = EVA_COLORFORMAT_NV12;
      imginfo.nPlane = 2;
      imginfo.nTotalSize = (stride * scanline) + (stride * scanline / 2);
      imginfo.nWidthStride[0] = stride;
      imginfo.nWidthStride[1] = stride;
      imginfo.nAlignedSize[0] = stride * scanline;
      imginfo.nAlignedSize[1] = imginfo.nTotalSize - imginfo.nAlignedSize[0];
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      imginfo.eFormat = EVA_COLORFORMAT_GRAY_8BIT;
      imginfo.nPlane = 1;
      imginfo.nTotalSize = stride * scanline;
      imginfo.nWidthStride[0] = stride;
      imginfo.nAlignedSize[0] = stride * scanline;
      break;
    default:
      GST_ERROR ("Unsupported video format: %s!",
          gst_video_format_to_string (GET_OPT_FORMAT (engine->settings)));
      gst_cv_optclflow_engine_free (engine);

      return NULL;
  }

  GST_INFO ("Configuration:");
  GST_INFO ("    Width:          %d", imginfo.nWidth);
  GST_INFO ("    Height:         %d", imginfo.nHeight);
  GST_INFO ("    Format:         %d", imginfo.eFormat);
  GST_INFO ("    Plane:          %d", imginfo.nPlane);
  GST_INFO ("    WidthStride:    %d", imginfo.nWidthStride[0]);
  GST_INFO ("    AlightedSize:   %d", imginfo.nAlignedSize[0]);

  config.nConfigs = 9;
  config.pConfigs = g_new0 (evaConfig, config.nConfigs);

  // Query the default configuration values.
  evaOFQueryConfigIndices(evaOFConfigStrings, &config);

  config.pConfigs[0].uValue.u32 = actlfps;
  config.pConfigs[1].uValue.u32 = actlfps;
  config.pConfigs[2].uValue.ptr = &imginfo;
  config.pConfigs[3].uValue.ptr = &imginfo;
  config.pConfigs[4].uValue.ptr = &amfconf;
  config.pConfigs[5].uValue.b = false;
  config.pConfigs[6].uValue.b = false;
  config.pConfigs[7].uValue.ptr = &mvpackformat;
  config.pConfigs[8].uValue.ptr = &direction;

  // Initialize optical flow using EVA.
  // TODO implement advanced configuration and function callbacks.
  engine->handle = evaInitOF (engine->session, &config, &requirements, NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->handle != NULL, NULL,
      gst_cv_optclflow_engine_free (engine), "Failed to init Optical Flow!");

  engine->mv_size = requirements.nFwdMvMapBytes;

  // Start the EVA session.
  status = evaStartSession (engine->session);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == EVA_SUCCESS, NULL,
      gst_cv_optclflow_engine_free (engine), "Failed to start EVA session!");

  engine->active = TRUE;
  g_free (config.pConfigs);
  GST_INFO ("Created EVA OpticalFlow engine: %p", engine);
  return engine;
}

void
gst_cv_optclflow_engine_free (GstCvOptclFlowEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->evaimages != NULL) {
    g_hash_table_foreach (engine->evaimages, gst_eva_delete_image, engine);
    g_hash_table_destroy (engine->evaimages);
  }

  if (engine->active)
    evaStopSession (engine->session);

  if (engine->handle != NULL)
    evaDeInitOF (engine->handle);

  if (engine->session != NULL)
    evaDeleteSession (engine->session);

  if (engine->settings != NULL)
    gst_structure_free (engine->settings);

  GST_INFO ("Destroyed EVA OpticalFlow engine: %p", engine);
  g_slice_free (GstCvOptclFlowEngine, engine);
}

gboolean
gst_cv_optclflow_engine_sizes (GstCvOptclFlowEngine * engine, guint * mvsize,
    guint * statsize)
{
  g_return_val_if_fail (engine != NULL, FALSE);

  //statsize is keep API same with cvp, not used
  *mvsize = engine->mv_size;

  GST_INFO ("Forword motion vector size: %d", engine->mv_size);
  return TRUE;
}

gboolean
gst_cv_optclflow_engine_execute (GstCvOptclFlowEngine * engine,
    const GstVideoFrame * inframes, guint n_inputs, GstBuffer * outbuffer)
{
  GstMapInfo *outmap = NULL;
  evaImage *evaimages[REQUIRED_N_INPUTS];
  evaOFOutput optcloutput;
  evaOFRefMode refmode = EVA_OF_NEW_FRAME;
  guint idx = 0, n_blocks = 0;
  evaStatus status = EVA_SUCCESS;

  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail ((inframes != NULL) && (n_inputs == REQUIRED_N_INPUTS), FALSE);
  g_return_val_if_fail (outbuffer != NULL, FALSE);

  // Expecting 1 memory blocks
  n_blocks = 1;

  if (gst_buffer_n_memory (outbuffer) != n_blocks) {
    GST_WARNING ("Output buffer has %u memory blocks but engine requires %u!",
        gst_buffer_n_memory (outbuffer), n_blocks);
    return FALSE;
  }

  for (idx = 0; idx < n_inputs; ++idx) {
    const GstVideoFrame *frame = &inframes[idx];
    GstMemory *memory = NULL;
    guint fd = 0;

    // Get the 1st (and only) memory block from the input GstBuffer.
    memory = gst_buffer_peek_memory (frame->buffer, 0);
    GST_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), FALSE,
        "Input buffer %p does not have FD memory!", frame->buffer);

    // Get the input buffer FD from the GstBuffer memory block.
    fd = gst_fd_memory_get_fd (memory);

    if (!g_hash_table_contains (engine->evaimages, GUINT_TO_POINTER (fd))) {
      evaimages[idx] = gst_eva_create_image (engine, frame);
      GST_RETURN_VAL_IF_FAIL (evaimages[idx] != NULL, FALSE,
          "Failed to create EVA image!");

      g_hash_table_insert (engine->evaimages, GUINT_TO_POINTER (fd),
          evaimages[idx]);
    } else {
      // Get the input EVA image from the input hash table.
      evaimages[idx] = (evaImage*) g_hash_table_lookup (engine->evaimages,
          GUINT_TO_POINTER (fd));
      if (idx == 0)
        refmode = EVA_OF_CONTINUOUS;
    }
  }

  outmap = g_new0 (GstMapInfo, n_blocks);

  optcloutput.pFwdMvMap = g_new0 (evaMem, 1);

  //Remain the loop for eva's output extension
  for (idx = 0; idx < n_blocks; ++idx) {
    GstMemory *memory = NULL;
    // Map output buffer memory blocks.
    memory = gst_buffer_peek_memory (outbuffer, idx);
    GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (gst_is_fd_memory (memory) , FALSE,
        g_free (outmap); g_free (optcloutput.pFwdMvMap),
        "Output buffer %p does not have FD memory!");

    if (!gst_memory_map (memory, &outmap[idx], GST_MAP_READWRITE)) {
      GST_ERROR ("Failed to map output memory block at idx %u!", idx);

      g_free (optcloutput.pFwdMvMap);

      for (n_blocks = idx, idx = 0; idx < n_blocks; idx++) {
        memory = gst_buffer_peek_memory (outbuffer, idx);
        gst_memory_unmap (memory, &outmap[idx]);
      }

      g_free (outmap);
      return FALSE;
    }

    if (idx == 0) {
      optcloutput.pFwdMvMap->eType = EVA_MEM_NON_SECURE;
      optcloutput.pFwdMvMap->nFD = gst_fd_memory_get_fd (memory);
      optcloutput.pFwdMvMap->nSize = outmap[idx].size;
      optcloutput.pFwdMvMap->pAddress = outmap[idx].data;
      optcloutput.pFwdMvMap->nOffset = 0;
      optcloutput.nFwdMvMapSize = outmap[idx].size;
    }
  }

  status = evaOF_Sync (engine->handle, evaimages[0], evaimages[1],
    refmode, &optcloutput, NULL);

  g_free (optcloutput.pFwdMvMap);

  for (idx = 0; idx < n_blocks; ++idx) {
    GstMemory *memory = gst_buffer_peek_memory (outbuffer, idx);
    gst_memory_unmap (memory, &outmap[idx]);
  }

  g_free (outmap);

  if (status != EVA_SUCCESS) {
    GST_ERROR ("Failed to process input images!");
    return FALSE;
  }

  gst_eva_append_custom_meta (engine, outbuffer);

  return TRUE;
}
