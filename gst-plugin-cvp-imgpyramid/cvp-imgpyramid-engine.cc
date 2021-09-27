/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cvp-imgpyramid-engine.h"

#include <cvp/v2.0/cvpTypes.h>
#include <cvp/v2.0/cvpMem.h>
#include <cvp/v2.0/cvpSession.h>
#include <cvp/v2.0/cvpPyramid.h>
#include <cvp/v2.0/cvpUtils.h>

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

#define GST_CAT_DEFAULT gst_cvp_imgpyramid_engine_debug_category ()

struct _GstCvpImgPyramidEngine
{
  // CVP session handle.
  cvpSession             session;
  // CVP handle for the PyramidImage algorithm.
  cvpHandle              handle;
  // Number of pyramid levels.
  guint                  nlevels;

  // Map of input buffer FDs and their corresponding CVP image.
  GHashTable             *incvpimages;
  // Output CVP images
  cvpImage               *outimages;
  // Output buffer map
  GstMapInfo             *outmaps;
};

static GstDebugCategory *
gst_cvp_imgpyramid_engine_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("cvp-imgpyramid-engine",
        0, "Computer Vision Pyramid Image Engine");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

static cvpImage *
gst_cvp_create_image (GstCvpImgPyramidEngine * engine, const GstVideoFrame * frame)
{
  GstMemory *memory = NULL;
  cvpImage *cvpimage = NULL;
  cvpImageInfo *imginfo = NULL;
  cvpStatus status = CVP_SUCCESS;

  // Get the memory block from the GstBuffer.
  memory = gst_buffer_peek_memory (frame->buffer, 0);
  GST_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), NULL,
      "The buffer %p does not have FD memory!", frame->buffer);

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
    case GST_VIDEO_FORMAT_GRAY8:
      // TODO workaround for CVP NV12 issue - Only use Y plane for all format
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
      imginfo->nWidthStride[0], imginfo->nAlignedSize[0]);

  status = cvpMemRegister (engine->session, cvpimage->pBuffer);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == CVP_SUCCESS, FALSE,
      g_free (cvpimage->pBuffer); g_free (cvpimage),
      "Failed to register CVP image buffer!");

  return cvpimage;
}

static void
gst_cvp_delete_image (gpointer key, gpointer value, gpointer userdata)
{
  GstCvpImgPyramidEngine *engine = (GstCvpImgPyramidEngine*) userdata;
  cvpImage *cvpimage = (cvpImage*) value;
  cvpStatus status = CVP_SUCCESS;

  status = cvpMemDeregister (engine->session, cvpimage->pBuffer);
  if (status != CVP_SUCCESS)
    GST_ERROR ("Failed to deregister CVP image buffer for key %p, "
        "error: %d!", key, status);

  g_free (cvpimage->pBuffer);
  g_free (cvpimage);

  GST_DEBUG ("Deleted CVP image for key %p", key);
  return;
}

