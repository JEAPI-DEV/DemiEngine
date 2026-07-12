#include "demi/runtime/scene/components/gameplay/GameplayDataComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

namespace demi::runtime {

void GameplayDataComponent::parse(const nlohmann::json &json, Entity &entity) {
  GameplayDataComponent component;
  if (const auto *values = scene_loading::objectField(json, "values")) {
    component.valuesJson = values->dump();
  }
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
