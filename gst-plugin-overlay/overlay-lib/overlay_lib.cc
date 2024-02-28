/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
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
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "Overlay"

#include <algorithm>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>
#include <cstring>
#include <string>
#include <assert.h>
#include <vector>
#include <math.h>
#include <fstream>

#ifdef HAVE_MMM_COLOR_FMT_H
#include <display/media/mmm_color_fmt.h>
#else
#include <media/msm_media_info.h>
#define MMM_COLOR_FMT_NV12_UBWC COLOR_FMT_NV12_UBWC
#define MMM_COLOR_FMT_ALIGN MSM_MEDIA_ALIGN
#define MMM_COLOR_FMT_Y_META_STRIDE VENUS_Y_META_STRIDE
#define MMM_COLOR_FMT_Y_META_SCANLINES VENUS_Y_META_SCANLINES
#endif // HAVE_MMM_COLOR_FMT_H

#include "tools.h"
#include "overlay.h"
#include "overlay_lib.h"

namespace overlay {

using namespace std;

#define ROUND_TO(val, round_to) ((val + round_to - 1) & ~(round_to - 1))

#define CL_CONTEXT_PERF_HINT_QCOM                   0x40C2

/*cl_perf_hint*/
#define CL_PERF_HINT_HIGH_QCOM                      0x40C3
#define CL_PERF_HINT_NORMAL_QCOM                    0x40C4
#define CL_PERF_HINT_LOW_QCOM                       0x40C5

cl_device_id OpenClKernel::device_id_ = nullptr;
cl_context OpenClKernel::context_ = nullptr;
cl_command_queue OpenClKernel::command_queue_ = nullptr;
std::mutex OpenClKernel::lock_;
int32_t OpenClKernel::ref_count = 0;

// Supported CL kernels
/* Parameters:
*    id
*    kernel_path
*    kernel_name
*    use_alpha_only
*    use_2D_image
*    global_devider_w
*    global_devider_h
*    local_size_w
*    local_size_h
*    instance
*/
CLKernelDescriptor OpenClKernel::supported_kernels[] = {
  {
    CL_KERNEL_BLIT_RGBA,
    "/usr/lib/overlay_blit_rgba_kernel.cl",
    "overlay_rgba_blit",
    false,
    true,
    4,
    2,
    16,
    16,
    nullptr
  },
  {
    CL_KERNEL_BLIT_BGRA,
    "/usr/lib/overlay_blit_bgra_kernel.cl",
    "overlay_bgra_blit",
    false,
    true,
    4,
    2,
    16,
    16,
    nullptr
  },
  {
    CL_KERNEL_PRIVACY_MASK,
    "/usr/lib/overlay_mask_kernel.cl",
    "overlay_cl_mask",
    true,
    false,
    8,
    2,
    16,
    16,
    nullptr
  }
};

int32_t OpenClKernel::OpenCLInit ()
{
  ref_count++;
  if (ref_count > 1) {
    return 0;
  }

  OVDBG_VERBOSE ("%s: Enter ", __func__);

  cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)0, CL_CONTEXT_PERF_HINT_QCOM, CL_PERF_HINT_NORMAL_QCOM, 0};
  cl_platform_id plat = 0;
  cl_uint ret_num_platform = 0;
  cl_uint ret_num_devices = 0;
  cl_int cl_err;

  cl_err = clGetPlatformIDs (1, &plat, &ret_num_platform);
  if ( (CL_SUCCESS != cl_err) || (ret_num_platform == 0)) {
    OVDBG_ERROR ("%s: Open cl hw platform not available. rc %d", __func__,
        cl_err);
    return -EINVAL;
  }

  properties[1] = (cl_context_properties) plat;

  cl_err = clGetDeviceIDs (plat, CL_DEVICE_TYPE_DEFAULT, 1, &device_id_,
      &ret_num_devices);
  if ( (CL_SUCCESS != cl_err) || (ret_num_devices != 1)) {
    OVDBG_ERROR ("%s: Open cl hw device not available. rc %d", __func__, cl_err);
    return -EINVAL;
  }

  context_ = clCreateContext (properties, 1, &device_id_, NULL, NULL, &cl_err);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to create Open cl context. rc: %d", __func__,
        cl_err);
    return -EINVAL;
  }

  command_queue_ = clCreateCommandQueueWithProperties (context_, device_id_, 0,
      &cl_err);
  if (CL_SUCCESS != cl_err) {
    clReleaseContext (context_);
    OVDBG_ERROR ("%s: Failed to create Open cl command queue. rc: %d", __func__,
        cl_err);
    return -EINVAL;
  }

  OVDBG_VERBOSE ("%s: Exit ", __func__);

  return 0;
}

int32_t OpenClKernel::OpenCLDeInit ()
{
  ref_count--;
  if (ref_count > 0) {
    return 0;
  } else if (ref_count < 0) {
    OVDBG_ERROR ("%s: Instance is already destroyed.", __func__);
    return -1;
  }

  OVDBG_VERBOSE ("%s: Enter ", __func__);

  assert (context_ != nullptr);

  if (command_queue_) {
    clReleaseCommandQueue (command_queue_);
    command_queue_ = nullptr;
  }

  if (context_) {
    clReleaseContext (context_);
    context_ = nullptr;
  }

  if (device_id_) {
    clReleaseDevice (device_id_);
    device_id_ = nullptr;
  }

  OVDBG_VERBOSE ("%s: Exit ", __func__);

  return 0;
}

/* This initializes Open CL context and command queue, loads and builds Open CL
 * program. This is reference instance which  cannot be use by itself because
 * there is no kernel instance */
std::shared_ptr<OpenClKernel> OpenClKernel::New (const std::string &path_to_src,
    const std::string &name)
{
  std::unique_lock < std::mutex > lock (lock_);
  OpenCLInit ();

  auto new_instance = std::shared_ptr < OpenClKernel
      > (new OpenClKernel (name), [](void const *) {
        if (ref_count == 1) {
          OpenCLDeInit();
          ref_count--;
        }
      });

  auto ret = new_instance->BuildProgram (path_to_src);
  if (ret) {
    OVDBG_ERROR ("%s: Failed to build blit program", __func__);
    return nullptr;
  }

  return new_instance;
}

/* This creates new instance  without loading and building Open CL program.
 * It uses program from reference instance */
std::shared_ptr<OpenClKernel> OpenClKernel::AddInstance ()
{
  std::unique_lock < std::mutex > lock (lock_);
  OpenCLInit ();

  auto new_instance = std::shared_ptr < OpenClKernel
      > (new OpenClKernel (*this), [this](void const *) {
        OpenCLDeInit();
      });

  new_instance->CreateKernelInstance ();

  return new_instance;
}

OpenClKernel::~OpenClKernel ()
{
  /* OpenCL program is created by reference instance which does not have
   * kernel instance. */
  if (kernel_) {
    clReleaseKernel (kernel_);
    kernel_ = nullptr;
  } else if (prog_) {
    clReleaseProgram (prog_);
    prog_ = nullptr;
  }
  g_mutex_clear (&sync_.lock_);
  g_cond_clear (&sync_.signal_);
}

int32_t OpenClKernel::BuildProgram (const std::string &path_to_src)
{
  OVDBG_VERBOSE ("%s: Enter ", __func__);

  assert (context_ != nullptr);

  if (path_to_src.empty ()) {
    OVDBG_ERROR ("%s: Invalid input source path! ", __func__);
    return -EINVAL;
  }

  std::ifstream src_file (path_to_src);
  if (!src_file.is_open ()) {
    OVDBG_ERROR ("%s: Fail to open source file: %s ", __func__,
        path_to_src.c_str ());
    return -EINVAL;
  }

  std::string kernel_src ( (std::istreambuf_iterator<char> (src_file)),
      std::istreambuf_iterator<char> ());

  cl_int cl_err;
  cl_int num_program_devices = 1;
  const char *strings[] = { kernel_src.c_str () };
  const size_t length = kernel_src.size ();
  prog_ = clCreateProgramWithSource (context_, num_program_devices, strings,
      &length, &cl_err);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Fail to create CL program! ", __func__);
    return -EINVAL;
  }

  cl_err = clBuildProgram (prog_, num_program_devices, &device_id_,
      " -cl-fast-relaxed-math -D ARTIFACT_REMOVE ", nullptr, nullptr);
  if (CL_SUCCESS != cl_err) {
    std::string build_log = CreateCLKernelBuildLog ();
    OVDBG_ERROR ("%s: Failed to build Open cl program. rc: %d", __func__,
        cl_err);
    OVDBG_ERROR ("%s: ---------- Open cl build log ----------\n%s", __func__,
        build_log.c_str ());
    return -EINVAL;
  }

  OVDBG_VERBOSE ("%s: Exit ", __func__);

  return 0;
}

int32_t OpenClKernel::CreateKernelInstance ()
{
  OVDBG_VERBOSE ("%s: Enter ", __func__);

  cl_int cl_err;

  assert (context_ != nullptr);

  kernel_ = clCreateKernel (prog_, kernel_name_.c_str (), &cl_err);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to create Open cl kernel rc: %d", __func__, cl_err);
    return -EINVAL;
  }

  OVDBG_VERBOSE ("%s: Exit ", __func__);

  return 0;
}

int32_t OpenClKernel::MapBuffer (cl_mem &cl_buffer, void *vaddr, int32_t fd,
    uint32_t size)
{
  OVDBG_VERBOSE ("%s: Enter addr %p fd %d size %d", __func__, vaddr, fd, size);

  cl_int rc;

  assert (context_ != nullptr);

  cl_mem_flags mem_flags = 0;
  mem_flags |= CL_MEM_READ_WRITE;
  mem_flags |= CL_MEM_USE_HOST_PTR;
  mem_flags |= CL_MEM_EXT_HOST_PTR_QCOM;

  cl_mem_ion_host_ptr ionmem { };
#ifdef HAVE_LINUX_DMA_HEAP_H
  ionmem.ext_host_ptr.allocation_type   = CL_MEM_DMABUF_HOST_PTR_QCOM;
  ionmem.ext_host_ptr.host_cache_policy = CL_MEM_HOST_IOCOHERENT_QCOM;
#else
  ionmem.ext_host_ptr.allocation_type = CL_MEM_ION_HOST_PTR_QCOM;
  ionmem.ext_host_ptr.host_cache_policy = CL_MEM_HOST_WRITEBACK_QCOM;
#endif // HAVE_LINUX_DMA_HEAP_H
  ionmem.ion_hostptr = vaddr;
  ionmem.ion_filedesc = fd;

  cl_buffer = clCreateBuffer (context_, mem_flags, size,
      mem_flags & CL_MEM_EXT_HOST_PTR_QCOM ? &ionmem : nullptr, &rc);
  if (CL_SUCCESS != rc) {
    OVDBG_ERROR ("%s: Cannot create cl buffer memory object! rc %d", __func__,
        rc);
    return -EINVAL;
  }

  return 0;
}

int32_t OpenClKernel::UnMapBuffer (cl_mem &cl_buffer)
{
  if (cl_buffer) {
    auto rc = clReleaseMemObject (cl_buffer);
    if (CL_SUCCESS != rc) {
      OVDBG_ERROR ("%s: cannot release buf! rc %d", __func__, rc);
      return -EINVAL;
    }
    cl_buffer = nullptr;
  }

  return 0;
}

// todo: add format as input argument
int32_t OpenClKernel::MapImage (cl_mem &cl_buffer, void *vaddr, int32_t fd,
    size_t width, size_t height, uint32_t stride)
{
  cl_int rc;
  uint32_t row_pitch = 0;

  assert (context_ != nullptr);

  cl_image_format format;
  format.image_channel_data_type = CL_UNSIGNED_INT8;
  format.image_channel_order = CL_RGBA;

  clGetDeviceImageInfoQCOM (device_id_, width, height, &format,
      CL_IMAGE_ROW_PITCH, sizeof (row_pitch), &row_pitch, NULL);
  if (stride < row_pitch) {
    OVDBG_ERROR ("%s: Error stride: %d platform stride: %d", __func__, stride,
        row_pitch);
    return -EINVAL;
  }

  cl_mem_flags mem_flags = 0;
  mem_flags |= CL_MEM_READ_WRITE;
  mem_flags |= CL_MEM_USE_HOST_PTR;
  mem_flags |= CL_MEM_EXT_HOST_PTR_QCOM;

  cl_mem_ion_host_ptr ionmem { };
#ifdef HAVE_LINUX_DMA_HEAP_H
  ionmem.ext_host_ptr.allocation_type   = CL_MEM_DMABUF_HOST_PTR_QCOM;
  ionmem.ext_host_ptr.host_cache_policy = CL_MEM_HOST_IOCOHERENT_QCOM;
#else
  ionmem.ext_host_ptr.allocation_type = CL_MEM_ION_HOST_PTR_QCOM;
  ionmem.ext_host_ptr.host_cache_policy = CL_MEM_HOST_WRITEBACK_QCOM;
#endif // HAVE_LINUX_DMA_HEAP_H
  ionmem.ion_hostptr = vaddr;
  ionmem.ion_filedesc = fd;

  cl_image_desc desc;
  desc.image_type = CL_MEM_OBJECT_IMAGE2D;
  desc.image_width = width;
  desc.image_height = height;
  desc.image_depth = 0;
  desc.image_array_size = 0;
  desc.image_row_pitch = stride;
  desc.image_slice_pitch = desc.image_row_pitch * desc.image_height;
  desc.num_mip_levels = 0;
  desc.num_samples = 0;
  desc.buffer = nullptr;

  cl_buffer = clCreateImage (context_, mem_flags, &format, &desc,
      mem_flags & CL_MEM_EXT_HOST_PTR_QCOM ? &ionmem : nullptr, &rc);
  if (CL_SUCCESS != rc) {
    OVDBG_ERROR ("%s: Cannot create cl image memory object! rc %d", __func__,
        rc);
    return -EINVAL;
  }

  return 0;
}

int32_t OpenClKernel::unMapImage (cl_mem &cl_buffer)
{
  return UnMapBuffer (cl_buffer);
}

int32_t OpenClKernel::SetKernelArgs (OpenClFrame &frame, DrawInfo args)
{
  OVDBG_VERBOSE ("%s: Enter ", __func__);

  cl_uint arg_index = 0;/*  */
  cl_int cl_err;

  assert (context_ != nullptr);
  assert (command_queue_ != nullptr);

  cl_mem buf_to_process = frame.cl_buffer;
  cl_mem mask_to_process = args.mask;

  cl_uint offset_y = frame.plane0_offset + args.y * frame.stride0 + args.x;
  // Use even x and y for chroma only to prevent color swapping due to
  // processing 4 pixels at once in the kernel
  cl_uint offset_nv = frame.plane1_offset + (args.y & ~1) *
      frame.stride1 / 2 + (args.x & ~1);
  cl_ushort swap_uv = frame.swap_uv;
  cl_ushort stride = frame.stride0;

  global_size_[0] = args.width / args.global_devider_w;
  global_size_[1] = args.height / args.global_devider_h;

  local_size_[0] = args.local_size_w;
  local_size_[1] = args.local_size_h;

  cl_ushort mask_stride = args.stride;

  // __read_only image2d_t mask,   // 1
  cl_err = clSetKernelArg (kernel_, arg_index++, sizeof(cl_mem),
      &mask_to_process);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return -EINVAL;
  }

  // __global uchar *frame,        // 2
  cl_err = clSetKernelArg (kernel_, arg_index++, sizeof(cl_mem),
      &buf_to_process);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return -EINVAL;
  }

  // uint y_offset,                // 3
  cl_err = clSetKernelArg (kernel_, arg_index++, sizeof(cl_uint), &offset_y);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return -EINVAL;
  }

  // uint nv_offset,               // 4
  cl_err = clSetKernelArg (kernel_, arg_index++, sizeof(cl_uint), &offset_nv);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return -EINVAL;
  }

  // ushort stride,                // 5
  cl_err = clSetKernelArg (kernel_, arg_index++, sizeof(cl_ushort), &stride);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return -EINVAL;
  }

  // ushort swap_uv                // 6
  cl_err = clSetKernelArg (kernel_, arg_index++, sizeof(cl_ushort), &swap_uv);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return -EINVAL;
  }

  // ushort mask_stride,          // 7
  cl_err = clSetKernelArg (kernel_, arg_index++, sizeof(cl_ushort), &mask_stride);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return -EINVAL;
  }

  OVDBG_VERBOSE ("%s: Exit ", __func__);

  return 0;
}

