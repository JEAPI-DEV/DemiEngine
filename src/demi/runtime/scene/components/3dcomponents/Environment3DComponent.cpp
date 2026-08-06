#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {
void Environment3DComponent::parse(const nlohmann::json &json,
                                   Entity &entity) {
  Environment3DComponent component;
  if (auto value = scene_loading::colorField(json, "ambient_color"))
    component.ambientColor = *value;
  component.ambientIntensity = std::max(
      scene_loading::numberField(json, "ambient_intensity").value_or(0.5F),
      0.0F);
  if (auto value = scene_loading::colorField(json, "fog_color"))
    component.fogColor = *value;
  component.fogStart = std::max(
      scene_loading::numberField(json, "fog_start").value_or(80.0F), 0.0F);
  component.fogEnd =
      std::max(scene_loading::numberField(json, "fog_end").value_or(220.0F),
               component.fogStart + 0.001F);
  component.shadowDistance = std::max(
      scene_loading::numberField(json, "shadow_distance").value_or(80.0F),
      0.0F);
  component.shadowResolution = std::clamp(
      static_cast<int>(
          scene_loading::numberField(json, "shadow_resolution")
              .value_or(1024.0F)),
      128, 4096);
  component.maxShadowLights = std::clamp(
      static_cast<int>(
          scene_loading::numberField(json, "max_shadow_lights").value_or(1.0F)),
      0, 4);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
