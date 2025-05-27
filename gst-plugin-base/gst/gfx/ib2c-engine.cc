/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cmath>
#include <algorithm>
#include <array>

#include "ib2c-engine.h"
#include "ib2c-formats.h"

namespace ib2c {

// Prefix for the higher 32 bits of the surface ID.
static const uint64_t kSurfaceIdPrefix = 0x00001B2C00000000;

/** X and Y axis vertex coordinates depending on the flip flags in the mask.
 *
 *            Y|
 *   -1,1      |      1,1
 *     +-------+-------+
 *     |       |       |
 *     |       |       |
 * ----+-------+-------+----
 *     |       |0,0    |   X
 *     |       |       |
 *     +-------+-------+
 *   -1,-1     |      1,-1
 *             |
 */
static const std::map<uint32_t, std::array<float, 8>> kVertices = {
  { 0,
    {
      -1.0f,  1.0f,
      -1.0f, -1.0f,
       1.0f,  1.0f,
       1.0f, -1.0f,
    }
  },
  { ConfigMask::kHFlip,
    {
       1.0f,  1.0f,
       1.0f, -1.0f,
      -1.0f,  1.0f,
      -1.0f, -1.0f,
    }
  },
  { ConfigMask::kVFlip,
    {
      -1.0f, -1.0f,
      -1.0f,  1.0f,
       1.0f, -1.0f,
       1.0f,  1.0f,
    }
  },
  { ConfigMask::kHFlip | ConfigMask::kVFlip,
    {
       1.0f, -1.0f,
       1.0f,  1.0f,
      -1.0f, -1.0f,
      -1.0f,  1.0f,
    }
  },
};

/** Default X and Y axis vertex coordinates for textures.
 *
 * 0,1           1,1
 *  +-------------+
 *  |             |
 *  |             |
 *  |             |
 *  +-------------+
 * 0,0           1,0
 */
static const std::array<float, 8> kTextureCoords = {
  0.0f, 1.0f,
  0.0f, 0.0f,
  1.0f, 1.0f,
  1.0f, 0.0f,
};

// Default/Identity matrix layout.
static const std::array<float, 16> kMatrix = {
  1.0, 0.0, 0.0, 0.0,
  0.0, 1.0, 0.0, 0.0,
  0.0, 0.0, 1.0, 0.0,
  0.0, 0.0, 0.0, 1.0,
};

Engine::Engine() {

  // Initialize main instance of the EGL environment.
  std::string error = EglEnvironment::NewEglEnvironment(m_egl_env_);
  if (!error.empty()) throw std::runtime_error(error);

  // Initialize secondary/auxiliary instance of the EGL environment.
  error = EglEnvironment::NewEglEnvironment(s_egl_env_, m_egl_env_->Context());
  if (!error.empty()) throw std::runtime_error(error);

  error = m_egl_env_->BindContext(EGL_NO_SURFACE, EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

  // Generate staging frame buffer.
  glGenFramebuffers(1, &stage_fbo_);
  EXCEPTION_IF_GL_ERROR("Failed to generate stage frame buffer");

  auto shader =
      std::make_shared<ShaderProgram>(kVertexShaderCode, kRgbFragmentShaderCode);
  shaders_.emplace(ShaderType::kRGB, shader);

  shader->Use();
  shader->SetInt("extTex", 0);
  shader->SetVec4("rgbaScale", 0, 0, 0, 0);
  shader->SetVec4("rgbaOffset", 0, 0, 0, 0);
  shader->SetBool("rgbaInverted", false);
  shader->SetBool("rbSwapped", false);
  shader->SetFloat("globalAlpha", 1.0);

  shader->SetFloat("rotationAngle", 0.0);

  GLuint pos    = shader->GetAttribLocation("vPosition");
  GLuint coords = shader->GetAttribLocation("inTexCoord");

  glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0, kVertices.at(0).data());
  EXCEPTION_IF_GL_ERROR("Failed to define position data");

  glEnableVertexAttribArray(pos);
  EXCEPTION_IF_GL_ERROR("Failed to enable position attribute array");

  glVertexAttribPointer(coords, 2, GL_FLOAT, GL_FALSE, 0, kTextureCoords.data());
  EXCEPTION_IF_GL_ERROR("Failed to define texture coords attribute array");

  glEnableVertexAttribArray(coords);
  EXCEPTION_IF_GL_ERROR("Failed to enable texture coords attribute array");

  shader =
      std::make_shared<ShaderProgram>(kVertexShaderCode, kYuvFragmentShaderCode);
  shaders_.emplace(ShaderType::kYUV, shader);

  shader->Use();
  shader->SetInt("extTex", 0);
  shader->SetInt("stageTex", 1);
  shader->SetInt("colorSpace", ColorMode::kBT601);
  shader->SetBool("stageInput", false);

  shader->SetFloat("rotationAngle", 0.0);

  pos    = shader->GetAttribLocation("vPosition");
  coords = shader->GetAttribLocation("inTexCoord");

  glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0, kVertices.at(0).data());
  EXCEPTION_IF_GL_ERROR("Failed to define position attribute array");

