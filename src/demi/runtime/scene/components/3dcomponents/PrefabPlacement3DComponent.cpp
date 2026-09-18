#include "demi/runtime/scene/components/3dcomponents/PrefabPlacement3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <nlohmann/json.hpp>

namespace demi::runtime {
nlohmann::json PrefabPlacement3DComponent::defaults() {
  const PrefabPlacement3DComponent value;
  return {{"prefab", value.prefab},
          {"root", value.root},
          {"preserve", value.preserve}};
}
void PrefabPlacement3DComponent::parse(const nlohmann::json &json,
                                       Entity &entity) {
  PrefabPlacement3DComponent component;
  component.prefab = json.value("prefab", std::string{});
  component.root = json.value("root", std::string("assembly"));
  component.preserve = json.value("preserve", true);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
