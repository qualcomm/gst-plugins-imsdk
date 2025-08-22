/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <cmath>
#include <algorithm>
#include <array>

#include "ib2c-gl-engine.h"
#include "ib2c-formats.h"

namespace ib2c {

namespace gl {

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
  std::string error = Environment::NewEnvironment(env_);
  if (!error.empty()) throw std::runtime_error(error);

  error = env_->BindContext(ContextType::kPrimary, EGL_NO_SURFACE,
                            EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

  // Generate frame buffer.
  env_->Gles()->GenFramebuffers(1, &fbo_);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to generate stage frame buffer");

  // Construct fragment shader code for interleaved RGB(A) formats.
  std::string fragment =
      kRgbFragmentHeader + kRgbFragmentInterleavedOutput + kRgbFragmentMain;

  auto shader = std::make_shared<ShaderProgram>(env_, kVertexShader, fragment);
  shaders_.emplace(ShaderType::kRGB, shader);

  env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("vPosition"));
  EXCEPTION_IF_GL_ERROR(env_, "Failed to enable position attribute array");

  env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("inTexCoord"));
  EXCEPTION_IF_GL_ERROR(env_, "Failed to enable texture coords attribute array");

  fragment = kRgbFragmentHeader + kRgbFragmentPlanarOutput + kRgbFragmentMain;

  shader = std::make_shared<ShaderProgram>(env_, kVertexShader, fragment);
  shaders_.emplace(ShaderType::kPlanarRGB, shader);

  env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("vPosition"));
  EXCEPTION_IF_GL_ERROR(env_, "Failed to enable position attribute array");

  env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("inTexCoord"));
  EXCEPTION_IF_GL_ERROR(env_, "Failed to enable texture coords attribute array");

  if (env_->QueryExtension("GL_EXT_YUV_target")) {
    // Create shader for direct rendering to YUV only if it is supported.
    shader = std::make_shared<ShaderProgram>(env_, kVertexShader,
                                             kYuvFragmentShader);
    shaders_.emplace(ShaderType::kYUV, shader);

    env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("vPosition"));
    EXCEPTION_IF_GL_ERROR(env_, "Failed to enable possition attribute array");

    env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("inTexCoord"));
    EXCEPTION_IF_GL_ERROR(env_, "Failed to enable texture coords attribute array");
  }

  shader = std::make_shared<ShaderProgram>(env_, kVertexShader,
                                           kLumaFragmentShader);
  shaders_.emplace(ShaderType::kLuma, shader);

  env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("vPosition"));
  EXCEPTION_IF_GL_ERROR(env_, "Failed to enable position attribute array");

  env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("inTexCoord"));
  EXCEPTION_IF_GL_ERROR(env_, "Failed to enable texture coords attribute array");

  shader = std::make_shared<ShaderProgram>(env_, kVertexShader,
                                           kChromaFragmentShader);
  shaders_.emplace(ShaderType::kChroma, shader);

  env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("vPosition"));
  EXCEPTION_IF_GL_ERROR(env_, "Failed to enable position attribute array");

  env_->Gles()->EnableVertexAttribArray(shader->GetAttribLocation("inTexCoord"));
  EXCEPTION_IF_GL_ERROR(env_, "Failed to enable texture coords attribute array");

  // Construct shader code for 8-bit unaligned RGB(A) output textures.
  std::string compute = kComputeHeader + kComputeOutputRGBA8 + kComputeMain;

  shader = std::make_shared<ShaderProgram>(env_, compute);
  shaders_.emplace(ShaderType::kCompute8, shader);

  if (env_->QueryExtension("GL_NV_image_formats")) {
    // Construct shader code for 16-bit unaligned RGB(A) output textures.
    compute = kComputeHeader + kComputeOutputRGBA16 + kComputeMain;

    shader = std::make_shared<ShaderProgram>(env_, compute);
    shaders_.emplace(ShaderType::kCompute16, shader);
  }

  // Construct shader code for 16-bit float unaligned RGB(A) output textures.
  compute = kComputeHeader + kComputeOutputRGBA16F + kComputeMain;

  shader = std::make_shared<ShaderProgram>(env_, compute);
  shaders_.emplace(ShaderType::kCompute16F, shader);

  // Construct shader code for 32-bit float unaligned RGB(A) output textures.
  compute = kComputeHeader + kComputeOutputRGBA32F + kComputeMain;

  shader = std::make_shared<ShaderProgram>(env_, compute);
  shaders_.emplace(ShaderType::kCompute32F, shader);

  // Construct shader code for 8-bit unaligned RGB(A) output textures.
  compute = kComputeHeader + kComputeOutputRGBA8 + kComputePlanarMain;
  shader = std::make_shared<ShaderProgram>(env_, compute);
  shaders_.emplace(ShaderType::kComputePlanar8, shader);

  if (env_->QueryExtension("GL_NV_image_formats")) {
    // Construct shader code for 16-bit unaligned RGB(A) output textures.
    compute = kComputeHeader + kComputeOutputRGBA16 + kComputePlanarMain;

    shader = std::make_shared<ShaderProgram>(env_, compute);
    shaders_.emplace(ShaderType::kComputePlanar16, shader);
  }

  // Construct shader code for 16-bit float unaligned RGB(A) output textures.
  compute = kComputeHeader + kComputeOutputRGBA16F + kComputePlanarMain;
  shader = std::make_shared<ShaderProgram>(env_, compute);
  shaders_.emplace(ShaderType::kComputePlanar16F, shader);

  // Construct shader code for 32-bit float unaligned RGB(A) output textures.
  compute = kComputeHeader + kComputeOutputRGBA32F + kComputePlanarMain;
  shader = std::make_shared<ShaderProgram>(env_, compute);
  shaders_.emplace(ShaderType::kComputePlanar32F, shader);

  error = env_->UnbindContext(ContextType::kPrimary) ;
  if (!error.empty()) throw std::runtime_error(error);
}