void OpenClKernel::ClCompleteCallback (cl_event event,
    cl_int event_command_exec_status, void *user_data)
{
  OVDBG_VERBOSE ("%s: Enter ", __func__);
  OV_UNUSED(event_command_exec_status);

  if (user_data != nullptr) {
    struct SyncObject *sync = reinterpret_cast<struct SyncObject *> (user_data);
    g_mutex_lock (&sync->lock_);
    sync->done_ = true;
    g_cond_signal (&sync->signal_);
    g_mutex_unlock (&sync->lock_);
  }
  clReleaseEvent (event);

  OVDBG_VERBOSE ("%s: Exit ", __func__);
}

int32_t OpenClKernel::RunCLKernel (bool wait_to_finish)
{
  OVDBG_VERBOSE ("%s: Enter ", __func__);

  cl_int cl_err = CL_SUCCESS;
  cl_event kernel_event = nullptr;

  assert (context_ != nullptr);
  assert (command_queue_ != nullptr);

  size_t *local_work_size =
      local_size_[0] + local_size_[1] == 0 ? nullptr : local_size_;

  cl_err = clEnqueueNDRangeKernel (command_queue_, kernel_, kernel_dimensions_,
      global_offset_, global_size_, local_work_size, 0, nullptr,
      wait_to_finish ? &kernel_event : nullptr);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to enqueue Open cl kernel! rc: %d ", __func__,
        cl_err);
    return -EINVAL;
  }

  if (wait_to_finish) {
    g_mutex_lock (&sync_.lock_);
    sync_.done_ = false;
    cl_err = clSetEventCallback (kernel_event, CL_COMPLETE, &ClCompleteCallback,
        reinterpret_cast<void *> (&sync_));
    if (CL_SUCCESS != cl_err) {
      OVDBG_ERROR ("%s: Failed to set Open cl kernel callback! rc: %d ",
          __func__, cl_err);
      g_mutex_unlock (&sync_.lock_);
      return -EINVAL;
    }
    g_mutex_unlock (&sync_.lock_);
  }

  if (wait_to_finish) {
    g_mutex_lock (&sync_.lock_);
    cl_err = clFlush (command_queue_);
    if (CL_SUCCESS != cl_err) {
      OVDBG_ERROR ("%s: Failed to flush Open cl command queue! rc: %d ",
          __func__, cl_err);
      g_mutex_unlock (&sync_.lock_);
      return -EINVAL;
    }
    gint64 wait_time = g_get_monotonic_time () + kWaitProcessTimeout;
    while (sync_.done_ == false) {
      auto ret = g_cond_wait_until (&sync_.signal_, &sync_.lock_, wait_time);
      if (!ret) {
        OVDBG_ERROR ("%s: Timed out on Wait", __func__);
        g_mutex_unlock (&sync_.lock_);
        return -ETIMEDOUT;
      }
    }
    g_mutex_unlock (&sync_.lock_);
  }

  OVDBG_VERBOSE ("%s: Exit ", __func__);

  return 0;
}

std::string OpenClKernel::CreateCLKernelBuildLog ()
{
  cl_int cl_err;
  size_t log_size;
  cl_err = clGetProgramBuildInfo (prog_, device_id_, CL_PROGRAM_BUILD_LOG, 0,
      nullptr, &log_size);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to get Open cl build log size. rc: %d ", __func__,
        cl_err);
    return std::string ();
  }

  std::string build_log;
  build_log.reserve (log_size);
  void *log = static_cast<void *> (const_cast<char *> (build_log.data ()));
  cl_err = clGetProgramBuildInfo (prog_, device_id_, CL_PROGRAM_BUILD_LOG,
      log_size, log, nullptr);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR ("%s: Failed to get Open cl build log. rc: %d ", __func__,
        cl_err);
    return std::string ();
  }

  return build_log;
}

Overlay::Overlay ()
    : ion_device_ (-1), id_ (0), blit_type_ (OverlayBlitType::kC2D) {

#ifdef ENABLE_C2D
  target_c2dsurface_id_ = -1;
#endif // ENABLE_C2D
}

Overlay::~Overlay ()
{
  OVDBG_INFO ("%s: Enter ", __func__);
  for (auto &iter : overlay_items_) {
    if (iter.second)
      delete iter.second;
  }
  overlay_items_.clear ();

  if (blit_type_ == OverlayBlitType::kC2D) {
#ifdef ENABLE_C2D
    if (target_c2dsurface_id_) {
      c2dDestroySurface (target_c2dsurface_id_);
      target_c2dsurface_id_ = 0;
      OVDBG_INFO ("%s: Destroyed c2d Target Surface", __func__);
    }
#endif // ENABLE_C2D
  } else if (blit_type_ == OverlayBlitType::kGLES) {
#ifdef ENABLE_GLES
    for (auto const& pair : ib2c_surfaces_) {
      uint64_t surface_id = pair.second;
      ib2c_engine_->DestroySurface(surface_id);
    }
    ib2c_surfaces_.clear();
#endif // ENABLE_GLES
  }

  if (ion_device_ != -1) {
    close (ion_device_);
    ion_device_ = -1;
  }

  OVDBG_INFO ("%s: Exit ", __func__);
}

int32_t Overlay::Init ()
{
  OVDBG_VERBOSE ("%s:Enter", __func__);

  OVDBG_INFO ("%s: Open /dev/dma_heap/qcom,system", __func__);
  ion_device_ = open ("/dev/dma_heap/qcom,system", O_RDONLY | O_CLOEXEC);

  if (ion_device_ < 0) {
    OVDBG_ERROR ("%s: Falling back to /dev/ion", __func__);
    ion_device_ = open ("/dev/ion", O_RDONLY | O_CLOEXEC);
  }

  if (ion_device_ < 0) {
    OVDBG_ERROR ("%s: Failed to open ION device FDn", __func__);
    return -1;
  }

  char prop_val[PROP_VALUE_MAX];
  property_get("persist.overlay.use_c2d_blit", prop_val, "1");
  auto value = atoi(prop_val);

  if (value == 1) {
    blit_type_ = OverlayBlitType::kC2D;
  } else if (value == 2) {
    blit_type_ = OverlayBlitType::kGLES;
  } else {
    blit_type_ = OverlayBlitType::kOpenCL;
  }

  if (blit_type_ == OverlayBlitType::kC2D) {
#ifdef ENABLE_C2D
    uint32_t c2dColotFormat = C2D_COLOR_FORMAT_420_NV21;
    // Create dummy C2D surface, it is required to Initialize
    // C2D driver before calling any c2d Apis.
    C2D_YUV_SURFACE_DEF surface_def =
    { c2dColotFormat, 1 * 4, 1 * 4, (void*) 0xaaaaaaaa, (void*) 0xaaaaaaaa, 1
      * 4, (void*) 0xaaaaaaaa, (void*) 0xaaaaaaaa, 1 * 4,
      (void*) 0xaaaaaaaa, (void*) 0xaaaaaaaa, 1 * 4,};

    auto ret = c2dCreateSurface (&target_c2dsurface_id_, C2D_TARGET,
        (C2D_SURFACE_TYPE) (
            C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS
            | C2D_SURFACE_WITH_PHYS_DUMMY), &surface_def);
    if (ret != C2D_STATUS_OK) {
      OVDBG_ERROR ("%s: c2dCreateSurface failed!", __func__);
      return ret;
    }
#else
    OVDBG_ERROR ("%s: C2D converter is not supported!", __func__);
    return -1;
#endif // ENABLE_C2D
  } else if (blit_type_ == OverlayBlitType::kGLES) {
#ifdef ENABLE_GLES
    void* handle = dlopen("libIB2C.so", RTLD_NOW);
    if (!handle || dlerror()) {
      OVDBG_ERROR ("%s: dlopen failed: '%s'", __func__, dlerror());
      return -1;
    }

    ::ib2c::NewIEngine NewEngine =
        (::ib2c::NewIEngine) dlsym(handle, IB2C_ENGINE_NEW_FUNC);
    if (dlerror()) {
      OVDBG_ERROR ("%s: dlsym failed: '%s'", __func__, dlerror());
      return -1;
    }

    ib2c_engine_ = std::shared_ptr<::ib2c::IEngine>(
        NewEngine(), [handle](::ib2c::IEngine* e) { delete e; dlclose(handle); }
    );
#else
    OVDBG_ERROR ("%s: GLES converter is not supported!", __func__);
    return -1;
#endif // ENABLE_GLES
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return 0;
}

int32_t Overlay::CreateOverlayItem (OverlayParam& param, uint32_t* overlay_id)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  OverlayItem* overlayItem = nullptr;

  switch (param.type) {
  case OverlayType::kDateType:
    overlayItem = new OverlayItemDateAndTime (ion_device_, blit_type_,
        CL_KERNEL_BLIT_RGBA);
    break;
  case OverlayType::kUserText:
    overlayItem = new OverlayItemText (ion_device_, blit_type_,
        CL_KERNEL_BLIT_RGBA);
    break;
  case OverlayType::kStaticImage:
    overlayItem = new OverlayItemStaticImage (ion_device_, blit_type_,
        CL_KERNEL_BLIT_BGRA);
    break;
  case OverlayType::kBoundingBox:
    overlayItem = new OverlayItemBoundingBox (ion_device_, blit_type_,
        CL_KERNEL_BLIT_RGBA);
    break;
  case OverlayType::kPrivacyMask:
    overlayItem = new OverlayItemPrivacyMask (ion_device_, blit_type_,
        CL_KERNEL_PRIVACY_MASK);
    break;
  case OverlayType::kGraph:
    overlayItem = new OverlayItemGraph (ion_device_, blit_type_,
        CL_KERNEL_BLIT_RGBA);
    break;
  case OverlayType::kArrow:
    overlayItem = new OverlayItemArrow (ion_device_, blit_type_,
        CL_KERNEL_BLIT_RGBA);
    break;
  default:
    OVDBG_ERROR ("%s: OverlayType(%d) not supported!", __func__,
        (int32_t) param.type);
    break;
  }

  if (!overlayItem) {
    OVDBG_ERROR ("%s: OverlayItem type(%d) failed!", __func__,
        (int32_t) param.type);
    return -EINVAL;
  }

#ifdef ENABLE_GLES
  auto ret = overlayItem->Init (ib2c_engine_, param);
#else
  auto ret = overlayItem->Init (param);
#endif // ENABLE_GLES

  if (ret != 0) {
    OVDBG_ERROR ("%s:OverlayItem failed of type(%d)", __func__,
        (int32_t) param.type);
    delete overlayItem;
    return ret;
  }

  // StaticImage type overlayItem never be dirty as its contents are static,
  // all other items are dirty at Init time and will be marked as dirty whenever
  // their configuration changes at run time after first draw.
  if (param.type == OverlayType::kStaticImage) {
    overlayItem->MarkDirty (false);
  } else {
    overlayItem->MarkDirty (true);
  }

  *overlay_id = ++id_;
  overlay_items_.insert ( { *overlay_id, overlayItem });
  OVDBG_INFO ("%s:OverlayItem Type(%d) Id(%d) Created Successfully !", __func__,
      (int32_t) param.type, (int32_t) *overlay_id);

  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}

int32_t Overlay::DeleteOverlayItem (uint32_t overlay_id)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  std::lock_guard < std::mutex > lock (lock_);

  int32_t ret = 0;
  if (!IsOverlayItemValid (overlay_id)) {
    OVDBG_ERROR ("%s: overlay_id(%d) is not valid!", __func__, overlay_id);
    return -EINVAL;
  }
  OverlayItem* overlayItem = overlay_items_.at (overlay_id);
  assert (overlayItem != nullptr);
  delete overlayItem;
  overlay_items_.erase (overlay_id);
  OVDBG_INFO ("%s: overlay_id(%d) & overlayItem(0x%p) Removed from map",
      __func__, overlay_id, overlayItem);

  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}

int32_t Overlay::GetOverlayParams (uint32_t overlay_id, OverlayParam& param)
{
  int32_t ret = 0;
  if (!IsOverlayItemValid (overlay_id)) {
    OVDBG_ERROR ("%s: overlay_id(%d) is not valid!", __func__, overlay_id);
    return -EINVAL;
  }
  OverlayItem* overlayItem = overlay_items_.at (overlay_id);
  assert (overlayItem != nullptr);

  memset (&param, 0x0, sizeof param);
  overlayItem->GetParameters (param);
  return ret;
}

int32_t Overlay::UpdateOverlayParams (uint32_t overlay_id, OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  std::lock_guard < std::mutex > lock (lock_);

  if (!IsOverlayItemValid (overlay_id)) {
    OVDBG_ERROR ("%s: overlay_id(%d) is not valid!", __func__, overlay_id);
    return -EINVAL;
  }
  OverlayItem* overlayItem = overlay_items_.at (overlay_id);
  assert (overlayItem != nullptr);

  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return overlayItem->UpdateParameters (param);
}

int32_t Overlay::EnableOverlayItem (uint32_t overlay_id)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  std::lock_guard < std::mutex > lock (lock_);

  int32_t ret = 0;
  if (!IsOverlayItemValid (overlay_id)) {
    OVDBG_ERROR ("%s: overlay_id(%d) is not valid!", __func__, overlay_id);
    return -EINVAL;
  }
  OverlayItem* overlayItem = overlay_items_.at (overlay_id);
  assert (overlayItem != nullptr);

  overlayItem->Activate (true);
  OVDBG_DEBUG ("%s: OverlayItem Id(%d) Activated", __func__, overlay_id);

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

int32_t Overlay::DisableOverlayItem (uint32_t overlay_id)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  std::lock_guard < std::mutex > lock (lock_);

  int32_t ret = 0;
  if (!IsOverlayItemValid (overlay_id)) {
    OVDBG_ERROR ("%s: overlay_id(%d) is not valid!", __func__, overlay_id);
    return -EINVAL;
  }
  OverlayItem* overlayItem = overlay_items_.at (overlay_id);
  assert (overlayItem != nullptr);

  overlayItem->Activate (false);
  OVDBG_DEBUG ("%s: OverlayItem Id(%d) DeActivated", __func__, overlay_id);

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

