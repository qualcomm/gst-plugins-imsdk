/*
* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "dfs-engine.h"
#include <dfs_factory.h>
#include <rvDFS.h>

#include<iomanip>
#include<iostream>

// DFS lib is looking for those symbols
int RV_LOG_LEVEL = 0;
bool RV_STDERR_LOGGING = true;

#if (RVSDK_API_VERSION >= 0x202403)
typedef rv_dfs::PointCloudType<float> PointCloudType;
#endif // RVSDK_API_VERSION

struct _GstDfsEngine
{
  rvDFS *handle;
  gint mode;
  void *out_work_buffer;
  GstVideoFormat format;
  guint32 width;
  guint32 height;
  guint32 stride;
  guint point_cloud_size;
};

static inline rvDFSMode
gst_rv_translate_mode (const DFSMode mode)
{
  switch (mode) {
    case MODE_CVP:
      return RV_DFS_CVP;
    case MODE_COVERAGE:
      return RV_DFS_COVERAGE;
    case MODE_SPEED:
      return RV_DFS_SPEED;
#if (RVSDK_API_VERSION >= 0x202403)
    case MODE_BALANCE:
      return RV_DFS_BALANCE;
#else
    case MODE_ACCURACY:
      return RV_DFS_ACCURACY;
#endif // RVSDK_API_VERSION
    default:
      break;
  }

  return RV_DFS_SPEED;
}

#if (RVSDK_API_VERSION >= 0x202307)
static void
fill_stereo_params (rvStereoCamera * rv_stereo_param,
    stereoConfiguration * stereo_param)
{
  g_assert (sizeof (rv_stereo_param->translation) ==
      sizeof (stereo_param->translation));
  memcpy (rv_stereo_param->translation, stereo_param->translation,
      sizeof (stereo_param->translation));

  g_assert (sizeof (rv_stereo_param->rotation) ==
      sizeof (stereo_param->rotation));
  memcpy (rv_stereo_param->rotation, stereo_param->rotation,
      sizeof (stereo_param->rotation));

  rv_stereo_param->camera[0].pixelWidth = stereo_param->camera[0].pixelWidth;
  rv_stereo_param->camera[1].pixelWidth = stereo_param->camera[1].pixelWidth;
  rv_stereo_param->camera[0].pixelHeight = stereo_param->camera[0].pixelHeight;
  rv_stereo_param->camera[1].pixelHeight = stereo_param->camera[1].pixelHeight;
  rv_stereo_param->camera[0].pixelStride =
      stereo_param->camera[0].memoryStride;
  rv_stereo_param->camera[1].pixelStride =
      stereo_param->camera[1].memoryStride;

  g_assert (sizeof (rv_stereo_param->camera[0].principalPoint) ==
      sizeof (stereo_param->camera[0].principalPoint));
  g_assert (sizeof (rv_stereo_param->camera[1].principalPoint) ==
      sizeof (stereo_param->camera[1].principalPoint));
  memcpy (rv_stereo_param->camera[0].principalPoint,
      stereo_param->camera[0].principalPoint,
      sizeof (stereo_param->camera[0].principalPoint));
  memcpy (rv_stereo_param->camera[1].principalPoint,
      stereo_param->camera[1].principalPoint,
      sizeof (stereo_param->camera[1].principalPoint));

  g_assert (sizeof (rv_stereo_param->camera[0].focalLength) ==
      sizeof (stereo_param->camera[0].focalLength));
  g_assert (sizeof (rv_stereo_param->camera[1].focalLength) ==
      sizeof (stereo_param->camera[1].focalLength));
  memcpy (rv_stereo_param->camera[0].focalLength,
      stereo_param->camera[0].focalLength,
      sizeof (stereo_param->camera[0].focalLength));
  memcpy (rv_stereo_param->camera[1].focalLength,
      stereo_param->camera[1].focalLength,
      sizeof (stereo_param->camera[1].focalLength));

  g_assert (sizeof (rv_stereo_param->camera[0].distortion) ==
      sizeof (stereo_param->camera[0].distortion));
  g_assert (sizeof (rv_stereo_param->camera[1].distortion) ==
      sizeof (stereo_param->camera[1].distortion));
  memcpy (rv_stereo_param->camera[0].distortion,
      stereo_param->camera[0].distortion,
      sizeof (stereo_param->camera[0].distortion));
  memcpy (rv_stereo_param->camera[1].distortion,
      stereo_param->camera[1].distortion,
      sizeof (stereo_param->camera[1].distortion));

  rv_stereo_param->camera[0].distortionModel =
      static_cast<rvDistortionModel>(stereo_param->camera[0].distortionModel);
  rv_stereo_param->camera[1].distortionModel =
      static_cast<rvDistortionModel>(stereo_param->camera[1].distortionModel);
}
#else
static void
fill_stereo_params (rvStereoConfiguration * rv_stereo_param,
    stereoConfiguration * stereo_param)
{
  g_assert (sizeof (rv_stereo_param->translation) ==
      sizeof (stereo_param->translation));
  memcpy (rv_stereo_param->translation, stereo_param->translation,
      sizeof (stereo_param->translation));

  g_assert (sizeof (rv_stereo_param->rotation) ==
      sizeof (stereo_param->rotation));
  memcpy (rv_stereo_param->rotation, stereo_param->rotation,
      sizeof (stereo_param->rotation));

  rv_stereo_param->camera[0].pixelWidth = stereo_param->camera[0].pixelWidth;
  rv_stereo_param->camera[1].pixelWidth = stereo_param->camera[1].pixelWidth;
  rv_stereo_param->camera[0].pixelHeight = stereo_param->camera[0].pixelHeight;
  rv_stereo_param->camera[1].pixelHeight = stereo_param->camera[1].pixelHeight;
  rv_stereo_param->camera[0].memoryStride =
      stereo_param->camera[0].memoryStride;
  rv_stereo_param->camera[1].memoryStride =
      stereo_param->camera[1].memoryStride;
  rv_stereo_param->camera[0].uvOffset = stereo_param->camera[0].uvOffset;
  rv_stereo_param->camera[1].uvOffset = stereo_param->camera[1].uvOffset;

  g_assert (sizeof (rv_stereo_param->camera[0].principalPoint) ==
      sizeof (stereo_param->camera[0].principalPoint));
  g_assert (sizeof (rv_stereo_param->camera[1].principalPoint) ==
      sizeof (stereo_param->camera[1].principalPoint));
  memcpy (rv_stereo_param->camera[0].principalPoint,
      stereo_param->camera[0].principalPoint,
      sizeof (stereo_param->camera[0].principalPoint));
  memcpy (rv_stereo_param->camera[1].principalPoint,
      stereo_param->camera[1].principalPoint,
      sizeof (stereo_param->camera[1].principalPoint));

  g_assert (sizeof (rv_stereo_param->camera[0].focalLength) ==
      sizeof (stereo_param->camera[0].focalLength));
  g_assert (sizeof (rv_stereo_param->camera[1].focalLength) ==
      sizeof (stereo_param->camera[1].focalLength));
  memcpy (rv_stereo_param->camera[0].focalLength,
      stereo_param->camera[0].focalLength,
      sizeof (stereo_param->camera[0].focalLength));
  memcpy (rv_stereo_param->camera[1].focalLength,
      stereo_param->camera[1].focalLength,
      sizeof (stereo_param->camera[1].focalLength));

  g_assert (sizeof (rv_stereo_param->camera[0].distortion) ==
      sizeof (stereo_param->camera[0].distortion));
  g_assert (sizeof (rv_stereo_param->camera[1].distortion) ==
      sizeof (stereo_param->camera[1].distortion));
  memcpy (rv_stereo_param->camera[0].distortion,
      stereo_param->camera[0].distortion,
      sizeof (stereo_param->camera[0].distortion));
  memcpy (rv_stereo_param->camera[1].distortion,
      stereo_param->camera[1].distortion,
      sizeof (stereo_param->camera[1].distortion));

  rv_stereo_param->camera[0].distortionModel =
      stereo_param->camera[0].distortionModel;
  rv_stereo_param->camera[1].distortionModel =
      stereo_param->camera[1].distortionModel;

  g_assert (sizeof (rv_stereo_param->correctionFactors) ==
      sizeof (stereo_param->correctionFactors));
  memcpy (rv_stereo_param->correctionFactors, stereo_param->correctionFactors,
      sizeof (stereo_param->correctionFactors));
}
#endif

GstDfsEngine *
gst_dfs_engine_new (DfsInitSettings * settings)
{
  rvDFSParameter dfs_param;
#if (RVSDK_API_VERSION >= 0x202307)
  rvStereoCamera stereo_param;
#else
  rvStereoConfiguration stereo_param;
#endif

  GstDfsEngine *engine = (GstDfsEngine *) g_malloc0 (sizeof (GstDfsEngine));
  if (!engine) {
    GST_ERROR ("Failed to allocate memory");
    return NULL;
  }

  engine->mode = settings->mode;
  engine->format = settings->format;
  engine->width = settings->stereo_frame_width / 2;
  engine->height = settings->stereo_frame_height;
  engine->stride = settings->stride;


  posix_memalign (reinterpret_cast < void **>(&engine->out_work_buffer), 128,
      engine->width * engine->height * sizeof (float));
  if (!engine->out_work_buffer) {
    GST_ERROR ("Failed to allocate memory for output work buffer");
    goto cleanup;
  }

  dfs_param.filterWidth = settings->filter_width;
  dfs_param.filterHeight = settings->filter_height;
  dfs_param.disparity.minDisparity = settings->min_disparity;
  dfs_param.disparity.numDisparityLevels = settings->num_disparity_levels;
  dfs_param.doRectification = settings->rectification;

#if (RVSDK_API_VERSION >= 0x202403)
  dfs_param.version = 1;
  dfs_param.paramSize = 1;
  dfs_param.inputSize.width = engine->width;
  dfs_param.inputSize.height = engine->height;
  dfs_param.inputSize.stride = engine->stride;
  dfs_param.imgFormat = Y_ONLY_FORMAT;
  dfs_param.outputSize.width = engine->width;
  dfs_param.outputSize.height = engine->height;
  dfs_param.outputSize.stride = engine->width;
  dfs_param.mode = gst_rv_translate_mode((DFSMode)settings->dfs_mode);
  dfs_param.ppLevel = (rvDFSPPLevel )settings->pplevel;
  dfs_param.useDisp = true;
  dfs_param.latestOnly = true;
  dfs_param.useIONMem = false;
  dfs_param.extInfoSize = 0;
  dfs_param.extInfo = NULL;
#else
  dfs_param.doGpuRect = settings->gpu_rect;
#endif // RVSDK_API_VERSION

  fill_stereo_params (&stereo_param, &settings->stereo_parameter);

  GST_INFO ("Filter: %dx%d min_disp: %d num_levels: %d doRectification: %s",
      dfs_param.filterWidth, dfs_param.filterHeight,
      dfs_param.disparity.minDisparity, dfs_param.disparity.numDisparityLevels,
      dfs_param.doRectification ? "enable" : "disable");

#if (RVSDK_API_VERSION >= 0x202403)
  engine->handle =
      rvDFS_InitializeF32 (dfs_param, stereo_param);
#else
  engine->handle =
      rvDFS_Initialize ((rvDFSMode) settings->dfs_mode, engine->width,
      engine->height, engine->stride, dfs_param, stereo_param);
#endif // RVSDK_API_VERSION

  if (!engine->handle) {
    GST_ERROR ("Failed to initialize DFS");
    goto cleanup;
  }


  GST_INFO ("DFS mode: %d dimension: %dx%d stride: %d", settings->dfs_mode,
      engine->width, engine->height, engine->stride);

  return engine;

cleanup:
#if (RVSDK_API_VERSION >= 0x202403)
  if (engine->handle) {
    rvDFS_DeinitializeF32 (engine->handle);
    engine->handle = NULL;
  }
#else
  if (engine->handle) {
    rvDFS_Deinitialize (engine->handle);
    engine->handle = NULL;
  }
#endif // RVSDK_API_VERSION

  if (engine->out_work_buffer) {
    free (engine->out_work_buffer);
    engine->out_work_buffer = NULL;
  }
  g_free (engine);
  return NULL;
}

static void
gst_dfs_normalize_disparity_map (GstDfsEngine * engine, float *disparity_map,
    gpointer output)
{
  float min = disparity_map[0];
  float max = disparity_map[0];
  for (int x = 1; x < engine->width * engine->height; x++) {
    if (disparity_map[x] > max) {
      max = disparity_map[x];
    }
    if (disparity_map[x] < min) {
      min = disparity_map[x];
    }
  }
  float scale = 255.0 / (max - min);

  uint8_t *dst = output ? (uint8_t *) output : (uint8_t *) disparity_map;
  for (int x = 0; x < engine->width * engine->height; x++) {
    dst[x] = (uint8_t) (((disparity_map[x]) - min) * scale);
  }
}

static void
gst_dfs_convert_to_rgb_image (GstDfsEngine * engine, float *map,
    gpointer output)
{
  uint8_t *dst = (uint8_t *) output;
  uint8_t *src = (uint8_t *) map;

  for (int x = 0; x < engine->width * engine->height; x++) {
    uint8_t r = 0, g = 0, b = 0;
    uint8_t val = src[x];

    if (val < (64)) {
      g = 4 * val;
      b = 255;
    } else if (val < 128) {
      g = 255;
      b = 255 + 4 * (64 - val);
    } else if (val < 192) {
      r = 4 * (val - 128);
      g = 255;
    } else {
      r = 255;
      g = 255 + 4 * (192 - val);
    }

    switch (engine->format) {
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_RGBx:
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = 0xFF;
        dst += 4;
        break;
      case GST_VIDEO_FORMAT_BGRA:
      case GST_VIDEO_FORMAT_BGRx:
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = 0xFF;
        dst += 4;
        break;
      case GST_VIDEO_FORMAT_RGB:
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst += 3;
        break;
      case GST_VIDEO_FORMAT_BGR:
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst += 3;
        break;
      default:
        GST_ERROR ("Error: unsupported format %d", engine->format);
        return;
    }
  }
}

static void
gst_dfs_convert_disparity_map_to_image (GstDfsEngine * engine, float *map,
    gpointer output)
{
  if (engine->format == GST_VIDEO_FORMAT_GRAY8) {
    gst_dfs_normalize_disparity_map (engine, map, output);
  } else {
    gst_dfs_normalize_disparity_map (engine, map, NULL);
    gst_dfs_convert_to_rgb_image (engine, map, output);
  }
}

static gboolean
write_point_cloud_ply (GstDfsEngine * engine, PointCloudType * pcl,
    gpointer output, gsize size)
{
  std::stringstream ply_content;
  uint8_t *dst = (uint8_t *) output;
  ply_content << "ply" << std::endl;
  ply_content << "format ascii 1.0" << std::endl;
  ply_content << "element vertex " << pcl->size () << std::endl;
  ply_content << "property float x" << std::endl;
  ply_content << "property float y" << std::endl;
  ply_content << "property float z" << std::endl;
  ply_content << "end_header" << std::endl;
  for (const auto& pc : *pcl) {
    ply_content << std::fixed << std::
        setprecision (2) << pc[0] << " " << pc[1] << " " << pc[2] << std::endl;
  }

  std::string ply_content_str = ply_content.str();
  if (ply_content_str.size() > size) {
    GST_ERROR ("Error: point cloud buffer overflow(%ld:%ld)",
              ply_content_str.size(), size);
    return FALSE;
  }

  for (int i = 0; i < (int) ply_content_str.size(); i++) {
    dst[i] = ply_content_str[i];
  }
  return TRUE;
}

gboolean
gst_dfs_engine_execute (GstDfsEngine * engine,
    const GstVideoFrame * inframe, gpointer output, gsize size)
{
  gboolean ret;
  float *disparity_map = NULL;
  gpointer img_left = GST_VIDEO_FRAME_PLANE_DATA (inframe, 0);

#if (RVSDK_API_VERSION >= 0x202403)
  rvDFSInputParam dfs_input;
  rvDFSOutputParam dfs_output;

  //Set input param
  dfs_input.meta.version = 0x00010000;
  dfs_input.meta.paramSize = 1;
  dfs_input.meta.numParams = 0;
  dfs_input.meta.dfsParam = NULL;
  dfs_input.meta.poseCameraInWorld = NULL;
  dfs_input.inV1 = DFS_IN_DATA_V1_INIT;
  dfs_input.inV1.imgLeft = (uint8_t *) img_left;
  dfs_input.inV1.imgRight = NULL;

  //Set output param
  dfs_output.meta.version = 0x10010000;
  dfs_output.meta.paramSize = 1;
  dfs_output.meta.dim.width = engine->width;
  dfs_output.meta.dim.height = engine->height;
  dfs_output.outV1 = DFS_OUT_DATA_V1_INIT;
  dfs_output.outV1.mapDataType = 0;
  dfs_output.outV1.pointBuffer = NULL;
  dfs_output.outV1.mapOfDisparity = NULL;
  dfs_output.outV1.mapOfDepth = NULL;

  if (engine->mode == OUTPUT_MODE_VIDEO) {
    disparity_map = (float *) engine->out_work_buffer;
    dfs_output.outV1.mapOfDisparity = (void *)disparity_map;

    ret = rvDFS_ComputeF32 (engine->handle,
        &dfs_input, &dfs_output);
    if (!ret) {
      GST_ERROR ("Error in DFS process function");
      return ret;
    }

    gst_dfs_convert_disparity_map_to_image (engine, disparity_map, output);
  } else if (engine->mode == OUTPUT_MODE_DISPARITY) {
    disparity_map = (float *) output;
    dfs_output.outV1.mapOfDisparity = disparity_map;

    ret = rvDFS_ComputeF32 (engine->handle,
        &dfs_input, &dfs_output);
    if (!ret) {
      GST_ERROR ("Error in DFS process function");
      return ret;
    }
  } else if (engine->mode == OUTPUT_MODE_POINT_CLOUD) {
    PointCloudType pcl;
    dfs_output.outV1.pointBuffer = &pcl;

    ret = rvDFS_ComputeF32 (engine->handle,
        &dfs_input, &dfs_output);
    if (!ret) {
      GST_ERROR ("Error in DFS process function");
      return ret;
    }

    ret = write_point_cloud_ply (engine, &pcl, output, size);
    if (!ret) {
      GST_ERROR ("Error: point cloud buffer overflow");
      return ret;
    }
  } else {
    GST_ERROR ("Error invalid output mode");
    ret = FALSE;
  }
#else
  if (engine->mode == OUTPUT_MODE_VIDEO) {
    disparity_map = (float *) engine->out_work_buffer;

    ret = rvDFS_CalculateDisparity (engine->handle,
        (uint8_t *) img_left, nullptr, disparity_map);
    if (!ret) {
      GST_ERROR ("Error in DFS process function");
      return ret;
    }

    gst_dfs_convert_disparity_map_to_image (engine, disparity_map, output);
  } else if (engine->mode == OUTPUT_MODE_DISPARITY) {
    disparity_map = (float *) output;

    ret = rvDFS_CalculateDisparity (engine->handle,
        (uint8_t *) img_left, nullptr, disparity_map);
    if (!ret) {
      GST_ERROR ("Error in DFS process function");
      return ret;
    }
  } else if (engine->mode == OUTPUT_MODE_POINT_CLOUD) {
    PointCloudType pcl;

    ret = rvDFS_CalculatePointCloud (engine->handle,
        (uint8_t *) img_left, nullptr, &pcl);
    if (!ret) {
      GST_ERROR ("Error in DFS process function");
      return ret;
    }

    ret = write_point_cloud_ply (engine, &pcl, output, size);
    if (!ret) {
      GST_ERROR ("Error: point cloud buffer overflow");
      return ret;
    }
  } else {
    GST_ERROR ("Error invalid output mode");
    ret = FALSE;
  }
#endif // RVSDK_API_VERSION

  return ret;
}

void
gst_dfs_engine_free (GstDfsEngine * engine)
{
  if (NULL == engine)
    return;

#if (RVSDK_API_VERSION >= 0x202403)
  if (engine->handle) {
    rvDFS_DeinitializeF32 (engine->handle);
    engine->handle = NULL;
  }
#else
  if (engine->handle) {
    rvDFS_Deinitialize (engine->handle);
    engine->handle = NULL;
  }
#endif // RVSDK_API_VERSION

  g_free (engine);
}
