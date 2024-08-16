/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "runtime-flags-parser.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <cstdio>

#include <gst/gst.h>

#define ASCII_SPACE 32

class Platform {
protected:
  Platform();

  static std::shared_ptr<Platform> previous_instance_;

public:
  Platform(Platform &other) = delete;
  void operator=(const Platform &) = delete;

  static std::shared_ptr<Platform> GetInstance();

  std::string value_;
};

std::shared_ptr<Platform> Platform::previous_instance_ = nullptr;

std::shared_ptr<Platform> Platform::GetInstance() {
  if (nullptr == previous_instance_) {
    std::shared_ptr<Platform> new_instance(new Platform);

    previous_instance_ = new_instance;
  }

  return previous_instance_;
}

Platform::Platform() {
  std::ifstream f_machine("/sys/devices/soc0/machine");

  if (!f_machine.is_open()) {
    std::string err = "Failed to open file : /sys/devices/soc0/machine";
    throw std::runtime_error(err);
  }

  std::stringstream temp;
  temp << f_machine.rdbuf();

  value_ = temp.str();

  value_.erase(std::remove(value_.begin(), value_.end(), '\n'), value_.cend());
}

RuntimeFlagsParser::RuntimeFlagsParser(const std::string& plugin)
  : platform_(Platform::GetInstance()) {
  boolean_map_ = {
    { "TRUE",  true  },
    { "FALSE", false },
    { "ON",    true  },
    { "OFF",   false }
  };

  std::shared_ptr<JsonParser> glib_parser(json_parser_new (), &g_object_unref);

  parser_ = glib_parser;

  std::stringstream full_path_to_runtime_flags;

  full_path_to_runtime_flags << "/opt/qti/runtime_flags/" << platform_->value_
      << "_runtime_flags.json";

  GError* error = NULL;

  gboolean success = json_parser_load_from_file (parser_.get(),
      full_path_to_runtime_flags.str().c_str(), &error);

  if (!success) {
    std::string err_message = error->message;
    g_error_free (error);

    throw std::runtime_error(err_message);
  }

  SetPlugin(plugin);
}

std::string RuntimeFlagsParser::ToUpper(std::string other) {
  other.erase(std::remove(other.begin(), other.end(), ASCII_SPACE),
      other.end());

  for (auto &&character : other) {
    character = std::toupper(character);
  }

  return other;
}

std::variant<int, float, bool, const gchar *>
RuntimeFlagsParser::GetFlag(const std::string& key) {
  const gchar * string_member = json_object_get_string_member_with_default (
      plugin_content_, key.c_str(), "FALSE");

  std::string value = string_member;

  if (boolean_map_.contains(ToUpper(value))) {
    return boolean_map_.at(ToUpper(value));
  }

  try {
    if (std::string::npos != value.find(".")) {
      return std::stof(value);
    }

    return std::stoi(value);

  } catch(const std::exception& e) {
    return string_member;
  }
}

void RuntimeFlagsParser::SetPlugin(const std::string& plugin) {
  // The returned node is owned by the JsonParser and should never be modified or freed.
  JsonNode* root = json_parser_get_root (parser_.get());

  JsonObject* whole_content = json_node_get_object (root);

  plugin_content_ = json_object_get_object_member (whole_content,
      plugin.c_str());
}

const std::string& RuntimeFlagsParser::GetPlatform() {
  return platform_->value_;
}
