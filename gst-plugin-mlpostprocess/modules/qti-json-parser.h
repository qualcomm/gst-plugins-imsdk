/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string.h>
#include <vector>

enum class JsonType {
  Null,
  Bool,
  Number,
  String,
  Array,
  Object
};

class JsonValue {
 public:
  using Ptr = std::shared_ptr<JsonValue>;

  JsonValue() : type(JsonType::Null) {}

  JsonValue(bool b) : type(JsonType::Bool), bool_value(b) {}

  JsonValue(double n) : type(JsonType::Number), number_value(n) {}

  JsonValue(const std::string& s)
      : type(JsonType::String), string_value(s) {}

  JsonValue(const char* s) : JsonValue(std::string(s)) {}

  JsonValue(const std::vector<Ptr>& a)
      : type(JsonType::Array), array_value(a) {}

  JsonValue(const std::map<std::string, Ptr>& o)
      : type(JsonType::Object), object_value(o) {}

  static Ptr Object() {

    return std::make_shared<JsonValue>(std::map<std::string, Ptr>{});
  }

  static Ptr Array() {

    return std::make_shared<JsonValue>(std::vector<Ptr>{});
  }

  void Put(const std::string& key, const std::string& val) {

    EnsureObject();
    object_value[key] = std::make_shared<JsonValue>(val);
  }

  void Put(const std::string& key, double val) {

    EnsureObject();
    object_value[key] = std::make_shared<JsonValue>(val);
  }

  void Put(const std::string& key, bool val) {

    EnsureObject();
    object_value[key] = std::make_shared<JsonValue>(val);
  }

  void Put(const std::string& key, const Ptr& val) {

    EnsureObject();
    object_value[key] = val;
  }

  void Add(const std::string& val) {

    EnsureArray();
    array_value.push_back(std::make_shared<JsonValue>(val));
  }

  void Add(double val) {

    EnsureArray();
    array_value.push_back(std::make_shared<JsonValue>(val));
  }

  void Add(bool val) {

    EnsureArray();
    array_value.push_back(std::make_shared<JsonValue>(val));
  }

  void Add(const Ptr& val) {

    EnsureArray();
    array_value.push_back(val);
  }

  static Ptr Parse(const std::string& input) {

    const char* s = input.c_str();
    s = SkipWhitespace(s);

    return ParseValue(s);
  }

  static std::string Stringify(const Ptr& value) {

    std::string out;
    StringifyValue(value, out);

    return out;
  }

  JsonType GetType() const {
    return type;
  }

  std::string GetString(const std::string& key) const {

    return AsObject().at(key)->AsString();
  }

  double GetNumber(const std::string& key) const {

    return AsObject().at(key)->AsNumber();
  }

  bool GetBool(const std::string& key) const {

    return AsObject().at(key)->AsBool();
  }

  std::vector<Ptr> GetArray(const std::string& key) const {

    return AsObject().at(key)->AsArray();
  }

  const std::map<std::string, Ptr>& GetObject() const {

    return AsObject();
  }

  const std::string& AsString() const {

    if (type != JsonType::String) throw std::runtime_error("Not a string");
    return string_value;
  }

  double AsNumber() const {

    if (type != JsonType::Number) throw std::runtime_error("Not a number");
    return number_value;
  }

  bool AsBool() const {

    if (type != JsonType::Bool) throw std::runtime_error("Not a bool");
    return bool_value;
  }

  const std::vector<Ptr>& AsArray() const {

    if (type != JsonType::Array) throw std::runtime_error("Not an array");
    return array_value;
  }

  const std::map<std::string, Ptr>& AsObject() const {

    if (type != JsonType::Object) throw std::runtime_error("Not an object");
    return object_value;
  }

 private:
  JsonType type;
  bool bool_value = false;
  double number_value = 0;
  std::string string_value;
  std::vector<Ptr> array_value;
  std::map<std::string, Ptr> object_value;

  void EnsureObject() {

    if (type != JsonType::Object) {
        type = JsonType::Object;
        object_value.clear();
    }
  }

  void EnsureArray() {

    if (type != JsonType::Array) {
        type = JsonType::Array;
        array_value.clear();
    }
  }

  static const char* SkipWhitespace(const char* s) {

    while (*s && std::isspace(*s)) ++s;
    return s;
  }

