#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {

void Tilemap2DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Tilemap2DComponent component;
  component.asset = scene_loading::stringOr(json, "asset");
  component.pixelsPerUnit = std::max(
      scene_loading::numberField(json, "pixels_per_unit").value_or(16.0F),
      0.01F);
  component.layer = scene_loading::stringOr(json, "layer");
  component.sortingOrder = static_cast<int>(
      scene_loading::numberField(json, "sorting_order").value_or(0.0F));
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
