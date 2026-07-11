#include "demi/runtime/scene/components/gameplay/BuildableComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void BuildableComponent::parse(const nlohmann::json &json, Entity &entity) {
  BuildableComponent component;
  component.asset = scene_loading::stringOr(json, "asset");
  component.blocksMovement =
      scene_loading::boolField(json, "blocks_movement").value_or(false);
  entity.buildable = component;
}
} // namespace demi::runtime