GstCvpImgPyramidEngine *
gst_cvp_imgpyramid_engine_new (GstCvpImgPyramidSettings * settings, GArray * sizes)
{
  GstCvpImgPyramidEngine *engine = NULL;
  cvpConfigPyramidImage config;
  cvpPyramidImageOutBuffReq requirements;
  cvpImageInfo imginfo;
  cvpStatus status = CVP_SUCCESS;
  guint stride = 0, scanline = 0, idx;

  engine = g_slice_new0 (GstCvpImgPyramidEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->incvpimages = g_hash_table_new (NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->incvpimages != NULL, NULL,
      gst_cvp_imgpyramid_engine_free (engine), "Failed to create hash table "
      "for input images!");

  engine->session = cvpCreateSession (NULL, NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->session != NULL, NULL,
      gst_cvp_imgpyramid_engine_free (engine), "Failed to create CVP session!");

  config.nActualFps            = settings->framerate;
  config.nOperationalFps       = settings->framerate;
  config.eOutFormat            = CVP_COLORFORMAT_GRAY_8BIT;
  config.nOctaves              = settings->n_octaves;
  config.nScalesPerOctave      = settings->n_scales;
  config.sSrcImageInfo.nWidth  = settings->width;
  config.sSrcImageInfo.nHeight = settings->height;
  for (idx = 0; idx < settings->n_octaves; idx++) {
    config.nFilterDiv2Coeff[idx] = g_array_index (settings->div2coef, guint, idx);
  }

  switch (settings->format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_GRAY8:
      // TODO workaround for CVP NV12 issue - only use Y plane for all format
      config.sSrcImageInfo.eFormat = CVP_COLORFORMAT_GRAY_8BIT;
      break;
    default:
      GST_ERROR ("Unsupported video format: %s!",
          gst_video_format_to_string (settings->format));
      gst_cvp_imgpyramid_engine_free (engine);

      return NULL;
  }

  config.sSrcImageInfo.nPlane = 1;

  stride = settings->stride;
  scanline = settings->scanline;

  config.sSrcImageInfo.nTotalSize = stride * scanline;
  config.sSrcImageInfo.nWidthStride[0] = stride;
  config.sSrcImageInfo.nAlignedSize[0] = config.sSrcImageInfo.nTotalSize;

  engine->handle = cvpInitPyramidImage (engine->session, &config, &requirements,
      NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->handle != NULL, NULL,
      gst_cvp_imgpyramid_engine_free (engine), "Failed to init Pyramid Image!");

  GST_INFO ("Configuration:");
  GST_INFO ("    Stride:         %d", stride);
  GST_INFO ("    Scanline:       %d", scanline);
  GST_INFO ("    Width:          %d", config.sSrcImageInfo.nWidth);
  GST_INFO ("    Height:         %d", config.sSrcImageInfo.nHeight);
  GST_INFO ("    Format:         %d", config.sSrcImageInfo.eFormat);
  GST_INFO ("    Plane:          %d", config.sSrcImageInfo.nPlane);
  GST_INFO ("    WidthStride:    %d", config.sSrcImageInfo.nWidthStride[0]);
  GST_INFO ("    AlightedSize:   %d", config.sSrcImageInfo.nAlignedSize[0]);

  engine->nlevels = requirements.nLevels;
  // Create placeholder for the output
  engine->outimages = g_new0 (cvpImage, requirements.nLevels);
  engine->outmaps = g_new0 (GstMapInfo, requirements.nLevels);

  for (idx=0; idx < engine->nlevels; idx++) {
    g_array_append_val (sizes, requirements.nImageBytes[idx]);
    engine->outimages[idx].pBuffer = g_new0 (cvpMem, 1);
  }

  // Start the CVP session.
  status = cvpStartSession (engine->session);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == CVP_SUCCESS, NULL,
      gst_cvp_imgpyramid_engine_free (engine), "Failed to start CVP session!");

  GST_INFO ("Created CVP Pyramid Scaler engine: %p", engine);
  return engine;
}

void
gst_cvp_imgpyramid_engine_free (GstCvpImgPyramidEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->incvpimages != NULL) {
    g_hash_table_foreach (engine->incvpimages, gst_cvp_delete_image, engine);
    g_hash_table_destroy (engine->incvpimages);
  }

  gint i;
  for (i = 0; i < engine->nlevels; i++) {
    g_free (engine->outimages[i].pBuffer);
  }

  g_free (engine->outimages);
  g_free (engine->outmaps);

  cvpStopSession (engine->session);

  if (engine->handle != NULL)
    cvpDeInitPyramidImage (engine->handle);

  if (engine->session != NULL)
    cvpDeleteSession (engine->session);

  GST_INFO ("Destroyed CVP Pyramid Scaler engine: %p", engine);
  g_slice_free (GstCvpImgPyramidEngine, engine);
}

