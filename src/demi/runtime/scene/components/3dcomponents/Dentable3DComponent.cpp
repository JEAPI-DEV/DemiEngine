#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

namespace demi::runtime {
void Dentable3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Dentable3DComponent component;
  auto &m = component.material;
  m.radius = scene_loading::numberField(json, "radius").value_or(m.radius);
  m.yieldEnergy =
      scene_loading::numberField(json, "yield_energy").value_or(m.yieldEnergy);
  m.stiffness =
      scene_loading::numberField(json, "stiffness").value_or(m.stiffness);
  m.absorption =
      scene_loading::numberField(json, "absorption").value_or(m.absorption);
  m.maximumDepth =
      scene_loading::numberField(json, "max_depth").value_or(m.maximumDepth);
  entity.setComponent(std::move(component));
}
nlohmann::json Dentable3DComponent::defaults() {
  const MeshImpactMaterial3D m;
  return {{"radius", m.radius},
          {"yield_energy", m.yieldEnergy},
          {"stiffness", m.stiffness},
          {"absorption", m.absorption},
          {"max_depth", m.maximumDepth}};
}
} // namespace demi::runtime