#ifdef ENABLE_C2D
int32_t Overlay::ApplyOverlay_C2D (const OverlayTargetBuffer& buffer)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

  int32_t ret = 0;
  int32_t obj_idx = 0;

  std::lock_guard < std::mutex > lock (lock_);
  size_t numActiveOverlays = 0;
  bool isItemsActive = false;
  for (auto &iter : overlay_items_) {
    if ( (iter).second->IsActive ()) {
      isItemsActive = true;
    }
  }
  if (!isItemsActive) {
    OVDBG_VERBOSE ("%s: No overlayItem is Active!", __func__);
    return ret;
  }
  assert (buffer.ion_fd != 0);
  assert (buffer.width != 0 && buffer.height != 0);
  assert (buffer.frame_len != 0);

  OVDBG_VERBOSE ("%s:OverlayTargetBuffer: ion_fd = %d", __func__, buffer.ion_fd);
  OVDBG_VERBOSE (
      "%s:OverlayTargetBuffer: Width = %d & Height = %d & frameLength" " =% d",
      __func__, buffer.width, buffer.height,
      (int32_t) buffer.frame_len);
  OVDBG_VERBOSE ("%s: OverlayTargetBuffer: format = %d", __func__,
      (int32_t) buffer.format);

  void* bufVaddr = mmap (nullptr, buffer.frame_len, PROT_READ | PROT_WRITE,
      MAP_SHARED, buffer.ion_fd, 0);
  if (!bufVaddr) {
    OVDBG_ERROR ("%s: mmap failed!", __func__);
    return -EINVAL;
  }

  SyncStart (buffer.ion_fd);
  // Map input YUV buffer to GPU.
  void *gpuAddr = nullptr;
  ret = c2dMapAddr (buffer.ion_fd, bufVaddr, buffer.frame_len, 0,
      KGSL_USER_MEM_TYPE_ION, &gpuAddr);
  if (ret != C2D_STATUS_OK) {
    OVDBG_ERROR ("%s: c2dMapAddr failed!", __func__);
    goto EXIT;
  }

  // Target surface format.
  C2D_YUV_SURFACE_DEF surface_def;
  surface_def.format = GetC2dColorFormat (buffer.format);
  surface_def.width = buffer.width;
  surface_def.height = buffer.height;
  surface_def.stride0 = buffer.stride[0];
  surface_def.stride1 = buffer.stride[1];
  surface_def.plane0 = (void*) bufVaddr;
  surface_def.phys0 = (void*) gpuAddr;
  surface_def.plane1 = (void*) ((intptr_t) bufVaddr + buffer.offset[1]);
  surface_def.phys1 = (void*) ((intptr_t) gpuAddr + buffer.offset[1]);

  //Create C2d target surface outof camera buffer. camera buffer
  //is target surface where c2d blits different types of overlays
  //static logo, system time and date.
  ret = c2dUpdateSurface (target_c2dsurface_id_, C2D_SOURCE,
      (C2D_SURFACE_TYPE) (C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS),
      &surface_def);
  if (ret != C2D_STATUS_OK) {
    OVDBG_ERROR ("%s: c2dUpdateSurface failed!", __func__);
    goto EXIT;
  }

  // Iterate all dirty overlay Items, and update them.
  for (auto &iter : overlay_items_) {
    if ( (iter).second->IsActive ()) {
      ret = (iter).second->UpdateAndDraw ();
      if (ret != 0) {
        OVDBG_ERROR ("%s: Update & Draw failed for Item=%d", __func__,
            (iter).first);
      }
    }
  }

  C2dObjects c2d_objects;
  memset (&c2d_objects, 0x0, sizeof c2d_objects);
  // Iterate all updated overlayItems, and get coordinates.
  for (auto &iter : overlay_items_) {
    std::vector<DrawInfo> draw_infos;
    OverlayItem* overlay_item = (iter).second;
    if (overlay_item->IsActive ()) {
      overlay_item->GetDrawInfo (buffer.width, buffer.height, draw_infos);
      auto info_size = draw_infos.size ();
      for (size_t i = 0; i < info_size; i++) {
        c2d_objects.objects[obj_idx].surface_id = draw_infos[i].c2dSurfaceId;
        c2d_objects.objects[obj_idx].config_mask = C2D_ALPHA_BLEND_SRC_ATOP
        | C2D_TARGET_RECT_BIT;
        if (draw_infos[i].in_width) {
          c2d_objects.objects[obj_idx].config_mask |= C2D_SOURCE_RECT_BIT;
          c2d_objects.objects[obj_idx].source_rect.x = draw_infos[i].in_x << 16;
          c2d_objects.objects[obj_idx].source_rect.y = draw_infos[i].in_y << 16;
          c2d_objects.objects[obj_idx].source_rect.width = draw_infos[i]
          .in_width << 16;
          c2d_objects.objects[obj_idx].source_rect.height = draw_infos[i]
          .in_height << 16;
        }
        c2d_objects.objects[obj_idx].target_rect.x = draw_infos[i].x << 16;
        c2d_objects.objects[obj_idx].target_rect.y = draw_infos[i].y << 16;
        c2d_objects.objects[obj_idx].target_rect.width = draw_infos[i].width
        << 16;
        c2d_objects.objects[obj_idx].target_rect.height = draw_infos[i].height
        << 16;

        OVDBG_VERBOSE ("%s: c2d_objects[%d].surface_id=%d", __func__, obj_idx,
            c2d_objects.objects[obj_idx].surface_id);
        OVDBG_VERBOSE ("%s: c2d_objects[%d].target_rect.x=%d", __func__, obj_idx,
            draw_infos[i].x);
        OVDBG_VERBOSE ("%s: c2d_objects[%d].target_rect.y=%d", __func__, obj_idx,
            draw_infos[i].y);
        OVDBG_VERBOSE ("%s: c2d_objects[%d].target_rect.width=%d", __func__,
            obj_idx, draw_infos[i].width);
        OVDBG_VERBOSE ("%s: c2d_objects[%d].target_rect.height=%d", __func__,
            obj_idx, draw_infos[i].height);
        ++numActiveOverlays;
        ++obj_idx;
      }
    }
  }

  OVDBG_VERBOSE ("%s: numActiveOverlays=%zu", __func__,
      numActiveOverlays);
  for (size_t i = 0; i < (numActiveOverlays - 1); i++) {
    c2d_objects.objects[i].next = &c2d_objects.objects[i + 1];
  }

  {
#ifdef DEBUG_BLIT_TIME
    static uint64_t avr_time = 0;
    Timer t("Apply overly ", &avr_time);
#endif
    ret = c2dDraw (target_c2dsurface_id_, 0, 0, 0, 0, c2d_objects.objects,
        numActiveOverlays);
    if (ret != C2D_STATUS_OK) {
      OVDBG_ERROR ("%s: c2dDraw failed!", __func__);
      goto EXIT;
    }

    ret = c2dFinish (target_c2dsurface_id_);
    if (ret != C2D_STATUS_OK) {
      OVDBG_ERROR ("%s: c2dFinish failed!", __func__);
      goto EXIT;
    }
  }

  // Unmap camera buffer from GPU after draw is completed.
  ret = c2dUnMapAddr (gpuAddr);
  if (ret != C2D_STATUS_OK) {
    OVDBG_ERROR ("%s: c2dUnMapAddr failed!", __func__);
    goto EXIT;
  }

  EXIT : if (bufVaddr) {
    if (buffer.ion_fd)
    SyncEnd (buffer.ion_fd);

    munmap (bufVaddr, buffer.frame_len);
    bufVaddr = nullptr;
  }

  OVDBG_VERBOSE ("%s: Exit ", __func__);
  return ret;
}
#endif // ENABLE_C2D

#ifdef ENABLE_GLES
int32_t Overlay::ApplyOverlay_GLES (const OverlayTargetBuffer& buffer)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

  std::lock_guard < std::mutex > lock (lock_);
  size_t numActiveOverlays = 0;
  bool isItemsActive = false;
  for (auto &iter : overlay_items_) {
    if ( (iter).second->IsActive ()) {
      isItemsActive = true;
    }
  }
  if (!isItemsActive) {
    OVDBG_VERBOSE ("%s: No overlayItem is Active!", __func__);
    return 0;
  }
  assert (buffer.ion_fd != 0);
  assert (buffer.width != 0 && buffer.height != 0);
  assert (buffer.frame_len != 0);

  OVDBG_VERBOSE ("%s:OverlayTargetBuffer: ion_fd = %d", __func__, buffer.ion_fd);
  OVDBG_VERBOSE (
      "%s:OverlayTargetBuffer: Width = %d & Height = %d & frameLength" " =% d",
      __func__, buffer.width, buffer.height,
      (int32_t) buffer.frame_len);
  OVDBG_VERBOSE ("%s: OverlayTargetBuffer: format = %d", __func__,
      (int32_t) buffer.format);

  uint64_t surface_id = 0;

  if (ib2c_surfaces_.count(buffer.ion_fd) == 0) {
    ib2c::Surface outsurface;

    outsurface.fd = buffer.ion_fd;
    outsurface.format = GetGlesColorFormat (buffer.format);
    outsurface.width = buffer.width;
    outsurface.height = buffer.height;
    outsurface.size = buffer.frame_len;
    outsurface.stride0 = buffer.stride[0];
    outsurface.stride1 = buffer.stride[1];
    outsurface.offset0 = buffer.offset[0];
    outsurface.offset1 = buffer.offset[1];
    outsurface.nplanes = 2;

    try {
      surface_id = ib2c_engine_->CreateSurface(outsurface,
          ib2c::SurfaceFlags::kOutput);
      ib2c_surfaces_.emplace(buffer.ion_fd, surface_id);
    } catch (std::exception& e) {
      OVDBG_ERROR ("%s: Create surface failed, error: '%s'!", __func__, e.what());
      return -1;
    }
  } else {
    surface_id = ib2c_surfaces_.at(buffer.ion_fd);
  }

  SyncStart (buffer.ion_fd);

  // Iterate all dirty overlay Items, and update them.
  for (auto &iter : overlay_items_) {
    if ( (iter).second->IsActive ()) {
      auto ret = (iter).second->UpdateAndDraw ();
      if (ret != 0) {
        OVDBG_ERROR ("%s: Update & Draw failed for Item=%d", __func__,
            (iter).first);
      }
    }
  }

  std::vector<::ib2c::Composition> blits;
  std::vector<::ib2c::Normalize> normalization;
  std::vector<::ib2c::Object> objects;

  // Iterate all updated overlayItems, and get coordinates.
  for (auto &iter : overlay_items_) {
    std::vector<DrawInfo> draw_infos;
    OverlayItem* overlay_item = (iter).second;
    if (overlay_item->IsActive ()) {
      overlay_item->GetDrawInfo (buffer.width, buffer.height, draw_infos);
      uint32_t info_size = draw_infos.size ();
      for (uint32_t i = 0; i < info_size; i++) {
        ::ib2c::Object object;

        object.id = draw_infos[i].ib2cSurfaceId;
        if (draw_infos[i].in_width) {
          object.source.x = draw_infos[i].in_x;
          object.source.y = draw_infos[i].in_y;
          object.source.w = draw_infos[i].in_width;
          object.source.h = draw_infos[i].in_height;
        }
        object.destination.x = draw_infos[i].x;
        object.destination.y = draw_infos[i].y;
        object.destination.w = draw_infos[i].width;
        object.destination.h = draw_infos[i].height;

        OVDBG_VERBOSE ("%s: object[%u].surface_id=%lx", __func__, i,
            object.id);
        OVDBG_VERBOSE ("%s: object[%u].destination.x=%u", __func__, i,
            object.destination.x);
        OVDBG_VERBOSE ("%s: object[%u].destination.y=%u", __func__, i,
            object.destination.y);
        OVDBG_VERBOSE ("%s: object[%u].destination.width=%u", __func__, i,
            object.destination.w);
        OVDBG_VERBOSE ("%s: object[%u].destination.height=%u", __func__, i,
            object.destination.h);
        ++numActiveOverlays;

        objects.push_back(object);
      }
    }
  }

  blits.push_back(std::move(
      std::make_tuple(surface_id, 0x00000000, false, normalization, objects)));

  OVDBG_VERBOSE ("%s: numActiveOverlays=%zu", __func__, numActiveOverlays);

  {
#ifdef DEBUG_BLIT_TIME
    static uint64_t avr_time = 0;
    Timer t("Apply overly ", &avr_time);
#endif
  }

  auto ret = ib2c_engine_->Compose(blits, true);
  if (ret != 0) {
    OVDBG_ERROR ("%s: c2dDraw failed!", __func__);
  }

  SyncEnd (buffer.ion_fd);

  if (!in_surf_cache_) {
    ib2c_engine_->DestroySurface(surface_id);
    ib2c_surfaces_.erase(buffer.ion_fd);
  }

  OVDBG_VERBOSE ("%s: Exit ", __func__);
  return ret;
}
#endif // ENABLE_GLES

int32_t Overlay::ApplyOverlay_CL (const OverlayTargetBuffer& buffer)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

  int32_t ret = 0;

  std::lock_guard < std::mutex > lock (lock_);
  bool isItemsActive = false;
  for (auto &iter : overlay_items_) {
    if ( (iter).second->IsActive ()) {
      isItemsActive = true;
    }
  }
  if (!isItemsActive) {
    OVDBG_VERBOSE ("%s: No overlayItem is Active!", __func__);
    return ret;
  }
  assert (buffer.ion_fd != 0);
  assert (buffer.width != 0 && buffer.height != 0);
  assert (buffer.frame_len != 0);

  OVDBG_VERBOSE ("%s:OverlayTargetBuffer: ion_fd = %d", __func__, buffer.ion_fd);
  OVDBG_VERBOSE (
      "%s:OverlayTargetBuffer: Width = %d & Height = %d & frameLength" " =% d",
      __func__, buffer.width, buffer.height, buffer.frame_len);
  OVDBG_VERBOSE ("%s: OverlayTargetBuffer: format = %d", __func__,
      (int32_t) buffer.format);

  void* bufVaddr = mmap (nullptr, buffer.frame_len, PROT_READ | PROT_WRITE,
      MAP_SHARED, buffer.ion_fd, 0);
  if (!bufVaddr) {
    OVDBG_ERROR ("%s: mmap failed!", __func__);
    return -EINVAL;
  }

  SyncStart (buffer.ion_fd);

  // map buffer
  OpenClFrame in_frame;
  ret = OpenClKernel::MapBuffer (in_frame.cl_buffer, bufVaddr, buffer.ion_fd,
      buffer.frame_len);
  if (ret) {
    OVDBG_ERROR ("%s: Fail to map buffer to Open CL!", __func__);
    munmap (bufVaddr, buffer.frame_len);
    return -EINVAL;
  }

  // Iterate all dirty overlay Items, and update them.
  for (auto &iter : overlay_items_) {
    if ( (iter).second->IsActive ()) {
      ret = (iter).second->UpdateAndDraw ();
      if (ret) {
        OVDBG_ERROR ("%s: Update & Draw failed for Item=%d", __func__,
            (iter).first);
      }
    }
  }

  // Get config from overlay instances
  std::vector<DrawInfo> draw_infos;
  for (auto &iter : overlay_items_) {
    OverlayItem* overlay_item = (iter).second;
    if (overlay_item->IsActive ()) {
      overlay_item->GetDrawInfo (buffer.width, buffer.height, draw_infos);
    }
  }

  in_frame.plane0_offset = buffer.offset[0];
  in_frame.plane1_offset = buffer.offset[1];
  in_frame.stride0 = buffer.stride[0];
  in_frame.stride1 = buffer.stride[1];
  in_frame.swap_uv =
      (buffer.format == TargetBufferFormat::kYUVNV12) ? false : true;

  // Configure kernels
  for (auto &item : draw_infos) {
    item.blit_inst->SetKernelArgs (in_frame, item);
  }

  // Apply kernels
  for (size_t i = 0; i < draw_infos.size (); i++) {
#ifdef DEBUG_BLIT_TIME
    static uint64_t avr_time = 0;
    Timer t("Apply overly ", &avr_time);
#endif
    draw_infos[i].blit_inst->RunCLKernel (i == draw_infos.size () - 1);
  }

  // unmap buffer
  OpenClKernel::UnMapBuffer (in_frame.cl_buffer);
  SyncEnd (buffer.ion_fd);
  munmap (bufVaddr, buffer.frame_len);

  OVDBG_VERBOSE ("%s: Exit ", __func__);
  return ret;
}

int32_t Overlay::ApplyOverlay (const OverlayTargetBuffer& buffer)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

#ifdef DEBUG_BLIT_TIME
  static uint64_t avr_time = 0;
  Timer t("Time taken in 2D draw + Blit", &avr_time);
#endif

  int32_t ret = 0;
  if (blit_type_ == OverlayBlitType::kC2D) {
#ifdef ENABLE_C2D
    ret = ApplyOverlay_C2D(buffer);
#endif // ENABLE_C2D
  } else if (blit_type_ == OverlayBlitType::kGLES) {
#ifdef ENABLE_GLES
    ret = ApplyOverlay_GLES(buffer);
#endif // ENABLE_GLES
  } else {
    ret = ApplyOverlay_CL(buffer);
  }
  OVDBG_VERBOSE ("%s: Exit ", __func__);
  return ret;
}

int32_t Overlay::ProcessOverlayItems (
    const std::vector<OverlayParam>& overlay_list)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  std::lock_guard < std::mutex > lock (lock_);

  int32_t ret = 0;
  uint32_t overlay_id = 0;
  uint32_t size = overlay_list.size ();
  uint32_t num_items = overlay_items_.size ();

  if (num_items < size) {
    auto overlay_param = overlay_list.at (0);
    for (auto i = 0; i < 10; i++) {
      ret = CreateOverlayItem (overlay_param, &overlay_id);
      if (ret) {
        OVDBG_ERROR ("%s: CreateOverlayItem failed for id:%u!!", __func__,
            overlay_id);
        return ret;
      }
    }
  }
  // Check overlay_items_ size and allocate in chunks of 10
  // If request size is greater than available allocate more
  // Remove active flag
  OVDBG_VERBOSE ("%s: size:%u num_items:%u", __func__, size, num_items);
  auto items_iter = overlay_items_.begin ();
  OverlayItem* overlayItem = nullptr;
  for (uint32_t index = 0; index < size; index++, items_iter++) {
    auto overlay_param = overlay_list.at (index);
    overlay_id = items_iter->first;
    overlayItem = items_iter->second;
    OVDBG_VERBOSE ("%s:id:%u w: %u h:%u", __func__, overlay_id,
        overlay_param.dst_rect.width, overlay_param.dst_rect.height);
    ret = overlayItem->UpdateParameters (overlay_param);

    if (ret) {
      OVDBG_ERROR ("%s: UpdateParameters failed for id: %u!", __func__,
          overlay_id);
      return ret;
    }

    if (!overlayItem->IsActive ()) {
      overlayItem->Activate (true);
      OVDBG_DEBUG ("%s: OverlayItem Id(%d) Activated", __func__, overlay_id);
    } else {
      OVDBG_DEBUG ("%s: OverlayItem Id(%d) already Activated", __func__,
          overlay_id);
    }
  }
  // Disable inactive overlay
  while (items_iter != overlay_items_.end ()) {
    overlay_id = items_iter->first;
    overlayItem = items_iter->second;
    if (overlayItem->IsActive ()) {
      OVDBG_DEBUG ("%s: Disable overlayItem for id: %u!", __func__, overlay_id);
      overlayItem->Activate (false);
    }
    items_iter++;
  }

  return ret;
  OVDBG_VERBOSE ("%s: Exit", __func__);
}