Engine::~Engine() {

  std::lock_guard<std::mutex> lk(mutex_);

  env_->BindContext(ContextType::kPrimary, EGL_NO_SURFACE, EGL_NO_SURFACE);

  for (auto& pair : stage_textures_) {
    GLuint texture = pair.first;
    env_->Gles()->DeleteTextures(1, &texture);
  }

  for (auto& pair : surfaces_) {
    auto graphics = std::get<std::vector<GraphicTuple>>(pair.second);

    for (auto& gltuple : graphics) {
      auto image = std::get<EGLImageKHR>(gltuple);
      auto texture = std::get<GLuint>(gltuple);

      env_->Egl()->DestroyImageKHR(env_->Display(), image);
      env_->Gles()->DeleteTextures(1, &texture);
    }
  }

  env_->Gles()->DeleteFramebuffers(1, &fbo_);

  env_->UnbindContext(ContextType::kPrimary) ;
}

uint64_t Engine::CreateSurface(const Surface& surface, uint32_t flags) {

  std::lock_guard<std::mutex> lk(mutex_);

  uint64_t surface_id = kSurfaceIdPrefix | surface.fd;

  if (surfaces_.count(surface_id) != 0)
    return surface_id;

  std::string error = env_->BindContext(ContextType::kPrimary, EGL_NO_SURFACE,
                                        EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

  env_->Gles()->ActiveTexture(GL_TEXTURE0);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set active texture unit 0");

  std::vector<GraphicTuple> graphics = ImportSurface(surface, flags);

  SurfaceTuple stuple = std::make_tuple(std::move(graphics), surface, flags);
  surfaces_.emplace(surface_id, std::move(stuple));

  error = env_->UnbindContext(ContextType::kPrimary) ;
  if (!error.empty()) throw std::runtime_error(error);

  return surface_id;
}

void Engine::DestroySurface(uint64_t id) {

  std::lock_guard<std::mutex> lk(mutex_);

  std::string error = env_->BindContext(ContextType::kPrimary, EGL_NO_SURFACE,
                                        EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

  auto stuple = std::move(surfaces_.at(id));
  surfaces_.erase(id);

  auto& graphics = std::get<std::vector<GraphicTuple>>(stuple);

  for (auto& gltuple : graphics) {
    auto image = std::get<EGLImageKHR>(gltuple);
    auto texture = std::get<GLuint>(gltuple);

    if (!env_->Egl()->DestroyImageKHR(env_->Display(), image)) {
      throw Exception("Failed to destroy EGL image, error: ", std::hex,
                      env_->Egl()->GetError(), "!");
    }

    env_->Gles()->DeleteTextures(1, &texture);
    EXCEPTION_IF_GL_ERROR(env_, "Failed to delete GL texture!");
  }

  error = env_->UnbindContext(ContextType::kPrimary) ;
  if (!error.empty()) throw std::runtime_error(error);
}

std::uintptr_t Engine::Compose(const Compositions& compositions,
                               bool synchronous) {

  std::lock_guard<std::mutex> lk(mutex_);

  std::string error = env_->BindContext(ContextType::kPrimary, EGL_NO_SURFACE,
                                        EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

  env_->Gles()->BindFramebuffer(GL_FRAMEBUFFER, fbo_);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to bind frame buffer");

  for (auto const& composition : compositions) {
    auto surface_id = std::get<uint64_t>(composition);
    auto color = std::get<uint32_t>(composition);
    auto clean = std::get<bool>(composition);
    auto normalize = std::get<Normalization>(composition);
    auto objects = std::get<Objects>(composition);

    SurfaceTuple& stuple = surfaces_.at(surface_id);

    Surface& surface = std::get<Surface>(stuple);
    auto& graphics = std::get<std::vector<GraphicTuple>>(stuple);

    // Resize normalization length and apply conversion needed for shaders.
    if (normalize.size() != 4) { normalize.resize(4); }

    for (auto& norm : normalize) {
      // Adjust data range to match fragment shader data representation.
      norm.offset /= 255.0;

      if (!Format::IsSigned(surface.format))
        continue;

      // Additional coefficients required for unsigned to signed conversion.
      norm.offset += 0.5;
      norm.scale *= 2.0;
    }

    // Use intermediary texture only if output surface is not renderable or
    // blending required and output is YUV as this combination is not suppoted.
    GLuint stgtex = GetStageTexture(surface, objects);

    // Insert internal blit object for the inplace surface at the begining.
    // Required only when there is intermediary stage texture and clean is false.
    if (!clean && (stgtex != 0)) {
      objects.insert(objects.begin(), Object());

      objects[0].source.w = objects[0].destination.w = surface.width;
      objects[0].source.h = objects[0].destination.h = surface.height;

      objects[0].id = surface_id;
    }

    // Blending is not supported in combination with direct rendering into YUV.
    if (Format::IsRgb(surface.format) || (stgtex != 0) ||
        (Format::IsYuv(surface.format) && shaders_.count(ShaderType::kYUV) == 0)) {
      env_->Gles()->Enable(GL_BLEND);
      EXCEPTION_IF_GL_ERROR(env_, "Failed to enable blend capability");

      env_->Gles()->BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      EXCEPTION_IF_GL_ERROR(env_, "Failed to set blend function");
    }

    if ((stgtex == 0) && Format::IsYuv(surface.format)) {
      uint32_t colorspace = Format::ColorSpace(surface.format);

      error = RenderYuvTexture(graphics, clean, color, colorspace, objects);
      if (!error.empty()) throw std::runtime_error(error);
    } else if ((stgtex == 0) && Format::IsRgb(surface.format)) {
      bool inverted = Format::IsInverted(surface.format);
      bool swapped = Format::IsSwapped(surface.format);

      error = RenderRgbTexture(graphics, clean, color, inverted, swapped,
                               normalize, objects);
      if (!error.empty()) throw std::runtime_error(error);
    } else if (stgtex != 0) {
      // Pass the inverted and swapped flags from main format to stage texture.
      bool inverted = Format::IsInverted(surface.format);
      bool swapped = Format::IsSwapped(surface.format);

      error = RenderStageTexture(stgtex, color, inverted, swapped, normalize,
                                 objects);
      if (!error.empty()) throw std::runtime_error(error);
    }

    // Make sure that blending is disabled for next stages.
    env_->Gles()->Disable(GL_BLEND);
    EXCEPTION_IF_GL_ERROR(env_, "Failed to disable blend capability");

    // In case output is unaligned RGB, apply compute shader.
    if ((stgtex != 0) && Format::IsRgb(surface.format)) {
      error = DispatchCompute(stgtex, surface, graphics);
      if (!error.empty()) throw std::runtime_error(error);
    }

    // Transmute the intermediary BGRA texture to YUV.
    if ((stgtex != 0) && Format::IsYuv(surface.format)) {
      error = ColorTransmute(stgtex, surface, graphics);
      if (!error.empty()) throw std::runtime_error(error);
    }
  }

  uintptr_t fence = reinterpret_cast<std::uintptr_t>(nullptr);

  if (synchronous) {
    env_->Gles()->Finish();
    EXCEPTION_IF_GL_ERROR(env_, "Failed to execute submitted compositions");

    fence = reinterpret_cast<std::uintptr_t>(nullptr);
  } else {
    GLsync sync = env_->Gles()->FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    EXCEPTION_IF_GL_ERROR(env_, "Failed to create fence object");

    fence = reinterpret_cast<std::uintptr_t>(sync);
  }

  error = env_->UnbindContext(ContextType::kPrimary) ;
  if (!error.empty()) throw std::runtime_error(error);

  return fence;
}

void Engine::Finish(std::uintptr_t fence) {

  if (fence == reinterpret_cast<std::uintptr_t>(nullptr))
    return;

  GLsync sync = reinterpret_cast<GLsync>(fence);

  std::string error = env_->BindContext(ContextType::kAuxilary, EGL_NO_SURFACE,
                                        EGL_NO_SURFACE);
  if (!error.empty()) throw std::runtime_error(error);

  auto status = env_->Gles()->ClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT,
                                             GL_TIMEOUT_IGNORED);

  if (status == GL_WAIT_FAILED) {
    throw Exception("Failed to sync fence object ", fence, "!");
  }

  env_->Gles()->DeleteSync(sync);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to delete fence object");

  error = env_->UnbindContext(ContextType::kAuxilary);
  if (!error.empty()) throw std::runtime_error(error);
}

std::string Engine::RenderYuvTexture(std::vector<GraphicTuple>& graphics,
                                     bool clean, uint32_t color,
                                     uint32_t colorspace, Objects& objects) {

  uint32_t maxwidth = 0, maxheight = 0;

  // Convert RGB color code to YUV channel values if output surface is YUV.
  color = Format::ToYuvColor(color, colorspace);

  for (auto& gltuple : graphics) {
    GLuint& texture = std::get<GLuint>(gltuple);
    ImageParam& imgparam = std::get<ImageParam>(gltuple);

    uint32_t width = std::get<0>(imgparam);
    uint32_t height = std::get<1>(imgparam);
    uint32_t format = std::get<2>(imgparam);

    maxwidth = std::max(maxwidth, width);
    maxheight = std::max(maxheight, height);

    GLenum textarget = GL_TEXTURE_2D;

    // Overwrite if direct YUV rendering is supported as FramebufferTexture2D
    // does not accept texture EXTERNAL_OES without that extension.
    if (shaders_.count(ShaderType::kYUV) != 0)
      textarget = GL_TEXTURE_EXTERNAL_OES;

    // Attach output texture to the rendering frame buffer.
    env_->Gles()->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       textarget, texture, 0);
    RETURN_IF_GL_ERROR(env_, "Failed to attach output texture ", texture, " to",
                       " frame buffer at color attachment 0 for YUV rendering");

    if (clean) {
      GLfloat luma = EXTRACT_RED_COLOR(color), alpha = EXTRACT_ALPHA_COLOR(color);
      GLfloat cr = EXTRACT_GREEN_COLOR(color), cb = EXTRACT_BLUE_COLOR(color);

      // Set/Clear the background of the texture attached to the frame buffer.
      if (format == ColorFormat::kGRAY8)
        env_->Gles()->ClearColor(luma, 0.0, 0.0, alpha);
      else if (format == ColorFormat::kRG88 || format == ColorFormat::kGR88)
        env_->Gles()->ClearColor(cr, cb, 0.0, alpha);
      else
        env_->Gles()->ClearColor(luma, cr, cb, alpha);

      env_->Gles()->Clear(GL_COLOR_BUFFER_BIT);
      RETURN_IF_GL_ERROR(env_, "Failed to clear buffer color bit");
    }

    // Choose shader based on the image texture format.
    ShaderType stype = ShaderType::kYUV;

    if (format == ColorFormat::kGRAY8)
      stype = ShaderType::kLuma;
    else if (format == ColorFormat::kRG88 || format == ColorFormat::kGR88)
      stype = ShaderType::kChroma;

    std::shared_ptr<ShaderProgram> shader = shaders_.at(stype);
    shader->Use();

    shader->SetBool("stageInput", false);
    shader->SetInt("stageTex", 1);

    shader->SetBool("rbSwapped", Format::IsSwapped(format));
    shader->SetInt("colorSpace", colorspace);

    shader->SetInt("extTex", 0);

    env_->Gles()->ActiveTexture(GL_TEXTURE0);
    RETURN_IF_GL_ERROR(env_, "Failed to set active texture unit 0");

    // Depending on the format the plane image may have different size.
    float wscale = static_cast<float>(width) / maxwidth;
    float hscale = static_cast<float>(height) / maxheight;

    for (auto& object : objects) {
      Region destination = object.destination;

      // Adjust destination coordinates which will be used for the view port.
      env_->Gles()->Viewport(destination.x * wscale, destination.y * hscale,
                             destination.w * wscale, destination.h * hscale);
      RETURN_IF_GL_ERROR(env_, "Failed to set destination viewport");

      std::string error = DrawObject(shader, object);
      if (!error.empty()) return error;
    }
  }

  return std::string();
}

std::string Engine::RenderRgbTexture(std::vector<GraphicTuple>& graphics,
                                     bool clean, uint32_t color, bool inverted,
                                     bool swapped, Normalization& normalize,
                                     Objects& objects) {

  // Declare the list of color buffers to be drawn into.
  std::vector<GLenum> buffers;

  for (size_t idx = 0; idx < graphics.size(); idx++) {
    GraphicTuple& gltuple = graphics.at(idx);
    GLuint& texture = std::get<GLuint>(gltuple);

    // Attach output texture to the rendering frame buffer.
    env_->Gles()->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + idx,
                                       GL_TEXTURE_2D, texture, 0);
    RETURN_IF_GL_ERROR(env_, "Failed to attach output texture ", texture, " to ",
                      "frame buffer at color attachment ", idx, " for RGB render");

    buffers.push_back(GL_COLOR_ATTACHMENT0 + idx);
  }

  // Specify the list of color buffers to be drawn into.
  env_->Gles()->DrawBuffers(buffers.size(), buffers.data());
  RETURN_IF_GL_ERROR(env_, "Failed to set color buffers to be drawn into");

  if (clean) {
    // Set/Clear the background of the texture attached to the frame buffer.
    env_->Gles()->ClearColor(EXTRACT_RED_COLOR(color), EXTRACT_GREEN_COLOR(color),
                             EXTRACT_BLUE_COLOR(color), EXTRACT_ALPHA_COLOR(color));
    env_->Gles()->Clear(GL_COLOR_BUFFER_BIT);
    RETURN_IF_GL_ERROR(env_, "Failed to clear buffer color bit");
  }

  ShaderType stype =
      (graphics.size() == 1) ? ShaderType::kRGB : ShaderType::kPlanarRGB;

  std::shared_ptr<ShaderProgram> shader = shaders_.at(stype);
  shader->Use();

  shader->SetVec4("rgbaScale", normalize[0].scale, normalize[1].scale,
                  normalize[2].scale, normalize[3].scale);
  shader->SetVec4("rgbaOffset", normalize[0].offset, normalize[1].offset,
                  normalize[2].offset, normalize[3].offset);

  shader->SetBool("rgbaInverted", inverted);
  shader->SetBool("rbSwapped", swapped);

  shader->SetInt("extTex", 0);

  env_->Gles()->ActiveTexture(GL_TEXTURE0);
  RETURN_IF_GL_ERROR(env_, "Failed to set active texture unit 0");

  for (auto& object : objects) {
    const Region& destination = object.destination;

    env_->Gles()->Viewport(
        destination.x, destination.y, destination.w, destination.h);
    RETURN_IF_GL_ERROR(env_, "Failed to set destination viewport");

    std::string error = DrawObject(shader, object);
    if (!error.empty()) return error;
  }

  return std::string();
}

