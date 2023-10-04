/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "imgpyramid-engine.h"

#include <evaTypes.h>
#include <evaMem.h>
#include <evaSession.h>
#include <evaPyramid.h>
#include <evaUtils.h>

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

#define GST_CAT_DEFAULT gst_eva_imgpyramid_engine_debug_category ()

struct _GstImgPyramidEngine
{
  // EVA session handle.
  evaSession             session;
  // EVA handle for the PyramidImage algorithm.
  evaHandle              handle;
  // Number of pyramid levels.
  guint                  nlevels;

  // Map of input buffer FDs and their corresponding EVA image.
  GHashTable             *inevaimages;
  // Output EVA images
  evaImage               *outimages;
  // Output buffer map
  GstMapInfo             *outmaps;
  // UBWC flag
  gboolean               is_ubwc;
};

static GstDebugCategory *
gst_eva_imgpyramid_engine_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("eva-imgpyramid-engine",
        0, "Engine for Video Analytics Pyramid Engine");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *) catonce;
}

static evaImage *
gst_eva_create_image (GstImgPyramidEngine * engine, const GstVideoFrame * frame)
{
  GstMemory *memory = NULL;
  evaImage *evaimage = NULL;
  evaImageInfo *imginfo = NULL;
  evaStatus status = EVA_SUCCESS;

  // Get the memory block from the GstBuffer.
  memory = gst_buffer_peek_memory (frame->buffer, 0);
  GST_RETURN_VAL_IF_FAIL (gst_is_fd_memory (memory), NULL,
      "The buffer %p does not have FD memory!", frame->buffer);

  evaimage = g_new0 (evaImage, 1);
  GST_RETURN_VAL_IF_FAIL (evaimage != NULL, NULL,
      "Failed to allocate memory for EVA image!");

  evaimage->pBuffer = g_new0 (evaMem, 1);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (evaimage->pBuffer != NULL, NULL,
      g_free (evaimage), "Failed to allocate memory for EVA image buffer!");

  evaimage->pBuffer->eType = EVA_MEM_NON_SECURE;
  evaimage->pBuffer->nSize = (GST_VIDEO_FRAME_N_PLANES (frame) == 2) ?
      GST_VIDEO_FRAME_PLANE_OFFSET (frame, 1) :
      gst_buffer_get_size (frame->buffer);
  evaimage->pBuffer->nFD = gst_fd_memory_get_fd (memory);
  evaimage->pBuffer->pAddress = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  evaimage->pBuffer->nOffset = GST_VIDEO_FRAME_PLANE_OFFSET (frame, 0);

  imginfo = &evaimage->sImageInfo;

  switch (GST_VIDEO_FRAME_FORMAT (frame)) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_GRAY8:
      // TODO workaround for EVA NV12 issue - Only use Y plane for all format
      imginfo->eFormat =
          engine->is_ubwc ? EVA_COLORFORMAT_GRAY_UBWC : EVA_COLORFORMAT_GRAY_8BIT;
      break;
    default:
      GST_ERROR ("Unsupported video format: %s!",
          gst_video_format_to_string (GST_VIDEO_FRAME_FORMAT (frame)));

      g_free (evaimage->pBuffer);
      g_free (evaimage);

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

  return evaimage;
}

static void
gst_eva_delete_image (gpointer key, gpointer value, gpointer userdata)
{
  GstImgPyramidEngine *engine = (GstImgPyramidEngine*) userdata;
  evaImage *evaimage = (evaImage*) value;
  evaStatus status = EVA_SUCCESS;

  g_free (evaimage->pBuffer);
  g_free (evaimage);

  GST_DEBUG ("Deleted EVA image for key %p", key);
  return;
}

static void
setup_output (GstImgPyramidEngine * engine,
    evaPyrImgOutBuffReq * outbufreq, GstVideoFormat format)
{
  guint idx;

  engine->nlevels = outbufreq->nLevels;

  // Create placeholder for the output
  engine->outimages = g_new0 (evaImage, engine->nlevels);
  engine->outmaps = g_new0 (GstMapInfo, engine->nlevels);

  for (idx=0; idx < engine->nlevels; idx++) {
    engine->outimages[idx].pBuffer = g_new0 (evaMem, 1);
  }
}

