#include "demi/runtime/scene/components/gameplay/GameplayDataComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

namespace demi::runtime {

bool GameplayDataComponent::serializeField(
    const GameplayDataComponent &component, std::string_view field,
    nlohmann::json &out) {
  if (field != "values")
    return false;
  out = nlohmann::json::parse(component.valuesJson);
  return true;
}

void GameplayDataComponent::parse(const nlohmann::json &json, Entity &entity) {
  GameplayDataComponent component;
  if (const auto *values = scene_loading::objectField(json, "values")) {
    component.valuesJson = values->dump();
  }
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