std::string Engine::RenderStageTexture(GLuint texture, uint32_t color,
                                       bool inverted, bool swapped,
                                       Normalization& normalize, Objects& objects) {

  // Attach staging texture to the rendering frame buffer.
  env_->Gles()->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_2D, texture, 0);
  RETURN_IF_GL_ERROR(env_, "Failed to attach stage texture ", texture, " to ",
                     "frame buffer at color attachment 0 for stage rendering");

  // Set/Clear the background of the texture attached to the frame buffer.
  env_->Gles()->ClearColor(EXTRACT_RED_COLOR(color), EXTRACT_GREEN_COLOR(color),
                           EXTRACT_BLUE_COLOR(color), EXTRACT_ALPHA_COLOR(color));
  env_->Gles()->Clear(GL_COLOR_BUFFER_BIT);
  RETURN_IF_GL_ERROR(env_, "Failed to clear buffer color bit");

  std::shared_ptr<ShaderProgram> shader = shaders_.at(ShaderType::kRGB);
  shader->Use();

  shader->SetVec4("rgbaScale", normalize[0].scale, normalize[1].scale,
                  normalize[2].scale, normalize[3].scale);
  shader->SetVec4("rgbaOffset", normalize[0].offset, normalize[1].offset,
                  normalize[2].offset, normalize[3].offset);

  shader->SetBool("rgbaInverted", inverted);
  shader->SetBool("rbSwapped", swapped);

  shader->SetInt("extTex", 0);

  env_->Gles()->ActiveTexture(GL_TEXTURE0);
  RETURN_IF_GL_ERROR(env_, "Failed to set active texture unit 0");

  for (auto& object : objects) {
    const Region& destination = object.destination;

    env_->Gles()->Viewport(
        destination.x, destination.y, destination.w,destination.h);
    RETURN_IF_GL_ERROR(env_, "Failed to set destination viewport");

    std::string error = DrawObject(shader, object);
    if (!error.empty()) return error;
  }

  return std::string();
}