void Overlay::DisableInputSurfaceCache() {
  in_surf_cache_ = false;
}

int32_t Overlay::DeleteOverlayItems ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  std::lock_guard < std::mutex > lock (lock_);
  int32_t ret = 0;
  uint32_t overlay_id = 0;
  OverlayItem* overlayItem = nullptr;

  auto items_iter = overlay_items_.begin ();
  while (items_iter != overlay_items_.end ()) {
    overlay_id = items_iter->first;
    overlayItem = items_iter->second;

    assert (overlayItem != nullptr);
    delete overlayItem;
    overlay_items_.erase (overlay_id);
    OVDBG_INFO ("%s: overlay_id(%d) & overlayItem(0x%p) Removed from map",
        __func__, overlay_id, overlayItem);
    items_iter++;
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

#ifdef ENABLE_C2D
uint32_t Overlay::GetC2dColorFormat (const TargetBufferFormat& format)
{
  uint32_t c2dColorFormat = C2D_COLOR_FORMAT_420_NV12;
  switch (format) {
  case TargetBufferFormat::kYUVNV12:
    c2dColorFormat = C2D_COLOR_FORMAT_420_NV12;
    break;
  case TargetBufferFormat::kYUVNV21:
    c2dColorFormat = C2D_COLOR_FORMAT_420_NV21;
    break;
  case TargetBufferFormat::kYUVNV12UBWC:
    c2dColorFormat = C2D_COLOR_FORMAT_420_NV12 | C2D_FORMAT_UBWC_COMPRESSED;
    break;
  default:
    OVDBG_ERROR ("%s: Unsupported buffer format: %d", __func__,
        (int32_t) format);
    break;
  }

  OVDBG_VERBOSE ("%s:Selected C2D ColorFormat=%d", __func__, c2dColorFormat);
  return c2dColorFormat;
}
#endif // ENABLE_C2D

#ifdef ENABLE_GLES
uint32_t Overlay::GetGlesColorFormat (const TargetBufferFormat& format)
{
  uint32_t colorFormat = ib2c::ColorFormat::kNV12;
  switch (format) {
  case TargetBufferFormat::kYUVNV12:
    colorFormat = ib2c::ColorFormat::kNV12;
    break;
  case TargetBufferFormat::kYUVNV21:
    colorFormat = ib2c::ColorFormat::kNV21;
    break;
  case TargetBufferFormat::kYUVNV12UBWC:
    colorFormat = ib2c::ColorFormat::kNV12;
    colorFormat |= ib2c::ColorMode::kUBWC;
    break;
  default:
    OVDBG_ERROR ("%s: Unsupported buffer format: %d", __func__,
        (int32_t) format);
    break;
  }

  OVDBG_VERBOSE ("%s:Selected GLES ColorFormat=%u", __func__, colorFormat);
  return colorFormat;
}
#endif // ENABLE_GLES

bool Overlay::IsOverlayItemValid (uint32_t overlay_id)
{
  OVDBG_DEBUG ("%s: Enter overlay_id(%d)", __func__, overlay_id);
  bool valid = false;
  for (auto& iter : overlay_items_) {
    if (overlay_id == (iter).first) {
      valid = true;
      break;
    }
  }
  OVDBG_DEBUG ("%s: Exit overlay_id(%d)", __func__, overlay_id);
  return valid;
}

OverlayItem::OverlayItem (int32_t ion_device, OverlayType type,
    OverlayBlitType blit_type, CLKernelIds kernel_id) :
    surface_ (), dirty_ (false), ion_device_ (ion_device), type_ (type),
    blit_type_ (blit_type), kernel_id_ (kernel_id), is_active_ (false)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);

  cr_surface_ = nullptr;
  cr_context_ = nullptr;

  use_alpha_only_ = false;

  if (blit_type == OverlayBlitType::kOpenCL) {
    for (CLKernelDescriptor kernel : OpenClKernel::supported_kernels) {
      if (kernel.id == kernel_id_) {
        if (kernel.instance == nullptr) {
          kernel.instance = OpenClKernel::New (kernel.kernel_path,
              kernel.kernel_name);
          if (!kernel.instance) {
            OVDBG_ERROR ("%s: Failed to build CL program", __func__);
            return;
          }
        }

        blit_ = kernel.instance;
        use_alpha_only_ = kernel.use_alpha_only;
        use_2D_image_ = kernel.use_2D_image;
        global_devider_w_ = kernel.global_devider_w;
        global_devider_h_ = kernel.global_devider_h;
        local_size_w_ = kernel.local_size_w;
        local_size_h_ = kernel.local_size_h;
        break;
      }
    }
  }

  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

OverlayItem::~OverlayItem ()
{
  DestroySurface ();
}

void OverlayItem::MarkDirty (bool dirty)
{
  dirty_ = dirty;
  OVDBG_VERBOSE ("%s: OverlayItem Type(%d) marked dirty!", __func__,
      (int32_t) type_);
}

void OverlayItem::Activate (bool value)
{
  is_active_ = value;
  OVDBG_VERBOSE ("%s: OverlayItem Type(%d) Activated!", __func__,
      (int32_t) type_);
}

uint32_t OverlayItem::CalcStride (uint32_t width, SurfaceFormat format)
{
  switch (format) {
  case SurfaceFormat::kARGB:
  case SurfaceFormat::kABGR:
    return width * 4;
  case SurfaceFormat::kRGB:
    return width * 3;
  case SurfaceFormat::kA8:
    return width;
  case SurfaceFormat::kA1:
    return (width + 7) / 8;
  default:
    OVDBG_ERROR ("%s:Format %d not supported", __func__, (int32_t)format);
    return 0;
  }
}

#ifdef ENABLE_C2D
uint32_t OverlayItem::GetC2DFormat (SurfaceFormat format)
{
  switch (format) {
  case SurfaceFormat::kARGB:
    return C2D_COLOR_FORMAT_8888_ARGB;
  case SurfaceFormat::kABGR:
    return C2D_FORMAT_SWAP_ENDIANNESS | C2D_COLOR_FORMAT_8888_RGBA;
  case SurfaceFormat::kRGB:
    return C2D_COLOR_FORMAT_888_RGB;
  case SurfaceFormat::kA8:
    return C2D_COLOR_FORMAT_8_A;
  case SurfaceFormat::kA1:
    return C2D_COLOR_FORMAT_1;
  default:
    OVDBG_ERROR ("%s:Format %d not supported", __func__, (int32_t)format);
    return -1;
  }
}
#endif // ENABLE_C2D

#ifdef ENABLE_GLES
uint32_t OverlayItem::GetGlesFormat (SurfaceFormat format)
{
  switch (format) {
  case SurfaceFormat::kARGB:
    return ib2c::ColorFormat::kARGB8888;
  case SurfaceFormat::kABGR:
    return ib2c::ColorFormat::kABGR8888;
  case SurfaceFormat::kRGB:
    return ib2c::ColorFormat::kRGB888;
  default:
    OVDBG_ERROR ("%s:Format %d not supported", __func__, (int32_t)format);
    return -1;
  }
}
#endif // ENABLE_GLES

cairo_format_t OverlayItem::GetCairoFormat (SurfaceFormat format)
{
  switch (format) {
  case SurfaceFormat::kARGB:
    return CAIRO_FORMAT_ARGB32;
  case SurfaceFormat::kRGB:
    return CAIRO_FORMAT_RGB24;
  case SurfaceFormat::kA8:
    return CAIRO_FORMAT_A8;
  case SurfaceFormat::kA1:
    return CAIRO_FORMAT_A1;
  case SurfaceFormat::kABGR:
  default:
    OVDBG_ERROR ("%s:Format %d not supported", __func__, (int32_t)format);
    return (cairo_format_t)-1;
  }
}

int32_t OverlayItem::AllocateIonMemory (IonMemInfo& mem_info, uint32_t size)
{
  OVDBG_VERBOSE ("%s:Enter", __func__);
  int32_t ret = 0, fd = -1;
  void* vaddr = nullptr;

#if defined(HAVE_LINUX_DMA_HEAP_H)
  struct dma_heap_allocation_data alloc_data;
#else
  struct ion_allocation_data alloc_data;
#if !defined(TARGET_ION_ABI_VERSION)
  struct ion_fd_data fd_data;
#endif // TARGET_ION_ABI_VERSION
#endif

  alloc_data.fd = 0;
  alloc_data.len = ROUND_TO(size, 4096);

#if defined(HAVE_LINUX_DMA_HEAP_H)
  // Permissions for the memory to be allocated.
  alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
  alloc_data.heap_flags = 0;
#else
  alloc_data.heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
  alloc_data.flags = ION_FLAG_CACHED;

#if !defined(TARGET_ION_ABI_VERSION)
  alloc_data.align = 4096;
#endif // TARGET_ION_ABI_VERSION
#endif

#if defined(HAVE_LINUX_DMA_HEAP_H)
  ret = ioctl (ion_device_, DMA_HEAP_IOCTL_ALLOC, &alloc_data);
#else
  ret = ioctl (ion_device_, ION_IOC_ALLOC, &alloc_data);
#endif

  if (ret != 0) {
    OVDBG_ERROR ("%s: Failed to allocate ION memory!", __func__);
    return -1;
  }

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  fd_data.handle = alloc_data.handle;

  ret = ioctl (ion_device_, ION_IOC_MAP, &fd_data);
  if (ret != 0) {
    OVDBG_ERROR ("%s: Failed to map to FD!", __func__);
    ioctl (ion_device_, ION_IOC_FREE, &alloc_data.handle);
    return -1;
  }

  fd = fd_data.fd;
#else
  fd = alloc_data.fd;
#endif // TARGET_ION_ABI_VERSION

  vaddr = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (vaddr == MAP_FAILED) {
    OVDBG_ERROR ("%s: mmap failed: %s (%d)\n", __func__, strerror (errno), errno);
#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
    ioctl (ion_device_, ION_IOC_FREE, &alloc_data.handle);
#endif // TARGET_ION_ABI_VERSION
    close(fd);
    return -1;
  }

  SyncStart (fd);
  mem_info.fd = fd;
  mem_info.size = size;
  mem_info.vaddr = vaddr;
#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  mem_info.handle = alloc_data.handle;
#endif // TARGET_ION_ABI_VERSION

  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}



#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
void OverlayItem::FreeIonMemory (void *&vaddr, int32_t &fd, uint32_t size,
                                 ion_user_handle_t handle)
#else
void OverlayItem::FreeIonMemory (void *&vaddr, int32_t &fd, uint32_t size)
#endif // TARGET_ION_ABI_VERSION
{
  if (vaddr) {
    if (fd != -1)
      SyncEnd (fd);
    munmap (vaddr, size);
    vaddr = nullptr;
  }

  if (fd != -1) {
#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
    if (ioctl (ion_device_, ION_IOC_FREE, &handle) < 0) {
      OVDBG_ERROR ("%s: Failed to free handle for FD %d!", __func__, fd);
    }
#endif // TARGET_ION_ABI_VERSION

    close (fd);
    fd = -1;
  }
}

int32_t OverlayItem::MapOverlaySurface (OverlaySurface &surface,
    IonMemInfo &mem_info)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);

  int32_t ret = 0;

  if (blit_type_ == OverlayBlitType::kOpenCL) {
    if (use_2D_image_) {
      ret = OpenClKernel::MapImage(surface.cl_buffer_, mem_info.vaddr,
          mem_info.fd, surface.width_, surface.height_, surface.stride_);
    } else {
      ret = OpenClKernel::MapBuffer (surface.cl_buffer_, mem_info.vaddr,
          mem_info.fd, mem_info.size);
    }
    if (ret) {
      OVDBG_ERROR ("%s: Failed to map image!", __func__);
      return -1;
    }
  } else if (blit_type_ == OverlayBlitType::kC2D) {
#ifdef ENABLE_C2D
    ret = c2dMapAddr (mem_info.fd, mem_info.vaddr, mem_info.size, 0,
        KGSL_USER_MEM_TYPE_ION, &surface.gpu_addr_);
    if (ret != C2D_STATUS_OK) {
      OVDBG_ERROR ("%s: c2dMapAddr failed!", __func__);
      return -1;
    }

    C2D_RGB_SURFACE_DEF c2dSurfaceDef;
    c2dSurfaceDef.format = GetC2DFormat (surface_.format_);
    c2dSurfaceDef.width = surface.width_;
    c2dSurfaceDef.height = surface.height_;
    c2dSurfaceDef.buffer = mem_info.vaddr;
    c2dSurfaceDef.phys = surface.gpu_addr_;
    c2dSurfaceDef.stride = surface.stride_;

    // Create source c2d surface.
    ret = c2dCreateSurface (&surface.c2dsurface_id_, C2D_SOURCE,
        (C2D_SURFACE_TYPE) (C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS),
        &c2dSurfaceDef);
    if (ret != C2D_STATUS_OK) {
      OVDBG_ERROR ("%s: c2dCreateSurface failed!", __func__);
      c2dUnMapAddr (surface.gpu_addr_);
      surface.gpu_addr_ = nullptr;
      return -1;
    }
#endif // ENABLE_C2D
  } else if (blit_type_ == OverlayBlitType::kGLES) {
#ifdef ENABLE_GLES
    ib2c::Surface insurface;

    insurface.fd = mem_info.fd;
    insurface.format = GetGlesFormat (surface_.format_);
    insurface.width = surface.width_;
    insurface.height = surface.height_;
    insurface.size = mem_info.size;
    insurface.stride0 = surface.stride_;
    insurface.offset0 = 0;
    insurface.nplanes = 1;

    try {
      surface.ib2c_surface_id_ = ib2c_engine_->CreateSurface(insurface,
          ib2c::SurfaceFlags::kInput);
    } catch (std::exception& e) {
      OVDBG_ERROR ("%s: Create surface failed, error: '%s'!", __func__, e.what());
      return -1;
    }
#endif // ENABLE_GLES
  }

  surface.ion_fd_ = mem_info.fd;
  surface.vaddr_ = mem_info.vaddr;
  surface.size_ = mem_info.size;
#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  surface.handle_ = mem_info.handle;
#endif // TARGET_ION_ABI_VERSION

  OVDBG_VERBOSE ("%s: Exit ", __func__);

  return 0;
}

void OverlayItem::UnMapOverlaySurface (OverlaySurface &surface)
{
  if (blit_type_ == OverlayBlitType::kOpenCL) {
    if (use_2D_image_) {
      OpenClKernel::unMapImage (surface.cl_buffer_);
    } else {
      OpenClKernel::UnMapBuffer (surface.cl_buffer_);
    }
  } else if (blit_type_ == OverlayBlitType::kC2D) {
#ifdef ENABLE_C2D
    if (surface.gpu_addr_) {
      c2dUnMapAddr (surface.gpu_addr_);
      surface.gpu_addr_ = nullptr;
      OVDBG_INFO ("%s: Unmapped text GPU address for type(%d)", __func__,
          (int32_t) type_);
    }

    if (surface.c2dsurface_id_) {
      c2dDestroySurface (surface.c2dsurface_id_);
      surface.c2dsurface_id_ = -1;
      OVDBG_INFO ("%s: Destroyed c2d text Surface for type(%d)", __func__,
          (int32_t) type_);
    }
#endif // ENABLE_C2D
  } else if (blit_type_ == OverlayBlitType::kGLES) {
#ifdef ENABLE_GLES
    try {
      if (surface.ib2c_surface_id_ != 0) {
        ib2c_engine_->DestroySurface(surface.ib2c_surface_id_);
        surface.ib2c_surface_id_ = 0;
      }
    } catch (std::exception& e) {
      OVDBG_ERROR ("%s: Destroy surface failed, error: '%s'!", __func__, e.what());
    }
#endif // ENABLE_GLES
  }
}

void OverlayItem::ExtractColorValues (uint32_t hex_color, RGBAValues* color)
{
  if (blit_type_ == OverlayBlitType::kGLES) {
    // TODO: Due to limitaion in IB2C library we have to switch the blue & red
    // colors when setting the cairo draw color otherwise it won't be displayed
    // correctly.
    color->blue = ( (hex_color >> 24) & 0xff) / 255.0;
    color->green = ( (hex_color >> 16) & 0xff) / 255.0;
    color->red = ( (hex_color >> 8) & 0xff) / 255.0;
    color->alpha = ( (hex_color) & 0xff) / 255.0;
  } else {
    color->red = ( (hex_color >> 24) & 0xff) / 255.0;
    color->green = ( (hex_color >> 16) & 0xff) / 255.0;
    color->blue = ( (hex_color >> 8) & 0xff) / 255.0;
    color->alpha = ( (hex_color) & 0xff) / 255.0;
  }
}

