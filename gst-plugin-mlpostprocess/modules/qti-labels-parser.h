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

const uint32_t colors[] = {
  0x5548f8ff, 0xa515beff, 0x2dc305ff, 0x61458dff, 0x042547ff, 0x89561cff,
  0x8c1e2fff, 0xe44999ff, 0xaa9310ff, 0x09bf77ff, 0xafd032ff, 0x9638c3ff,
  0x943e08ff, 0x386136ff, 0x4110fbff, 0x02d97cff, 0xc67c67ff, 0x9d84e3ff,
  0x886350ff, 0xe31f15ff, 0xbf6989ff, 0x662f8eff, 0x268a06ff, 0x8a743dff,
  0xc78f49ff, 0xbcbc6dff, 0x242b25ff, 0xc953a5ff, 0x7d710cff, 0x4d150bff,
  0x95394cff, 0x782907ff, 0x87f257ff, 0x20a9fbff, 0x7dd89bff, 0x3e2097ff,
  0xe5e002ff, 0xeb3353ff, 0x101681ff, 0x5467dbff, 0x520f53ff, 0xe2a4afff,
  0x295e74ff, 0x43d4e3ff, 0xe1ae0dff, 0x3d2e5dff, 0x883a17ff, 0x7e42d8ff,
  0xfb04a4ff, 0xf04c61ff
};

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
    std::cout << "Falling back to plain text label parsing.\n";
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
  uint32_t n_colours = sizeof(colors) / sizeof(colors[0]);

  while (std::getline(file, line)) {
    if (line.empty()) continue;

    // Check if labels are supported
    if (line.find("(structure)") != std::string::npos) {
      std::cerr << "Deprecated labels format detected. "
                   "Please use community format (one label per line) or JSON.\n";
      return false;
    }

    labels[id++] = {line, colors[id % n_colours]};
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