std::string Engine::DrawObject(std::shared_ptr<ShaderProgram>& shader,
                               const Object& object) {

  SurfaceTuple& stuple = surfaces_.at(object.id);

  Surface& insurface = std::get<Surface>(stuple);
  auto& graphics = std::get<std::vector<GraphicTuple>>(stuple);

  GraphicTuple& gltuple = graphics.at(0);
  GLuint& intexture = std::get<GLuint>(gltuple);

  // Bind the input surface texture to EXTERNAL_OES for current active texture.
  env_->Gles()->BindTexture(GL_TEXTURE_EXTERNAL_OES, intexture);
  RETURN_IF_GL_ERROR(env_, "Failed to bind input texture ", intexture);

  if (shader->HasVariable("globalAlpha"))
    shader->SetFloat("globalAlpha", (object.alpha / 255.0));

  // Rotation angle in radians.
  shader->SetFloat("rotationAngle", object.rotation * M_PI / 180);

  auto mask = object.mask & (ConfigMask::kHFlip | ConfigMask::kVFlip);
  GLuint pos = shader->GetAttribLocation("vPosition");

  env_->Gles()->VertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0,
                                    kVertices.at(mask).data());
  RETURN_IF_GL_ERROR(env_, "Failed to define main vertex array");

  const Region& source = object.source;
  std::array<float, 8> coords = kTextureCoords;

  uint32_t width = insurface.width;
  uint32_t height = insurface.height;

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
  env_->Gles()->VertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0,
                                    coords.data());
  RETURN_IF_GL_ERROR(env_, "Failed to define texture vertex array");

  env_->Gles()->EnableVertexAttribArray(texcoord);
  RETURN_IF_GL_ERROR(env_, "Failed to enable vertex array");

  env_->Gles()->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  RETURN_IF_GL_ERROR(env_, "Failed to render array data");

  return std::string();
}

