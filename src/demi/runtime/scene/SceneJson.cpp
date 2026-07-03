#include "demi/runtime/scene/SceneJson.h"

#include <fstream>

namespace demi::runtime::scene_loading {

std::optional<Json> readJsonFile(const std::filesystem::path& path, std::string& error) {
  std::ifstream input(path);
  if (!input) {
    error = "Failed to read file: " + path.string();
    return std::nullopt;
  }

  try {
    return Json::parse(input);
  } catch (const Json::parse_error& parseError) {
    error = "Failed to parse JSON file " + path.string() + ": " + parseError.what();
    return std::nullopt;
  }
}

std::optional<std::string> stringField(const Json& object, const char* key) {
  if (!object.is_object()) {
    return std::nullopt;
  }
  const auto iter = object.find(key);
  if (iter == object.end() || !iter->is_string()) {
    return std::nullopt;
  }
  return iter->get<std::string>();
}

std::string stringOr(const Json& object, const char* key, std::string fallback) {
  return stringField(object, key).value_or(std::move(fallback));
}

std::optional<float> numberField(const Json& object, const char* key) {
  if (!object.is_object()) {
    return std::nullopt;
  }
  const auto iter = object.find(key);
  if (iter == object.end() || !iter->is_number()) {
    return std::nullopt;
  }
  return static_cast<float>(iter->get<double>());
}

std::optional<bool> boolField(const Json& object, const char* key) {
  if (!object.is_object()) {
    return std::nullopt;
  }
  const auto iter = object.find(key);
  if (iter == object.end() || !iter->is_boolean()) {
    return std::nullopt;
  }
  return iter->get<bool>();
}

const Json* objectField(const Json& object, const char* key) {
  if (!object.is_object()) {
    return nullptr;
  }
  const auto iter = object.find(key);
  if (iter == object.end() || !iter->is_object()) {
    return nullptr;
  }
  return &*iter;
}

const Json* arrayField(const Json& object, const char* key) {
  if (!object.is_object()) {
    return nullptr;
  }
  const auto iter = object.find(key);
  if (iter == object.end() || !iter->is_array()) {
    return nullptr;
  }
  return &*iter;
}

template <std::size_t Count>
std::optional<std::array<float, Count>> numberArrayField(const Json& object, const char* key) {
  const Json* array = arrayField(object, key);
  if (array == nullptr || array->size() < Count) {
    return std::nullopt;
  }

  std::array<float, Count> values{};
  for (std::size_t index = 0; index < Count; ++index) {
    if (!(*array)[index].is_number()) {
      return std::nullopt;
    }
    values[index] = static_cast<float>((*array)[index].get<double>());
  }
  return values;
}

std::optional<Vec2> vec2Field(const Json& object, const char* key) {
  const std::optional<std::array<float, 2>> values = numberArrayField<2>(object, key);
  if (!values.has_value()) {
    return std::nullopt;
  }
  return Vec2{.x = (*values)[0], .y = (*values)[1]};
}

std::optional<Vec3> vec3Field(const Json& object, const char* key) {
  const std::optional<std::array<float, 3>> values = numberArrayField<3>(object, key);
  if (!values.has_value()) {
    return std::nullopt;
  }
  return Vec3{.x = (*values)[0], .y = (*values)[1], .z = (*values)[2]};
}

std::optional<Color> colorField(const Json& object, const char* key) {
  const std::optional<std::array<float, 4>> values = numberArrayField<4>(object, key);
  if (!values.has_value()) {
    return std::nullopt;
  }
  return Color{.r = (*values)[0], .g = (*values)[1], .b = (*values)[2], .a = (*values)[3]};
}

} // namespace demi::runtime::scene_loading
