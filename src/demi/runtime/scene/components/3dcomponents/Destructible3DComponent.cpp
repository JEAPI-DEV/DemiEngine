#include "demi/runtime/scene/components/3dcomponents/Destructible3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <set>
#include <stdexcept>
#include <limits>

namespace demi::runtime {
void Destructible3DComponent::parse(const nlohmann::json &json,
                                    Entity &entity) {
  Destructible3DComponent value;
  value.energyPerHealth = json.value("energy_per_health", 1000.0F);
  if (!std::isfinite(value.energyPerHealth) || value.energyPerHealth < 0.000001F || value.energyPerHealth > 1e12F)
    throw std::invalid_argument("Destructible3D.energy_per_health must be 0.000001..1e12 joules");
  if ((json.contains("seed") && !json["seed"].is_number_integer()) ||
      (json.contains("generator_version") && !json["generator_version"].is_number_integer()))
    throw std::invalid_argument("Destructible3D seed and generator_version must be integers");
  const auto seed = json.value("seed", std::int64_t(1));
  if (seed < 0 || seed > UINT32_MAX || json.value("generator_version", 1) != 1)
    throw std::invalid_argument("Invalid Destructible3D seed or generator version");
  value.seed = static_cast<std::uint32_t>(seed);
  const auto parts = json.value("parts", nlohmann::json::object());
  if (!parts.is_object() || parts.size() >= UINT32_MAX)
    throw std::invalid_argument(
        "Destructible3D.parts requires an object of part-to-visual entity IDs");
  std::set<std::string> visuals;
  for (const auto &[part, visual] : parts.items()) {
    if (part.empty() || !visual.is_string() ||
        visual.get<std::string>().empty() ||
        !visuals.insert(visual.get<std::string>()).second)
      throw std::invalid_argument(
          "Destructible3D visual IDs must be nonempty and unique");
    value.parts.emplace(part, visual.get<std::string>());
  }
  if (json.contains("max_bodies") && !json["max_bodies"].is_number())
    throw std::invalid_argument("Destructible3D.max_bodies must be an integer");
  const double limit = json.value("max_bodies", 64.0);
  if (!std::isfinite(limit) || limit < 1 || limit > std::numeric_limits<int>::max() || std::trunc(limit) != limit)
    throw std::invalid_argument("Destructible3D.max_bodies must be a positive 32-bit integer");
  value.maxBodies = static_cast<int>(limit);
  if (json.contains("deferred_visuals")) {
    const auto &groups=json["deferred_visuals"];
    if (!groups.is_object()) throw std::invalid_argument("Deferred visuals must be region-to-template arrays");
    std::set<std::string> ids;
    for (const auto &[region,templates]:groups.items()) {
      if (region.empty() || !templates.is_array() || templates.empty())
        throw std::invalid_argument("Invalid deferred visual region");
      for (const auto &item:templates) {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || item["id"].get<std::string>().empty() ||
            !item.contains("components") || !item["components"].is_object() ||
            !ids.insert(item["id"].get<std::string>()).second)
          throw std::invalid_argument("Invalid or duplicate deferred visual template");
        for (const auto &[name,component]:item["components"].items())
          if (name!="Transform3D" && name!="MeshRenderer" && name!="SurfaceRelief3D")
            throw std::invalid_argument("Deferred visual templates may contain only render and transform components");
      }
    }
    value.deferredVisuals=std::make_shared<const nlohmann::json>(groups);
  }
  auto &parsed=value;
  const auto fading=json.value("fading_parts",nlohmann::json::object());
  if(!fading.is_object()) throw std::invalid_argument("fading_parts must be an object");
  for(const auto &[part,settings]:fading.items()) {
    const float lifetime=settings.at("lifetime").get<float>(), fade=settings.at("fade").get<float>();
    if(!parsed.parts.contains(part) || !std::isfinite(lifetime) || lifetime<=0 || !std::isfinite(fade) || fade<0)
      throw std::invalid_argument("Invalid fading part");
    parsed.fadingParts.emplace(part,std::pair{lifetime,fade});
  }
  entity.setComponent(std::move(value));
}
nlohmann::json Destructible3DComponent::defaults() {
  return {{"parts", nlohmann::json::object()}, {"fading_parts", nlohmann::json::object()}, {"deferred_visuals", nlohmann::json::object()}, {"max_bodies", 64}, {"energy_per_health", 1000}, {"seed", 1}, {"generator_version", 1}};
}
} // namespace demi::runtime
