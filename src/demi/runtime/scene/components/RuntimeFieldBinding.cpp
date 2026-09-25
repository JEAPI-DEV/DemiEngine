#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace demi::runtime {

namespace {
bool runtimeFloatArray(std::initializer_list<float> values,
                       nlohmann::json &out) {
  auto result = nlohmann::json::array();
  for (float value : values) {
    nlohmann::json number;
    if (!runtimeFieldJson(value, number))
      return false;
    result.push_back(std::move(number));
  }
  out = std::move(result);
  return true;
}
} // namespace

bool runtimeFieldJson(bool value, nlohmann::json &out) {
  out = value;
  return true;
}
bool runtimeFieldJson(std::int64_t value, nlohmann::json &out) {
  out = value;
  return true;
}
bool runtimeFieldJson(std::uint64_t value, nlohmann::json &out) {
  out = value;
  return true;
}
bool runtimeFieldJson(float value, nlohmann::json &out) {
  if (!std::isfinite(value))
    return false;
  // Format at native float precision before storing JSON's double. Promoting
  // first exposes binary float noise (0.05F becomes 0.05000000074505806).
  // Shortest round-trip formatting also preserves subnormals and tiny values.
  char buffer[64];
  const auto formatted = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (formatted.ec != std::errc{})
    return false;
  double decimal;
  const auto parsed = std::from_chars(buffer, formatted.ptr, decimal);
  if (parsed.ec != std::errc{} || parsed.ptr != formatted.ptr)
    return false;
  out = decimal;
  return true;
}
bool runtimeFieldJson(double value, nlohmann::json &out) {
  if (!std::isfinite(value))
    return false;
  out = value;
  return true;
}
bool runtimeFieldJson(const std::string &value, nlohmann::json &out) {
  out = value;
  return true;
}
bool runtimeFieldJson(const Vec2 &value, nlohmann::json &out) {
  return runtimeFloatArray({value.x, value.y}, out);
}
bool runtimeFieldJson(const Vec3 &value, nlohmann::json &out) {
  return runtimeFloatArray({value.x, value.y, value.z}, out);
}
bool runtimeFieldJson(const Color &value, nlohmann::json &out) {
  return runtimeFloatArray({value.r, value.g, value.b, value.a}, out);
}
bool runtimeFieldJson(const std::vector<Vec2> &value, nlohmann::json &out) {
  out = nlohmann::json::array();
  for (const auto &point : value) {
    nlohmann::json item;
    if (!runtimeFieldJson(point, item))
      return false;
    out.push_back(std::move(item));
  }
  return true;
}
bool runtimeFieldJson(const std::vector<Vec3> &value, nlohmann::json &out) {
  out = nlohmann::json::array();
  for (const auto &point : value) {
    nlohmann::json item;
    if (!runtimeFieldJson(point, item))
      return false;
    out.push_back(std::move(item));
  }
  return true;
}

} // namespace demi::runtime