void OverlayItem::ClearSurface ()
{
  RGBAValues bg_color;
  memset (&bg_color, 0x0, sizeof bg_color);
  // Painting entire surface with background color or with fully transparent
  // color doesn't work since cairo uses the OVER compositing operator
  // by default, and blending something entirely transparent OVER something
  // else has no effect at all until compositing operator is changed to SOURCE,
  // the SOURCE operator copies both color and alpha values directly from the
  // source to the destination instead of blending.
#ifdef DEBUG_BACKGROUND_SURFACE
  ExtractColorValues(BG_DEBUG_COLOR, &bg_color);
  cairo_set_source_rgba(cr_context_, bg_color.red, bg_color.green,
      bg_color.blue, bg_color.alpha);
  cairo_set_operator(cr_context_, CAIRO_OPERATOR_SOURCE);
#else
  cairo_set_operator (cr_context_, CAIRO_OPERATOR_CLEAR);
#endif
  cairo_paint (cr_context_);
  cairo_surface_flush (cr_surface_);
  cairo_set_operator (cr_context_, CAIRO_OPERATOR_OVER);
  assert (CAIRO_STATUS_SUCCESS == cairo_status (cr_context_));
  cairo_surface_mark_dirty (cr_surface_);
}

void OverlayItem::DestroySurface ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  MarkDirty (true);
  UnMapOverlaySurface (surface_);
#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  FreeIonMemory (surface_.vaddr_, surface_.ion_fd_, surface_.size_,
                 surface_.handle_);
#else
  FreeIonMemory (surface_.vaddr_, surface_.ion_fd_, surface_.size_);
#endif // TARGET_ION_ABI_VERSION

  if (cr_surface_) {
    cairo_surface_destroy (cr_surface_);
  }
  if (cr_context_) {
    cairo_destroy (cr_context_);
  }
  OVDBG_VERBOSE ("%s: Exit", __func__);
}

void OverlayItemStaticImage::DestroySurface ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  MarkDirty (true);
  UnMapOverlaySurface (surface_);
#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  FreeIonMemory (surface_.vaddr_, surface_.ion_fd_, surface_.size_,
                 surface_.handle_);
#else
  FreeIonMemory (surface_.vaddr_, surface_.ion_fd_, surface_.size_);
#endif // TARGET_ION_ABI_VERSION
  OVDBG_VERBOSE ("%s: Exit", __func__);
}

#ifdef ENABLE_GLES
int32_t OverlayItemStaticImage::Init (std::shared_ptr<ib2c::IEngine> ib2c_engine,
                                      OverlayParam& param)
#else
int32_t OverlayItemStaticImage::Init (OverlayParam& param)
#endif // ENABLE_GLES
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  int32_t ret = 0;

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

#ifdef ENABLE_GLES
  ib2c_engine_ = ib2c_engine;
#endif // ENABLE_GLES

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;

  image_buffer_ = param.image_info.image_buffer;
  image_size_ = param.image_info.image_size;
  surface_.width_ = param.image_info.source_rect.width;
  surface_.height_ = param.image_info.source_rect.height;
  surface_.format_ = SurfaceFormat::kABGR;
  if (use_alpha_only_) {
    surface_.format_ = SurfaceFormat::kA8;
  }
  surface_.stride_ = CalcStride (surface_.width_, surface_.format_);
  if (blit_type_ == OverlayBlitType::kOpenCL) {
    surface_.blit_inst_ = blit_->AddInstance ();
  }

  OVDBG_VERBOSE (
      "%s: image blob  image_buffer_::0x%p  image_size_::%u " "image_width_::%u image_height_::%u ",
      __func__, image_buffer_, image_size_, surface_.width_,
      surface_.height_);

  crop_rect_x_ = param.image_info.source_rect.start_x;
  crop_rect_y_ = param.image_info.source_rect.start_y;
  crop_rect_width_ = param.image_info.source_rect.width;
  crop_rect_height_ = param.image_info.source_rect.height;
  OVDBG_VERBOSE (
      "%s: image blob  crop_rect_x_::%u  crop_rect_y_::%u " "crop_rect_width_::%u  crop_rect_height_::%u",
      __func__, crop_rect_x_, crop_rect_y_, crop_rect_width_,
      crop_rect_height_);

  ret = CreateSurface ();
  if (ret != 0) {
    OVDBG_ERROR ("%s: createLogoSurface failed!", __func__);
    return ret;
  }
  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemStaticImage::UpdateAndDraw ()
{
  if (blit_type_ == OverlayBlitType::kC2D) {
#ifdef ENABLE_C2D
    // Nothing to update, contents are static.
    // Never marked as dirty.
    std::lock_guard < std::mutex > lock (update_param_lock_);
    if (blob_buffer_updated_) {
      c2dSurfaceUpdated (surface_.c2dsurface_id_, nullptr);
      blob_buffer_updated_ = false;
    }
#endif // ENABLE_C2D
  }
  return 0;
}

void OverlayItemStaticImage::GetDrawInfo (uint32_t targetWidth,
    uint32_t targetHeight, std::vector<DrawInfo>& draw_infos)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  OV_UNUSED(targetWidth);
  OV_UNUSED(targetHeight);

  DrawInfo draw_info = {};
  draw_info.width = width_;
  draw_info.height = height_;
  draw_info.x = x_;
  draw_info.y = y_;
  draw_info.stride = surface_.stride_;
  draw_info.mask = surface_.cl_buffer_;
  draw_info.blit_inst = surface_.blit_inst_;
#ifdef ENABLE_C2D
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // ENABLE_C2D
#ifdef ENABLE_GLES
  draw_info.ib2cSurfaceId = surface_.ib2c_surface_id_;
#endif // ENABLE_GLES
  draw_info.global_devider_w = global_devider_w_;
  draw_info.global_devider_h = global_devider_h_;
  draw_info.local_size_w = local_size_w_;
  draw_info.local_size_h = local_size_h_;

  if (width_ != crop_rect_width_ || height_ != crop_rect_height_) {
    draw_info.in_width = crop_rect_width_;
    draw_info.in_height = crop_rect_height_;
    draw_info.in_x = crop_rect_x_;
    draw_info.in_y = crop_rect_y_;
  } else {
    draw_info.in_width = 0;
    draw_info.in_height = 0;
    draw_info.in_x = 0;
    draw_info.in_y = 0;
  }
  draw_infos.push_back (draw_info);

  OVDBG_VERBOSE ("%s: Exit", __func__);
}

void OverlayItemStaticImage::GetParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  param.type = OverlayType::kStaticImage;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width = width_;
  param.dst_rect.height = height_;
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

int32_t OverlayItemStaticImage::UpdateParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  std::lock_guard < std::mutex > lock (update_param_lock_);
  int32_t ret = 0;

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;

  image_buffer_ = param.image_info.image_buffer;
  image_size_ = param.image_info.image_size;
  surface_.width_ = param.image_info.source_rect.width;
  surface_.height_ = param.image_info.source_rect.height;
  surface_.stride_ = CalcStride (surface_.width_, surface_.format_);
  OVDBG_DEBUG (
      "%s: updated image blob  image_buffer_::0x%p image_size_::%u " "image_width_::%u image_height_::%u ",
      __func__, image_buffer_, param.image_info.image_size, surface_.width_,
      surface_.height_);

  crop_rect_x_ = param.image_info.source_rect.start_x;
  crop_rect_y_ = param.image_info.source_rect.start_y;
  crop_rect_width_ = param.image_info.source_rect.width;
  crop_rect_height_ = param.image_info.source_rect.height;
  OVDBG_DEBUG (
      "%s: updated image blob  crop_rect_x_::%u crop_rect_y_::%u " "crop_rect_width_::%u  crop_rect_height_::%u",
      __func__, crop_rect_x_, crop_rect_y_, crop_rect_width_,
      crop_rect_height_);

  // only buffer content is changed not buffer size
  if (param.image_info.buffer_updated
      && (param.image_info.image_size == image_size_)) {
    OVDBG_DEBUG (
        "%s: updated image_size_:: %u param.image_info.image_size:: %u ",
        __func__, image_size_, param.image_info.image_size);
    uint32_t size = param.image_info.image_size;
    uint32_t* pixels = static_cast<uint32_t*> (surface_.vaddr_);
    memcpy (pixels, image_buffer_, size);
    blob_buffer_updated_ = param.image_info.buffer_updated;
    MarkDirty (true);
  } else if (param.image_info.image_size != image_size_) {
    image_size_ = param.image_info.image_size;
    DestroySurface ();
    ret = CreateSurface ();
    if (ret != 0) {
      OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
      return ret;
    }
  }
  image_size_ = param.image_info.image_size;

  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}

int32_t OverlayItemStaticImage::CreateSurface ()
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  int32_t ret = 0;
  IonMemInfo mem_info;
  memset (&mem_info, 0x0, sizeof(IonMemInfo));

  ret = AllocateIonMemory (mem_info, image_size_);
  if (0 != ret) {
    OVDBG_ERROR ("%s:AllocateIonMemory failed", __func__);
    return ret;
  }
  uint32_t* pixels = static_cast<uint32_t*> (mem_info.vaddr);
  memcpy (pixels, image_buffer_, image_size_);

  ret = MapOverlaySurface (surface_, mem_info);
  if (ret) {
    OVDBG_ERROR ("%s: Map failed!", __func__);
    goto ERROR;
  }

  OVDBG_VERBOSE ("%s: Exit ", __func__);
  return ret;

ERROR:
  close (surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

OverlayItemDateAndTime::OverlayItemDateAndTime (int32_t ion_device,
    OverlayBlitType blit_type, CLKernelIds kernel_id) :
    OverlayItem (ion_device, OverlayType::kDateType, blit_type, kernel_id)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  memset (&date_time_type_, 0x0, sizeof date_time_type_);
  date_time_type_.time_format = OverlayTimeFormatType::kHHMM_24HR;
  date_time_type_.date_format = OverlayDateFormatType::kMMDDYYYY;
  OVDBG_VERBOSE ("%s:Exit", __func__);
}

OverlayItemDateAndTime::~OverlayItemDateAndTime ()
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

#ifdef ENABLE_GLES
int32_t OverlayItemDateAndTime::Init (std::shared_ptr<ib2c::IEngine> ib2c_engine,
                                      OverlayParam& param)
#else
int32_t OverlayItemDateAndTime::Init (OverlayParam& param)
#endif // ENABLE_GLES
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

#ifdef ENABLE_GLES
  ib2c_engine_ = ib2c_engine;
#endif // ENABLE_GLES

  text_color_ = param.color;
  font_size_ = param.font_size;
  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;
  prev_time_ = 0;

  date_time_type_.date_format = param.date_time.date_format;
  date_time_type_.time_format = param.date_time.time_format;

  // Create surface with the same aspect ratio
  surface_.width_ = GST_ROUND_UP_128(font_size_ * 6);
  surface_.height_ = font_size_ * 6 * height_ / width_;

  // Recalculate if surface height is less than minimum
  if (surface_.height_ < font_size_ * 2) {
    surface_.height_ = font_size_ * 2;
    surface_.width_ = GST_ROUND_UP_128(font_size_ * 2 * width_ / height_);
    // recalculated height according to aligned width
    surface_.height_ = surface_.width_ * height_ / width_;
  }
  surface_.format_ = SurfaceFormat::kARGB;
  if (use_alpha_only_) {
    surface_.format_ = SurfaceFormat::kA8;
  }
  surface_.stride_ = CalcStride (surface_.width_, surface_.format_);
  if (blit_type_ == OverlayBlitType::kOpenCL) {
    surface_.blit_inst_ = blit_->AddInstance ();
  }

  OVDBG_INFO ("%s: Offscreen buffer:(%dx%d)", __func__, surface_.width_,
      surface_.height_);

  auto ret = CreateSurface ();
  if (ret != 0) {
    OVDBG_ERROR ("%s: createLogoSurface failed!", __func__);
    return ret;
  }
  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemDateAndTime::UpdateAndDraw ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  int32_t ret = 0;
  if (!dirty_)
    return ret;

  struct timeval tv;
  time_t now_time;
  struct tm *time;
  char date_buf[40];
  char time_buf[40];

  gettimeofday (&tv, nullptr);
  now_time = tv.tv_sec;
  OVDBG_VERBOSE ("%s: curr time %ld prev time %ld", __func__, now_time,
      prev_time_);

  if (prev_time_ == now_time) {
    MarkDirty (true);
    return ret;
  }
  prev_time_ = now_time;
  time = localtime (&now_time);

  switch (date_time_type_.date_format) {
  case OverlayDateFormatType::kYYYYMMDD:
    strftime (date_buf, sizeof date_buf, "%Y/%m/%d", time);
    break;
  case OverlayDateFormatType::kMMDDYYYY:
  default:
    strftime (date_buf, sizeof date_buf, "%m/%d/%Y", time);
    break;
  }
  switch (date_time_type_.time_format) {
  case OverlayTimeFormatType::kHHMMSS_24HR:
    strftime (time_buf, sizeof time_buf, "%H:%M:%S", time);
    break;
  case OverlayTimeFormatType::kHHMMSS_AMPM:
    strftime (time_buf, sizeof time_buf, "%r", time);
    break;
  case OverlayTimeFormatType::kHHMM_24HR:
    strftime (time_buf, sizeof time_buf, "%H:%M", time);
    break;
  case OverlayTimeFormatType::kHHMM_AMPM:
  default:
    strftime (time_buf, sizeof time_buf, "%I:%M %p", time);
    break;
  }
  OVDBG_VERBOSE ("%s: date:time (%s:%s)", __func__, date_buf, time_buf);

  double x_date, x_time, y_date, y_time;
  x_date = x_time = y_date = y_time = 0.0;

  SyncStart (surface_.ion_fd_);

  // Clear the privous drawn contents.
  ClearSurface ();
  cairo_select_font_face (cr_context_, "@cairo:Georgia",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size (cr_context_, font_size_);
  cairo_set_antialias (cr_context_, CAIRO_ANTIALIAS_BEST);
  assert (CAIRO_STATUS_SUCCESS == cairo_status (cr_context_));

  cairo_font_extents_t font_extent;
  cairo_font_extents (cr_context_, &font_extent);
  OVDBG_VERBOSE (
      "%s: ascent=%f, descent=%f, height=%f, max_x_advance=%f," " max_y_advance = %f",
      __func__, font_extent.ascent, font_extent.descent, font_extent.height,
      font_extent.max_x_advance, font_extent.max_y_advance);

  cairo_text_extents_t date_text_extents;
  cairo_text_extents (cr_context_, date_buf, &date_text_extents);

  OVDBG_VERBOSE (
      "%s: Date: te.x_bearing=%f, te.y_bearing=%f, te.width=%f," " te.height=%f, te.x_advance=%f, te.y_advance=%f",
      __func__, date_text_extents.x_bearing, date_text_extents.y_bearing,
      date_text_extents.width, date_text_extents.height,
      date_text_extents.x_advance, date_text_extents.y_advance);

  cairo_font_options_t *options;
  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_DEFAULT);
  cairo_set_font_options (cr_context_, options);
  cairo_font_options_destroy (options);

  //(0,0) is at topleft corner of draw buffer.
  x_date = (surface_.width_ - date_text_extents.width) / 2.0;
  y_date = std::max (surface_.height_ / 2.0, date_text_extents.height);
  OVDBG_VERBOSE ("%s: x_date=%f, y_date=%f, ref=%f", __func__, x_date, y_date,
      date_text_extents.height - (font_extent.descent/2.0));
  cairo_move_to (cr_context_, x_date, y_date);

  // Draw date.
  RGBAValues text_color;
  memset (&text_color, 0x0, sizeof text_color);
  ExtractColorValues (text_color_, &text_color);
  cairo_set_source_rgba (cr_context_, text_color.red, text_color.green,
      text_color.blue, text_color.alpha);

  cairo_show_text (cr_context_, date_buf);
  assert (CAIRO_STATUS_SUCCESS == cairo_status (cr_context_));

  // Draw time.
  cairo_text_extents_t time_text_extents;
  cairo_text_extents (cr_context_, time_buf, &time_text_extents);
  OVDBG_VERBOSE (
      "%s: Time: te.x_bearing=%f, te.y_bearing=%f, te.width=%f," " te.height=%f, te.x_advance=%f, te.y_advance=%f",
      __func__, time_text_extents.x_bearing, time_text_extents.y_bearing,
      time_text_extents.width, time_text_extents.height,
      time_text_extents.x_advance, time_text_extents.y_advance);
  // Calculate the x_time to draw the time text extact middle of buffer.
  // Use x_width which usually few pixel less than the width of the actual
  // drawn text.
  x_time = (surface_.width_ - time_text_extents.width) / 2.0;
  y_time = y_date + date_text_extents.height;
  cairo_move_to (cr_context_, x_time, y_time);
  cairo_show_text (cr_context_, time_buf);
  assert (CAIRO_STATUS_SUCCESS == cairo_status (cr_context_));

  cairo_surface_flush (cr_surface_);
  cairo_surface_mark_dirty (cr_surface_);

  SyncEnd (surface_.ion_fd_);
  MarkDirty (true);
  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

