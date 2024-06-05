/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cstdlib>
#include <dlfcn.h>
#include <gst/gst.h>

#include "open_cl_funcs.h"

std::shared_ptr<OpenClFuncs> OpenClFuncs::New() {
  void* lib_opencl_handle;

  dlerror();
  lib_opencl_handle = dlopen ("libOpenCl.so", RTLD_LAZY);

  if (nullptr != lib_opencl_handle) {
    GST_ERROR ("%s", dlerror());
    return nullptr;
  }

  static std::shared_ptr<OpenClFuncs> o =
     std::make_shared<OpenClFuncs>(lib_opencl_handle);
  return o;
}

OpenClFuncs::OpenClFuncs(void* lib_opencl_handle) :
    lib_opencl_handle_(lib_opencl_handle) {

  BuildProgram = reinterpret_cast<clBuildProgram_fnp*>(
    dlsym(lib_opencl_handle_, "clBuildProgram"));
  CreateBuffer = reinterpret_cast<clCreateBuffer_fnp*>(
    dlsym(lib_opencl_handle_, "clCreateBuffer"));
  CreateCommandQueueWithProperties = reinterpret_cast<clCreateCommandQueueWithProperties_fnp*>(
    dlsym(lib_opencl_handle_, "clCreateCommandQueueWithProperties"));
  CreateContext = reinterpret_cast<clCreateContext_fnp*>(
    dlsym(lib_opencl_handle_, "clCreateContext"));
  CreateImage = reinterpret_cast<clCreateImage_fnp*>(
    dlsym(lib_opencl_handle_, "clCreateImage"));
  CreateKernel = reinterpret_cast<clCreateKernel_fnp*>(
    dlsym(lib_opencl_handle_, "clCreateKernel"));
  CreateProgramWithSource = reinterpret_cast<clCreateProgramWithSource_fnp*>(
    dlsym(lib_opencl_handle_, "clCreateProgramWithSource"));
  EnqueueNDRangeKernel = reinterpret_cast<clEnqueueNDRangeKernel_fnp*>(
    dlsym(lib_opencl_handle_, "clEnqueueNDRangeKernel"));
  Flush = reinterpret_cast<clFlush_fnp*>(
    dlsym(lib_opencl_handle_, "clFlush"));
  GetDeviceIDs = reinterpret_cast<clGetDeviceIDs_fnp*>(
    dlsym(lib_opencl_handle_, "clGetDeviceIDs"));
#ifdef HAVE_CL_EXT_QCOM_H
  GetDeviceImageInfoQCOM = reinterpret_cast<clGetDeviceImageInfoQCOM_fnp*>(
    dlsym(lib_opencl_handle_, "clGetDeviceImageInfoQCOM"));
#endif // HAVE_CL_EXT_QCOM_H
  GetPlatformIDs = reinterpret_cast<clGetPlatformIDs_fnp*>(
    dlsym(lib_opencl_handle_, "clGetPlatformIDs"));
  GetProgramBuildInfo = reinterpret_cast<clGetProgramBuildInfo_fnp*>(
    dlsym(lib_opencl_handle_, "clGetProgramBuildInfo"));
  ReleaseCommandQueue = reinterpret_cast<clReleaseCommandQueue_fnp*>(
    dlsym(lib_opencl_handle_, "clReleaseCommandQueue"));
  ReleaseContext = reinterpret_cast<clReleaseContext_fnp*>(
    dlsym(lib_opencl_handle_, "clReleaseContext"));
  ReleaseDevice = reinterpret_cast<clReleaseDevice_fnp*>(
    dlsym(lib_opencl_handle_, "clReleaseDevice"));
  ReleaseEvent = reinterpret_cast<clReleaseEvent_fnp*>(
    dlsym(lib_opencl_handle_, "clReleaseEvent"));
  ReleaseKernel = reinterpret_cast<clReleaseKernel_fnp*>(
    dlsym(lib_opencl_handle_, "clReleaseKernel"));
  ReleaseMemObject = reinterpret_cast<clReleaseMemObject_fnp*>(
    dlsym(lib_opencl_handle_, "clReleaseMemObject"));
  ReleaseProgram = reinterpret_cast<clReleaseProgram_fnp*>(
    dlsym(lib_opencl_handle_, "clReleaseProgram"));
  SetEventCallback = reinterpret_cast<clSetEventCallback_fnp*>(
    dlsym(lib_opencl_handle_, "clSetEventCallback"));
  SetKernelArg = reinterpret_cast<clSetKernelArg_fnp*>(
    dlsym(lib_opencl_handle_, "clSetKernelArg"));
}

OpenClFuncs::~OpenClFuncs() {
  dlclose (lib_opencl_handle_);
}