std::string Engine::DispatchCompute(GLuint stgtex, Surface& surface,
                                    std::vector<GraphicTuple>& graphics) {

  ShaderType stype = ShaderType::kCompute8;
  auto bitdepth = Format::BitDepth(surface.format);

  // Overwrite default shader type if necessary.
  if (Format::IsFloat(surface.format) && (bitdepth == 32))
    stype = Format::IsPlanar(surface.format) ?
        ShaderType::kComputePlanar32F : ShaderType::kCompute32F;
  else if (Format::IsFloat(surface.format) && (bitdepth == 16))
    stype = Format::IsPlanar(surface.format) ? ShaderType::kComputePlanar16F :
        ShaderType::kCompute16F;
  else if (!Format::IsFloat(surface.format) && (bitdepth == 16))
    stype = Format::IsPlanar(surface.format) ? ShaderType::kComputePlanar16 :
        ShaderType::kCompute16;
  else
    stype = Format::IsPlanar(surface.format) ? ShaderType::kComputePlanar8 :
        ShaderType::kCompute8;

  std::shared_ptr<ShaderProgram> shader = shaders_.at(stype);
  shader->Use();

  uint32_t width = surface.width;
  uint32_t height = surface.height;

  auto& gltuple = graphics.at(0);
  GLuint& otexture = std::get<GLuint>(gltuple);
  ImageParam& imgparam = std::get<ImageParam>(gltuple);

  shader->SetInt("targetWidth", width);
  shader->SetInt("targetHeight", height);
  shader->SetInt("imageWidth", std::get<0>(imgparam));
  shader->SetInt("numPixels", (width * height));
  shader->SetInt("numChannels", Format::NumComponents(surface.format));
  shader->SetInt("inTex", 1);

  env_->Gles()->ActiveTexture(GL_TEXTURE1);
  RETURN_IF_GL_ERROR(env_, "Failed to set active texture unit 1");

  env_->Gles()->BindTexture(GL_TEXTURE_2D, stgtex);
  RETURN_IF_GL_ERROR(env_, "Failed to bind staging texture");

  GLenum format = Format::ToGL(std::get<2>(imgparam));

  env_->Gles()->BindImageTexture(1, otexture, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                                 format);
  RETURN_IF_GL_ERROR(env_, "Failed to bind output image texture ", otexture);

  // Align to the divisor for the number of X groups explained below.
  uint32_t n_pixels = (((width * height) + ((32 * 4) - 1)) & ~((32 * 4) - 1));

  // 32 because of the local size and 4 pixels are processed at a time.
  GLuint xgroups = n_pixels / (32 * 4);

  env_->Gles()->DispatchCompute(xgroups, 1, 1);
  RETURN_IF_GL_ERROR(env_, "Failed to dispatch compute");

  return std::string();
}

