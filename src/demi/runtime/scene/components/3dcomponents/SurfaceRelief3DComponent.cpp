#include "demi/runtime/scene/components/3dcomponents/SurfaceRelief3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace demi::runtime {
void SurfaceRelief3DComponent::parse(const nlohmann::json &j, Entity &entity) {
  SurfaceRelief3DComponent value;
  value.heightMap = j.at("height_map").get<std::string>();
  value.depth = j.value("depth", .012F);
  value.heightMin = j.value("height_min", .45F);
  value.heightMax = j.value("height_max", .9F);
  auto read = [&](const char *name, Vec2 &v) {
    if (j.contains(name)) {
      const auto a = j.at(name).get<std::array<float, 2>>();
      v = {a[0], a[1]};
    }
    if (!std::isfinite(v.x) || !std::isfinite(v.y))
      throw std::invalid_argument("Invalid relief vector");
  };
  read("uv_offset", value.uvOffset);
  read("uv_scale", value.uvScale);
  read("segments", value.segments);
  read("tiles", value.tiles);
  read("atlas_grid", value.atlasGrid);
  for (float n : {value.tiles.x, value.tiles.y, value.atlasGrid.x, value.atlasGrid.y})
    if (n < 1 || n >= float(INT32_MAX) || std::floor(n) != n)
      throw std::invalid_argument("Relief tile and atlas dimensions must be positive integers");
  if ((!value.heightMap.empty() && (!value.heightMap.starts_with("asset://") || value.heightMap.size() <= 8)) ||
      !std::isfinite(value.depth) || value.depth < 0 || value.depth > 1 ||
      !std::isfinite(value.heightMin) || !std::isfinite(value.heightMax) ||
      value.heightMin < 0 || value.heightMax > 1 ||
      value.heightMin >= value.heightMax || value.uvOffset.x < 0 ||
      value.uvOffset.y < 0 || value.uvScale.x <= 0 || value.uvScale.y <= 0 ||
      value.uvOffset.x + value.uvScale.x > 1.00001F ||
      value.uvOffset.y + value.uvScale.y > 1.00001F)
    throw std::invalid_argument("Invalid SurfaceRelief3D height/UV range");
  for (float n : {value.segments.x, value.segments.y})
    if (n < 1 || n > 64 || std::floor(n) != n)
      throw std::invalid_argument("Relief segments must be integers in 1..64");
  entity.setComponent(std::move(value));
}
nlohmann::json SurfaceRelief3DComponent::defaults() {
  return {{"height_map", ""},    {"depth", .012},       {"height_min", .45},
          {"height_max", .9},    {"uv_offset", {0, 0}}, {"uv_scale", {1, 1}},
          {"segments", {24, 10}}, {"tiles", {1, 1}}, {"atlas_grid", {1, 1}}};
}
} // namespace demi::runtime