GstImgPyramidEngine *
gst_imgpyramid_engine_new (GstImgPyramidSettings * settings,
    GArray * sizes)
{
  GstImgPyramidEngine *engine = NULL;
  evaConfigList config;
  evaPyrImgOutBuffReq requirements;
  evaImageInfo srcimginfo;
  evaColorFormat outformat;
  evaStatus status = EVA_SUCCESS;
  guint stride = settings->stride, scanline = settings->scanline, idx;

  engine = g_slice_new0 (GstImgPyramidEngine);
  g_return_val_if_fail (engine != NULL, NULL);

  engine->inevaimages = g_hash_table_new (NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->inevaimages != NULL, NULL,
      gst_imgpyramid_engine_free (engine), "Failed to create hash table "
      "for input images!");

  engine->session = evaCreateSession (NULL, NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->session != NULL, NULL,
      gst_imgpyramid_engine_free (engine), "Failed to create EVA session!");

  switch (settings->format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_GRAY8:
      srcimginfo.eFormat = EVA_COLORFORMAT_GRAY_8BIT;
      if (settings->is_ubwc) {
        engine->is_ubwc = settings->is_ubwc;
        srcimginfo.eFormat = EVA_COLORFORMAT_GRAY_UBWC;
      }
      break;
    default:
      GST_ERROR ("Unsupported video format: %s!",
          gst_video_format_to_string (settings->format));
      gst_imgpyramid_engine_free (engine);
      return NULL;
  }

  srcimginfo.nWidth          = settings->width;
  srcimginfo.nHeight         = settings->height;
  srcimginfo.nPlane          = 1;
  srcimginfo.nTotalSize      = stride * scanline;
  srcimginfo.nWidthStride[0] = stride;
  srcimginfo.nAlignedSize[0] = srcimginfo.nTotalSize;
  outformat                  =
      engine->is_ubwc ? EVA_COLORFORMAT_GRAY_UBWC : EVA_COLORFORMAT_GRAY_8BIT;

  config.nConfigs = EVA_PYRIMG_NUM_ICONFIG;
  config.pConfigs = g_new0 (evaConfig, config.nConfigs);
  evaPyramidQueryConfigIndices(evaPyramidConfigStrings, &config);
  // CONFIG_ACTUAL_FPS
  config.pConfigs[0].uValue.u32                   = settings->framerate;
  // CONFIG_OPERATIONAL_FPS
  config.pConfigs[1].uValue.u32                   = settings->framerate;
  // CONFIG_SOURCE_IMAGE_INFO
  config.pConfigs[2].uValue.ptr                   = &srcimginfo;
  // CONFIG_OCTAVES
  config.pConfigs[3].uValue.u32                   = settings->n_octaves;
  // CONFIG_SCALES_PER_OCTAVE
  config.pConfigs[4].uValue.u32                   = settings->n_scales;
  // CONFIG_OUTPUT_COLOR_FORMAT
  config.pConfigs[5].uValue.ptr                   = &outformat;
  // CONFIG_OUTPUT_BASEIMAGE
  config.pConfigs[6].uValue.b                     = false;

  engine->handle = evaInitPyrImg (engine->session, &config, &requirements,
      NULL, NULL);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (engine->handle != NULL, NULL,
      gst_imgpyramid_engine_free (engine), "Failed to init Pyramid Image!");

  GST_INFO ("Input Configuration:");
  GST_INFO ("    Stride:         %d", stride);
  GST_INFO ("    Scanline:       %d", scanline);
  GST_INFO ("    Width:          %d", srcimginfo.nWidth);
  GST_INFO ("    Height:         %d", srcimginfo.nHeight);
  GST_INFO ("    Format:         %d", srcimginfo.eFormat);
  GST_INFO ("    Plane:          %d", srcimginfo.nPlane);
  GST_INFO ("    WidthStride:    %d", srcimginfo.nWidthStride[0]);
  GST_INFO ("    AlightedSize:   %d", srcimginfo.nAlignedSize[0]);

  setup_output (engine, &requirements, settings->format);

  // Start the EVA session.
  status = evaStartSession (engine->session);
  GST_RETURN_VAL_IF_FAIL_WITH_CLEAN (status == EVA_SUCCESS, NULL,
      gst_imgpyramid_engine_free (engine), "Failed to start EVA session!");

  for (idx = 0; idx < engine->nlevels; idx++) {
    g_array_append_val (sizes, requirements.nImageBytes[idx]);
  }

  GST_INFO ("Created EVA Pyramid Scaler engine: %p", engine);
  return engine;
}