std::string Engine::ColorTransmute(GLuint stgtex, Surface& surface,
                                   std::vector<GraphicTuple>& graphics) {

  auto& gltuple = graphics.at(0);
  GLuint& otexture = std::get<GLuint>(gltuple);

  env_->Gles()->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                     GL_TEXTURE_EXTERNAL_OES, otexture, 0);
  RETURN_IF_GL_ERROR(env_, "Failed to attach output texture ", otexture, " to ",
                     "frame buffer at color attachment 0 for color transmute");

  uint32_t width = surface.width;
  uint32_t height = surface.height;

  env_->Gles()->Viewport(0, 0, width, height);
  RETURN_IF_GL_ERROR(env_, "Failed to set destination viewport");

  std::shared_ptr<ShaderProgram> shader = shaders_.at(ShaderType::kYUV);
  shader->Use();

  shader->SetInt("stageTex", 1);
  shader->SetBool("stageInput", true);
  shader->SetInt("colorSpace", Format::ColorSpace(surface.format));
  shader->SetFloat("rotationAngle", 0);

  // Load the vertex position.
  GLuint pos = shader->GetAttribLocation("vPosition");

  env_->Gles()->VertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0,
                                    kVertices.at(0).data());
  RETURN_IF_GL_ERROR(env_, "Failed to define main vertex array");

  // Load texture coordinates.
  GLuint texcoord = shader->GetAttribLocation("inTexCoord");
  std::array<float, 8> coords = kTextureCoords;

  env_->Gles()->VertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0,
                                    coords.data());
  RETURN_IF_GL_ERROR(env_, "Failed to define vertex array");

  env_->Gles()->EnableVertexAttribArray(texcoord);
  RETURN_IF_GL_ERROR(env_, "Failed to enable vertex array");

  env_->Gles()->ActiveTexture(GL_TEXTURE1);
  RETURN_IF_GL_ERROR(env_, "Failed to set active texture unit 1");

  env_->Gles()->BindTexture(GL_TEXTURE_2D, stgtex);
  RETURN_IF_GL_ERROR(env_, "Failed to bind staging texture");

  env_->Gles()->DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  RETURN_IF_GL_ERROR(env_, "Failed to render array data");

  return std::string();
}

bool Engine::IsSurfaceRenderable(const Surface& surface) {

  uint32_t alignment = QueryAlignment();
  uint32_t bytedepth = Format::BitDepth(surface.format) / 8;

  bool aligned =
      (((surface.planes[0].stride / bytedepth) % alignment) == 0) ? true : false;

  // For YUV surfaces check only if it satisfies GPU alignment requirement.
  if (Format::IsYuv(surface.format))
    return aligned;

  // Unalined RGB(A) surfaces are not renderable, nothing further to check.
  if (!aligned)
    return false;

  // Signed integer RGB(A) surfaces are not renderable, nothing further to check.
  if (Format::IsSigned(surface.format))
    return false;

  // Float planar RGB formats are not rederable as there is no DRM format.
  // TODO Remove when float planar RGB formats are supported.
  if (Format::IsFloat(surface.format) && Format::IsPlanar(surface.format))
    return false;

  uint32_t n_components = Format::NumComponents(surface.format);

  // 3 channeled Float RGB surfaces are not renderable due to limitation.
  // TODO Remove when 3 channel RGB float formats are supported.
  if (Format::IsFloat(surface.format) && (n_components == 3))
    return false;

  // 3 channeled 16-bit integer RGB surfaces are not renderable, format missing.
  // TODO Remove when 3 channel 16-bit integer RGB float formats are supported.
  if (!Format::IsFloat(surface.format) && (n_components == 3) &&
      (Format::BitDepth(surface.format) == 16))
    return false;

  return true;
}