void OverlayItemDateAndTime::GetDrawInfo (uint32_t targetWidth,
    uint32_t targetHeight, std::vector<DrawInfo>& draw_infos)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  OV_UNUSED(targetWidth);
  OV_UNUSED(targetHeight);

  DrawInfo draw_info = {};

  draw_info.width = width_;
  draw_info.height = height_;
  draw_info.x = x_;
  draw_info.y = y_;
  draw_info.stride = surface_.stride_;
  draw_info.mask = surface_.cl_buffer_;
  draw_info.blit_inst = surface_.blit_inst_;
#ifdef ENABLE_C2D
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // ENABLE_C2D
#ifdef ENABLE_GLES
  draw_info.ib2cSurfaceId = surface_.ib2c_surface_id_;
#endif // ENABLE_GLES
  draw_info.global_devider_w = global_devider_w_;
  draw_info.global_devider_h = global_devider_h_;
  draw_info.local_size_w = local_size_w_;
  draw_info.local_size_h = local_size_h_;
  draw_infos.push_back (draw_info);
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

void OverlayItemDateAndTime::GetParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  param.type = OverlayType::kDateType;
  param.color = text_color_;
  param.font_size = font_size_;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width = width_;
  param.dst_rect.height = height_;
  param.date_time.date_format = date_time_type_.date_format;
  param.date_time.time_format = date_time_type_.time_format;
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

int32_t OverlayItemDateAndTime::UpdateParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  int32_t ret = 0;

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

  text_color_ = param.color;
  font_size_ = param.font_size;
  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;

  date_time_type_.date_format = param.date_time.date_format;
  date_time_type_.time_format = param.date_time.time_format;

  if (width_ != param.dst_rect.width || height_ != param.dst_rect.height) {
    width_ = param.dst_rect.width;
    height_ = param.dst_rect.height;
    prev_time_ = 0;

    // Create surface with the same aspect ratio
    surface_.width_ = GST_ROUND_UP_128(font_size_ * 6);
    surface_.height_ = font_size_ * 6 * height_ / width_;

    // Recalculate if surface height is less than minimum
    if (surface_.height_ < font_size_ * 2) {
      surface_.height_ = font_size_ * 2;
      surface_.width_ = GST_ROUND_UP_128(font_size_ * 2 * width_ / height_);
      // recalculated height according to aligned width
      surface_.height_ = surface_.width_ * height_ / width_;
    }
    surface_.stride_ = CalcStride (surface_.width_, surface_.format_);

    OVDBG_INFO ("%s: New Offscreen buffer:(%dx%d)", __func__, surface_.width_,
        surface_.height_);

    DestroySurface ();
    ret = CreateSurface ();
    if (ret != 0) {
      OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
      return ret;
    }
  }

  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}

int32_t OverlayItemDateAndTime::CreateSurface ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  int32_t size = surface_.stride_ * surface_.height_;
  IonMemInfo mem_info;
  memset (&mem_info, 0x0, sizeof(IonMemInfo));
  auto ret = AllocateIonMemory (mem_info, size);
  if (0 != ret) {
    OVDBG_ERROR ("%s:AllocateIonMemory failed", __func__);
    return ret;
  }
  OVDBG_DEBUG ("%s: Ion memory allocated fd(%d)", __func__, mem_info.fd);

  cr_surface_ = cairo_image_surface_create_for_data (
      static_cast<uint8_t*> (mem_info.vaddr), GetCairoFormat (surface_.format_),
      surface_.width_, surface_.height_, surface_.stride_);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);

  UpdateAndDraw ();

  ret = MapOverlaySurface (surface_, mem_info);
  if (ret) {
    OVDBG_ERROR ("%s: Map failed!", __func__);
    goto ERROR;
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;

ERROR:
  close (surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

OverlayItemBoundingBox::OverlayItemBoundingBox (int32_t ion_device,
    OverlayBlitType blit_type, CLKernelIds kernel_id) :
    OverlayItem (ion_device, OverlayType::kBoundingBox, blit_type, kernel_id),
    text_height_ (0)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  OVDBG_VERBOSE ("%s: Exit", __func__);
}

OverlayItemBoundingBox::~OverlayItemBoundingBox ()
{
  OVDBG_INFO ("%s: Enter", __func__);
  DestroyTextSurface ();
  OVDBG_INFO ("%s: Exit", __func__);
}

#ifdef ENABLE_GLES
int32_t OverlayItemBoundingBox::Init (std::shared_ptr<ib2c::IEngine> ib2c_engine,
                                      OverlayParam& param)
#else
int32_t OverlayItemBoundingBox::Init (OverlayParam& param)
#endif // ENABLE_GLES
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

#ifdef ENABLE_GLES
  ib2c_engine_ = ib2c_engine;
#endif // ENABLE_GLES

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;
  bbox_color_ = param.color;
  font_size_ = param.font_size;

  surface_.width_ = kBoxBuffWidth;
  surface_.height_ = ROUND_TO( (surface_.width_ * height_) / width_, 2);
  surface_.format_ = SurfaceFormat::kARGB;
  if (use_alpha_only_) {
    surface_.format_ = SurfaceFormat::kA8;
  }
  surface_.stride_ = CalcStride (surface_.width_, surface_.format_);
  if (blit_type_ == OverlayBlitType::kOpenCL) {
    surface_.blit_inst_ = blit_->AddInstance ();
  }

  OVDBG_INFO ("%s: Offscreen buffer:(%dx%d)", __func__, surface_.width_,
      surface_.height_);

  text_surface_.width_ = 384;
  text_surface_.height_ = 80;
  text_surface_.format_ = surface_.format_;
  text_surface_.stride_ = CalcStride (text_surface_.width_, text_surface_.format_);
  if (blit_type_ == OverlayBlitType::kOpenCL) {
    text_surface_.blit_inst_ = blit_->AddInstance ();
  }

  box_stroke_width_ = (kStrokeWidth * surface_.width_ + width_ - 1) / width_;

  char prop_val[PROP_VALUE_MAX];
  property_get (PROP_BOX_STROKE_WIDTH, prop_val, "4");
  box_stroke_width_ =
      (static_cast<uint32_t> (atoi (prop_val)) > box_stroke_width_) ?
          static_cast<uint32_t> (atoi (prop_val)) : box_stroke_width_;

  bbox_name_ = param.bounding_box.box_name;

  auto ret = CreateSurface ();
  if (ret != 0) {
    OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
    return -EINVAL;
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemBoundingBox::UpdateAndDraw ()
{
  OVDBG_VERBOSE ("%s: Enter ", __func__);
  int32_t ret = 0;

  if (!dirty_) {
    OVDBG_DEBUG ("%s: Item is not dirty! Don't draw!", __func__);
    return ret;
  }
  //  First text is drawn.
  //  ----------
  //  | TEXT   |
  //  ----------
  // Then bounding box is drawn
  //  ----------
  //  |        |
  //  |  BOX   |
  //  |        |
  //  ----------

  SyncStart (surface_.ion_fd_);
  SyncStart (text_surface_.ion_fd_);

  OVDBG_INFO ("%s: Draw bounding box and text!", __func__);
  ClearSurface ();
  ClearTextSurface ();
  // Draw text first.
  cairo_select_font_face (text_cr_context_, "@cairo:Georgia",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

  cairo_set_font_size (text_cr_context_, font_size_);
  cairo_set_antialias (text_cr_context_, CAIRO_ANTIALIAS_BEST);

  cairo_font_extents_t font_extents;
  cairo_font_extents (text_cr_context_, &font_extents);
  OVDBG_VERBOSE (
      "%s: BBox Font: ascent=%f, descent=%f, height=%f, " "max_x_advance=%f, max_y_advance = %f",
      __func__, font_extents.ascent, font_extents.descent, font_extents.height,
      font_extents.max_x_advance, font_extents.max_y_advance);

  cairo_text_extents_t text_extents;
  cairo_text_extents (text_cr_context_, bbox_name_.c_str (), &text_extents);

  OVDBG_VERBOSE (
      "%s: BBox Text: te.x_bearing=%f, te.y_bearing=%f, te.width=%f," " te.height=%f, te.x_advance=%f, te.y_advance=%f",
      __func__, text_extents.x_bearing, text_extents.y_bearing,
      text_extents.width, text_extents.height, text_extents.x_advance,
      text_extents.y_advance);

  cairo_font_options_t *options;
  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_BEST);
  cairo_set_font_options (text_cr_context_, options);
  cairo_font_options_destroy (options);

  double x_text = 0.0;
  double y_text = text_extents.height + (font_extents.descent / 2.0);
  OVDBG_VERBOSE ("%s: x_text=%f, y_text=%f", __func__, x_text, y_text);
  cairo_move_to (text_cr_context_, x_text, y_text);

  RGBAValues bbox_color;
  memset (&bbox_color, 0x0, sizeof bbox_color);
  ExtractColorValues (bbox_color_, &bbox_color);
  cairo_set_source_rgba (text_cr_context_, bbox_color.red, bbox_color.green,
      bbox_color.blue, bbox_color.alpha);
  cairo_show_text (text_cr_context_, bbox_name_.c_str ());
  assert (CAIRO_STATUS_SUCCESS == cairo_status (text_cr_context_));
  cairo_surface_flush (text_cr_surface_);

  // Draw rectangle
  cairo_set_line_width (cr_context_, box_stroke_width_);
  cairo_set_source_rgba (cr_context_, bbox_color.red, bbox_color.green,
      bbox_color.blue, bbox_color.alpha);
  cairo_rectangle (cr_context_, box_stroke_width_ / 2, box_stroke_width_ / 2,
      surface_.width_ - box_stroke_width_,
      surface_.height_ - box_stroke_width_);
  cairo_stroke (cr_context_);
  assert (CAIRO_STATUS_SUCCESS == cairo_status (cr_context_));

  cairo_surface_flush (cr_surface_);

  SyncEnd (surface_.ion_fd_);
  SyncEnd (text_surface_.ion_fd_);
  MarkDirty (false);
  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

void OverlayItemBoundingBox::GetDrawInfo (uint32_t targetWidth,
    uint32_t targetHeight, std::vector<DrawInfo>& draw_infos)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  OV_UNUSED(targetHeight);

  DrawInfo draw_info_bbox = {};
  draw_info_bbox.x = x_;
  draw_info_bbox.y = y_;
  draw_info_bbox.width = width_;
  draw_info_bbox.height = height_;
  draw_info_bbox.stride = surface_.stride_;
  draw_info_bbox.mask = surface_.cl_buffer_;
  draw_info_bbox.blit_inst = surface_.blit_inst_;
#ifdef ENABLE_C2D
  draw_info_bbox.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // ENABLE_C2D
#ifdef ENABLE_GLES
  draw_info_bbox.ib2cSurfaceId = surface_.ib2c_surface_id_;
#endif // ENABLE_GLES
  draw_info_bbox.global_devider_w = global_devider_w_;
  draw_info_bbox.global_devider_h = global_devider_h_;
  draw_info_bbox.local_size_w = local_size_w_;
  draw_info_bbox.local_size_h = local_size_h_;
  draw_infos.push_back (draw_info_bbox);

  DrawInfo draw_info_text = {};
  draw_info_text.x = x_ + kTextMargin;
  draw_info_text.y = y_ + kTextMargin;
  draw_info_text.width = (targetWidth * kTextPercent) / 100;
  draw_info_text.height = (draw_info_text.width * text_surface_.height_)
      / text_surface_.width_;
  draw_info_text.stride = text_surface_.stride_;
  draw_info_text.mask = text_surface_.cl_buffer_;
  draw_info_text.blit_inst = text_surface_.blit_inst_;
#ifdef ENABLE_C2D
  draw_info_text.c2dSurfaceId = text_surface_.c2dsurface_id_;
#endif // ENABLE_C2D
#ifdef ENABLE_GLES
  draw_info_text.ib2cSurfaceId = text_surface_.ib2c_surface_id_;
#endif // ENABLE_GLES
  draw_info_text.global_devider_w = global_devider_w_;
  draw_info_text.global_devider_h = global_devider_h_;
  draw_info_text.local_size_w = local_size_w_;
  draw_info_text.local_size_h = local_size_h_;
  draw_infos.push_back (draw_info_text);

  OVDBG_VERBOSE ("%s: Exit", __func__);
}

void OverlayItemBoundingBox::GetParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  param.type = OverlayType::kBoundingBox;
  param.color = bbox_color_;
  param.font_size = font_size_;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width = width_;
  param.dst_rect.height = height_;
  int size = std::min (bbox_name_.length (), sizeof (param.user_text) - 1);
  bbox_name_.copy (param.user_text, size);
  param.user_text[size + 1] = '\0';
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

void OverlayItemBoundingBox::ClearTextSurface ()
{
  RGBAValues bg_color;
  memset (&bg_color, 0x0, sizeof bg_color);
  // Painting entire surface with background color or with fully transparent
  // color doesn't work since cairo uses the OVER compositing operator
  // by default, and blending something entirely transparent OVER something
  // else has no effect at all until compositing operator is changed to SOURCE,
  // the SOURCE operator copies both color and alpha values directly from the
  // source to the destination instead of blending.
#ifdef DEBUG_BACKGROUND_SURFACE
  ExtractColorValues(BG_DEBUG_COLOR, &bg_color);
  cairo_set_source_rgba(text_cr_context_, bg_color.red, bg_color.green,
      bg_color.blue, bg_color.alpha);
  cairo_set_operator(text_cr_context_, CAIRO_OPERATOR_SOURCE);
#else
  cairo_set_operator (text_cr_context_, CAIRO_OPERATOR_CLEAR);
#endif
  cairo_paint (text_cr_context_);
  cairo_surface_flush (text_cr_surface_);
  cairo_set_operator (text_cr_context_, CAIRO_OPERATOR_OVER);
  assert (CAIRO_STATUS_SUCCESS == cairo_status (text_cr_context_));
  cairo_surface_mark_dirty (text_cr_surface_);
}

int32_t OverlayItemBoundingBox::UpdateParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  int32_t ret = 0;

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;

  if (surface_.height_ != ROUND_TO( (surface_.width_ * height_) / width_, 2)) {
    surface_.height_ = ROUND_TO( (surface_.width_ * height_) / width_, 2);
    DestroySurface ();
    DestroyTextSurface ();
    ret = CreateSurface ();
    if (ret != 0) {
      OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
      return ret;
    }
  }

  if (box_stroke_width_
      != (kStrokeWidth * surface_.width_ + width_ - 1) / width_) {
    box_stroke_width_ = (kStrokeWidth * surface_.width_ + width_ - 1) / width_;
    MarkDirty (true);
  }

  if (bbox_color_ != param.color) {
    bbox_color_ = param.color;
    MarkDirty (true);
  }

  if (font_size_ != param.font_size) {
    font_size_ = param.font_size;
    MarkDirty (true);
  }

  if (bbox_name_.compare (param.bounding_box.box_name)) {
    bbox_name_ = param.bounding_box.box_name;
    MarkDirty (true);
  }

  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}

int32_t OverlayItemBoundingBox::CreateSurface ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  int32_t size = surface_.stride_ * surface_.height_;
  IonMemInfo mem_info;
  memset (&mem_info, 0x0, sizeof(IonMemInfo));
  auto ret = AllocateIonMemory (mem_info, size);
  if (0 != ret) {
    OVDBG_ERROR ("%s:AllocateIonMemory failed", __func__);
    return ret;
  }
  OVDBG_DEBUG ("%s: Ion memory allocated fd(%d)", __func__, mem_info.fd);

  cr_surface_ = cairo_image_surface_create_for_data (
      static_cast<uint8_t*> (mem_info.vaddr), GetCairoFormat (surface_.format_),
      surface_.width_, surface_.height_, surface_.stride_);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);

  ret = MapOverlaySurface (surface_, mem_info);
  if (ret) {
    OVDBG_ERROR ("%s: Map failed!", __func__);
    goto ERROR;
  }

  // Setup text surface
  size = text_surface_.stride_ * text_surface_.height_;
  memset (&mem_info, 0x0, sizeof(IonMemInfo));
  ret = AllocateIonMemory (mem_info, size);
  if (ret) {
    OVDBG_ERROR ("%s:AllocateIonMemory failed", __func__);
    return ret;
  }
  OVDBG_INFO ("%s: Ion memory allocated fd = %d", __func__, mem_info.fd);

  text_cr_surface_ = cairo_image_surface_create_for_data (
      static_cast<uint8_t*> (mem_info.vaddr),
      GetCairoFormat (text_surface_.format_),
      text_surface_.width_, text_surface_.height_, text_surface_.stride_);
  assert (text_cr_surface_ != nullptr);
  text_cr_context_ = cairo_create (text_cr_surface_);
  assert (text_cr_context_ != nullptr);

  ret = MapOverlaySurface (text_surface_, mem_info);
  if (ret) {
    OVDBG_ERROR ("%s: Map failed!", __func__);
    goto ERROR;
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;

ERROR:
  close (surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  close (text_surface_.ion_fd_);
  text_surface_.ion_fd_ = -1;
  return ret;
}

void OverlayItemBoundingBox::DestroyTextSurface ()
{
  UnMapOverlaySurface (text_surface_);

#if !defined(HAVE_LINUX_DMA_HEAP_H) && !defined(TARGET_ION_ABI_VERSION)
  FreeIonMemory (text_surface_.vaddr_, text_surface_.ion_fd_,
                 text_surface_.size_, text_surface_.handle_);
#else
  FreeIonMemory (text_surface_.vaddr_, text_surface_.ion_fd_,
                 text_surface_.size_);
#endif // TARGET_ION_ABI_VERSION


  if (text_cr_surface_) {
    cairo_surface_destroy (text_cr_surface_);
  }
  if (text_cr_context_) {
    cairo_destroy (text_cr_context_);
  }
}

OverlayItemText::~OverlayItemText ()
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

#ifdef ENABLE_GLES
int32_t OverlayItemText::Init (std::shared_ptr<ib2c::IEngine> ib2c_engine,
                               OverlayParam& param)
#else
int32_t OverlayItemText::Init (OverlayParam& param)
#endif // ENABLE_GLES
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

#ifdef ENABLE_GLES
  ib2c_engine_ = ib2c_engine;
#endif // ENABLE_GLES

  text_color_ = param.color;
  font_size_ = param.font_size;
  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;
  text_ = param.user_text;

  surface_.width_ = std::max (font_size_ * 4, width_);
  surface_.width_ = GST_ROUND_UP_128 (surface_.width_);
  surface_.height_ = std::max (font_size_, height_);
  surface_.format_ = SurfaceFormat::kARGB;
  if (use_alpha_only_) {
    surface_.format_ = SurfaceFormat::kA8;
  }
  surface_.stride_ = CalcStride (surface_.width_, surface_.format_);
  if (blit_type_ == OverlayBlitType::kOpenCL) {
    surface_.blit_inst_ = blit_->AddInstance ();
  }

  OVDBG_INFO ("%s: Offscreen buffer:(%dx%d)", __func__, surface_.width_,
      surface_.height_);

  auto ret = CreateSurface ();
  if (ret != 0) {
    OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
    return ret;
  }
  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemText::UpdateAndDraw ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  int32_t ret = 0;

  if (!dirty_)
    return ret;

  SyncStart (surface_.ion_fd_);

  // Split the Text based on new line character.
  vector < string > res;
  stringstream ss (text_); // Turn the string into a stream.
  string tok;
  while (getline (ss, tok, '\n')) {
    OVDBG_INFO ("%s: UserText:: Substring: %s", __func__, tok.c_str ());
    res.push_back (tok);
  }

  ClearSurface ();
  cairo_select_font_face (cr_context_, "@cairo:Georgia",
      CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size (cr_context_, font_size_);
  cairo_set_antialias (cr_context_, CAIRO_ANTIALIAS_BEST);
  assert (CAIRO_STATUS_SUCCESS == cairo_status (cr_context_));

  cairo_font_extents_t font_extent;
  cairo_font_extents (cr_context_, &font_extent);
  OVDBG_VERBOSE (
      "%s: ascent=%f, descent=%f, height=%f, max_x_advance=%f," " max_y_advance = %f",
      __func__, font_extent.ascent, font_extent.descent, font_extent.height,
      font_extent.max_x_advance, font_extent.max_y_advance);

  cairo_text_extents_t text_extents;
  cairo_text_extents (cr_context_, text_.c_str (), &text_extents);

  OVDBG_VERBOSE (
      "%s: Custom text: te.x_bearing=%f, te.y_bearing=%f," " te.width=%f, te.height=%f, te.x_advance=%f, te.y_advance=%f",
      __func__, text_extents.x_bearing, text_extents.y_bearing,
      text_extents.width, text_extents.height, text_extents.x_advance,
      text_extents.y_advance);

  cairo_font_options_t *options;
  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_DEFAULT);
  cairo_set_font_options (cr_context_, options);
  cairo_font_options_destroy (options);

  //(0,0) is at topleft corner of draw buffer.
  double x_text = 0.0;
  double y_text = 0.0;

  // Draw Text.
  RGBAValues text_color;
  memset (&text_color, 0x0, sizeof text_color);
  ExtractColorValues (text_color_, &text_color);
  cairo_set_source_rgba (cr_context_, text_color.red, text_color.green,
      text_color.blue, text_color.alpha);
  for (string substr : res) {
    y_text += text_extents.height + (font_extent.descent / 2.0);
    OVDBG_VERBOSE ("%s: x_text=%f, y_text=%f", __func__, x_text, y_text);
    cairo_move_to (cr_context_, x_text, y_text);
    cairo_show_text (cr_context_, substr.c_str ());
    assert (CAIRO_STATUS_SUCCESS == cairo_status (cr_context_));
  }
  cairo_surface_flush (cr_surface_);

  SyncEnd (surface_.ion_fd_);
  dirty_ = false;
  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

void OverlayItemText::GetDrawInfo (uint32_t targetWidth, uint32_t targetHeight,
    std::vector<DrawInfo>& draw_infos)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  OV_UNUSED(targetWidth);
  OV_UNUSED(targetHeight);

  DrawInfo draw_info = {};

  draw_info.width = width_;
  draw_info.height = height_;

  draw_info.x = x_;
  draw_info.y = y_;
  draw_info.stride = surface_.stride_;
  draw_info.mask = surface_.cl_buffer_;
  draw_info.blit_inst = surface_.blit_inst_;
#ifdef ENABLE_C2D
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // ENABLE_C2D
#ifdef ENABLE_GLES
  draw_info.ib2cSurfaceId = surface_.ib2c_surface_id_;
#endif // ENABLE_GLES
  draw_info.global_devider_w = global_devider_w_;
  draw_info.global_devider_h = global_devider_h_;
  draw_info.local_size_w = local_size_w_;
  draw_info.local_size_h = local_size_h_;
  draw_infos.push_back (draw_info);

  OVDBG_VERBOSE ("%s: Exit", __func__);
}

