#include "demi/runtime/scene/components/animation/AnimationCollision2DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {
namespace {

AnimationCollisionShape2D parseShape(const nlohmann::json &json) {
  AnimationCollisionShape2D shape;
  if (const auto offset = scene_loading::vec2Field(json, "offset"))
    shape.offset = *offset;
  if (const auto size = scene_loading::vec2Field(json, "size"))
    shape.size = *size;
  shape.layer = scene_loading::stringOr(json, "layer");
  return shape;
}

} // namespace

void AnimationCollision2DComponent::parse(const nlohmann::json &json,
                                          Entity &entity) {
  AnimationCollision2DComponent component;
  if (const auto *receivers = scene_loading::objectField(json, "receivers")) {
    for (const auto &[name, value] : receivers->items())
      if (value.is_object())
        component.receivers.emplace(name, parseShape(value));
  }
  if (const auto *windows = scene_loading::objectField(json, "windows")) {
    for (const auto &[state, value] : windows->items()) {
      if (!value.is_object())
        continue;
      const AnimationCollisionShape2D shape = parseShape(value);
      AnimationCollisionWindow2D window;
      window.offset = shape.offset;
      window.size = shape.size;
      window.layer = shape.layer;
      window.start = std::max(
          scene_loading::numberField(value, "start").value_or(0.0F), 0.0F);
      window.end =
          std::max(scene_loading::numberField(value, "end").value_or(0.1F),
                   window.start);
      window.mask = scene_loading::stringOr(value, "mask");
      component.windows.emplace(state, std::move(window));
    }
  }
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
