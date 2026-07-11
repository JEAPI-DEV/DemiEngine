#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void DirectionalLightComponent::parse(const nlohmann::json &json,
                                      Entity &entity) {
  DirectionalLightComponent component;
  if (auto value = scene_loading::vec3Field(json, "direction"))
    component.direction = *value;
  if (auto value = scene_loading::colorField(json, "color"))
    component.color = *value;
  if (auto value = scene_loading::numberField(json, "intensity"))
    component.intensity = *value;
  entity.directionalLight = component;
}
} // namespace demi::runtime