  glEnableVertexAttribArray(pos);
  EXCEPTION_IF_GL_ERROR("Failed to enable position attribute array");

  glVertexAttribPointer(coords, 2, GL_FLOAT, GL_FALSE, 0, kTextureCoords.data());
  EXCEPTION_IF_GL_ERROR("Failed to define texture coords attribute array");

  glEnableVertexAttribArray(coords);
  EXCEPTION_IF_GL_ERROR("Failed to enable texture coords attribute array");

  // Construct shader code for 8-bit unaligned RGB(A) output textures.
  std::string code = kComputeHeader + kComputeOutputRgba8 + kComputeMainUnaligned;

  shader = std::make_shared<ShaderProgram>(code);
  shaders_.emplace(ShaderType::kUnaligned8, shader);

  shader->Use();
  shader->SetInt("inTex", 2);

  // Construct shader code for 16-bit float unaligned RGB(A) output textures.
  code = kComputeHeader + kComputeOutputRgba16F + kComputeMainUnaligned;

  shader = std::make_shared<ShaderProgram>(code);
  shaders_.emplace(ShaderType::kUnaligned16F, shader);

  shader->Use();
  shader->SetInt("inTex", 2);

  // Construct shader code for 32-bit float unaligned RGB(A) output textures.
  code = kComputeHeader + kComputeOutputRgba32F + kComputeMainUnaligned;

  shader = std::make_shared<ShaderProgram>(code);
  shaders_.emplace(ShaderType::kUnaligned32F, shader);

  shader->Use();
  shader->SetInt("inTex", 2);

  error = m_egl_env_->UnbindContext();
  if (!error.empty()) throw std::runtime_error(error);
}

Engine::~Engine() {

  std::lock_guard<std::mutex> lk(mutex_);

  m_egl_env_->BindContext(EGL_NO_SURFACE, EGL_NO_SURFACE);

  for (auto& pair : stage_textures_) {
    GLuint texture = pair.first;
    glDeleteTextures(1, &texture);
  }

  for (auto& pair : graphics_) {
    auto image = std::get<EGLImageKHR>(pair.second);
    eglDestroyImageKHR(m_egl_env_->Display(), image);

    auto texture = std::get<GLuint>(pair.second);
    glDeleteTextures(1, &texture);
  }

  glDeleteFramebuffers(1, &stage_fbo_);

  m_egl_env_->UnbindContext();
}