  static Ptr ParseValue(const char*& s) {

    s = SkipWhitespace(s);
    if (*s == '"')
      return ParseString(s);
    if (*s == '{')
      return ParseObject(s);
    if (*s == '[')
      return ParseArray(s);
    if (std::isdigit(*s) || *s == '-')
      return ParseNumber(s);
    if (strncmp(s, "true", 4) == 0 || strncmp(s, "false", 5) == 0)
      return ParseBool(s);
    if (strncmp(s, "null", 4) == 0)
      return ParseNull(s);

    return std::make_shared<JsonValue>();
  }

  static Ptr ParseString(const char*& s) {

    std::string result;

    ++s;
    while (*s && *s != '"') {
      if (*s == '\\') {
        ++s;
        switch (*s) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            default: result += *s; break;
        }
      } else {
        result += *s;
      }
      ++s;
    }
    ++s;

    return std::make_shared<JsonValue>(result);
  }

  static Ptr ParseNumber(const char*& s) {

    const char* start = s;

    if (*s == '-') ++s;

    while (std::isdigit(*s)) ++s;

    if (*s == '.') {
      ++s;
      while (std::isdigit(*s)) ++s;
    }

    if (*s == 'e' || *s == 'E') {
      ++s;
      if (*s == '+' || *s == '-') ++s;
      while (std::isdigit(*s)) ++s;
    }

    double value = std::strtod(start, nullptr);

    return std::make_shared<JsonValue>(value);
  }

  static Ptr ParseBool(const char*& s) {

    if (strncmp(s, "true", 4) == 0) {
      s += 4;
      return std::make_shared<JsonValue>(true);
    } else {
      s += 5;
      return std::make_shared<JsonValue>(false);
    }
  }

  static Ptr ParseNull(const char*& s) {

    s += 4;
    return std::make_shared<JsonValue>();
  }

  static Ptr ParseArray(const char*& s) {

    ++s;
    std::vector<Ptr> items;

    s = SkipWhitespace(s);
    if (*s == ']') {
      ++s;
      return std::make_shared<JsonValue>(items);
    }

    while (true) {
      Ptr value = ParseValue(s);

      items.push_back(value);
      s = SkipWhitespace(s);

      if (*s == ',') {
        ++s;
        s = SkipWhitespace(s);
        continue;
      } else if (*s == ']') {
        ++s;
        break;
      } else {
        throw std::runtime_error("Unexpected character in array");
      }
    }

    return std::make_shared<JsonValue>(items);
  }

  static Ptr ParseObject(const char*& s) {

    ++s;

    std::map<std::string, Ptr> obj;

    s = SkipWhitespace(s);

    if (*s == '}') {
      ++s;

      return std::make_shared<JsonValue>(obj);
    }
    while (true) {
      Ptr key = ParseString(s);
      s = SkipWhitespace(s);

      if (*s != ':') {
        throw std::runtime_error("Expected ':' after key in object");
      }
      ++s;

      Ptr value = ParseValue(s);

      obj[key->AsString()] = value;
      s = SkipWhitespace(s);

      if (*s == '}') {
        ++s;
        break;
      }

      if (*s != ',') {
        throw std::runtime_error("Expected ',' between object members");
      }

      ++s;

      s = SkipWhitespace(s);
    }

    return std::make_shared<JsonValue>(obj);
  }

  static void StringifyValue(const Ptr& value, std::string& out) {

    switch (value->type) {
      case JsonType::Null:
        out += "null";
        break;
      case JsonType::Bool:
        out += value->bool_value ? "true" : "false";
        break;
      case JsonType::Number: {
        std::ostringstream oss;

        oss << std::setprecision(15) << value->number_value;
        out += oss.str();
        break;
      }
      case JsonType::String:
        out += '"' + value->string_value + '"';
        break;
      case JsonType::Array:
        out += "[";

        for (size_t i = 0; i < value->array_value.size(); ++i) {
          if (i > 0) out += ", ";
          StringifyValue(value->array_value[i], out);
        }

        out += "]";
        break;
      case JsonType::Object:
        out += "{";

        for (auto it = value->object_value.begin(); it != value->object_value.end(); ++it) {
          if (it != value->object_value.begin()) {
            out += ", ";
          }

          out += '"' + it->first + "\": ";
          StringifyValue(it->second, out);
        }

        out += "}";
        break;
    }
  }
};
