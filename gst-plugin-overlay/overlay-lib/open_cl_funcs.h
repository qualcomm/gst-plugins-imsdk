/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#pragma once

#include <memory>

#include <CL/cl.h>

#ifdef HAVE_CL_EXT_QCOM_H
#include <CL/cl_ext_qcom.h>
#endif // HAVE_CL_EXT_QCOM_H

using clBuildProgram_fnp = decltype(clBuildProgram);
using clCreateBuffer_fnp = decltype(clCreateBuffer);
using clCreateCommandQueueWithProperties_fnp = decltype(clCreateCommandQueueWithProperties);
using clCreateContext_fnp = decltype(clCreateContext);
using clCreateImage_fnp = decltype(clCreateImage);
using clCreateKernel_fnp = decltype(clCreateKernel);
using clCreateProgramWithSource_fnp = decltype(clCreateProgramWithSource);
using clEnqueueNDRangeKernel_fnp = decltype(clEnqueueNDRangeKernel);
using clFlush_fnp = decltype(clFlush);
using clGetDeviceIDs_fnp = decltype(clGetDeviceIDs);
#ifdef HAVE_CL_EXT_QCOM_H
using clGetDeviceImageInfoQCOM_fnp = decltype(clGetDeviceImageInfoQCOM);
#endif // HAVE_CL_EXT_QCOM_H
using clGetPlatformIDs_fnp = decltype(clGetPlatformIDs);
using clGetProgramBuildInfo_fnp = decltype(clGetProgramBuildInfo);
using clReleaseCommandQueue_fnp = decltype(clReleaseCommandQueue);
using clReleaseContext_fnp = decltype(clReleaseContext);
using clReleaseDevice_fnp = decltype(clReleaseDevice);
using clReleaseEvent_fnp = decltype(clReleaseEvent);
using clReleaseKernel_fnp = decltype(clReleaseKernel);
using clReleaseMemObject_fnp = decltype(clReleaseMemObject);
using clReleaseProgram_fnp = decltype(clReleaseProgram);
using clSetEventCallback_fnp = decltype(clSetEventCallback);
using clSetKernelArg_fnp = decltype(clSetKernelArg);

class OpenClFuncs {
public:
  static std::shared_ptr<OpenClFuncs> New();

  OpenClFuncs(void* lib_opencl_handle_);
  ~OpenClFuncs();

  clBuildProgram_fnp* BuildProgram;
  clCreateBuffer_fnp* CreateBuffer;
  clCreateCommandQueueWithProperties_fnp* CreateCommandQueueWithProperties;
  clCreateContext_fnp* CreateContext;
  clCreateImage_fnp* CreateImage;
  clCreateKernel_fnp* CreateKernel;
  clCreateProgramWithSource_fnp* CreateProgramWithSource;
  clEnqueueNDRangeKernel_fnp* EnqueueNDRangeKernel;
  clFlush_fnp* Flush;
  clGetDeviceIDs_fnp* GetDeviceIDs;
#ifdef HAVE_CL_EXT_QCOM_H
  clGetDeviceImageInfoQCOM_fnp* GetDeviceImageInfoQCOM;
#endif // HAVE_CL_EXT_QCOM_H
  clGetPlatformIDs_fnp* GetPlatformIDs;
  clGetProgramBuildInfo_fnp* GetProgramBuildInfo;
  clReleaseCommandQueue_fnp* ReleaseCommandQueue;
  clReleaseContext_fnp* ReleaseContext;
  clReleaseDevice_fnp* ReleaseDevice;
  clReleaseEvent_fnp* ReleaseEvent;
  clReleaseKernel_fnp* ReleaseKernel;
  clReleaseMemObject_fnp* ReleaseMemObject;
  clReleaseProgram_fnp* ReleaseProgram;
  clSetEventCallback_fnp* SetEventCallback;
  clSetKernelArg_fnp* SetKernelArg;

private:
  void* lib_opencl_handle_;
};
