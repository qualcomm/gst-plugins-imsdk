/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#include "ib2c-shader-program.h"
#include "ib2c-utils.h"

namespace ib2c {

ShaderProgram::ShaderProgram(const std::string& vshader, const std::string& fshader) {

  GLuint vertex = 0, fragment = 0;

  if ((vertex = glCreateShader(GL_VERTEX_SHADER)) == 0) {
    throw Exception("Failed to create GL vertex shader, error: ",
        std::hex, glGetError(), "!");
  }

  const GLchar *code = vshader.c_str();

  glShaderSource(vertex, 1, &code, NULL);
  EXCEPTION_IF_GL_ERROR ("Failed to set GL vertex shader code");

  glCompileShader(vertex);
  EXCEPTION_IF_GL_ERROR ("Failed to compile GL vertex shader");

  if ((fragment = glCreateShader(GL_FRAGMENT_SHADER)) == 0) {
    throw Exception("Failed to create GL fragment shader, error: ",
        std::hex, glGetError(), "!");
  }

  code = fshader.c_str();

  glShaderSource(fragment, 1, &code, NULL);
  EXCEPTION_IF_GL_ERROR ("Failed to set GL fragment shader code");

  glCompileShader(fragment);
  EXCEPTION_IF_GL_ERROR ("Failed to compile GL fragment shader");

  if ((id_ = glCreateProgram()) == 0) {
    throw Exception("Failed to create GL program, error: ",
        std::hex, glGetError(), "!");
  }

  glAttachShader(id_, vertex);
  EXCEPTION_IF_GL_ERROR ("Failed to attach GL vertex shader to program ", id_);

  glAttachShader(id_, fragment);
  EXCEPTION_IF_GL_ERROR ("Failed to attach GL fragment shader to program ", id_);

  glLinkProgram(id_);
  EXCEPTION_IF_GL_ERROR ("Failed to link GL program ", id_);

  glDeleteShader(vertex);
  glDeleteShader(fragment);
}

ShaderProgram::ShaderProgram(const std::string& cshader) {

  GLuint compute = 0;

  if ((compute = glCreateShader(GL_COMPUTE_SHADER)) == 0) {
    throw Exception("Failed to create GL compute shader, error: ",
        std::hex, glGetError(), "!");
  }

  const GLchar *code = cshader.c_str();

  glShaderSource(compute, 1, &code, NULL);
  EXCEPTION_IF_GL_ERROR ("Failed to set GL compute shader code");

  glCompileShader(compute);
  EXCEPTION_IF_GL_ERROR ("Failed to compile GL compute shader");

  if ((id_ = glCreateProgram()) == 0) {
    throw Exception("Failed to create GL program, error: ",
        std::hex, glGetError(), "!");
  }

  glAttachShader(id_, compute);
  EXCEPTION_IF_GL_ERROR ("Failed to attach GL compute shader to program ", id_);

  glLinkProgram(id_);
  EXCEPTION_IF_GL_ERROR ("Failed to link GL program ", id_);

  glDeleteShader(compute);
}

ShaderProgram::~ShaderProgram() {

  glDeleteProgram(id_);
}

void ShaderProgram::Use() {

  // Install shader program as part of current rendering context.
  glUseProgram(id_);
  EXCEPTION_IF_GL_ERROR ("Failed to install program for current rendering state");
}

void ShaderProgram::SetBool(const char* name, bool value) const {

  glUniform1i(glGetUniformLocation(id_, name), static_cast<int>(value));
  EXCEPTION_IF_GL_ERROR ("Failed to set location of program attribute: ", name);
}

void ShaderProgram::SetInt(const char* name, int value) const {

  glUniform1i(glGetUniformLocation(id_, name), value);
  EXCEPTION_IF_GL_ERROR ("Failed to set location of program attribute: ", name);
}

void ShaderProgram::SetFloat(const char* name, float value) const {

  glUniform1f(glGetUniformLocation(id_, name), value);
  EXCEPTION_IF_GL_ERROR ("Failed to set location of program attribute: ", name);
}

void ShaderProgram::SetVec2(const char* name, float x, float y) const {

  glUniform2f(glGetUniformLocation(id_, name), x, y);
  EXCEPTION_IF_GL_ERROR ("Failed to set location of program attribute: ", name);
}

void ShaderProgram::SetVec3(const char* name, float x, float y, float z) const {

  glUniform3f(glGetUniformLocation(id_, name), x, y, z);
  EXCEPTION_IF_GL_ERROR ("Failed to set location of program attribute: ", name);
}

void ShaderProgram::SetVec4(const char* name, float x, float y, float z, float w) const {

  glUniform4f(glGetUniformLocation(id_, name), x, y, z, w);
  EXCEPTION_IF_GL_ERROR ("Failed to set location of program attribute: ", name);
}

void ShaderProgram::SetMat4(const char* name, const float* matrix) const {

  glUniformMatrix4fv(glGetUniformLocation(id_, name), 1, GL_FALSE, matrix);
  EXCEPTION_IF_GL_ERROR ("Failed to set location of program attribute: ", name);
}

bool ShaderProgram::HasVariable(const char* name) const {

  GLint value = glGetUniformLocation(id_, name);
  EXCEPTION_IF_GL_ERROR ("Failed to get location of uniform variable: ", name);

  return (value != (-1)) ? true : false;
}

GLint ShaderProgram::GetAttribLocation(const char* name) const {

  GLint value = glGetAttribLocation(id_, name);
  EXCEPTION_IF_GL_ERROR ("Failed to get location of program attribute: ", name);

  return value;
}

} // namespace ib2c
