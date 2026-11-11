#pragma once
// Type-tolerant accessors for untrusted JSON. nlohmann's value() only falls back to the
// default when a key is *missing*; a present-but-null or wrongly typed field throws.
#include <string>

#include <nlohmann/json.hpp>

namespace mg::js {
using nlohmann::json;

inline const json& empty_obj() { static const json j = json::object(); return j; }
inline const json& empty_arr() { static const json j = json::array(); return j; }

inline const json& obj(const json& j, const char* key) {
  if (!j.is_object()) return empty_obj();
  auto it = j.find(key);
  return it != j.end() && it->is_object() ? *it : empty_obj();
}

inline const json& arr(const json& j, const char* key) {
  if (!j.is_object()) return empty_arr();
  auto it = j.find(key);
  return it != j.end() && it->is_array() ? *it : empty_arr();
}

// Strings as-is, numbers/bools stringified, null/missing/containers -> def.
inline std::string str(const json& j, const char* key, const std::string& def = "") {
  if (!j.is_object()) return def;
  auto it = j.find(key);
  if (it == j.end()) return def;
  if (it->is_string()) return it->get<std::string>();
  if (it->is_number_integer()) return std::to_string(it->get<long long>());
  if (it->is_number() || it->is_boolean()) return it->dump();
  return def;
}

inline long long num(const json& j, const char* key, long long def = 0) {
  if (!j.is_object()) return def;
  auto it = j.find(key);
  if (it == j.end()) return def;
  if (it->is_number_integer()) return it->get<long long>();
  if (it->is_number_float()) return static_cast<long long>(it->get<double>());
  if (it->is_string()) {
    try { return std::stoll(it->get<std::string>()); } catch (...) { return def; }
  }
  return def;
}

}  // namespace mg::js
