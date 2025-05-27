/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstdlib>
#include <cstdint>
#include <mutex>
#include <string>
#include <memory>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl32.h>

namespace ib2c {

class EglEnvironment {
 public:
  static std::string NewEglEnvironment(
      std::unique_ptr<EglEnvironment>& environment,
      EGLContext shrctx = EGL_NO_CONTEXT);

  EglEnvironment() = default;
  ~EglEnvironment();

  EGLDisplay Display() const { return display_; }
  const EGLContext& Context() const { return context_; }

  std::string BindContext(EGLSurface draw, EGLSurface read);
  std::string UnbindContext();

 private:
  std::string Initialize(EGLContext shrctx);

  /// Mutex for protecting EGL display creation/termination.
  static std::mutex mutex_;
  /// EGL display.
  static EGLDisplay display_;
  /// Reference counter for EGL display usage.
  static uint32_t   refcnt_;

  /// EGL rendering context.
  EGLContext        context_;
};

} // namespace ib2c
