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
  component.viewportX =
      std::clamp(scene_loading::numberField(json, "viewport_x").value_or(0.0F),
                 0.0F, 1.0F);
  component.viewportY =
      std::clamp(scene_loading::numberField(json, "viewport_y").value_or(0.0F),
                 0.0F, 1.0F);
  component.viewportWidth = std::clamp(
      scene_loading::numberField(json, "viewport_width").value_or(1.0F), 0.0F,
      1.0F);
  component.viewportHeight = std::clamp(
      scene_loading::numberField(json, "viewport_height").value_or(1.0F),
      0.0F, 1.0F);
  component.renderScale = std::clamp(
      scene_loading::numberField(json, "render_scale").value_or(1.0F), 0.25F,
      2.0F);
  component.priority = static_cast<int>(
      scene_loading::numberField(json, "priority").value_or(0.0F));
  component.primary =
      scene_loading::boolField(json, "primary").value_or(false);
  component.renderMask = scene_loading::stringOr(json, "render_mask");
  component.clearMode = scene_loading::stringOr(json, "clear_mode", "color");
  component.debugMode =
      scene_loading::stringOr(json, "debug_mode", "shaded");
  component.renderTarget = scene_loading::stringOr(json, "render_target");
  component.updateInterval = std::max(
      scene_loading::numberField(json, "update_interval").value_or(0.0F),
      0.0F);
  component.renderHudToTarget =
      scene_loading::boolField(json, "render_hud_to_target").value_or(false);
  component.renderHud =
      scene_loading::boolField(json, "render_hud").value_or(true);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
