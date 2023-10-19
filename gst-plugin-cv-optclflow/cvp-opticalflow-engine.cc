/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <cvp/v2.0/cvpTypes.h>
#include <cvp/v2.0/cvpMem.h>
#include <cvp/v2.0/cvpSession.h>
#include <cvp/v2.0/cvpOpticalFlow.h>
#include <cvp/v2.0/cvpUtils.h>

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

#define EXTRACT_DATA_VALUE(data, offset, bits) \
    (data[offset / 32] >> (offset - ((offset / 32) * 32))) & ((1 << bits) - 1)

#define REQUIRED_N_INPUTS             2

#define CVP_MV_X_FIELD_SIZE           9
#define CVP_MV_Y_FIELD_SIZE           7
#define CVP_MV_RESERVED_FIELD_SIZE    12
#define CVP_MV_CONFIDENCE_FIELD_SIZE  4

#define CVP_STATS_VARIANCE_FIELD_SIZE 16
#define CVP_STATS_MEAN_FIELD_SIZE     8
#define CVP_STATS_RESERVED_FIELD_SIZE 8
#define CVP_STATS_BEST_SAD_FIELD_SIZE 16
#define CVP_STATS_SAD_FIELD_SIZE      16

#define CVP_PAXEL_WIDTH               4
#define CVP_PAXEL_HEIGHT              16

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
#define GET_OPT_STATS(s) get_opt_bool(s, \
    GST_CV_OPTCLFLOW_ENGINE_OPT_ENABLE_STATS, DEFAULT_OPT_ENABLE_STATS)

#define GST_CAT_DEFAULT gst_cvp_optclflow_engine_debug_category()

struct _GstCvOptclFlowEngine
{
  GstStructure *settings;

  // CVP session handle.
  cvpSession   session;
  // CVP handle for the OpticalFlow algorithm.
  cvpHandle    handle;
  // Indicates whether the CVP session is active.
  gboolean     active;

  // Size requirements for the Motion Vector and Stats output buffers.
  guint        mvsize;
  guint        statsize;

  // Map of buffer FDs and their corresponding CVP image.
  GHashTable   *cvpimages;
};