void OverlayItemText::GetParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  param.type = OverlayType::kUserText;
  param.color = text_color_;
  param.font_size = font_size_;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width = width_;
  param.dst_rect.height = height_;
  int size = std::min (text_.length (), sizeof (param.user_text) - 1);
  text_.copy (param.user_text, size);
  param.user_text[size + 1] = '\0';
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

int32_t OverlayItemText::UpdateParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  int32_t ret = 0;

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;

  if (width_ != param.dst_rect.width || height_ != param.dst_rect.height) {
    width_ = param.dst_rect.width;
    height_ = param.dst_rect.height;

    surface_.width_ = std::max (font_size_ * 4, width_);
    surface_.width_ = GST_ROUND_UP_128 (surface_.width_);
    surface_.height_ = std::max (font_size_, height_);
    surface_.stride_ = CalcStride (surface_.width_, surface_.format_);

    OVDBG_INFO ("%s: New Offscreen buffer:(%dx%d)", __func__, surface_.width_,
        surface_.height_);

    DestroySurface ();
    ret = CreateSurface ();
    if (ret != 0) {
      OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
      return ret;
    }
  }

  if (text_color_ != param.color) {
    text_color_ = param.color;
    MarkDirty (true);
  }

  if (font_size_ != param.font_size) {
    font_size_ = param.font_size;
    MarkDirty (true);
  }

  if (text_.compare (param.user_text)) {
    text_ = param.user_text;
    MarkDirty (true);
  }

  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}

int32_t OverlayItemText::CreateSurface ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  int32_t size = surface_.stride_ * surface_.height_;
  IonMemInfo mem_info;
  memset (&mem_info, 0x0, sizeof(IonMemInfo));
  auto ret = AllocateIonMemory (mem_info, size);
  if (0 != ret) {
    OVDBG_ERROR ("%s:AllocateIonMemory failed", __func__);
    return ret;
  }
  OVDBG_DEBUG ("%s: Ion memory allocated fd(%d)", __func__, mem_info.fd);

  cr_surface_ = cairo_image_surface_create_for_data (
      static_cast<uint8_t*> (mem_info.vaddr), GetCairoFormat (surface_.format_),
      surface_.width_, surface_.height_, surface_.stride_);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);

  UpdateAndDraw ();

  ret = MapOverlaySurface (surface_, mem_info);
  if (ret) {
    OVDBG_ERROR ("%s: Map failed!", __func__);
    goto ERROR;
  }

  OVDBG_INFO ("%s: Exit", __func__);
  return ret;

