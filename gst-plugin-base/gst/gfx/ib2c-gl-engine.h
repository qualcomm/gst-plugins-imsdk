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
#include "ib2c-gl-environment.h"
#include "ib2c-gl-shader-program.h"
#include "ib2c-shaders.h"

namespace ib2c {

namespace gl {

// Tuple of <width, height, format>
typedef std::tuple<GLsizei, GLsizei, GLenum> TextureTuple;

// Map of <Shader Type, Shader Program>
typedef std::map<ShaderType, std::shared_ptr<ShaderProgram>> ShaderPrograms;

// Tuple of <width, height, format>
typedef std::tuple<uint32_t, uint32_t, uint32_t> ImageParam;

// Tuple of <GL texture, EGL image, Image Parameters>
typedef std::tuple<GLuint, EGLImageKHR, ImageParam> GraphicTuple;

// Tuple of <<List of GL textures, EGL images, Image Parameters>, Surface, Flags>
typedef std::tuple<std::vector<GraphicTuple>, Surface, uint32_t> SurfaceTuple;

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
  std::string BindContext(EGLSurface draw, EGLSurface read, EGLContext context);
  std::string UnbindContext(EGLContext context);

  std::string RenderYuvTexture(std::vector<GraphicTuple>& graphics, bool clean,
                               uint32_t color, uint32_t colorspace,
                               Objects& objects);
  std::string RenderRgbTexture(std::vector<GraphicTuple>& graphics, bool clean,
                               uint32_t color, bool inverted, bool swapped,
                               Normalization& normalize, Objects& objects);
  std::string RenderStageTexture(GLuint texture, uint32_t color, bool inverted,
                                 bool swapped, Normalization& normalize,
                                 Objects& objects);

  std::string DrawObject(std::shared_ptr<ShaderProgram>& shader,
                         const Object& object);

  std::string DispatchCompute(GLuint stgtex, Surface& surface,
                              std::vector<GraphicTuple>& graphics);
  std::string ColorTransmute(GLuint stgtex, Surface& surface,
                             std::vector<GraphicTuple>& graphics);

  bool IsSurfaceRenderable(const Surface& surface);

  GLuint GetStageTexture(const Surface& surface, const Objects& objects);

  std::vector<GraphicTuple> ImportSurface(const Surface& surface, uint32_t flags);
  std::vector<Surface> GetImageSurfaces(const Surface& surface, uint32_t flags);

  /// Global mutex protecting EGL context switching and internal variables.
  std::mutex                       mutex_;

  /// EGL and GLES environment.
  std::shared_ptr<Environment>     env_;

  /// GL frame buffer for rendering.
  GLuint                           fbo_;

  /// Map of GL Texture and a tuple with its width, height and format.
  std::map<GLuint, TextureTuple>   stage_textures_;

  /// Map of <ShaderType, ShaderProgram>
  ShaderPrograms                   shaders_;

  /// Map of surface_id and its GL textures, EGL images and Surface.
  std::map<uint64_t, SurfaceTuple> surfaces_;
};

} // namespace gl

} // namespace ib2c
