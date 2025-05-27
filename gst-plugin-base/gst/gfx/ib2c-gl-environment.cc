/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cstring>

#include "ib2c-gl-environment.h"
#include "ib2c-utils.h"

namespace ib2c {

std::mutex EglEnvironment::mutex_;
EGLDisplay EglEnvironment::display_= EGL_NO_DISPLAY;
uint32_t EglEnvironment::refcnt_ = 0;

std::string EglEnvironment::NewEglEnvironment(
      std::unique_ptr<EglEnvironment>& environment, EGLContext shrctx) {

  environment = std::make_unique<EglEnvironment>();
  return environment->Initialize(shrctx);
}

EglEnvironment::~EglEnvironment() {

  if (display_ != EGL_NO_DISPLAY)
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  if (context_ != EGL_NO_CONTEXT)
    eglDestroyContext(display_, context_);

  std::lock_guard<std::mutex> lk(mutex_);
  if ((--refcnt_) == 0) eglTerminate(display_);
}

std::string EglEnvironment::Initialize(EGLContext shrctx) {

  std::lock_guard<std::mutex> lk(mutex_);

  if (refcnt_ == 0) {
    if ((display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
      return Error("Failed to get EGL display, error: ", std::hex,
                   eglGetError(), "!");
    }

    EGLint major = 0, minor = 0;

    // Initialize the display.
    if (!eglInitialize(display_, &major, &minor)) {
      return Error("Failed to initilize EGL display, error: ", std::hex,
                   eglGetError(), "!");
    }

    Log("Initilized EGL display version: ", major, ".", minor);
  }

  refcnt_++;

  // Set the rendering API in current thread.
  if (!eglBindAPI (EGL_OPENGL_ES_API)) {
    throw Exception("Failed to set rendering API, error: ", std::hex,
                    eglGetError(), "!");
  }

  const EGLint attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };

  // Create EGL rendering context.
  context_ = eglCreateContext (display_, EGL_NO_CONFIG_KHR, shrctx, attribs);

  if (context_ == EGL_NO_CONTEXT) {
    throw Exception("Failed to create EGL context, error: ", std::hex,
                    eglGetError(), "!");
  }

  return std::string();
}

std::string EglEnvironment::BindContext(EGLSurface draw, EGLSurface read) {

  if (context_ == eglGetCurrentContext())
    return std::string();

  // Attach the EGL rendering context.
  if (!eglMakeCurrent(display_, draw, read, context_)) {
    return Error("Failed to attach EGL context, error: ", std::hex,
                 eglGetError(), "!");
  }

  return std::string();
}

std::string EglEnvironment::UnbindContext() {

  // Set the rendering API in current thread.
  if ((eglQueryAPI() != EGL_OPENGL_ES_API) && !eglBindAPI (EGL_OPENGL_ES_API)) {
    return Error("Failed to set rendering API, error: ", std::hex,
                 eglGetError(), "!");
  }

  if (context_ != eglGetCurrentContext())
    return std::string();

  if (!eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
    return Error("Failed to deattach EGL context, error: ", std::hex,
                 eglGetError(), "!");
  }

  return std::string();
}

} // namespace ib2c
