/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <memory>
#include <iostream>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl32.h>

#include "ib2c-utils.h"

namespace ib2c {

namespace gl {

#define EXCEPTION_IF_GL_ERROR(env, ...)                              \
do {                                                                 \
  GLenum error = env->Gles()->GetError();                            \
                                                                     \
  if (error != GL_NO_ERROR)                                          \
    throw Exception(__VA_ARGS__, ", error: ", std::hex, error, "!"); \
} while (false)

#define RETURN_IF_GL_ERROR(env, ...)                              \
do {                                                              \
  GLenum error = env->Gles()->GetError();                         \
                                                                  \
  if (error != GL_NO_ERROR)                                       \
    return Error(__VA_ARGS__, ", error: ", std::hex, error, "!"); \
} while (false)

class EglLib {
 public:
  EglLib();
  ~EglLib();

  // Main API functions.
  decltype(eglGetProcAddress)*    GetProcAddress;
  decltype(eglGetError)*          GetError;
  decltype(eglGetDisplay)*        GetDisplay;

  decltype(eglInitialize)*        Initialize;
  decltype(eglTerminate)*         Terminate;

  decltype(eglBindAPI)*           BindAPI;
  decltype(eglQueryAPI)*          QueryAPI;

  decltype(eglCreateContext)*     CreateContext;
  decltype(eglDestroyContext)*    DestroyContext;
  decltype(eglGetCurrentContext)* GetCurrentContext;
  decltype(eglMakeCurrent)*       MakeCurrent;

  decltype(eglQueryString)*       QueryString;

  // Extension functions.
  PFNEGLCREATEIMAGEKHRPROC        CreateImageKHR;
  PFNEGLDESTROYIMAGEKHRPROC       DestroyImageKHR;

 private:
  void *handle_;
};

class GlesLib {
 public:
  GlesLib();
  ~GlesLib();

  // Main API functions.
  PFNGLGETERRORPROC                   GetError;

  PFNGLENABLEPROC                     Enable;
  PFNGLDISABLEPROC                    Disable;

  PFNGLGENTEXTURESPROC                GenTextures;
  PFNGLDELETETEXTURESPROC             DeleteTextures;
  PFNGLACTIVETEXTUREPROC              ActiveTexture;
  PFNGLBINDTEXTUREPROC                BindTexture;
  PFNGLTEXSTORAGE2DPROC               TexStorage2D;
  PFNGLBINDIMAGETEXTUREPROC           BindImageTexture;

  PFNGLGENFRAMEBUFFERSPROC            GenFramebuffers;
  PFNGLDELETEFRAMEBUFFERSPROC         DeleteFramebuffers;
  PFNGLBINDFRAMEBUFFERPROC            BindFramebuffer;
  PFNGLFRAMEBUFFERTEXTURE2DPROC       FramebufferTexture2D;

  PFNGLCLEARPROC                      Clear;
  PFNGLCLEARCOLORPROC                 ClearColor;
  PFNGLBLENDFUNCPROC                  BlendFunc;

  PFNGLGETATTRIBLOCATIONPROC          GetAttribLocation;
  PFNGLENABLEVERTEXATTRIBARRAYPROC    EnableVertexAttribArray;
  PFNGLDISABLEVERTEXATTRIBARRAYPROC   DisableVertexAttribArray;
  PFNGLVERTEXATTRIBPOINTERPROC        VertexAttribPointer;

  PFNGLGETUNIFORMLOCATIONPROC         GetUniformLocation;
  PFNGLUNIFORM1IPROC                  Uniform1i;
  PFNGLUNIFORM1FPROC                  Uniform1f;
  PFNGLUNIFORM2FPROC                  Uniform2f;
  PFNGLUNIFORM3FPROC                  Uniform3f;
  PFNGLUNIFORM4FPROC                  Uniform4f;
  PFNGLUNIFORMMATRIX4FVPROC           UniformMatrix4fv;

  PFNGLGETINTEGERVPROC                GetIntegerv;
  PFNGLGETSTRINGIPROC                 GetStringi;

  PFNGLVIEWPORTPROC                   Viewport;

  PFNGLDRAWBUFFERSPROC                DrawBuffers;
  PFNGLDRAWARRAYSPROC                 DrawArrays;
  PFNGLDISPATCHCOMPUTEPROC            DispatchCompute;

  PFNGLFINISHPROC                     Finish;
  PFNGLFENCESYNCPROC                  FenceSync;
  PFNGLDELETESYNCPROC                 DeleteSync;
  PFNGLCLIENTWAITSYNCPROC             ClientWaitSync;

  PFNGLCREATESHADERPROC               CreateShader;
  PFNGLDELETESHADERPROC               DeleteShader;
  PFNGLGETSHADERIVPROC                GetShaderiv;
  PFNGLGETSHADERINFOLOGPROC           GetShaderInfoLog;
  PFNGLSHADERSOURCEPROC               ShaderSource;
  PFNGLCOMPILESHADERPROC              CompileShader;
  PFNGLATTACHSHADERPROC               AttachShader;

  PFNGLCREATEPROGRAMPROC              CreateProgram;
  PFNGLDELETEPROGRAMPROC              DeleteProgram;
  PFNGLLINKPROGRAMPROC                LinkProgram;
  PFNGLUSEPROGRAMPROC                 UseProgram;

  // Extension functions.
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC EGLImageTargetTexture2DOES;

 private:
  void *handle_;
};

enum class ContextType {
  kPrimary,
  kAuxilary,
};

class Environment {
 public:
  static std::string NewEnvironment(std::shared_ptr<Environment>& environment);

  Environment() = default;
  ~Environment();

  const EglLib* Egl() const { return egl_lib_; }
  const GlesLib* Gles() const { return gles_lib_; }

  EGLDisplay Display() const { return display_; }

  const EGLContext& Context(ContextType type) const {
    return (type == ContextType::kPrimary) ? m_context_ : s_context_;
  }

  std::string BindContext(ContextType type, EGLSurface draw, EGLSurface read);
  std::string UnbindContext(ContextType type);

  bool QueryExtension(const std::string extname);

 private:
  std::string Initialize();

  /// Mutex for protecting EGL display creation/termination.
  static std::mutex mutex_;

  /// Interface to the dynamically loaded EGL library.
  static EglLib*    egl_lib_;
  /// Interface to the dynamically loaded GLES library.
  static GlesLib*   gles_lib_;

  /// EGL display.
  static EGLDisplay display_;
  /// Reference counter for EGL display usage.
  static uint32_t   refcnt_;

  /// Main/Primary EGL rendering context.
  EGLContext        m_context_;
  /// Secondary EGL rendering context.
  EGLContext        s_context_;
};

} // namespace gl

} // namespace ib2c
