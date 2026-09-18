#include "demi/runtime/scene/components/3dcomponents/Masonry3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace demi::runtime {
void Masonry3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Masonry3DComponent value;
  if (json.contains("size")) {
    const auto size = json.at("size").get<std::array<float, 3>>();
    value.size = {size[0], size[1], size[2]};
  }
  for (float axis : {value.size.x, value.size.y, value.size.z})
    if (!std::isfinite(axis) || axis <= 0 || axis > 1000)
      throw std::invalid_argument(
          "Masonry3D size must be positive and at most 1000 m per axis");
  for (const char *key : {"columns", "rows"})
    if (json.contains(key) && !json[key].is_number_integer())
      throw std::invalid_argument("Masonry3D cell counts must be integers");
  value.columns = json.value("columns", 4);
  value.rows = json.value("rows", 10);
  if (value.columns < 1 || value.columns > 64 || value.rows < 1 ||
      value.rows > 128 || value.columns * value.rows > 256)
    throw std::invalid_argument(
        "Masonry3D supports at most 256 cells per region");
  if (json.contains("models") &&
      (!json["models"].is_object() || json["models"].size() > 32))
    throw std::invalid_argument(
        "Masonry3D supports at most 32 shared model variants");
  value.models = json.value("models", value.models);
  for (const auto &[key, model] : value.models)
    if (key.empty() || !model.starts_with("asset://") || model.size() <= 8)
      throw std::invalid_argument(
          "Masonry3D models require named asset references");
  value.texture = json.value("texture", "");
  value.heightMap = json.value("height_map", "");
  value.reliefDepth = json.value("relief_depth", .012F);
  if (json.contains("texture_grid")) {
    const auto grid = json["texture_grid"].get<std::array<float, 2>>();
    value.textureGrid = {grid[0], grid[1]};
  }
  for (float n : {value.textureGrid.x, value.textureGrid.y})
    if (!std::isfinite(n) || n < 1 || n > 64 || std::floor(n) != n)
      throw std::invalid_argument(
          "Masonry texture grid requires integers in 1..64");
  if (!std::isfinite(value.reliefDepth) || value.reliefDepth < 0 ||
      value.reliefDepth > 1 ||
      (!value.heightMap.empty() &&
       (!value.heightMap.starts_with("asset://") ||
        value.heightMap.size() <= 8 || !value.models.empty() ||
        value.reliefDepth > value.size.z * .45F)))
    throw std::invalid_argument(
        "Masonry height map requires a valid texture reference, no model "
        "variants, and relief inside the box");
  value.density = json.value("density", 1800.F);
  value.bondHealth = json.value("bond_health", .5F);
  if (!std::isfinite(value.density) || value.density < .001F ||
      value.density > 1000000 || !std::isfinite(value.bondHealth) ||
      value.bondHealth < .000001F || value.bondHealth > 1e30F)
    throw std::invalid_argument("Invalid Masonry3D density or bond health");
  if (json.contains("anchor_below") && !json["anchor_below"].is_null()) {
    value.anchorBelow = json["anchor_below"].get<float>();
    if (!std::isfinite(*value.anchorBelow))
      throw std::invalid_argument("Masonry3D anchor_below must be finite");
  }
  entity.setComponent(std::move(value));
}
nlohmann::json Masonry3DComponent::defaults() {
  return {{"size", {1, 1, .115}},
          {"columns", 4},
          {"height_map", ""},
          {"relief_depth", .012},
          {"texture_grid", {8, 14}},
          {"rows", 10},
          {"models", nlohmann::json::object()},
          {"texture", ""},
          {"density", 1800},
          {"bond_health", .5},
          {"anchor_below", nullptr}};
}
} // namespace demi::runtime