GLuint Engine::GetStageTexture(const Surface& surface, const Objects& objects) {

  // Stage texture is not required if surface is RGB and is renderable or
  // output surface is YUV and direct rendering into YUV is not supported.
  if ((Format::IsRgb(surface.format) && IsSurfaceRenderable(surface)) ||
      (Format::IsYuv(surface.format) && (shaders_.count(ShaderType::kYUV) == 0)))
    return 0;

  // Iterate over the objects and determine if alpha blending is required.
  auto iter = std::find_if(objects.begin(), objects.end(),
      [&](const Object& obj) -> bool {
        SurfaceTuple& stuple = surfaces_.at(obj.id);
        uint32_t format = std::get<Surface>(stuple).format;

        // Enable blending if atleast one object has global alpha or is RGBA.
        return (obj.alpha != 0xFF) ||
            (Format::IsRgb(format) && Format::NumComponents(format) == 4);
      }
  );

  bool blending = (iter != objects.end()) ? true : false;

  // Stage texture needed when blending and direct rendering into YUV is supported.
  if (Format::IsYuv(surface.format) && !blending)
    return 0;

  GLsizei width = surface.width;
  GLsizei height = surface.height;

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

  env_->Gles()->GenTextures(1, &texture);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to generate staging texture");

  env_->Gles()->ActiveTexture(GL_TEXTURE0);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set active texture unit 0");

  env_->Gles()->BindTexture(GL_TEXTURE_2D, texture);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to bind staging texture");

  env_->Gles()->TexStorage2D(GL_TEXTURE_2D, 1, format, width, height);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set staging texture storage");

  stage_textures_.emplace(
      texture, std::move(std::make_tuple(width, height, format)));

  return texture;
}