uint64_t Engine::CreateSurface(const Surface& surface, uint32_t flags) {

  std::lock_guard<std::mutex> lk(mutex_);

#if defined(ANDROID)
  int32_t fd = surface.buffer->handle->data[0];
#else   // ANDROID
  int32_t fd = surface.fd;
#endif  // !ANDROID

  if (graphics_.count(kSurfaceIdPrefix | fd) != 0) {
    return (kSurfaceIdPrefix | fd);
  }

  std::string error = m_egl_env_->BindContext(EGL_NO_SURFACE, EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

#if defined(ANDROID)
  EGLImageKHR image =
      eglCreateImageKHR(m_egl_env_->Display(), EGL_NO_CONTEXT,
                        EGL_NATIVE_BUFFER_ANDROID, surface.buffer, NULL);
#else // ANDROID
  EGLint attribs[64] = { EGL_NONE };
  uint32_t index = 0;

  bool aligned = IsAligned(surface);

  // Retrieve the tuple of DRM format and its modifier.
  std::tuple<uint32_t, uint64_t> internal =
      Format::ToInternal(surface.format, aligned);

  attribs[index++] = EGL_WIDTH;
  attribs[index++] = surface.width;
  attribs[index++] = EGL_HEIGHT;
  attribs[index++] = surface.height;
  attribs[index++] = EGL_LINUX_DRM_FOURCC_EXT;
  attribs[index++] = std::get<0>(internal);
  attribs[index++] = EGL_DMA_BUF_PLANE0_FD_EXT;
  attribs[index++] = surface.fd;
  attribs[index++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
  attribs[index++] = surface.stride0;
  attribs[index++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
  attribs[index++] = surface.offset0;
  attribs[index++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
  attribs[index++] = std::get<1>(internal) & 0xFFFFFFFF;
  attribs[index++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
  attribs[index++] = std::get<1>(internal) >> 32;

  // Adjust width, height and stride values for unaligned RGB(A) images.
  if ((flags & SurfaceFlags::kOutput) && Format::IsRgb(surface.format) &&
      (!aligned || (Format::IsFloat(surface.format) &&
          Format::NumChannels(surface.format) == 3) ||
      Format::IsSigned(surface.format))) {
    auto dims = AlignedDimensions(surface);

    attribs[1] = std::get<0>(dims);
    attribs[3] = std::get<1>(dims);

    // Channels is 4 because output texture for compute is (RGBA).
    attribs[9] = std::get<0>(dims) * 4 * Format::BytesPerChannel(surface.format);
  }

  if (surface.nplanes >= 2) {
    attribs[index++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
    attribs[index++] = surface.stride1;
    attribs[index++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
    attribs[index++] = surface.offset1;
    attribs[index++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
    attribs[index++] = std::get<1>(internal) & 0xFFFFFFFF;
    attribs[index++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
    attribs[index++] = std::get<1>(internal) >> 32;
  }

  if (surface.nplanes == 3) {
    attribs[index++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
    attribs[index++] = surface.stride2;
    attribs[index++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
    attribs[index++] = surface.offset2;
    attribs[index++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
    attribs[index++] = std::get<1>(internal) & 0xFFFFFFFF;
    attribs[index++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
    attribs[index++] = std::get<1>(internal) >> 32;
  }

  attribs[index] = EGL_NONE;

  EGLImageKHR image =
      eglCreateImageKHR (m_egl_env_->Display(), EGL_NO_CONTEXT,
                         EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
#endif // !ANDROID

  if (image == EGL_NO_IMAGE) {
    throw Exception("Failed to create EGL image, error: ", std::hex,
                    eglGetError(), "!");
  }

  glActiveTexture(GL_TEXTURE0);
  EXCEPTION_IF_GL_ERROR("Failed to set active texture unit 0");

  GLuint texture;

  // Create GL texture to the image will be binded.
  glGenTextures (1, &texture);
  EXCEPTION_IF_GL_ERROR("Failed to generate GL texture!");

  // Bind the surface texture to EXTERNAL_OES.
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
  EXCEPTION_IF_GL_ERROR("Failed to bind output texture ", texture);

  glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
                               reinterpret_cast<GLeglImageOES>(image));
  EXCEPTION_IF_GL_ERROR("Failed to associate image ", image,
      " with external texture ", texture);

  uint64_t surface_id = kSurfaceIdPrefix | fd;
  graphics_.emplace(
      surface_id, std::move(std::make_tuple(texture, image, surface)));

  error = m_egl_env_->UnbindContext();
  if (!error.empty()) throw std::runtime_error(error);

  return surface_id;
}

void Engine::DestroySurface(uint64_t id) {

  std::lock_guard<std::mutex> lk(mutex_);

  auto tuple = std::move(graphics_.at(id));
  graphics_.erase(id);

  std::string error = m_egl_env_->BindContext(EGL_NO_SURFACE, EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

  EGLImageKHR image = std::get<1>(tuple);
  if (!eglDestroyImageKHR(m_egl_env_->Display(), image)) {
    throw Exception("Failed to destroy EGL image, error: ", std::hex,
                    eglGetError(), "!");
  }

  GLuint texture= std::get<0>(tuple);

  glDeleteTextures(1, &texture);
  EXCEPTION_IF_GL_ERROR("Failed to delete GL texture!");

  error = m_egl_env_->UnbindContext();
  if (!error.empty()) throw std::runtime_error(error);
}

std::uintptr_t Engine::Compose(const Compositions& compositions,
                               bool synchronous) {

  std::lock_guard<std::mutex> lk(mutex_);

  std::string error = m_egl_env_->BindContext(EGL_NO_SURFACE, EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

  for (auto const& composition : compositions) {
    auto surface_id = std::get<uint64_t>(composition);
    auto color = std::get<uint32_t>(composition);
    auto clean = std::get<bool>(composition);
    auto normalization = std::get<Normalization>(composition);
    auto objects = std::get<Objects>(composition);

    GraphicTuple& otuple = graphics_.at(surface_id);

    GLuint& otexture = std::get<0>(otuple);
    Surface& osurface = std::get<2>(otuple);

    glActiveTexture(GL_TEXTURE0);
    EXCEPTION_IF_GL_ERROR("Failed to set active texture unit 0");

    // Bind the output surface texture to EXTERNAL_OES for current active texture.
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, otexture);
    EXCEPTION_IF_GL_ERROR("Failed to bind output texture ", otexture);

    // Get the staging texture if required.
    GLuint stgtex = GetStageTexture(osurface, objects);

    glBindFramebuffer(GL_FRAMEBUFFER, stage_fbo_);
    EXCEPTION_IF_GL_ERROR("Failed to bind frame buffer");

    // Attach output/staging texture to the rendering frame buffer.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           ((stgtex == 0) ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D),
                           ((stgtex == 0) ? otexture : stgtex), 0);
    EXCEPTION_IF_GL_ERROR("Failed to attach output texture ",
        (stgtex == 0) ? otexture : stgtex, " to stage frame buffer");

    if (clean || (stgtex != 0)) {
      // Convert RGB to YUV channel values of output is directly to YUV.
      if ((stgtex == 0) && Format::IsYuv(osurface.format))
        color = RgbToYuv(color, Format::ColorSpace(osurface.format));

      // Set/Clear the background of the texture attached to the frame buffer.
      glClearColor(EXTRACT_RED_COLOR(color), EXTRACT_GREEN_COLOR(color),
                   EXTRACT_BLUE_COLOR(color), EXTRACT_ALPHA_COLOR(color));
      glClear(GL_COLOR_BUFFER_BIT);
      EXCEPTION_IF_GL_ERROR("Failed to clear buffer color bit");
    }

    // Insert internal blit object for the inplace surface at the begining.
    if (!clean && (stgtex != 0)) {
      objects.insert(objects.begin(), Object());

      objects[0].id = surface_id;

#if defined(ANDROID)
      objects[0].source.w = objects[0].destination.w = osurface.buffer->width;
      objects[0].source.h = objects[0].destination.h = osurface.buffer->height;
#else   // ANDROID
      objects[0].source.w = objects[0].destination.w = osurface.width;
      objects[0].source.h = objects[0].destination.h = osurface.height;
#endif  // !ANDROID
    }

    std::shared_ptr<ShaderProgram> shader;

    // Get the main shader depending on the configuration.
    if ((stgtex != 0) || Format::IsRgb(osurface.format)) {
      glEnable(GL_BLEND);
      EXCEPTION_IF_GL_ERROR("Failed to enable blend capability");

      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      EXCEPTION_IF_GL_ERROR("Failed to set blend function");

      Normalization normalize = std::get<Normalization>(composition);

      if (normalize.size() != 4) { normalize.resize(4); }

      std::transform(normalize.begin(), normalize.end(), normalize.begin(),
                     [&osurface](Normalize n) {
          // Adjust data range to match to fragment sharer data representation.
          n.offset /= 255.0;
          // Adjust date range for signed RGB format
          n.scale *= Format::IsSigned(osurface.format) ? 2.0 : 1.0;
          return n;
      });

      shader = shaders_.at(ShaderType::kRGB);
      shader->Use();

      shader->SetVec4("rgbaScale", normalize[0].scale, normalize[1].scale,
                      normalize[2].scale, normalize[3].scale);
      shader->SetVec4("rgbaOffset", normalize[0].offset, normalize[1].offset,
                      normalize[2].offset, normalize[3].offset);

      shader->SetBool("rgbaInverted", Format::IsInverted(osurface.format));
      shader->SetBool("rbSwapped", Format::IsSwapped(osurface.format));
    } else if (Format::IsYuv(osurface.format)) {
      // Blending does not work for YUV output formats.
      glDisable(GL_BLEND);
      EXCEPTION_IF_GL_ERROR("Failed to disable blend capability");

      shader = shaders_.at(ShaderType::kYUV);
      shader->Use();

      shader->SetInt("colorSpace", Format::ColorSpace(osurface.format));
      shader->SetBool("stageInput", false);
    }

    // Iterate over the objects and dispatch draw commands.
    for (auto const& object : objects) {
      error = DrawObject(shader, object);
      if (!error.empty()) throw std::runtime_error(error);
    }

    // Make sure that blending is disabled for next stages.
    glDisable(GL_BLEND);
    EXCEPTION_IF_GL_ERROR("Failed to disable blend capability");

    // In case output is unaligned RGB, apply compute shader.
    if ((stgtex != 0) && Format::IsRgb(osurface.format)) {
      error = DispatchCompute(stgtex, otexture, osurface);
      if (!error.empty()) throw std::runtime_error(error);
    }

    // Transform the intermediary BGRA texture to YUV.
    if ((stgtex != 0) && Format::IsYuv(osurface.format)) {
      error = Transform(stgtex, otexture, osurface);
      if (!error.empty()) throw std::runtime_error(error);
    }
  }

  uintptr_t fence = reinterpret_cast<std::uintptr_t>(nullptr);

  if (synchronous) {
    glFinish();
    EXCEPTION_IF_GL_ERROR("Failed to execute submitted compositions");

    fence = reinterpret_cast<std::uintptr_t>(nullptr);
  } else {
    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    EXCEPTION_IF_GL_ERROR("Failed to create fence object");

    fence = reinterpret_cast<std::uintptr_t>(sync);
  }

  error = m_egl_env_->UnbindContext();
  if (!error.empty()) throw std::runtime_error(error);

  return fence;
}

void Engine::Finish(std::uintptr_t fence) {

  if (fence == reinterpret_cast<std::uintptr_t>(nullptr))
    return;

  GLsync sync = reinterpret_cast<GLsync>(fence);

  std::string error = s_egl_env_->BindContext(EGL_NO_SURFACE, EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

  auto status = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT,
                                 GL_TIMEOUT_IGNORED);

  if (status == GL_WAIT_FAILED) {
    throw Exception("Failed to sync fence object ", fence, "!");
  }

  glDeleteSync(sync);
  EXCEPTION_IF_GL_ERROR("Failed to delete fence object");

  error = s_egl_env_->UnbindContext();
  if (!error.empty()) throw std::runtime_error(error);
}

std::string Engine::DrawObject(std::shared_ptr<ShaderProgram>& shader,
                               const Object& object) {

  const Region& destination = object.destination;

  glViewport(destination.x, destination.y, destination.w, destination.h);
  RETURN_IF_GL_ERROR("Failed to set destination viewport");

  GraphicTuple& intuple = graphics_.at(object.id);
  GLuint& intexture = std::get<GLuint>(intuple);
  Surface& insurface = std::get<Surface>(intuple);

  // Bind the input surface texture to EXTERNAL_OES for current active texture.
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, intexture);
  RETURN_IF_GL_ERROR("Failed to bind input texture ", intexture);

  if (shader->HasVariable("globalAlpha"))
    shader->SetFloat("globalAlpha", (object.alpha / 255.0));

  // Rotation angle in radians.
  shader->SetFloat("rotationAngle", object.rotation * M_PI / 180);

  auto mask = object.mask & (ConfigMask::kHFlip | ConfigMask::kVFlip);
  GLuint pos = shader->GetAttribLocation("vPosition");

  glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0, kVertices.at(mask).data());
  RETURN_IF_GL_ERROR("Failed to define main vertex array");

  const Region& source = object.source;
  std::array<float, 8> coords = kTextureCoords;

#if defined(ANDROID)
  uint32_t width = insurface.buffer->width;
  uint32_t height = insurface.buffer->height;
#else   // ANDROID
  uint32_t width = insurface.width;
  uint32_t height = insurface.height;
#endif  // !ANDROID

  if ((source.w != 0) && (source.h != 0)) {
    coords[0] = static_cast<float>(source.x) / width;
    coords[1] = static_cast<float>(source.y + source.h) / height;
    coords[2] = static_cast<float>(source.x) / width;
    coords[3] = static_cast<float>(source.y) / height;
    coords[4] = static_cast<float>(source.x + source.w) / width;
    coords[5] = static_cast<float>(source.y + source.h) / height;
    coords[6] = static_cast<float>(source.x + source.w) / width;
    coords[7] = static_cast<float>(source.y) / height;
  }

  // Load the vertex position.
  GLuint texcoord = shader->GetAttribLocation("inTexCoord");
  glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0, coords.data());
  RETURN_IF_GL_ERROR("Failed to define texture vertex array");

  glEnableVertexAttribArray(texcoord);
  RETURN_IF_GL_ERROR("Failed to enable vertex array");

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  RETURN_IF_GL_ERROR("Failed to render array data");

  return std::string();
}

std::string Engine::DispatchCompute(GLuint& stgtex, GLuint& texture,
                                    Surface& surface) {

  std::shared_ptr<ShaderProgram> shader;

  if ((surface.format & (0b11 << 11)) == ColorMode::kFloat32) {
    shader = shaders_.at(ShaderType::kUnaligned32F);
  } else if ((surface.format & (0b11 << 11)) == ColorMode::kFloat16) {
    shader = shaders_.at(ShaderType::kUnaligned16F);
  } else {
    shader = shaders_.at(ShaderType::kUnaligned8);
  }

#if defined(ANDROID)
  uint32_t width = surface.buffer->width;
  uint32_t height = surface.buffer->height;
#else   // ANDROID
  uint32_t width = surface.width;
  uint32_t height = surface.height;
#endif  // !ANDROID

  auto dims = AlignedDimensions(surface);

  shader->Use();

  shader->SetInt("targetWidth", width);
  shader->SetInt("alignedWidth", std::get<0>(dims));
  shader->SetInt("numPixels", (width * height));
  shader->SetInt("numChannels", Format::NumChannels(surface.format));

  glActiveTexture(GL_TEXTURE2);
  RETURN_IF_GL_ERROR("Failed to set active texture unit 2");

  glBindTexture(GL_TEXTURE_2D, stgtex);
  RETURN_IF_GL_ERROR("Failed to bind staging texture ", stgtex);

  GLenum format = Format::ToGL(surface.format);

  glBindImageTexture(1, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, format);
  RETURN_IF_GL_ERROR("Failed to bind output image texture ", texture);

  // Align to the divisor for the number of X groups explained below.
  uint32_t n_pixels = (((width * height) + ((32 * 4) - 1)) & ~((32 * 4) - 1));

  // 32 because of the local size and 4 pixels are processed at a time.
  GLuint xgroups = n_pixels / (32 * 4);

  glDispatchCompute(xgroups, 1, 1);
  RETURN_IF_GL_ERROR("Failed to dispatch compute");

  return std::string();
}

std::string Engine::Transform(GLuint& stgtex, GLuint& texture,
                              Surface& surface) {

  std::shared_ptr<ShaderProgram> shader = shaders_.at(ShaderType::kYUV);

  glActiveTexture(GL_TEXTURE1);
  RETURN_IF_GL_ERROR("Failed to set active texture unit 1");

  glBindTexture(GL_TEXTURE_2D, stgtex);
  RETURN_IF_GL_ERROR("Failed to bind staging texture");

  glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
  RETURN_IF_GL_ERROR("Failed to bind output texture ", texture);

#if defined(ANDROID)
  glViewport(0, 0, surface.buffer->width, surface.buffer->height);
#else   // ANDROID
  glViewport(0, 0, surface.width, surface.height);
#endif  // !ANDROID

  RETURN_IF_GL_ERROR("Failed to set destination viewport");

  glBindFramebuffer(GL_FRAMEBUFFER, stage_fbo_);
  RETURN_IF_GL_ERROR("Failed to bind frame buffer");

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_EXTERNAL_OES, texture, 0);
  RETURN_IF_GL_ERROR("Failed to attach output texture ", texture,
      " to stage frame buffer");

  shader->Use();
  shader->SetInt("colorSpace", Format::ColorSpace(surface.format));
  shader->SetBool("stageInput", true);

  // Load the vertex position.
  GLuint texcoord = shader->GetAttribLocation("inTexCoord");
  std::array<float, 8> coords = kTextureCoords;

  glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0, coords.data());
  RETURN_IF_GL_ERROR("Failed to define vertex array");

  glEnableVertexAttribArray(texcoord);
  RETURN_IF_GL_ERROR("Failed to enable vertex array");

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  RETURN_IF_GL_ERROR("Failed to render array data");

  return std::string();
}

GLuint Engine::GetStageTexture(const Surface& surface, const Objects& objects) {

  bool aligned = IsAligned(surface);

  // TODO Remove IsFloat when 3 channel RGB float formats are supported.
  if (Format::IsRgb(surface.format) && aligned &&
      !(Format::IsFloat(surface.format) &&
          Format::NumChannels(surface.format) == 3) &&
      !Format::IsSigned(surface.format))
    return 0;

  bool blending = false;

  // Iterate over the objects and determine if alpha blending is required.
  for (auto const& object : objects) {
    // Enable blending if atleats one object has alpha mask.
    if (object.alpha != 0xFF) {
      blending = true;
      break;
    }

    GraphicTuple& tuple = graphics_.at(object.id);
    uint32_t format = std::get<Surface>(tuple).format;

    // Enable blending if atleast one object is RGB with alpha channel.
    if (Format::IsRgb(format) && Format::NumChannels(format) == 4) {
      blending = true;
      break;
    }
  }

  if ((Format::IsYuv(surface.format) && !blending))
    return 0;

#if defined(ANDROID)
  GLsizei width = surface.buffer->width;
  GLsizei height = surface.buffer->height;
#else   // ANDROID
  GLsizei width = surface.width;
  GLsizei height = surface.height;
#endif  // !ANDROID

  GLenum format = Format::ToGL(surface.format);

  auto it = std::find_if(stage_textures_.begin(), stage_textures_.end(),
    [width, height, format](const std::pair<GLuint, TextureTuple>& pair) -> bool {
        const auto& tuple = pair.second;

        return (std::get<2>(tuple) == format) &&
            (std::get<0>(tuple) == width) && (std::get<1>(tuple) == height);
    }
  );

  if (it != stage_textures_.end())
    return it->first;

  GLuint texture;

  glGenTextures(1, &texture);
  EXCEPTION_IF_GL_ERROR ("Failed to generate staging texture");

  glBindTexture(GL_TEXTURE_2D, texture);
  EXCEPTION_IF_GL_ERROR ("Failed to bind staging texture");

  glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);
  EXCEPTION_IF_GL_ERROR ("Failed to set staging texture storage");

  stage_textures_.emplace(
      texture, std::move(std::make_tuple(width, height, format)));

  return texture;
}

IEngine* NewGlEngine() {

  return new Engine();
}

} // namespace ib2c