ERROR:
  close (surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

#ifdef ENABLE_GLES
int32_t OverlayItemPrivacyMask::Init (std::shared_ptr<ib2c::IEngine> ib2c_engine,
                                      OverlayParam& param)
#else
int32_t OverlayItemPrivacyMask::Init (OverlayParam& param)
#endif // ENABLE_GLES
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

#ifdef ENABLE_GLES
  ib2c_engine_ = ib2c_engine;
#endif // ENABLE_GLES

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;
  mask_color_ = param.color;
  config_ = param.privacy_mask;

  surface_.width_ = GST_ROUND_UP_128 (std::min (width_, kMaskBoxBufWidth));
  surface_.height_ = (surface_.width_ * height_) / width_;
  surface_.height_ = ROUND_TO(surface_.height_, 2);
  surface_.format_ = SurfaceFormat::kARGB;
  if (use_alpha_only_) {
    surface_.format_ = SurfaceFormat::kA8;
  }
  surface_.stride_ = CalcStride (surface_.width_, surface_.format_);
  if (blit_type_ == OverlayBlitType::kOpenCL) {
    surface_.blit_inst_ = blit_->AddInstance ();
  }

  OVDBG_INFO ("%s: Offscreen buffer:(%dx%d)", __func__, surface_.width_,
      surface_.height_);

  auto ret = CreateSurface ();
  if (ret != 0) {
    OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
    return ret;
  }
  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemPrivacyMask::UpdateAndDraw ()
{
  OVDBG_VERBOSE ("%s: Enter ", __func__);
  int32_t ret = 0;

  if (!dirty_) {
    OVDBG_DEBUG ("%s: Item is not dirty! Don't draw!", __func__);
    return ret;
  }

  SyncStart (surface_.ion_fd_);
  ClearSurface ();

  RGBAValues mask_color;
  switch (surface_.format_) {
  case SurfaceFormat::kARGB:
    ExtractColorValues (mask_color_, &mask_color);
    cairo_set_source_rgba (cr_context_, mask_color.red, mask_color.green,
         mask_color.blue, mask_color.alpha);
    break;

  case SurfaceFormat::kRGB:
    ExtractColorValues (mask_color_, &mask_color);
    cairo_set_source_rgb (cr_context_, mask_color.red, mask_color.green,
         mask_color.blue);
    break;

  case SurfaceFormat::kA8:
  case SurfaceFormat::kA1:
    // no color but still supported
    break;

  case SurfaceFormat::kABGR:
  default:
    OVDBG_ERROR ("%s: Format %d is not supported by Cairo", __func__,
        (int32_t) surface_.format_);
    return -1;
    break;
  }

  cairo_set_antialias (cr_context_, CAIRO_ANTIALIAS_BEST);

  switch (config_.type) {
  case OverlayPrivacyMaskType::kRectangle: {
    uint32_t x = (config_.rectangle.start_x * surface_.width_) / width_;
    uint32_t y = (config_.rectangle.start_y * surface_.width_) / width_;
    uint32_t w = (config_.rectangle.width * surface_.width_) / width_;
    uint32_t h = (config_.rectangle.height * surface_.width_) / width_;
    cairo_rectangle (cr_context_, x, y, w, h);
    cairo_fill (cr_context_);
  }
    break;

  case OverlayPrivacyMaskType::kInverseRectangle: {
    uint32_t x = (config_.rectangle.start_x * surface_.width_) / width_;
    uint32_t y = (config_.rectangle.start_y * surface_.width_) / width_;
    uint32_t w = (config_.rectangle.width * surface_.width_) / width_;
    uint32_t h = (config_.rectangle.height * surface_.width_) / width_;
    cairo_rectangle (cr_context_, 0, 0, surface_.width_, surface_.height_);
    cairo_rectangle (cr_context_, x, y, w, h);
    cairo_set_fill_rule (cr_context_, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill (cr_context_);
  }
    break;

  case OverlayPrivacyMaskType::kCircle: {
    uint32_t cx = (config_.circle.center_x * surface_.width_) / width_;
    uint32_t cy = (config_.circle.center_y * surface_.height_) / height_;
    uint32_t rad = (config_.circle.radius * surface_.width_) / width_;
    cairo_arc (cr_context_, cx, cy, rad, 0, 2 * M_PI);
    cairo_fill (cr_context_);
  }
    break;

  case OverlayPrivacyMaskType::kInverseCircle: {
    uint32_t cx = (config_.circle.center_x * surface_.width_) / width_;
    uint32_t cy = (config_.circle.center_y * surface_.height_) / height_;
    uint32_t rad = (config_.circle.radius * surface_.width_) / width_;
    cairo_arc (cr_context_, cx, cy, rad, 0, 2 * M_PI);
    cairo_rectangle (cr_context_, 0, 0, surface_.width_, surface_.height_);
    cairo_set_fill_rule (cr_context_, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill (cr_context_);
  }
    break;

  default:
    OVDBG_ERROR ("%s: Unsupported privacy mask type %d", __func__,
        (int32_t) config_.type);
    return -1;
  }
  assert (CAIRO_STATUS_SUCCESS == cairo_status (cr_context_));

  cairo_surface_flush (cr_surface_);

  SyncEnd (surface_.ion_fd_);
  // Don't paint until params gets updated by app(UpdateParameters).
  MarkDirty (false);
  return 0;
}

void OverlayItemPrivacyMask::GetDrawInfo (uint32_t targetWidth,
    uint32_t targetHeight, std::vector<DrawInfo>& draw_infos)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  OV_UNUSED(targetWidth);
  OV_UNUSED(targetHeight);

  DrawInfo draw_info = {};
  draw_info.x = x_;
  draw_info.y = y_;
  draw_info.width = width_;
  draw_info.height = height_;
  draw_info.stride = surface_.stride_;
  draw_info.mask = surface_.cl_buffer_;
  draw_info.blit_inst = surface_.blit_inst_;
#ifdef ENABLE_C2D
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // ENABLE_C2D
#ifdef ENABLE_GLES
  draw_info.ib2cSurfaceId = surface_.ib2c_surface_id_;
#endif // ENABLE_GLES
  draw_info.global_devider_w = global_devider_w_;
  draw_info.global_devider_h = global_devider_h_;
  draw_info.local_size_w = local_size_w_;
  draw_info.local_size_h = local_size_h_;
  draw_infos.push_back (draw_info);
  OVDBG_VERBOSE ("%s: Exit", __func__);
}

void OverlayItemPrivacyMask::GetParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  param.type = OverlayType::kPrivacyMask;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width = width_;
  param.dst_rect.height = height_;
  param.color = mask_color_;
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

int32_t OverlayItemPrivacyMask::UpdateParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  int32_t ret = 0;

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;
  mask_color_ = param.color;
  config_ = param.privacy_mask;

  surface_.width_ = kMaskBoxBufWidth;
  surface_.height_ = (surface_.width_ * height_) / width_;
  surface_.height_ = ROUND_TO(surface_.height_, 2);
  surface_.stride_ = CalcStride (surface_.width_, surface_.format_);

  OVDBG_INFO ("%s: Offscreen buffer:(%dx%d)", __func__, surface_.width_,
      surface_.height_);

  // Mark dirty, updated contents would be re-painted in next paint cycle.
  MarkDirty (true);
  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}

int32_t OverlayItemPrivacyMask::CreateSurface ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  int32_t size = surface_.stride_ * surface_.height_;
  IonMemInfo mem_info;
  memset (&mem_info, 0x0, sizeof(IonMemInfo));
  auto ret = AllocateIonMemory (mem_info, size);
  if (0 != ret) {
    OVDBG_ERROR ("%s:AllocateIonMemory failed", __func__);
    return ret;
  }
  OVDBG_DEBUG ("%s: Ion memory allocated fd(%d)", __func__, mem_info.fd);

  cr_surface_ = cairo_image_surface_create_for_data (
      static_cast<uint8_t*> (mem_info.vaddr), GetCairoFormat (surface_.format_),
      surface_.width_, surface_.height_, surface_.stride_);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);

  ret = MapOverlaySurface (surface_, mem_info);
  if (ret) {
    OVDBG_ERROR ("%s: Map failed!", __func__);
    goto ERROR;
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;

ERROR:
  close (surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

#ifdef ENABLE_GLES
int32_t OverlayItemGraph::Init (std::shared_ptr<ib2c::IEngine> ib2c_engine,
                                OverlayParam& param)
#else
int32_t OverlayItemGraph::Init (OverlayParam& param)
#endif // ENABLE_GLES
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

  if (param.graph.points_count > OVERLAY_GRAPH_NODES_MAX_COUNT) {
    OVDBG_ERROR ("%s: failed: points_count %d", __func__,
        param.graph.points_count);
    return -EINVAL;
  }

  if (param.graph.chain_count > OVERLAY_GRAPH_CHAIN_MAX_COUNT) {
    OVDBG_ERROR ("%s: failed: chain_count %d", __func__,
        param.graph.chain_count);
    return -EINVAL;
  }

#ifdef ENABLE_GLES
  ib2c_engine_ = ib2c_engine;
#endif // ENABLE_GLES

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;
  graph_color_ = param.color;
  graph_ = param.graph;

  float scaled_width = static_cast<float> (width_) / DOWNSCALE_FACTOR;
  float scaled_height = static_cast<float> (height_) / DOWNSCALE_FACTOR;

  float aspect_ratio = scaled_width / scaled_height;

  OVDBG_INFO ("%s: Graph(W:%dxH:%d), aspect_ratio(%f), scaled(W:%fxH:%f)",
      __func__, param.dst_rect.width, param.dst_rect.height, aspect_ratio,
      scaled_width, scaled_height);

  int32_t width = static_cast<int32_t> (round (scaled_width));
  width = GST_ROUND_UP_128 (width); // Round to multiple of 128.
  width = width > kGraphBufWidth ? width : kGraphBufWidth;
  int32_t height = (static_cast<int32_t> (width / aspect_ratio + 15) >> 4) << 4;
  height = height > kGraphBufHeight ? height : kGraphBufHeight;

  surface_.width_ = width;
  surface_.height_ = height;
  surface_.format_ = SurfaceFormat::kARGB;
  if (use_alpha_only_) {
    surface_.format_ = SurfaceFormat::kA8;
  }
  surface_.stride_ = CalcStride (surface_.width_, surface_.format_);
  if (blit_type_ == OverlayBlitType::kOpenCL) {
    surface_.blit_inst_ = blit_->AddInstance ();
  }

  downscale_ratio_ = (float) width_ / (float) surface_.width_;

  OVDBG_INFO ("%s: Offscreen buffer:(%dx%d)", __func__, surface_.width_,
      surface_.height_);

  auto ret = CreateSurface ();
  if (ret != 0) {
    OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
    return ret;
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemGraph::UpdateAndDraw ()
{
  OVDBG_VERBOSE ("%s: Enter ", __func__);
  int32_t ret = 0;

  if (!dirty_) {
    OVDBG_DEBUG ("%s: Item is not dirty! Don't draw!", __func__);
    return ret;
  }

  SyncStart (surface_.ion_fd_);
  OVDBG_INFO ("%s: Draw graph!", __func__);
  ClearSurface ();

  RGBAValues bbox_color;
  memset (&bbox_color, 0x0, sizeof bbox_color);
  ExtractColorValues (graph_color_, &bbox_color);
  cairo_set_source_rgba (cr_context_, bbox_color.red, bbox_color.green,
      bbox_color.blue, bbox_color.alpha);
  cairo_set_line_width (cr_context_, kLineWidth);

  // draw key points
  for (uint32_t i = 0; i < graph_.points_count; i++) {
    if (graph_.points[i].x >= 0 && graph_.points[i].y >= 0) {
      cairo_arc (cr_context_,
          (uint32_t) ((float) graph_.points[i].x / downscale_ratio_),
          (uint32_t) ((float) graph_.points[i].y / downscale_ratio_),
          kDotRadius, 0, 2 * M_PI);
      cairo_fill (cr_context_);
    }
  }

  // draw links
  for (uint32_t i = 0; i < graph_.chain_count; i++) {
    cairo_move_to (cr_context_,
        (uint32_t) (
            (float) graph_.points[graph_.chain[i][0]].x / downscale_ratio_),
        (uint32_t) (
            (float) graph_.points[graph_.chain[i][0]].y / downscale_ratio_));
    cairo_line_to (cr_context_,
        (uint32_t) (
            (float) graph_.points[graph_.chain[i][1]].x / downscale_ratio_),
        (uint32_t) (
            (float) graph_.points[graph_.chain[i][1]].y / downscale_ratio_));
    cairo_stroke (cr_context_);
  }

  cairo_surface_flush (cr_surface_);
  SyncEnd (surface_.ion_fd_);

  MarkDirty (false);
  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

void OverlayItemGraph::GetDrawInfo (uint32_t targetWidth, uint32_t targetHeight,
    std::vector<DrawInfo>& draw_infos)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  OV_UNUSED(targetWidth);
  OV_UNUSED(targetHeight);

  DrawInfo draw_info = {};
  draw_info.x = x_;
  draw_info.y = y_;
  draw_info.width = width_;
  draw_info.height = height_;
  draw_info.stride = surface_.stride_;
  draw_info.mask = surface_.cl_buffer_;
  draw_info.blit_inst = surface_.blit_inst_;
#ifdef ENABLE_C2D
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // ENABLE_C2D
#ifdef ENABLE_GLES
  draw_info.ib2cSurfaceId = surface_.ib2c_surface_id_;
#endif // ENABLE_GLES
  draw_info.global_devider_w = global_devider_w_;
  draw_info.global_devider_h = global_devider_h_;
  draw_info.local_size_w = local_size_w_;
  draw_info.local_size_h = local_size_h_;
  draw_infos.push_back (draw_info);
  OVDBG_VERBOSE ("%s: Exit", __func__);
}

void OverlayItemGraph::GetParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  param.type = OverlayType::kGraph;
  param.color = graph_color_;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width = width_;
  param.dst_rect.height = height_;
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

int32_t OverlayItemGraph::UpdateParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  int32_t ret = 0;

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

  if (param.graph.points_count > OVERLAY_GRAPH_NODES_MAX_COUNT) {
    OVDBG_ERROR ("%s: failed: points_count %d", __func__,
        param.graph.points_count);
    return -EINVAL;
  }

  if (param.graph.chain_count > OVERLAY_GRAPH_CHAIN_MAX_COUNT) {
    OVDBG_ERROR ("%s: failed: chain_count %d", __func__,
        param.graph.chain_count);
    return -EINVAL;
  }

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;
  graph_color_ = param.color;
  graph_ = param.graph;
  MarkDirty (true);

  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}

int32_t OverlayItemGraph::CreateSurface ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  int32_t size = surface_.stride_ * surface_.height_;
  IonMemInfo mem_info;
  memset (&mem_info, 0x0, sizeof(IonMemInfo));
  auto ret = AllocateIonMemory (mem_info, size);
  if (0 != ret) {
    OVDBG_ERROR ("%s:AllocateIonMemory failed", __func__);
    return ret;
  }
  OVDBG_DEBUG ("%s: Ion memory allocated fd(%d)", __func__, mem_info.fd);

  cr_surface_ = cairo_image_surface_create_for_data (
      static_cast<uint8_t*> (mem_info.vaddr), GetCairoFormat (surface_.format_),
      surface_.width_, surface_.height_, surface_.stride_);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);

  ret = MapOverlaySurface (surface_, mem_info);
  if (ret) {
    OVDBG_ERROR ("%s: Map failed!", __func__);
    goto ERROR;
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;

ERROR:
  close (surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

OverlayItemArrow::OverlayItemArrow (int32_t ion_device,
    OverlayBlitType blit_type, CLKernelIds kernel_id) :
    OverlayItem (ion_device, OverlayType::kArrow, blit_type, kernel_id),
    arrows_ (NULL)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  OVDBG_VERBOSE ("%s: Exit", __func__);
}

OverlayItemArrow::~OverlayItemArrow ()
{
  OVDBG_INFO ("%s: Enter", __func__);
  if (arrows_)
    free (arrows_);
  OVDBG_INFO ("%s: Exit", __func__);
}

#ifdef ENABLE_GLES
int32_t OverlayItemArrow::Init (std::shared_ptr<ib2c::IEngine> ib2c_engine,
                                OverlayParam& param)
#else
int32_t OverlayItemArrow::Init (OverlayParam& param)
#endif // ENABLE_GLES
{
  OVDBG_VERBOSE ("%s: Enter", __func__);

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

#ifdef ENABLE_GLES
  ib2c_engine_ = ib2c_engine;
#endif // ENABLE_GLES

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;
  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;
  arrow_color_ = param.color;
  arrows_ = (OverlayArrow *)
      malloc (sizeof (OverlayArrow) * width_ * height_ / 64);
  param.arrows = arrows_;
  arrows_count_ = 0;

  surface_.width_ = GST_ROUND_UP_128 (width_ / kBufferDiv);
  surface_.height_ = ROUND_TO( (surface_.width_ * height_) / width_, 2);
  surface_.format_ = SurfaceFormat::kARGB;
  if (use_alpha_only_) {
    surface_.format_ = SurfaceFormat::kA8;
  }
  surface_.stride_ = CalcStride (surface_.width_, surface_.format_);
  if (blit_type_ == OverlayBlitType::kOpenCL) {
    surface_.blit_inst_ = blit_->AddInstance ();
  }

  OVDBG_INFO ("%s: Offscreen buffer:(%dx%d)", __func__, surface_.width_,
      surface_.height_);

  auto ret = CreateSurface ();
  if (ret != 0) {
    OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
    return -EINVAL;
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

void OverlayItemArrow::calcVertexes(int32_t start_x, int32_t start_y,
    int32_t end_x, int32_t end_y,
    double& x1, double& y1, double& x2, double& y2)
{
    double angle = atan2 (end_y - start_y, end_x - start_x) + M_PI;
    x1 = end_x + (20 / kBufferDiv) * cos(angle - 0.3);
    y1 = end_y + (20 / kBufferDiv) * sin(angle - 0.3);
    x2 = end_x + (20 / kBufferDiv) * cos(angle + 0.3);
    y2 = end_y + (20 / kBufferDiv) * sin(angle + 0.3);
}

int32_t OverlayItemArrow::UpdateAndDraw ()
{
  OVDBG_VERBOSE ("%s: Enter ", __func__);
  int32_t ret = 0;

  if (!dirty_) {
    OVDBG_DEBUG ("%s: Item is not dirty! Don't draw!", __func__);
    return ret;
  }

  SyncStart (surface_.ion_fd_);
  OVDBG_INFO ("%s: Draw arrow arrows_count_ - %d", __func__, arrows_count_);
  ClearSurface ();

  RGBAValues arrow_color;
  memset (&arrow_color, 0x0, sizeof arrow_color);
  ExtractColorValues (arrow_color_, &arrow_color);

  double x1;
  double y1;
  double x2;
  double y2;

  cairo_set_antialias (cr_context_, CAIRO_ANTIALIAS_BEST);
  cairo_set_source_rgba (cr_context_, arrow_color.red, arrow_color.green,
      arrow_color.blue, arrow_color.alpha);
  cairo_set_line_width (cr_context_, 2.0 / kBufferDiv);

  for(uint32_t x = 0; x < arrows_count_; x++) {
    int32_t start_x = arrows_[x].start_x / kBufferDiv;
    int32_t start_y = arrows_[x].start_y / kBufferDiv;
    int32_t end_x = arrows_[x].end_x / kBufferDiv;
    int32_t end_y = arrows_[x].end_y / kBufferDiv;

    calcVertexes (start_x, start_y, end_x, end_y, x1, y1, x2, y2);

    cairo_move_to (cr_context_, end_x, end_y);
    cairo_line_to (cr_context_, x1, y1);
    cairo_stroke (cr_context_);
    cairo_move_to (cr_context_, end_x, end_y);
    cairo_line_to (cr_context_, x2, y2);
    cairo_stroke (cr_context_);

    cairo_move_to (cr_context_, end_x, end_y);
    cairo_line_to (cr_context_, start_x, start_y);
    cairo_stroke (cr_context_);
  }

  cairo_surface_flush (cr_surface_);
  SyncEnd (surface_.ion_fd_);
  MarkDirty (false);

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;
}

void OverlayItemArrow::GetDrawInfo (uint32_t targetWidth,
    uint32_t targetHeight, std::vector<DrawInfo>& draw_infos)
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  OV_UNUSED(targetWidth);
  OV_UNUSED(targetHeight);

  DrawInfo draw_info_arrows = {};
  draw_info_arrows.x = x_;
  draw_info_arrows.y = y_;
  draw_info_arrows.width = width_;
  draw_info_arrows.height = height_;
  draw_info_arrows.stride = surface_.stride_;
  draw_info_arrows.mask = surface_.cl_buffer_;
  draw_info_arrows.blit_inst = surface_.blit_inst_;
#ifdef ENABLE_C2D
  draw_info_arrows.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // ENABLE_C2D
#ifdef ENABLE_GLES
  draw_info_arrows.ib2cSurfaceId = surface_.ib2c_surface_id_;
#endif // ENABLE_GLES
  draw_info_arrows.global_devider_w = global_devider_w_;
  draw_info_arrows.global_devider_h = global_devider_h_;
  draw_info_arrows.local_size_w = local_size_w_;
  draw_info_arrows.local_size_h = local_size_h_;
  draw_infos.push_back (draw_info_arrows);

  OVDBG_VERBOSE ("%s: Exit", __func__);
}

void OverlayItemArrow::GetParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  param.type = OverlayType::kArrow;
  param.color = arrow_color_;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width = width_;
  param.dst_rect.height = height_;
  param.dst_rect.width = width_;
  param.arrows = arrows_;
  param.arrows_count = arrows_count_;
  OVDBG_VERBOSE ("%s:Exit ", __func__);
}

int32_t OverlayItemArrow::UpdateParameters (OverlayParam& param)
{
  OVDBG_VERBOSE ("%s:Enter ", __func__);
  int32_t ret = 0;

  if (param.dst_rect.width == 0 || param.dst_rect.height == 0) {
    OVDBG_ERROR ("%s: Image Width & Height is not correct!", __func__);
    return -EINVAL;
  }

  x_ = param.dst_rect.start_x;
  y_ = param.dst_rect.start_y;

  if (width_ != param.dst_rect.width || height_ != param.dst_rect.height) {
    surface_.width_ = GST_ROUND_UP_128 (width_ / kBufferDiv);
    surface_.height_ = (surface_.width_ * height_) / width_;
    surface_.height_ = ROUND_TO(surface_.height_, 2);
    surface_.stride_ = CalcStride (surface_.width_, surface_.format_);

    DestroySurface ();
    ret = CreateSurface ();
    if (ret != 0) {
      OVDBG_ERROR ("%s: CreateSurface failed!", __func__);
      return ret;
    }
  }

  width_ = param.dst_rect.width;
  height_ = param.dst_rect.height;

  arrow_color_ = param.color;
  arrows_count_ = param.arrows_count;

  MarkDirty (true);
  OVDBG_VERBOSE ("%s:Exit ", __func__);
  return ret;
}

int32_t OverlayItemArrow::CreateSurface ()
{
  OVDBG_VERBOSE ("%s: Enter", __func__);
  int32_t size = surface_.stride_ * surface_.height_;
  IonMemInfo mem_info;
  memset (&mem_info, 0x0, sizeof(IonMemInfo));
  auto ret = AllocateIonMemory (mem_info, size);
  if (0 != ret) {
    OVDBG_ERROR ("%s:AllocateIonMemory failed", __func__);
    return ret;
  }
  OVDBG_DEBUG ("%s: Ion memory allocated fd(%d)", __func__, mem_info.fd);

  cr_surface_ = cairo_image_surface_create_for_data (
      static_cast<uint8_t*> (mem_info.vaddr), GetCairoFormat (surface_.format_),
      surface_.width_, surface_.height_, surface_.stride_);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);

  ret = MapOverlaySurface (surface_, mem_info);
  if (ret) {
    OVDBG_ERROR ("%s: Map failed!", __func__);
    goto ERROR;
  }

  OVDBG_VERBOSE ("%s: Exit", __func__);
  return ret;

ERROR:
  close (surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

}; // namespace overlay