static GstDebugCategory *
gst_cvp_optclflow_engine_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("cvp-opticalflow-engine",
        0, "Computer Vision Optical Flow Engine");
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
gst_cvp_append_custom_meta (GstCvOptclFlowEngine * engine,GstBuffer * buffer)
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
  size = CVP_MV_X_FIELD_SIZE;

  // Fill offset and size of the motion vector X field in bits.
  ADD_FIELD_PARAMS (params, &entries, &value, "X", offset, size, false);

  offset += size;
  size = CVP_MV_Y_FIELD_SIZE;

  // Fill offset and size of the motion vector Y field in bits.
  ADD_FIELD_PARAMS (params, &entries, &value, "Y", offset, size, false);

  offset += size;
  size = CVP_MV_RESERVED_FIELD_SIZE;

  // Fill offset and size of the motion vector reserved field in bits.
  ADD_FIELD_PARAMS (params, &entries, &value, "reserved", offset, size, false);

  offset += size;
  size = CVP_MV_CONFIDENCE_FIELD_SIZE;

  // Fill offset and size of the motion vector confidence field in bits.
  ADD_FIELD_PARAMS (params, &entries, &value, "confidence", offset, size, false);

  // Add the motion vector parameters to the main info structure.
  gst_structure_set (info,
      "motion-vector-params", GST_TYPE_STRUCTURE, params, NULL);
  gst_structure_free (params);

  // Sanity checks for errors that should not occur.
  size = CVP_MV_X_FIELD_SIZE + CVP_MV_Y_FIELD_SIZE +
      CVP_MV_RESERVED_FIELD_SIZE + CVP_MV_CONFIDENCE_FIELD_SIZE;
  g_return_if_fail (size == (sizeof (cvpMotionVector) * CHAR_BIT));

  // In case there is a 2nd memory block add statistics information as well.
  if (gst_buffer_n_memory (buffer) == 2) {
    // Names, offsets and sizes in bits of the fields in the statistics.
    params = gst_structure_new_empty ("Statistics");

    offset = 0;
    size = CVP_STATS_VARIANCE_FIELD_SIZE;

    // Fill offset and size of the statistics variance field in bits.
    ADD_FIELD_PARAMS (params, &entries, &value, "variance", offset, size, true);

    offset += size;
    size = CVP_STATS_MEAN_FIELD_SIZE;

    // Fill offset and size of the statistics mean field in bits.
    ADD_FIELD_PARAMS (params, &entries, &value, "mean", offset, size, true);

    offset += size;
    size = CVP_STATS_RESERVED_FIELD_SIZE;

    // Fill offset and size of the statistics reserved field in bits.
    ADD_FIELD_PARAMS (params, &entries, &value, "reserved", offset, size, true);

    offset += size;
    size = CVP_STATS_BEST_SAD_FIELD_SIZE;

    // Fill offset and size of the statistics best-SAD field in bits.
    ADD_FIELD_PARAMS (params, &entries, &value, "best-SAD", offset, size, true);

    offset += size;
    size = CVP_STATS_SAD_FIELD_SIZE;

    // Fill offset and size of the statistics SAD field in bits.
    ADD_FIELD_PARAMS (params, &entries, &value, "SAD", offset, size, true);

    gst_structure_set (info,
        "statistics-params", GST_TYPE_STRUCTURE, params, NULL);
    gst_structure_free (params);

    // Sanity checks for errors that should not occur.
    size = (sizeof cvpOFStats().nVariance) * CHAR_BIT;
    g_return_if_fail (size == CVP_STATS_VARIANCE_FIELD_SIZE);

    size = (sizeof cvpOFStats().nMean) * CHAR_BIT;
    g_return_if_fail (size == CVP_STATS_MEAN_FIELD_SIZE);

    size = (sizeof cvpOFStats().nReserved) * CHAR_BIT;
    g_return_if_fail (size == CVP_STATS_RESERVED_FIELD_SIZE);

    size = (sizeof cvpOFStats().nBestMVSad) * CHAR_BIT;
    g_return_if_fail (size == CVP_STATS_BEST_SAD_FIELD_SIZE);

    size = (sizeof cvpOFStats().nSad) * CHAR_BIT;
    g_return_if_fail (size == CVP_STATS_SAD_FIELD_SIZE);

    size = CVP_STATS_VARIANCE_FIELD_SIZE + CVP_STATS_MEAN_FIELD_SIZE +
        CVP_STATS_RESERVED_FIELD_SIZE + CVP_STATS_BEST_SAD_FIELD_SIZE +
        CVP_STATS_SAD_FIELD_SIZE;
    g_return_if_fail (size == (sizeof (cvpOFStats) * CHAR_BIT));
  }

  g_value_unset (&value);
  g_value_unset (&entries);

  g_value_init (&value, G_TYPE_UINT);

  // Add the dimensions of single motion vector paxel to the info structure.
  g_value_set_uint (&value, CVP_PAXEL_WIDTH);
  gst_structure_set_value (info, "mv-paxel-width", &value);
  g_value_set_uint (&value, CVP_PAXEL_HEIGHT);
  gst_structure_set_value (info, "mv-paxel-height", &value);

  // Add the number of paxels in a single row and column to the info structure.
  g_value_set_uint (&value,
      GST_ROUND_UP_32 (GET_OPT_WIDTH (engine->settings)) / CVP_PAXEL_WIDTH);
  gst_structure_set_value (info, "mv-paxels-row-length", &value);
  g_value_set_uint (&value,
      GST_ROUND_UP_32 (GET_OPT_HEIGHT (engine->settings)) / CVP_PAXEL_HEIGHT);
  gst_structure_set_value (info, "mv-paxels-column-length", &value);

  g_value_unset (&value);

  // Append custom meta to the output buffer for data decryption.
  gst_buffer_add_protection_meta (buffer, info);
}