void
gst_imgpyramid_engine_free (GstImgPyramidEngine * engine)
{
  if (NULL == engine)
    return;

  if (engine->inevaimages != NULL) {
    g_hash_table_foreach (engine->inevaimages, gst_eva_delete_image, engine);
    g_hash_table_destroy (engine->inevaimages);
  }

  gint i;
  for (i = 0; i < engine->nlevels; i++) {
    g_free (engine->outimages[i].pBuffer);
  }

  g_free (engine->outimages);
  g_free (engine->outmaps);

  evaStopSession (engine->session);

  if (engine->handle != NULL)
    evaDeInitPyrImg (engine->handle);

  if (engine->session != NULL)
    evaDeleteSession (engine->session);

  GST_INFO ("Destroyed EVA Pyramid Scaler engine: %p", engine);
  g_slice_free (GstImgPyramidEngine, engine);
}

gboolean
gst_imgpyramid_engine_execute (GstImgPyramidEngine * engine,
    const GstVideoFrame * inframe, GstBufferList * outbuffers)
{
  evaImage *inevaimage, *outimages;
  guint idx = 0, fd = 0;
  GstBuffer *outbuffer = NULL;
  GstMemory *memory = NULL;
  GstMapInfo *outmap = NULL;
  evaPyrImg imgpyramidout;
  evaConfigList config;
  evaScaledownInterpolation interpolation = EVA_SCALEDOWN_BILINEAR;
  evaStatus status = EVA_SUCCESS;
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

  if (!g_hash_table_contains (engine->inevaimages, GUINT_TO_POINTER (fd))) {
    inevaimage = gst_eva_create_image (engine, inframe);
    GST_RETURN_VAL_IF_FAIL (inevaimage != NULL, FALSE,
        "Failed to create input EVA image!");

    g_hash_table_insert (engine->inevaimages, GUINT_TO_POINTER (fd),
        inevaimage);
  } else {
    // Get the input EVA image from the input hash table.
    inevaimage = (evaImage*) g_hash_table_lookup (engine->inevaimages,
        GUINT_TO_POINTER (fd));
  }

  n_outputs = engine->nlevels;
  outimages = engine->outimages;
  for (idx = 1; idx < n_outputs; idx++) {
    outbuffer = gst_buffer_list_get (outbuffers, idx-1);
    evaImage *outimage = &outimages[idx];
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

    outimage->pBuffer->eType = EVA_MEM_NON_SECURE;
    outimage->pBuffer->nFD   = gst_fd_memory_get_fd (memory);
    outimage->pBuffer->nSize = outmap->size;
    outimage->pBuffer->pAddress = outmap->data;
    outimage->pBuffer->nOffset = 0;
  }

  imgpyramidout.pImage = outimages;
  config.nConfigs = 1;
  config.pConfigs = g_new0 (evaConfig, config.nConfigs);
  config.pConfigs[0].eType = EVA_PTR;
  config.pConfigs[0].nIndex = 7;
  config.pConfigs[0].uValue.ptr = &interpolation;
  status = evaPyrImg_Sync (engine->handle, inevaimage, &imgpyramidout, &config);

  // Set buffer info for the buffers based on the output info from EVA
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

  if (status != EVA_SUCCESS) {
    GST_ERROR ("Failed to process input images!");
    return FALSE;
  }

  return TRUE;
}