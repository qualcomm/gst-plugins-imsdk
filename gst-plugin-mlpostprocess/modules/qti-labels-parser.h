/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once
#include "qti-json-parser.h"

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

struct Label {
  std::string name;
  uint32_t color;
};

class LabelsParser {
 public:
  bool LoadFromFile(const std::string& path);

  std::string GetLabel(int32_t idx) const;

  uint32_t GetColor(int32_t idx) const;

  int32_t Size() const {
    return labels.size();
  }

 private:
  std::map<int32_t, Label> labels;

  bool LoadPlainTextLabels(const std::string& path);

  bool LoadJsonLabels(const std::string& content);

  static bool ReadFile(const std::string& path, std::string& out);
};

bool LabelsParser::ReadFile(const std::string& path, std::string& out) {

  std::ifstream file(path);
  if (!file.is_open())
    return false;

  std::ostringstream ss;
  ss << file.rdbuf();
  out = ss.str();

  return true;
}

bool LabelsParser::LoadFromFile(const std::string& path) {

  std::string content;

  if (!ReadFile(path, content)) {
    std::cerr << "Failed to read file: " << path << std::endl;
    return false;
  }

  if (!LoadJsonLabels(content)) {
    std::cerr << "Falling back to plain text label parsing.\n";
    return LoadPlainTextLabels(path);
  }

  return true;
}

bool LabelsParser::LoadPlainTextLabels(const std::string& path) {

  std::ifstream file(path);
  if (!file.is_open())
    return false;

  std::string line;
  int32_t id = 0;

  while (std::getline(file, line)) {
    if (line.empty()) continue;
    labels[id++] = {line, 0x00FF00FF};
  }

  return true;
}

bool LabelsParser::LoadJsonLabels(const std::string& content) {

  auto root = JsonValue::Parse(content);
  if (!root || root->GetType() != JsonType::Array)
    return false;

  for (const auto& item : root->AsArray()) {
    if (!item || item->GetType() != JsonType::Object)
      return false;

    int32_t id = static_cast<int32_t>(item->GetNumber("id"));
    std::string name = item->GetString("label");
    std::string colorHex = item->GetString("color");
    uint32_t color =
        static_cast<uint32_t>(strtoul(colorHex.c_str(), nullptr, 16));

    labels[id] = {name, color};
  }

  return true;
}

std::string LabelsParser::GetLabel(int32_t idx) const {

  auto it = labels.find(idx);
  if (it != labels.end()) return it->second.name;

  return "unknown";
}

uint32_t LabelsParser::GetColor(int32_t idx) const {

  auto it = labels.find(idx);
  if (it != labels.end()) return it->second.color;

  return 0x0000000F;
}