static cvpImage *
gst_cvp_create_image (GstCvOptclFlowEngine * engine, const GstVideoFrame * frame)
{
  GstMemory *memory = NULL;
  cvpImage *cvpimage = NULL;
  cvpImageInfo *imginfo = NULL;
  cvpStatus status = CVP_SUCCESS;

  // Get the 1st (and only) memory block from the input GstBuffer.
  memory = gst_buffer_peek_memory (frame->buffer, 0);
  GST_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), NULL,
      "Input buffer %p does not have FD memory!", frame->buffer);

  cvpimage = g_new0 (cvpImage, 1);
  GST_RETURN_VAL_IF_FAIL (cvpimage != NULL, NULL,
      "Failed to allocate memory for CVP image!");

  cvpimage->pBuffer = g_new0 (cvpMem, 1);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (cvpimage->pBuffer != NULL, NULL,
      g_free (cvpimage), "Failed to allocate memory for CVP image buffer!");

  cvpimage->pBuffer->eType = CVP_MEM_NON_SECURE;
  cvpimage->pBuffer->nSize = (GST_VIDEO_FRAME_N_PLANES (frame) == 2) ?
      GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1) :
      gst_buffer_get_size (frame->buffer);
  cvpimage->pBuffer->nFD = gst_fd_memory_get_fd (memory);
  cvpimage->pBuffer->pAddress = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  cvpimage->pBuffer->nOffset = GST_VIDEO_FRAME_PLANE_OFFSET (frame, 0);

  imginfo = &cvpimage->sImageInfo;

  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
    case GST_VIDEO_FORMAT_NV12:
      imginfo->eFormat = CVP_COLORFORMAT_NV12;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      imginfo->eFormat = CVP_COLORFORMAT_GRAY_8BIT;
      break;
    default:
      GST_ERROR ("Unsupported video format: %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));

      g_free (cvpimage->pBuffer);
      g_free (cvpimage);

      return NULL;
  }

    imginfo->nWidth = GST_VIDEO_FRAME_WIDTH (frame);
    imginfo->nHeight = GST_VIDEO_FRAME_HEIGHT (frame);
    imginfo->nPlane = GST_VIDEO_FRAME_N_PLANES (frame);
    imginfo->nTotalSize = (GST_VIDEO_FRAME_N_PLANES (frame) == 2) ?
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1) :
        gst_buffer_get_size (frame->buffer);;

    // TODO workaround for CVP NV12 issue - Only use Y plane for all format
    imginfo->eFormat = CVP_COLORFORMAT_GRAY_8BIT;
    imginfo->nPlane = 1;

    imginfo->nWidthStride[0] = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
    imginfo->nAlignedSize[0] = (GST_VIDEO_FRAME_N_PLANES (frame) == 2) ?
        GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1) : imginfo->nTotalSize;

    if (GST_VIDEO_FRAME_N_PLANES (frame) == 2) {
      imginfo->nWidthStride[1] = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      imginfo->nAlignedSize[1] = imginfo->nTotalSize - imginfo->nAlignedSize[0];
    }

  GST_INFO ("Format(%d) Width(%u) Height(%u) Planes(%u) TotalSize(%u)",
      imginfo->eFormat, imginfo->nWidth, imginfo->nHeight, imginfo->nPlane,
      imginfo->nTotalSize);

  GST_INFO ("Plane[0] - Stride(%u) AlignedSize(%u)",
      imginfo->nWidthStride[0], + imginfo->nAlignedSize[0]);

  status = cvpMemRegister (engine->session, cvpimage->pBuffer);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == CVP_SUCCESS, FALSE,
      g_free (cvpimage->pBuffer); g_free (cvpimage),
      "Failed to register CVP image buffer!");

  status = cvpRegisterOpticalFlowImageBuf (engine->handle, cvpimage);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == CVP_SUCCESS, FALSE,
      g_free (cvpimage->pBuffer); g_free (cvpimage);
      cvpMemDeregister (engine->session, cvpimage->pBuffer),
      "Failed to register CVP image!");

  return cvpimage;
}

static void
gst_cvp_delete_image (gpointer key, gpointer value, gpointer userdata)
{
  GstCvOptclFlowEngine *engine = (GstCvOptclFlowEngine*) userdata;
  cvpImage *cvpimage = (cvpImage*) value;
  cvpStatus status = CVP_SUCCESS;

  status = cvpDeregisterOpticalFlowImageBuf (engine->handle, cvpimage);
  if (status != CVP_SUCCESS)
    GST_ERROR ("Failed to deregister CVP image for key %p, "
        "error: %d!", key, status);

  status = cvpMemDeregister (engine->session, cvpimage->pBuffer);
  if (status != CVP_SUCCESS)
    GST_ERROR ("Failed to deregister CVP image buffer for key %p, "
        "error: %d!", key, status);

  g_free (cvpimage->pBuffer);
  g_free (cvpimage);

  GST_DEBUG ("Deleted CVP image for key %p", key);
  return;
}

