#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
namespace demi::runtime {
void Camera3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Camera3DComponent component;
  if (auto value = scene_loading::colorField(json, "clear_color"))
    component.clearColor = *value;
  if (auto value = scene_loading::numberField(json, "fov"))
    component.fov = *value;
  if (auto value = scene_loading::numberField(json, "near_clip"))
    component.nearClip = std::max(*value, 0.001F);
  if (auto value = scene_loading::numberField(json, "far_clip"))
    component.farClip = std::max(*value, component.nearClip);
  if (auto value = scene_loading::numberField(json, "orthographic_size"))
    component.orthographicSize = *value;
  if (auto value = scene_loading::vec3Field(json, "target_offset"))
    component.targetOffset = *value;
  component.perspective =
      scene_loading::boolField(json, "perspective").value_or(true);
  if (auto value = scene_loading::numberField(json, "position_x"))
    component.positionX = *value;
  if (auto value = scene_loading::numberField(json, "up_axis"))
    component.upAxis = *value;
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
