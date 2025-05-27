/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cmath>
#include <algorithm>

#include "ib2c-engine.h"
#include "ib2c-formats.h"

namespace ib2c {

// Prefix for the higher 32 bits of the surface ID.
static const uint64_t kSurfaceIdPrefix = 0x00001B2C00000000;

static const float kPositions[] = {
  -1.0f, -1.0f,
  -1.0f,  1.0f,
   1.0f, -1.0f,
   1.0f,  1.0f,
};

static const float kTextureCoords[] = {
  0.0f, 0.0f,
  0.0f, 1.0f,
  1.0f, 0.0f,
  1.0f, 1.0f,
};

// X and Y axis coordinates multipliers depending on the configuration mask.
const std::map<uint32_t, std::pair<float, float>> kPosFactors = {
  { 0,                                       {  1.0f, -1.0f } },
  { ConfigMask::kHFlip,                      { -1.0f, -1.0f } },
  { ConfigMask::kVFlip,                      {  1.0f,  1.0f } },
  { ConfigMask::kHFlip | ConfigMask::kVFlip, { -1.0f,  1.0f } },
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

  shader->SetFloat("xPosFactor", std::get<0>(kPosFactors.at(0)));
  shader->SetFloat("yPosFactor", std::get<1>(kPosFactors.at(0)));
  shader->SetFloat("rotationAngle", 0);

  GLuint pos    = shader->GetAttribLocation("position");
  GLuint coords = shader->GetAttribLocation("inTexCoord");

  glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0, kPositions);
  EXCEPTION_IF_GL_ERROR("Failed to define position data");

  glEnableVertexAttribArray(pos);
  EXCEPTION_IF_GL_ERROR("Failed to enable position attribute array");

  glVertexAttribPointer(coords, 2, GL_FLOAT, GL_FALSE, 0, kTextureCoords);
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

  shader->SetFloat("xPosFactor", std::get<0>(kPosFactors.at(0)));
  shader->SetFloat("yPosFactor", std::get<1>(kPosFactors.at(0)));
  shader->SetFloat("rotationAngle", 0);

  pos    = shader->GetAttribLocation("position");
  coords = shader->GetAttribLocation("inTexCoord");

  glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0, kPositions);
  EXCEPTION_IF_GL_ERROR("Failed to define position attribute array");

  glEnableVertexAttribArray(pos);
  EXCEPTION_IF_GL_ERROR("Failed to enable position attribute array");

  glVertexAttribPointer(coords, 2, GL_FLOAT, GL_FALSE, 0, kTextureCoords);
  EXCEPTION_IF_GL_ERROR("Failed to define texture coords attribute array");

  glEnableVertexAttribArray(coords);
  EXCEPTION_IF_GL_ERROR("Failed to enable texture coords attribute array");

  shader = std::make_shared<ShaderProgram>(kUnaligned8CShaderCode);
  shaders_.emplace(ShaderType::kUnaligned8, shader);

  shader->Use();
  shader->SetInt("inTex", 2);

  shader = std::make_shared<ShaderProgram>(kUnaligned16FCShaderCode);
  shaders_.emplace(ShaderType::kUnaligned16F, shader);

  shader->Use();
  shader->SetInt("inTex", 2);

  shader = std::make_shared<ShaderProgram>(kUnaligned32FCShaderCode);
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
  EGLint attribs[32] = {
    EGL_WIDTH, 0,
    EGL_HEIGHT, 0,
    EGL_LINUX_DRM_FOURCC_EXT, 0,
    EGL_DMA_BUF_PLANE0_FD_EXT, 0,
    EGL_DMA_BUF_PLANE0_PITCH_EXT, 0,
    EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
    EGL_NONE
  };

  bool aligned = IsAligned(surface);

  // Retrieve the tuple of DRM format and its modifier.
  std::tuple<uint32_t, uint64_t> internal =
      Format::ToInternal(surface.format, aligned);

  attribs[1] = surface.width;
  attribs[3] = surface.height;
  attribs[5] = std::get<0>(internal);
  attribs[7] = surface.fd;
  attribs[9] = surface.stride0;
  attribs[11] = surface.offset0;

  // Adjust width, height and stride values for unaligned RGB(A) images.
  if ((flags & SurfaceFlags::kOutput) && Format::IsRgb(surface.format) &&
      (!aligned || (Format::IsFloat(surface.format) &&
          Format::NumChannels(surface.format) == 3) ||
      Format::IsSigned(surface.format))) {
    auto dims = AlignedDimensions(surface);

    attribs[1] = std::get<0>(dims);
    attribs[3] = std::get<1>(dims);

    // TODO Channels is 4 because staged texture is GL_RGBA8.
    attribs[9] = std::get<0>(dims) * 4 * Format::BytesPerChannel(surface.format);
  }

  if (surface.nplanes >= 2) {
    attribs[12] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
    attribs[13] = surface.stride1;
    attribs[14] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
    attribs[15] = surface.offset1;
    attribs[16] = EGL_NONE;

    if (std::get<1>(internal) != 0) {
      attribs[16] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
      attribs[17] = std::get<1>(internal) & 0xFFFFFFFF;
      attribs[18] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
      attribs[19] = std::get<1>(internal) >> 32;
      attribs[20] = EGL_NONE;
    }
  }

  if (surface.nplanes == 3) {
    attribs[16] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
    attribs[17] = surface.stride2;
    attribs[18] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
    attribs[19] = surface.offset2;
    attribs[20] = EGL_NONE;
  }

  EGLImageKHR image =
      eglCreateImageKHR (m_egl_env_->Display(), EGL_NO_CONTEXT,
                         EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
#endif // !ANDROID

  if (image == EGL_NO_IMAGE) {
    throw Exception("Failed to create EGL image, error: ", std::hex,
                    eglGetError(), "!");
  }

  GLuint texture;

  // Create GL texture to the image will be binded.
  glGenTextures (1, &texture);
  EXCEPTION_IF_GL_ERROR("Failed to generate GL texture!");

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
    EGLImageKHR& oimage = std::get<1>(otuple);
    Surface& osurface = std::get<2>(otuple);

    glActiveTexture(GL_TEXTURE0);
    EXCEPTION_IF_GL_ERROR("Failed to set active texture unit 0");

    // Bind the output surface texture to EXTERNAL_OES for current active texture.
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, otexture);
    EXCEPTION_IF_GL_ERROR("Failed to bind output texture ", otexture);

    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
                                 reinterpret_cast<GLeglImageOES>(oimage));
    EXCEPTION_IF_GL_ERROR("Failed to associate output image ", oimage,
        " with external texture");

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
  EGLImageKHR& inimage = std::get<EGLImageKHR>(intuple);
  Surface& insurface = std::get<Surface>(intuple);

  // Bind the input surface texture to EXTERNAL_OES for current active texture.
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, intexture);
  RETURN_IF_GL_ERROR("Failed to bind input texture ", intexture);

  glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
                               reinterpret_cast<GLeglImageOES>(inimage));
  RETURN_IF_GL_ERROR("Failed to associate input image ", inimage);

  if (shader->HasVariable("globalAlpha"))
    shader->SetFloat("globalAlpha", (object.alpha / 255.0));

  auto mask = object.mask & (ConfigMask::kHFlip | ConfigMask::kVFlip);
  shader->SetFloat("xPosFactor", std::get<0>(kPosFactors.at(mask)));
  shader->SetFloat("yPosFactor", std::get<1>(kPosFactors.at(mask)));

  // Rotation angle in radians.
  shader->SetFloat("rotationAngle", (object.rotation * M_PI / 180));

  const Region& source = object.source;
  float coords[8] = { 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0 };

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
  glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0, coords);
  RETURN_IF_GL_ERROR("Failed to define vertex array");

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

  glActiveTexture(GL_TEXTURE2);
  RETURN_IF_GL_ERROR("Failed to set active texture unit 2");

  auto dims = AlignedDimensions(surface);

  shader->Use();

#if defined(ANDROID)
  shader->SetInt("targetWidth", surface.buffer->width);
  shader->SetInt("targetHeight", surface.buffer->height);
#else   // ANDROID
  shader->SetInt("targetWidth", surface.width);
  shader->SetInt("targetHeight", surface.height);
#endif  // !ANDROID

  shader->SetInt("alignedWidth", std::get<0>(dims));
  shader->SetInt("numChannels", Format::NumChannels(surface.format));

  glBindTexture(GL_TEXTURE_2D, stgtex);
  RETURN_IF_GL_ERROR("Failed to bind staging texture ", stgtex);

  GLenum format = Format::ToGL(surface.format);

  glBindImageTexture(1, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, format);
  RETURN_IF_GL_ERROR("Failed to bind output image texture ", texture);

  GLuint xgroups = std::get<0>(dims) / (Format::NumChannels(surface.format) * 32);
  GLuint ygroups = std::get<1>(dims);

  glDispatchCompute(xgroups, ygroups, 1);
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

  float coords[8] = { 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0 };
  glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0, coords);
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
