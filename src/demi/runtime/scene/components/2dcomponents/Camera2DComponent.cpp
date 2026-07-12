#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void Camera2DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Camera2DComponent component;
  if (auto value = scene_loading::colorField(json, "clear_color"))
    component.clearColor = *value;
  if (auto value = scene_loading::numberField(json, "orthographic_size"))
    component.orthographicSize = *value;
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
