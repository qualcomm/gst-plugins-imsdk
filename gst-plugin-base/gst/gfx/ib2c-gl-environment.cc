/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <unistd.h>
#include <dlfcn.h>

#include "ib2c-gl-environment.h"
#include "ib2c-utils.h"

namespace ib2c {

namespace gl {

#define LOAD_EGL_SYMBOL(handle, symbol)                                     \
do {                                                                        \
  symbol = (typeof(symbol)) dlsym(handle, "egl"#symbol);                    \
                                                                            \
  if (nullptr == symbol)                                                    \
    throw Exception("Failed to load egl", #symbol, ", error: ", dlerror()); \
} while (false)

#define LOAD_GL_SYMBOL(handle, symbol)                                     \
do {                                                                       \
  symbol = (typeof(symbol)) dlsym(handle, "gl"#symbol);                    \
                                                                           \
  if (nullptr == symbol)                                                   \
    throw Exception("Failed to load gl", #symbol, ", error: ", dlerror()); \
} while (false)

#define GET_EGL_SYMBOL(libegl, symbol)                               \
do {                                                                 \
  libegl->symbol =                                                   \
      (typeof(libegl->symbol)) libegl->GetProcAddress("egl"#symbol); \
                                                                     \
  if (nullptr == libegl->symbol)                                     \
    throw Exception("Failed to get egl", #symbol, " !");             \
} while (false)

#define GET_GL_SYMBOL(libgles, libegl, symbol)                       \
do {                                                                 \
  libgles->symbol =                                                  \
      (typeof(libgles->symbol)) libegl->GetProcAddress("gl"#symbol); \
                                                                     \
  if (nullptr == libgles->symbol)                                    \
    throw Exception("Failed to get gl", #symbol, " !");              \
} while (false)

std::mutex Environment::mutex_;
EglLib*    Environment::egl_lib_ = nullptr;
GlesLib*   Environment::gles_lib_ = nullptr;
EGLDisplay Environment::display_= EGL_NO_DISPLAY;
uint32_t   Environment::refcnt_ = 0;

std::string Environment::NewEnvironment(
      std::shared_ptr<Environment>& environment) {

  environment = std::make_shared<Environment>();
  return environment->Initialize();
}

Environment::~Environment() {

  if (display_ != EGL_NO_DISPLAY)
    egl_lib_->MakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  if (m_context_ != EGL_NO_CONTEXT)
    egl_lib_->DestroyContext(display_, m_context_);

  if (s_context_ != EGL_NO_CONTEXT)
    egl_lib_->DestroyContext(display_, s_context_);

  std::lock_guard<std::mutex> lk(mutex_);

  if ((--refcnt_) == 0)  {
    egl_lib_->Terminate(display_);

    delete gles_lib_;
    delete egl_lib_;
  }
}

std::string Environment::Initialize() {

  std::lock_guard<std::mutex> lk(mutex_);

  if (refcnt_ == 0) {
    egl_lib_ = new EglLib();
    gles_lib_ = new GlesLib();

    display_ = egl_lib_->GetDisplay(EGL_DEFAULT_DISPLAY);

    if (display_ == EGL_NO_DISPLAY) {
      return Error("Failed to get EGL display, error: ", std::hex,
                   egl_lib_->GetError(), "!");
    }

    EGLint major = 0, minor = 0;

    // Initialize the display.
    if (!egl_lib_->Initialize(display_, &major, &minor)) {
      return Error("Failed to initilize EGL display, error: ", std::hex,
                   egl_lib_->GetError(), "!");
    }

    // Set the rendering API in current thread.
    if (!egl_lib_->BindAPI(EGL_OPENGL_ES_API)) {
      throw Exception("Failed to set rendering API, error: ", std::hex,
                      egl_lib_->GetError(), "!");
    }

    GET_EGL_SYMBOL(egl_lib_, CreateImageKHR);
    GET_EGL_SYMBOL(egl_lib_, DestroyImageKHR);

    GET_GL_SYMBOL(gles_lib_, egl_lib_, EGLImageTargetTexture2DOES);

    Log("Initilized EGL display version: ", major, ".", minor);
  }

  refcnt_++;

  const EGLint attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };

  // Create Main/Primary EGL rendering context.
  m_context_ = egl_lib_->CreateContext(display_, EGL_NO_CONFIG_KHR,
                                       EGL_NO_CONTEXT, attribs);

  if (m_context_ == EGL_NO_CONTEXT) {
    throw Exception("Failed to create primary EGL context, error: ", std::hex,
                    egl_lib_->GetError(), "!");
  }

  // Create Secondary/Auxilary EGL rendering context.
  s_context_ = egl_lib_->CreateContext(display_, EGL_NO_CONFIG_KHR, m_context_,
                                       attribs);

  if (s_context_ == EGL_NO_CONTEXT) {
    throw Exception("Failed to create secondary EGL context, error: ", std::hex,
                    egl_lib_->GetError(), "!");
  }

  return std::string();
}

std::string Environment::BindContext(ContextType type, EGLSurface draw,
                                     EGLSurface read) {

  EGLContext context =
      (type == ContextType::kPrimary) ? m_context_ : s_context_;

  if (context == egl_lib_->GetCurrentContext())
    return std::string();

  // Attach the EGL rendering context.
  if (!egl_lib_->MakeCurrent(display_, draw, read, context)) {
    return Error("Failed to attach EGL context, error: ", std::hex,
                 egl_lib_->GetError(), "!");
  }

  return std::string();
}

std::string Environment::UnbindContext(ContextType type) {

  EGLContext context =
      (type == ContextType::kPrimary) ? m_context_ : s_context_;

  // Set the rendering API in current thread.
  if ((egl_lib_->QueryAPI() != EGL_OPENGL_ES_API) &&
      !egl_lib_->BindAPI (EGL_OPENGL_ES_API)) {
    return Error("Failed to set rendering API, error: ", std::hex,
                 egl_lib_->GetError(), "!");
  }

  if (context != egl_lib_->GetCurrentContext())
    return std::string();

  EGLBoolean success =
      egl_lib_->MakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                            EGL_NO_CONTEXT);

  if (!success) {
    return Error("Failed to deattach EGL context, error: ", std::hex,
                 egl_lib_->GetError(), "!");
  }

  return std::string();
}

bool Environment::QueryExtension(const std::string extname) {

  auto extensions =
      reinterpret_cast<const char *>(
          egl_lib_->QueryString(display_, EGL_EXTENSIONS));

  if (extensions == nullptr) {
    throw Exception("Failed to query extensions, error: ", std::hex,
                    egl_lib_->GetError(), "!");
  }

  if (strstr(extensions, extname.c_str()) != nullptr)
    return true;

  GLint n_extensions;

  gles_lib_->GetIntegerv(GL_NUM_EXTENSIONS, &n_extensions);
  GLenum error = gles_lib_->GetError();

  if (error != GL_NO_ERROR) {
    throw Exception("Failed to get number of supported extensions, error: ",
                    std::hex, error, "!");
  }

  for (GLint idx = 0; idx < n_extensions; idx++) {
    auto name =
        reinterpret_cast<const char*>(gles_lib_->GetStringi(GL_EXTENSIONS, idx));

    if ((error = gles_lib_->GetError()) != GL_NO_ERROR) {
      throw Exception("Failed to get name of extension at ", idx, ", error: ",
                      std::hex, error, "!");
    }

    if (extname.compare(name) == 0)
      return true;
  }

  return false;
}

EglLib::EglLib() {

  if ((handle_ = dlopen("libEGL.so.1", RTLD_LAZY)) == nullptr)
    throw Exception("Failed to load EGL lib, error: ", dlerror(), "!");

  LOAD_EGL_SYMBOL(handle_, GetProcAddress);
  LOAD_EGL_SYMBOL(handle_, GetError);
  LOAD_EGL_SYMBOL(handle_, GetDisplay);
  LOAD_EGL_SYMBOL(handle_, Initialize);
  LOAD_EGL_SYMBOL(handle_, Terminate);
  LOAD_EGL_SYMBOL(handle_, BindAPI);
  LOAD_EGL_SYMBOL(handle_, QueryAPI);
  LOAD_EGL_SYMBOL(handle_, CreateContext);
  LOAD_EGL_SYMBOL(handle_, DestroyContext);
  LOAD_EGL_SYMBOL(handle_, GetCurrentContext);
  LOAD_EGL_SYMBOL(handle_, MakeCurrent);
  LOAD_EGL_SYMBOL(handle_, QueryString);
}

EglLib::~EglLib() {

  dlclose(handle_);
}

GlesLib::GlesLib() {

  if ((handle_ = dlopen("libGLESv2.so.2", RTLD_LAZY)) == nullptr)
    throw Exception("Failed to load GLESv2 lib, error: ", dlerror(), "!");

  LOAD_GL_SYMBOL(handle_, GetError);
  LOAD_GL_SYMBOL(handle_, GenTextures);
  LOAD_GL_SYMBOL(handle_, DeleteTextures);
  LOAD_GL_SYMBOL(handle_, ActiveTexture);
  LOAD_GL_SYMBOL(handle_, BindTexture);
  LOAD_GL_SYMBOL(handle_, TexStorage2D);
  LOAD_GL_SYMBOL(handle_, BindImageTexture);
  LOAD_GL_SYMBOL(handle_, GenFramebuffers);
  LOAD_GL_SYMBOL(handle_, DeleteFramebuffers);
  LOAD_GL_SYMBOL(handle_, BindFramebuffer);
  LOAD_GL_SYMBOL(handle_, FramebufferTexture2D);
  LOAD_GL_SYMBOL(handle_, Clear);
  LOAD_GL_SYMBOL(handle_, ClearColor);
  LOAD_GL_SYMBOL(handle_, BlendFunc);
  LOAD_GL_SYMBOL(handle_, Enable);
  LOAD_GL_SYMBOL(handle_, Disable);
  LOAD_GL_SYMBOL(handle_, GetAttribLocation);
  LOAD_GL_SYMBOL(handle_, EnableVertexAttribArray);
  LOAD_GL_SYMBOL(handle_, DisableVertexAttribArray);
  LOAD_GL_SYMBOL(handle_, VertexAttribPointer);
  LOAD_GL_SYMBOL(handle_, GetUniformLocation);
  LOAD_GL_SYMBOL(handle_, Uniform1i);
  LOAD_GL_SYMBOL(handle_, Uniform1f);
  LOAD_GL_SYMBOL(handle_, Uniform2f);
  LOAD_GL_SYMBOL(handle_, Uniform3f);
  LOAD_GL_SYMBOL(handle_, Uniform4f);
  LOAD_GL_SYMBOL(handle_, UniformMatrix4fv);
  LOAD_GL_SYMBOL(handle_, GetIntegerv);
  LOAD_GL_SYMBOL(handle_, GetStringi);
  LOAD_GL_SYMBOL(handle_, Viewport);
  LOAD_GL_SYMBOL(handle_, DrawArrays);
  LOAD_GL_SYMBOL(handle_, DispatchCompute);
  LOAD_GL_SYMBOL(handle_, Finish);
  LOAD_GL_SYMBOL(handle_, FenceSync);
  LOAD_GL_SYMBOL(handle_, DeleteSync);
  LOAD_GL_SYMBOL(handle_, ClientWaitSync);
  LOAD_GL_SYMBOL(handle_, CreateShader);
  LOAD_GL_SYMBOL(handle_, DeleteShader);
  LOAD_GL_SYMBOL(handle_, GetShaderiv);
  LOAD_GL_SYMBOL(handle_, GetShaderInfoLog);
  LOAD_GL_SYMBOL(handle_, ShaderSource);
  LOAD_GL_SYMBOL(handle_, CompileShader);
  LOAD_GL_SYMBOL(handle_, AttachShader);
  LOAD_GL_SYMBOL(handle_, CreateProgram);
  LOAD_GL_SYMBOL(handle_, DeleteProgram);
  LOAD_GL_SYMBOL(handle_, LinkProgram);
  LOAD_GL_SYMBOL(handle_, UseProgram);
}

GlesLib::~GlesLib() {

  dlclose(handle_);
}

} // namespace gl

} // namespace ib2c