gboolean
gst_cvp_imgpyramid_engine_execute (GstCvpImgPyramidEngine * engine,
    const GstVideoFrame * inframe, GstBufferList * outbuffers)
{
  cvpImage *incvpimage, *outimages;
  guint idx = 0, fd = 0;
  GstBuffer *outbuffer = NULL;
  GstMemory *memory = NULL;
  GstMapInfo *outmap = NULL;
  cvpPyramidImage imgpyramidout;
  cvpStatus status = CVP_SUCCESS;
  guint n_outputs = gst_buffer_list_length (outbuffers);


  g_return_val_if_fail (engine != NULL, FALSE);
  g_return_val_if_fail (inframe != NULL, FALSE);
  g_return_val_if_fail (outbuffers != NULL , FALSE);

  // Get the memory block from the input GstBuffer.
  memory = gst_buffer_peek_memory (inframe->buffer, 0);
  GST_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), FALSE,
      "Input buffer %p does not have FD memory!", inframe->buffer);

  // Get the input buffer FD from the GstBuffer memory block.
  fd = gst_fd_memory_get_fd (memory);

  if (!g_hash_table_contains (engine->incvpimages, GUINT_TO_POINTER (fd))) {
    incvpimage = gst_cvp_create_image (engine, inframe);
    GST_RETURN_VAL_IF_FAIL (incvpimage != NULL, FALSE,
        "Failed to create input CVP image!");

    g_hash_table_insert (engine->incvpimages, GUINT_TO_POINTER (fd),
        incvpimage);
  } else {
    // Get the input CVP image from the input hash table.
    incvpimage = (cvpImage*) g_hash_table_lookup (engine->incvpimages,
        GUINT_TO_POINTER (fd));
  }

  n_outputs = engine->nlevels;
  outimages = engine->outimages;
  for (idx = 1; idx < n_outputs; idx++) {
    outbuffer = gst_buffer_list_get (outbuffers, idx-1);
    cvpImage *outimage = &outimages[idx];
    outmap = &(engine->outmaps[idx]);
    // Map output buffer memory blocks.
    memory = gst_buffer_peek_memory (outbuffer, 0);
    GST_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), FALSE,
        "Output buffer %p does not have FD memory!", outbuffer);

    if (!gst_buffer_map (outbuffer, outmap, GST_MAP_READWRITE)) {
      GST_ERROR ("Failed to map output buffer at idx %u!", idx);

      for (n_outputs = idx, idx = 1; idx < n_outputs; idx++) {
        outbuffer = gst_buffer_list_get (outbuffers, idx-1);
        outmap = &(engine->outmaps[idx]);
        gst_buffer_unmap (outbuffer, outmap);
      }
      return FALSE;
    }

    outimage->pBuffer->eType = CVP_MEM_NON_SECURE;
    outimage->pBuffer->nFD   = gst_fd_memory_get_fd (memory);
    outimage->pBuffer->nSize = outmap->size;
    outimage->pBuffer->pAddress = outmap->data;
    outimage->pBuffer->nOffset = 0;
  }

  imgpyramidout.pImage = outimages;
  status = cvpPyramidImage_Sync (engine->handle, incvpimage, &imgpyramidout);

  // Set buffer info for the buffers based on the output info from cvp
  gsize offset[1] = { 0 };
  gint  stride[1] = { 0 };
  guint width = 0, height = 0, nplanes = 0, size = 0;
  for (idx = 1; idx < n_outputs; idx++) {
    GstBuffer *outbuffer = gst_buffer_list_get (outbuffers, idx-1);

    width = imgpyramidout.pImage[idx].sImageInfo.nWidth;
    height = imgpyramidout.pImage[idx].sImageInfo.nHeight;
    nplanes = imgpyramidout.pImage[idx].sImageInfo.nPlane;
    stride[0] = imgpyramidout.pImage[idx].sImageInfo.nWidthStride[0];
    size = imgpyramidout.pImage[idx].sImageInfo.nTotalSize;
    GST_TRACE ("Outbuffer meta info, wxh=%ux%u, nplanes=%u, stride=%d size=%u",
        width, height, nplanes, stride[0], size);
    gst_buffer_add_video_meta_full (outbuffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_FORMAT_GRAY8, width, height, nplanes, offset, stride);
  }

  for (idx = 1; idx < n_outputs; idx++) {
    outbuffer = gst_buffer_list_get (outbuffers, idx-1);
    outmap = &(engine->outmaps[idx]);
    gst_buffer_unmap (outbuffer, outmap);
  }

  if (status != CVP_SUCCESS) {
    GST_ERROR ("Failed to process input images!");
    return FALSE;
  }

  return TRUE;
}