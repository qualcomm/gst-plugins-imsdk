/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstdlib>
#include <cstdint>
#include <string>

#include <GLES3/gl32.h>

namespace ib2c {

namespace gl {

class ShaderProgram {
 public:
  ShaderProgram(const std::string& vshader, const std::string& fshader);
  ShaderProgram(const std::string& cshader);

  ~ShaderProgram();

  void Use();

  void SetBool(const char* name, bool value) const;
  void SetInt(const char* name, int value) const;
  void SetFloat(const char* name, float value) const;
  void SetVec2(const char* name, float x, float y) const;
  void SetVec3(const char* name, float x, float y, float z) const;
  void SetVec4(const char* name, float x, float y, float z, float w) const;
  void SetMat4(const char* name, const float* matrix) const;

  bool HasVariable(const char* name) const;

  int GetAttribLocation(const char* name) const;

 private:
  /// GL program identification.
  GLuint id_;
};

} // namespace gl

} // namespace ib2c
