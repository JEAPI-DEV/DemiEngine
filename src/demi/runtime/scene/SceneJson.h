#pragma once

#include "demi/runtime/scene/SceneData.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace demi::runtime::scene_loading {

using Json = nlohmann::json;

[[nodiscard]] std::optional<Json> readJsonFile(const std::filesystem::path& path, std::string& error);

[[nodiscard]] std::string stringOr(const Json& object, const char* key, std::string fallback = {});
[[nodiscard]] std::optional<std::string> stringField(const Json& object, const char* key);
[[nodiscard]] std::optional<float> numberField(const Json& object, const char* key);
[[nodiscard]] std::optional<bool> boolField(const Json& object, const char* key);
[[nodiscard]] std::optional<Vec2> vec2Field(const Json& object, const char* key);
[[nodiscard]] std::optional<Vec3> vec3Field(const Json& object, const char* key);
[[nodiscard]] std::optional<Color> colorField(const Json& object, const char* key);
[[nodiscard]] const Json* objectField(const Json& object, const char* key);
[[nodiscard]] const Json* arrayField(const Json& object, const char* key);

} // namespace demi::runtime::scene_loading
