#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void IsoTransformComponent::parse(const nlohmann::json &json, Entity &entity) {
  IsoTransformComponent component;
  if (auto value = scene_loading::vec2Field(json, "tile"))
    component.tile = *value;
  if (auto value = scene_loading::numberField(json, "height"))
    component.height = *value;
  if (auto value = scene_loading::vec2Field(json, "footprint"))
    component.footprint = *value;
  entity.isoTransform = component;
}
} // namespace demi::runtime
