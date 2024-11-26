/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "runtime-flags-parser.h"
#include "runtime-flags-parser-c-api.h"

extern "C" {

void* init_runtime_flags_parser (const char* plugin) {
  std::string plugin_str = plugin;

  RuntimeFlagsParser* obj = new RuntimeFlagsParser(plugin_str);

  return reinterpret_cast<void *>(obj);
}

void deinit_runtime_flags_parser (void* object) {
  RuntimeFlagsParser* obj = reinterpret_cast<RuntimeFlagsParser *>(object);

  if (obj != NULL)
    delete obj;
}

char* get_platform (void* object) {
  RuntimeFlagsParser* obj = reinterpret_cast<RuntimeFlagsParser *>(object);
  const std::string& reference_platform = obj->GetPlatform();

  return (char *)reference_platform.c_str();
}

const gchar * get_flag_as_string (void* object, const char* key) {
  RuntimeFlagsParser* obj = reinterpret_cast<RuntimeFlagsParser *>(object);
  std::string key_string = key;

  auto flag = obj->GetFlag(key_string);

  return std::get<const gchar *>(flag);
}

gboolean get_flag_as_bool (void* object, const char* key) {
  RuntimeFlagsParser* obj = reinterpret_cast<RuntimeFlagsParser *>(object);
  std::string key_string = key;

  auto flag = obj->GetFlag(key_string);
  gboolean result = static_cast<gboolean>(std::get<bool>(flag));

  return result;
}

float get_flag_as_float (void* object, const char* key) {
  RuntimeFlagsParser* obj = reinterpret_cast<RuntimeFlagsParser *>(object);
  std::string key_string = key;

  auto flag = obj->GetFlag(key_string);
  float result = std::get<float>(flag);

  return result;
}

int get_flag_as_int (void* object, const char* key) {
  RuntimeFlagsParser* obj = reinterpret_cast<RuntimeFlagsParser *>(object);
  std::string key_string = key;

  auto flag = obj->GetFlag(key_string);
  int result = std::get<int>(flag);

  return result;
}

static void* qmmfsrc_parser = nullptr;

void* get_qmmfsrc_parser () {
  return qmmfsrc_parser;
}

static __attribute__((constructor))
void InitQmmfSrcParser(void) {
  qmmfsrc_parser = init_runtime_flags_parser ("gst_plugin_qmmfsrc");
}

static __attribute__((destructor))
void DeinitQmmfSrcParser(void) {
  deinit_runtime_flags_parser (qmmfsrc_parser);
}

} // extern "C"
