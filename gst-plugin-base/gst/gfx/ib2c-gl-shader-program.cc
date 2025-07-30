/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#include "ib2c-gl-shader-program.h"

namespace ib2c {

namespace gl {

#define EXCEPTION_IF_COMPILE_FAILED(shader, ...)                  \
do {                                                              \
  GLint success;                                                  \
  env_->Gles()->GetShaderiv(shader, GL_COMPILE_STATUS, &success); \
                                                                  \
  if (!success) {                                                 \
    char info[512];                                               \
    env_->Gles()->GetShaderInfoLog(shader, 512, NULL, info);      \
                                                                  \
    throw Exception(__VA_ARGS__, ", log: ", info, " !");          \
  }                                                               \
} while (false)

ShaderProgram::ShaderProgram(std::shared_ptr<Environment> environment,
                             const std::string& vshader,
                             const std::string& fshader)
    : env_(environment) {

  GLuint vertex = 0, fragment = 0;

  if ((vertex = env_->Gles()->CreateShader(GL_VERTEX_SHADER)) == 0) {
    throw Exception("Failed to create GL vertex shader, error: ",
                    std::hex, env_->Gles()->GetError(), "!");
  }

  const GLchar *code = vshader.c_str();

  env_->Gles()->ShaderSource(vertex, 1, &code, NULL);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set GL vertex shader code");

  env_->Gles()->CompileShader(vertex);
  EXCEPTION_IF_COMPILE_FAILED (vertex, "Failed to compile GL vertex shader");

  if ((fragment = env_->Gles()->CreateShader(GL_FRAGMENT_SHADER)) == 0) {
    throw Exception("Failed to create GL fragment shader, error: ",
                    std::hex, env_->Gles()->GetError(), "!");
  }

  code = fshader.c_str();

  env_->Gles()->ShaderSource(fragment, 1, &code, NULL);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set GL fragment shader code");

  env_->Gles()->CompileShader(fragment);
  EXCEPTION_IF_COMPILE_FAILED (fragment, "Failed to compile GL fragment shader");

  if ((id_ = env_->Gles()->CreateProgram()) == 0) {
    throw Exception("Failed to create GL program, error: ",
                    std::hex, env_->Gles()->GetError(), "!");
  }

  env_->Gles()->AttachShader(id_, vertex);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to attach vertex shader to program ", id_);

  env_->Gles()->AttachShader(id_, fragment);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to attach fragment shader to program ", id_);

  env_->Gles()->LinkProgram(id_);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to link GL program ", id_);

  env_->Gles()->DeleteShader(vertex);
  env_->Gles()->DeleteShader(fragment);
}

ShaderProgram::ShaderProgram(std::shared_ptr<Environment> environment,
                             const std::string& cshader)
    : env_(environment) {

  GLuint compute = 0;

  if ((compute = env_->Gles()->CreateShader(GL_COMPUTE_SHADER)) == 0) {
    throw Exception("Failed to create GL compute shader, error: ",
                    std::hex, env_->Gles()->GetError(), "!");
  }

  const GLchar *code = cshader.c_str();

  env_->Gles()->ShaderSource(compute, 1, &code, NULL);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set GL compute shader code");

  env_->Gles()->CompileShader(compute);
  EXCEPTION_IF_COMPILE_FAILED (compute, "Failed to compile GL compute shader");

  if ((id_ = env_->Gles()->CreateProgram()) == 0) {
    throw Exception("Failed to create GL program, error: ",
                    std::hex, env_->Gles()->GetError(), "!");
  }

  env_->Gles()->AttachShader(id_, compute);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to attach compute shader to program ", id_);

  env_->Gles()->LinkProgram(id_);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to link GL program ", id_);

  env_->Gles()->DeleteShader(compute);
}

ShaderProgram::~ShaderProgram() {

  env_->Gles()->DeleteProgram(id_);
}

void ShaderProgram::Use() {

  // Install shader program as part of current rendering context.
  env_->Gles()->UseProgram(id_);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to install program for rendering state");
}

void ShaderProgram::SetBool(const char* name, bool value) const {

  GLint location = env_->Gles()->GetUniformLocation(id_, name);
  env_->Gles()->Uniform1i(location, static_cast<int>(value));
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set program attribute: ", name);
}

void ShaderProgram::SetInt(const char* name, int value) const {

  GLint location = env_->Gles()->GetUniformLocation(id_, name);
  env_->Gles()->Uniform1i(location, value);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set program attribute: ", name);
}

void ShaderProgram::SetFloat(const char* name, float value) const {

  GLint location = env_->Gles()->GetUniformLocation(id_, name);
  env_->Gles()->Uniform1f(location, value);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set program attribute: ", name);
}

void ShaderProgram::SetVec2(const char* name, float x, float y) const {

  GLint location = env_->Gles()->GetUniformLocation(id_, name);
  env_->Gles()->Uniform2f(location, x, y);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set program attribute: ", name);
}

void ShaderProgram::SetVec3(const char* name, float x, float y, float z) const {

  GLint location = env_->Gles()->GetUniformLocation(id_, name);
  env_->Gles()->Uniform3f(location, x, y, z);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set program attribute: ", name);
}

void ShaderProgram::SetVec4(const char* name, float x, float y, float z, float w) const {

  GLint location = env_->Gles()->GetUniformLocation(id_, name);
  env_->Gles()->Uniform4f(location, x, y, z, w);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set program attribute: ", name);
}

void ShaderProgram::SetMat4(const char* name, const float* matrix) const {

  GLint location = env_->Gles()->GetUniformLocation(id_, name);
  env_->Gles()->UniformMatrix4fv(location, 1, GL_FALSE, matrix);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to set program attribute: ", name);
}

bool ShaderProgram::HasVariable(const char* name) const {

  GLint value = env_->Gles()->GetUniformLocation(id_, name);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to get uniform variable: ", name);

  return (value != (-1)) ? true : false;
}

GLint ShaderProgram::GetAttribLocation(const char* name) const {

  GLint value = env_->Gles()->GetAttribLocation(id_, name);
  EXCEPTION_IF_GL_ERROR(env_, "Failed to get program attribute: ", name);

  return value;
}

} // namespace gl

} // namespace ib2c
