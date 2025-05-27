/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstdlib>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <mutex>

#include "ib2c.h"
#include "ib2c-utils.h"
#include "ib2c-egl-environment.h"
#include "ib2c-shader-program.h"
#include "ib2c-shaders.h"

namespace ib2c {

// Tuple of <width, height, format>
typedef std::tuple<GLsizei, GLsizei, GLenum> TextureTuple;

// Map of <Shader Type, Shader Program>
typedef std::map<ShaderType, std::shared_ptr<ShaderProgram>> ShaderPrograms;

// Tuple of <GL textures, EGL image>
typedef std::tuple<GLuint, EGLImageKHR> GraphicTuple;

// Tuple of <List of GL textures and EGL images, Surface>
typedef std::tuple<std::vector<GraphicTuple>, Surface> SurfaceTuple;

struct ImagePlane {
  uint32_t stride;
  uint32_t offset;

  ImagePlane() : stride(0), offset(0) {}

  ImagePlane(uint32_t stride, uint32_t offset)
      : stride(stride), offset(offset) {}
};

struct ImageParam {
  uint32_t                width;
  uint32_t                height;
  uint32_t                format;
  std::vector<ImagePlane> planes;

  ImageParam()
      : width(0), height(0), format(ColorFormat::kGRAY8), planes() {}

  ImageParam& operator=(const ImageParam&) = default;
};

class Engine : public IEngine {
 public:
  Engine();
  ~Engine();

  uint64_t CreateSurface(const Surface& surface, uint32_t flags) override;
  void DestroySurface(uint64_t surface_id) override;

  std::uintptr_t Compose(const Compositions& compositions,
                         bool synchronous) override;
  void Finish(std::uintptr_t fence) override;

 private:
  std::string DrawObject(std::shared_ptr<ShaderProgram>& shader,
                         const Object& object);

  std::string DispatchCompute(GLuint& stgtex, GLuint& texture, Surface& surface);
  std::string Transform(GLuint& stgtex, GLuint& texture, Surface& surface);

  bool IsSurfaceRenderable(const Surface& surface);

  GLuint GetStageTexture(const Surface& surface, const Objects& objects);

#if defined(ANDROID)
  std::vector<GraphicTuple> ImportAndroidSurface(const Surface& surface,
                                                 uint32_t flags);
#else // !ANDROID
  std::vector<GraphicTuple> ImportLinuxSurface(const Surface& surface,
                                               uint32_t flags);

  ImageParam GetImageParams(const Surface& surface, uint32_t flags);
#endif // defined(ANDROID)

  /// Global mutex protecting EGL context switching and internal variables.
  std::mutex                       mutex_;

  /// Main EGL environment.
  std::unique_ptr<EglEnvironment>  m_egl_env_;
  /// Secondary/Auxiliary EGL environment, used for waiting GLsync objects.
  std::unique_ptr<EglEnvironment>  s_egl_env_;

  /// GL staging frame buffer.
  GLuint                           stage_fbo_;
  /// Map of GL Texture and a tuple with its width, height and format.
  std::map<GLuint, TextureTuple>   stage_textures_;

  /// Map of <ShaderType, ShaderProgram>
  ShaderPrograms                   shaders_;

  /// Map of surface_id and its GL textures, EGL images and Surface.
  std::map<uint64_t, SurfaceTuple>  surfaces_;
};

} // namespace ib2c
