#include "demi/runtime/scene/components/3dcomponents/FractureDebris3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace demi::runtime {
nlohmann::json FractureDebris3DComponent::defaults() {
  return {{"count", 12},
          {"max_fragments", 128},
          {"lifetime", 3},
          {"fade_duration", 1},
          {"size", {.12, .05, .08}},
          {"speed", 2},
          {"spin", 180},
          {"gravity", {0, -9.81, 0}},
          {"color", {.65, .45, .3, 1}},
          {"seed", 1},
          {"model", ""},
          {"texture", ""},
          {"render_layer", ""}};
}
void FractureDebris3DComponent::parse(const nlohmann::json &j, Entity &entity) {
  FractureDebris3DComponent value;
  const auto number = [&](const char *key, float fallback) {
    const float n = j.value(key, fallback);
    if (!std::isfinite(n) || n < 0)
      throw std::invalid_argument(std::string("FractureDebris3D.") + key +
                                  " must be finite and nonnegative");
    return n;
  };
  const auto integer = [&](const char *key, std::int64_t fallback,
                           std::int64_t maximum) {
    if (j.contains(key) && !j[key].is_number_integer())
      throw std::invalid_argument(std::string(key) + " must be an integer");
    const auto n = j.value(key, fallback);
    if (n < 0 || n > maximum)
      throw std::invalid_argument(std::string(key) +
                                  " exceeds its integer representation");
    return n;
  };
  value.count = static_cast<int>(integer("count", value.count, INT32_MAX));
  value.maxFragments =
      static_cast<int>(integer("max_fragments", value.maxFragments, INT32_MAX));
  value.seed =
      static_cast<std::uint32_t>(integer("seed", value.seed, UINT32_MAX));
  value.lifetime = number("lifetime", value.lifetime);
  value.fadeDuration = number("fade_duration", value.fadeDuration);
  value.speed = number("speed", value.speed);
  value.spin = number("spin", value.spin);
  const auto vector = [&](const char *key, Vec3 &v, bool positive) {
    if (j.contains(key)) {
      const auto a = j.at(key).get<std::array<float, 3>>();
      v = {a[0], a[1], a[2]};
    }
    for (float n : {v.x, v.y, v.z})
      if (!std::isfinite(n) || (positive && n <= 0))
        throw std::invalid_argument(std::string("Invalid debris ") + key);
  };
  vector("size", value.size, true);
  vector("gravity", value.gravity, false);
  if (auto color = scene_loading::colorField(j, "color"))
    value.color = *color;
  value.model = j.value("model", std::string{});
  value.texture = j.value("texture", std::string{});
  value.renderLayer = j.value("render_layer", std::string{});
  entity.setComponent(value);
}
} // namespace demi::runtime
