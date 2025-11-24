/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <memory>

#include <json-glib/json-glib.h>

class Platform;

class RuntimeFlagsParser {
public:
  RuntimeFlagsParser() = delete;
  explicit RuntimeFlagsParser(const std::string& plugin);

  const std::string& GetPlatform();

  std::variant<int, float, bool, const gchar *> GetFlag(const std::string& key);

private:
  void SetPlugin(const std::string& plugin);

  std::string ToUpper(std::string other);

  std::shared_ptr<Platform> platform_;

  std::shared_ptr<JsonParser> parser_;
  JsonObject* plugin_content_;

  std::unordered_map<std::string, bool> boolean_map_;
};