GstCvOptclFlowEngine *
gst_cv_optclflow_engine_new (GstStructure * settings)
{
  GstCvOptclFlowEngine *engine = NULL;
  cvpConfigOpticalFlow config;
  cvpAdvConfigOpticalFlow advcfg;
  cvpOpticalFlowOutBuffReq requirements;
  cvpImageInfo imginfo;
  cvpStatus status = CVP_SUCCESS;
  guint stride = 0, scanline = 0;

  engine = g_slice_new0 (GstCvOptclFlowEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->settings = gst_structure_copy (settings);
  gst_structure_free (settings);

  engine->cvpimages = g_hash_table_new (NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->cvpimages != NULL, NULL,
      gst_cv_optclflow_engine_free (engine), "Failed to create hash table "
      "for source images!");

  // Create a CVP session.
  // TODO Initialize configuration and callbacks.
  engine->session = cvpCreateSession (NULL, NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->session != NULL, NULL,
      gst_cv_optclflow_engine_free (engine), "Failed to create CVP session!");

  config.eMode              = CVP_OPTICALFLOW_SEVEN_PASS;
  config.sImageInfo.nWidth  = GET_OPT_WIDTH (engine->settings);
  config.sImageInfo.nHeight = GET_OPT_HEIGHT (engine->settings);
  config.nActualFps         = GET_OPT_FPS (engine->settings);
  config.nOperationalFps    = GET_OPT_FPS (engine->settings);
  config.bStatsEnable       = GET_OPT_STATS (engine->settings);

  switch (GET_OPT_FORMAT (engine->settings)) {
    case GST_VIDEO_FORMAT_NV12:
      config.sImageInfo.eFormat = CVP_COLORFORMAT_NV12;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      config.sImageInfo.eFormat = CVP_COLORFORMAT_GRAY_8BIT;
      break;
    default:
      GST_ERROR ("Unsupported video format: %s!",
          gst_video_format_to_string (GET_OPT_FORMAT (engine->settings)));
      gst_cv_optclflow_engine_free (engine);

      return NULL;
  }

  // TODO workaround for CVP NV12 issue - only use Y plane for all format
  config.sImageInfo.eFormat = CVP_COLORFORMAT_GRAY_8BIT;
  config.sImageInfo.nPlane = 1;

  stride = GET_OPT_STRIDE (engine->settings);
  scanline = GET_OPT_SCANLINE (engine->settings);

  config.sImageInfo.nTotalSize = stride * scanline;
  config.sImageInfo.nWidthStride[0] = stride;
  config.sImageInfo.nAlignedSize[0] = config.sImageInfo.nTotalSize;

  GST_INFO ("Configuration:");
  GST_INFO ("    Width:          %d", config.sImageInfo.nWidth);
  GST_INFO ("    Height:         %d", config.sImageInfo.nHeight);
  GST_INFO ("    Format:         %d", config.sImageInfo.eFormat);
  GST_INFO ("    Plane:          %d", config.sImageInfo.nPlane);
  GST_INFO ("    WidthStride:    %d", config.sImageInfo.nWidthStride[0]);
  GST_INFO ("    AlightedSize:   %d", config.sImageInfo.nAlignedSize[0]);

  advcfg.nMvDist = 2;
  advcfg.nMvWeights[0] = 10;
  advcfg.nMvWeights[1] = 2;
  advcfg.nMvWeights[2] = 2;
  advcfg.nMvWeights[3] = 1;
  advcfg.nMvWeights[4] = 1;
  advcfg.nMvWeights[5] = 7;
  advcfg.nMvWeights[6] = 20;
  advcfg.nMedianFiltType = 5;
  advcfg.nThresholdMedFilt = 900;
  advcfg.nSmoothnessPenaltyThresh = 500;
  advcfg.nSearchRangeX = 96;
  advcfg.nSearchRangeY = 48;
  advcfg.bEnableEic = false;

  // Initialize optical flow using CVP.
  // TODO implement advanced configuration and function callbacks.
  engine->handle = cvpInitOpticalFlow (engine->session, &config, &advcfg,
      &requirements, NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->handle != NULL, NULL,
      gst_cv_optclflow_engine_free (engine), "Failed to init Optical Flow!");

  engine->mvsize = requirements.nMotionVectorBytes;
  engine->statsize = requirements.nStatsBytes;

  // Start the CVP session.
  status = cvpStartSession (engine->session);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == CVP_SUCCESS, NULL,
      gst_cv_optclflow_engine_free (engine), "Failed to start CVP session!");

  engine->active = TRUE;
  GST_INFO ("Created CVP OpticalFlow engine: %p", engine);
  return engine;
}

void
gst_cv_optclflow_engine_free (GstCvOptclFlowEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->cvpimages != NULL) {
    g_hash_table_foreach (engine->cvpimages, gst_cvp_delete_image, engine);
    g_hash_table_destroy (engine->cvpimages);
  }

  if (engine->active)
    cvpStopSession (engine->session);

  if (engine->handle != NULL)
    cvpDeInitOpticalFlow (engine->handle);

  if (engine->session != NULL)
    cvpDeleteSession (engine->session);

  if (engine->settings != NULL)
    gst_structure_free (engine->settings);

  GST_INFO ("Destroyed CVP OpticalFlow engine: %p", engine);
  g_slice_free (GstCvOptclFlowEngine, engine);
}

