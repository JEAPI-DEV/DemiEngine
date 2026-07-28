#include "demi/runtime/scene/components/3dcomponents/PostProcessStackComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {
void PostProcessStackComponent::parse(const nlohmann::json &json,
                                      Entity &entity) {
  PostProcessStackComponent component;
  component.exposure =
      scene_loading::numberField(json, "exposure").value_or(0.0F);
  component.contrast = std::max(
      scene_loading::numberField(json, "contrast").value_or(1.0F), 0.0F);
  component.saturation = std::max(
      scene_loading::numberField(json, "saturation").value_or(1.0F), 0.0F);
  if (auto value = scene_loading::colorField(json, "tint"))
    component.tint = *value;
  component.vignette = std::clamp(
      scene_loading::numberField(json, "vignette").value_or(0.0F), 0.0F,
      1.0F);
  component.bloom = std::clamp(
      scene_loading::numberField(json, "bloom").value_or(0.0F), 0.0F, 2.0F);
  component.bloomThreshold = std::max(
      scene_loading::numberField(json, "bloom_threshold").value_or(1.0F),
      0.0F);
  if (auto value = scene_loading::colorField(json, "fade_color"))
    component.fadeColor = *value;
  component.fade = std::clamp(
      scene_loading::numberField(json, "fade").value_or(0.0F), 0.0F, 1.0F);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