std::vector<GraphicTuple> Engine::ImportSurface(const Surface& surface,
                                                uint32_t flags) {

  std::vector<Surface> imgsurfaces = GetImageSurfaces(surface, flags);
  std::vector<GraphicTuple> graphics;

  for (auto& subsurface : imgsurfaces) {
    // Retrieve the tuple of DRM format and its modifier.
    std::tuple<uint32_t, uint64_t> internal = Format::ToInternal(subsurface.format);

    EGLint attribs[64] = { EGL_NONE };
    uint32_t index = 0;

    attribs[index++] = EGL_WIDTH;
    attribs[index++] = subsurface.width;
    attribs[index++] = EGL_HEIGHT;
    attribs[index++] = subsurface.height;
    attribs[index++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[index++] = std::get<0>(internal);
    attribs[index++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attribs[index++] = subsurface.fd;
    attribs[index++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attribs[index++] = subsurface.planes[0].stride;
    attribs[index++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attribs[index++] = subsurface.planes[0].offset;
    attribs[index++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
    attribs[index++] = std::get<1>(internal) & 0xFFFFFFFF;
    attribs[index++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
    attribs[index++] = std::get<1>(internal) >> 32;

    if (subsurface.planes.size() >= 2) {
      attribs[index++] = EGL_DMA_BUF_PLANE1_FD_EXT;
      attribs[index++] = subsurface.fd;
      attribs[index++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
      attribs[index++] = subsurface.planes[1].stride;
      attribs[index++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
      attribs[index++] = subsurface.planes[1].offset;
      attribs[index++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
      attribs[index++] = std::get<1>(internal) & 0xFFFFFFFF;
      attribs[index++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
      attribs[index++] = std::get<1>(internal) >> 32;
    }

    if (subsurface.planes.size() == 3) {
      attribs[index++] = EGL_DMA_BUF_PLANE2_FD_EXT;
      attribs[index++] = subsurface.fd;
      attribs[index++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
      attribs[index++] = subsurface.planes[2].stride;
      attribs[index++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
      attribs[index++] = subsurface.planes[2].offset;
      attribs[index++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
      attribs[index++] = std::get<1>(internal) & 0xFFFFFFFF;
      attribs[index++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
      attribs[index++] = std::get<1>(internal) >> 32;
    }

    attribs[index] = EGL_NONE;

    EGLImageKHR image =
        env_->Egl()->CreateImageKHR(env_->Display(), EGL_NO_CONTEXT,
                                    EGL_LINUX_DMA_BUF_EXT, NULL, attribs);

    if (image == EGL_NO_IMAGE) {
      throw Exception("Failed to create EGL image, error: ", std::hex,
                      env_->Egl()->GetError(), "!");
    }

    GLuint texture;

    // Create GL texture to the image will be binded.
    env_->Gles()->GenTextures (1, &texture);
    EXCEPTION_IF_GL_ERROR(env_, "Failed to generate GL texture!");

    GLenum textarget = GL_TEXTURE_EXTERNAL_OES;

    // Overwrite if surface is output and format is RGB or it is YUV but direct
    // rendering into YUV textures is not supported as FramebufferTexture2D
    // doesn't accept texture EXTERNAL_OES without that extension.
    if ((flags & SurfaceFlags::kOutput) && (Format::IsRgb(surface.format) ||
        (Format::IsYuv(surface.format) && (shaders_.count(ShaderType::kYUV) == 0))))
      textarget = GL_TEXTURE_2D;

    env_->Gles()->BindTexture(textarget, texture);
    EXCEPTION_IF_GL_ERROR(env_, "Failed to bind output texture ", texture);

    env_->Gles()->EGLImageTargetTexture2DOES(
        textarget, reinterpret_cast<GLeglImageOES>(image));
    EXCEPTION_IF_GL_ERROR(env_, "Failed to associate image ", image,
                          " with external texture ", texture);

    ImageParam imgparam = std::make_tuple(
        subsurface.width, subsurface.height, subsurface.format);

    graphics.emplace_back(texture, image, std::move(imgparam));
  }

  return graphics;
}

std::vector<Surface> Engine::GetImageSurfaces(const Surface& surface,
                                              uint32_t flags) {

  std::vector<Surface> imgsurfaces;

  if ((flags & SurfaceFlags::kOutput) && Format::IsYuv(surface.format) &&
      (shaders_.count(ShaderType::kYUV) == 0)) {
    // Direct rendering into YUV buffer is not supported and output is YUV.
    // A separate sub-surface must be created for each plane of the YUV surface.
    uint64_t size = (surface.planes.size() >= 2) ?
        surface.planes[1].offset : surface.size;

    // First sub-surface will contain only Luminosity info.
    Surface subsurface(surface.fd, ColorFormat::kGRAY8, surface.width,
                       surface.height, size, Planes({surface.planes[0]}));
    imgsurfaces.push_back(subsurface);

    // Second sub-surface will represent the chroma info.
    if (surface.planes.size() == 2) {
      subsurface.planes[0] = surface.planes[1];
      subsurface.size = surface.size - surface.planes[1].offset;
      subsurface.format = ColorFormat::kRG88;

      if ((surface.format == ColorFormat::kNV21) ||
          (surface.format == ColorFormat::kNV61))
        subsurface.format = ColorFormat::kGR88;

      // Depending on the surface format the chroma plane has different size.
      if (surface.format == ColorFormat::kNV12 ||
          surface.format == ColorFormat::kNV21) {
        subsurface.width /= 2;
        subsurface.height /= 2;
      } else if (surface.format == ColorFormat::kNV16 ||
                 surface.format == ColorFormat::kNV61) {
        subsurface.width /= 2;
      }

      imgsurfaces.push_back(subsurface);
    }
  } else if ((flags & SurfaceFlags::kOutput) && Format::IsRgb(surface.format) &&
             Format::IsPlanar(surface.format) && IsSurfaceRenderable(surface)) {
    // Planar RGB(A) needs to be split into multiple images for rendering.
    uint32_t n_planes = surface.planes.size();

    for (uint32_t idx = 0; idx < n_planes; idx++) {
      // Size of current plane is offset to the next one (or total size for last)
      // minus the offset to previous (or 0 in the case of the first plane).
      uint64_t size = ((idx + 1) < n_planes) ?
          surface.planes[(idx + 1)].offset : surface.size;
      size -= (idx == 0) ? 0 : surface.planes[idx].offset;

      Surface subsurface(surface.fd, ColorFormat::kGRAY8, surface.width,
                         surface.height, size, Planes({surface.planes[idx]}));
      imgsurfaces.push_back(std::move(subsurface));
    }
  } else if ((flags & SurfaceFlags::kOutput) && Format::IsRgb(surface.format) &&
             !IsSurfaceRenderable(surface)) {
    // Non-renderable RGB(A) output, reshape its dimensions and format.
    Surface subsurface(surface);

    uint32_t n_components = Format::NumComponents(surface.format);
    uint32_t bitdepth = Format::BitDepth(surface.format);

    // For planar formats reset to single plane and adjust stride.
    if (Format::IsPlanar(surface.format)) {
      subsurface.planes.resize(1);
      subsurface.planes[0].stride *= n_components;
    }

    // Overwrite the 3 channeled format to corresponding 4 channeled format.
    // This will make it compatible for creating EGL image and use in compute.
    if (n_components == 3) {
      subsurface.format = ColorFormat::kRGBA8888;

      if (Format::IsFloat(surface.format) && (bitdepth == 32))
        subsurface.format = ColorFormat::kRGBA32323232F;
      else if (Format::IsFloat(surface.format) && (bitdepth == 16))
        subsurface.format = ColorFormat::kRGBA16161616F;
      else if (Format::IsUnsigned(surface.format) && (bitdepth == 16))
        subsurface.format = ColorFormat::kRGBA16161616;
      else if (Format::IsSigned(surface.format) && (bitdepth == 16))
        subsurface.format = ColorFormat::kRGBA16161616I;
      else if (Format::IsSigned(surface.format) && (bitdepth == 8))
        subsurface.format = ColorFormat::kRGBA8888I;

      n_components = Format::NumComponents(subsurface.format);
      bitdepth = Format::BitDepth(subsurface.format);
    }

    uint32_t alignment = QueryAlignment();

    // Divide by 8 in order to get bytes depth and then Bytes Per Pixel.
    uint32_t bytedepth = bitdepth / 8;
    uint32_t bpp = bytedepth * n_components;

    // Adjust width, height and stride values for non-renderable RGB(A) surface.
    // Align stride and calculate the width for the compute texture.
    uint32_t stride = subsurface.planes[0].stride / bytedepth;
    subsurface.planes[0].stride =
        ((stride + (alignment - 1)) & ~(alignment - 1)) * bytedepth;

    subsurface.width = subsurface.planes[0].stride / bpp;

    // Calculate the aligned height value rounded up based on surface size.
    uint32_t size = subsurface.size - subsurface.planes[0].offset;
    subsurface.height =
        std::ceil((size / bpp) / static_cast<float>(subsurface.width));

    imgsurfaces.push_back(subsurface);
  } else {
    // Surface is either input or renderable output, no reshape is required.
    imgsurfaces.push_back(surface);
  }

  return imgsurfaces;
}

} // namespace gl

IEngine* NewGlEngine() {

  return new gl::Engine();
}

} // namespace ib2c