gboolean
gst_cv_optclflow_engine_sizes (GstCvOptclFlowEngine * engine, guint * mvsize,
    guint * statsize)
{
  g_return_val_if_fail (engine != NULL, FALSE);

  *mvsize = engine->mvsize;
  *statsize = engine->statsize;

  GST_INFO ("MV size: %d, Stats size: %d", engine->mvsize, engine->statsize);
  return TRUE;
}

gboolean
gst_cv_optclflow_engine_execute (GstCvOptclFlowEngine * engine,
    const GstVideoFrame * inframes, guint n_inputs, GstBuffer * outbuffer)
{
  GstMapInfo *outmap = NULL;
  cvpImage *cvpimages[REQUIRED_N_INPUTS];
  cvpOpticalFlowOutput optcloutput;
  guint idx = 0, n_blocks = 0;
  cvpStatus status = CVP_SUCCESS;

  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail ((inframes != NULL) && (n_inputs == REQUIRED_N_INPUTS), FALSE);
  g_return_val_if_fail (outbuffer != NULL, FALSE);

  // Expecting 2 memory blocks if stats is enabled.
  n_blocks = GET_OPT_STATS (engine->settings) ? 2 : 1;

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

    if (!g_hash_table_contains (engine->cvpimages, GUINT_TO_POINTER (fd))) {
      cvpimages[idx] = gst_cvp_create_image (engine, frame);
      GST_RETURN_VAL_IF_FAIL (cvpimages[idx] != NULL, FALSE,
          "Failed to create CVP image!");

      g_hash_table_insert (engine->cvpimages, GUINT_TO_POINTER (fd),
          cvpimages[idx]);
    } else {
      // Get the input CVP image from the input hash table.
      cvpimages[idx] = (cvpImage*) g_hash_table_lookup (engine->cvpimages,
          GUINT_TO_POINTER (fd));
    }
  }

  outmap = g_new0 (GstMapInfo, n_blocks);

  optcloutput.pMotionVector = g_new0 (cvpMem, 1);
  optcloutput.pStats = (n_blocks == 2) ? g_new0 (cvpMem, 1) : NULL;

  for (idx = 0; idx < n_blocks; ++idx) {
    GstMemory *memory = NULL;
    // Map output buffer memory blocks.
    memory = gst_buffer_peek_memory (outbuffer, idx);
    GST_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory) , FALSE,
        "Output buffer %p does not have FD memory!", outbuffer);

    if (!gst_memory_map (memory, &outmap[idx], GST_MAP_READWRITE)) {
      GST_ERROR ("Failed to map output memory block at idx %u!", idx);

      g_free (optcloutput.pMotionVector);
      g_free (optcloutput.pStats);

      for (n_blocks = idx, idx = 0; idx < n_blocks; idx++) {
        memory = gst_buffer_peek_memory (outbuffer, idx);
        gst_memory_unmap (memory, &outmap[idx]);
      }

      g_free (outmap);
      return FALSE;
    }

    if (idx == 0) {
      optcloutput.pMotionVector->eType = CVP_MEM_NON_SECURE;
      optcloutput.pMotionVector->nFD = gst_fd_memory_get_fd (memory);
      optcloutput.pMotionVector->nSize = outmap[idx].size;
      optcloutput.pMotionVector->pAddress = outmap[idx].data;
      optcloutput.pMotionVector->nOffset = 0;
      optcloutput.nMVSize = outmap[idx].size;
    } else if (GET_OPT_STATS (engine->settings) && idx == 1) {
      optcloutput.pStats->eType = CVP_MEM_NON_SECURE;
      optcloutput.pStats->nFD = gst_fd_memory_get_fd (memory);
      optcloutput.pStats->nSize = outmap[idx].size;
      optcloutput.pStats->pAddress = outmap[idx].data;
      optcloutput.pStats->nOffset = 0;
      optcloutput.nStatsSize = outmap[idx].size;
    }
  }

  status = cvpOpticalFlow_Sync (engine->handle, cvpimages[0], cvpimages[1],
      true, true, &optcloutput);

  g_free (optcloutput.pMotionVector);
  g_free (optcloutput.pStats);

  for (idx = 0; idx < n_blocks; ++idx) {
    GstMemory *memory = gst_buffer_peek_memory (outbuffer, idx);
    gst_memory_unmap (memory, &outmap[idx]);
  }

  g_free (outmap);

  if (status != CVP_SUCCESS) {
    GST_ERROR ("Failed to process input images!");
    return FALSE;
  }

  gst_cvp_append_custom_meta (engine, outbuffer);
  return TRUE;
}
