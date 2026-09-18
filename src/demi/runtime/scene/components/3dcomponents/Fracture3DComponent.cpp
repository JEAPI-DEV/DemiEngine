#include "demi/runtime/scene/components/3dcomponents/Fracture3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace demi::runtime {
void Fracture3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Fracture3DComponent value;
  if (json.contains("pieces") && !json["pieces"].is_number_integer())
    throw std::invalid_argument("Fracture3D.pieces must be an integer");
  value.pieces = json.value("pieces", 8);
  value.collider = json.value("collider", "source");
  if ((value.collider != "source" && value.collider != "box") ||
      (value.collider == "box" && value.pieces != 1))
    throw std::invalid_argument("Fracture3D box proxy requires pieces=1; collider must be source or box");
  value.bondHealth = json.value("bond_health", 1.0F);
  if (json.contains("density") && !json["density"].is_null()) {
    value.density = json["density"].get<float>();
    if (!std::isfinite(*value.density) || *value.density < 0.001F ||
        *value.density > 1000000)
      throw std::invalid_argument("Fracture3D.density must be 0.001..1000000 kg/m^3");
  }
  if (value.pieces < 1 || value.pieces > 128 ||
      !std::isfinite(value.bondHealth) || value.bondHealth <= 0 ||
      value.bondHealth > 1e30F)
    throw std::invalid_argument("Invalid fracture piece count or bond health");
  if (json.contains("anchor_below") && !json["anchor_below"].is_null()) {
    value.anchorBelow = json["anchor_below"].get<float>();
    if (!std::isfinite(*value.anchorBelow))
      throw std::invalid_argument("Fracture3D.anchor_below must be finite");
  }
  if (json.contains("interior_color"))
    value.interiorColor = json["interior_color"].get<std::array<float, 4>>();
  for (float channel : value.interiorColor)
    if (!std::isfinite(channel) || channel < 0 || channel > 1)
      throw std::invalid_argument(
          "Fracture3D.interior_color requires normalized RGBA");
  value.interiorMaterial = json.value("interior_material", "");
  entity.setComponent(std::move(value));
}
nlohmann::json Fracture3DComponent::defaults() {
  return {{"pieces", 8},
          {"collider", "source"},
          {"bond_health", 1},
          {"anchor_below", nullptr},
          {"density", nullptr},
          {"interior_color", {0.35, 0.33, 0.30, 1}},
          {"interior_material", ""}};
}
} // namespace demi::runtime
