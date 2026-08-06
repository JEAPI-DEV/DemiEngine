#include "demi/runtime/ui/UiTweenSystem.h"
#include "demi/runtime/ui/UiMutationQueue.h"
#include "demi/runtime/ui/UiStateController.h"
#include <algorithm>
#include <cmath>
namespace demi::runtime::ui {
namespace {
float read(const UiNode &node, UiTweenProperty property) {
  switch (property) {
  case UiTweenProperty::Opacity: return node.color.a;
  case UiTweenProperty::PositionX: return node.layout.position.x;
  case UiTweenProperty::PositionY: return node.layout.position.y;
  case UiTweenProperty::Scale: return node.scale;
  }
  return 0.0F;
}
void write(UiNode &node, UiTweenProperty property, float value) {
  switch (property) {
  case UiTweenProperty::Opacity:
    value = std::clamp(value, 0.0F, 1.0F);
    node.color.a = node.backgroundColor.a = node.borderColor.a =
        node.hoverColor.a = node.textColor.a = value;
    break;
  case UiTweenProperty::PositionX: node.layout.position.x = value; break;
  case UiTweenProperty::PositionY: node.layout.position.y = value; break;
  case UiTweenProperty::Scale: node.scale = std::max(value, 0.0F); break;
  }
}
}
UiTweenHandle UiTweenSystem::start(UiDocument &document, UiNodeHandle node,
                                   UiTweenProperty property, float target,
                                   float duration) {
  if (!UiMutationQueue::alive(document, node) || !std::isfinite(target) ||
      !std::isfinite(duration)) return {};
  UiNode *value = UiStateController{}.find(document, node.id);
  const UiTweenHandle handle{next_++};
  if (reducedMotion_ || duration <= 0.0F) {
    write(*value, property, target);
    return handle;
  }
  std::erase_if(tweens_, [&](const Tween &tween) {
    return tween.node == node && tween.property == property;
  });
  tweens_.push_back({handle, std::move(node), property, read(*value, property),
                     target, duration});
  return handle;
}
bool UiTweenSystem::cancel(UiTweenHandle handle) {
  const auto before = tweens_.size();
  std::erase_if(tweens_, [&](const Tween &value) { return value.handle.value == handle.value; });
  return before != tweens_.size();
}
void UiTweenSystem::update(UiDocument &document, float dt) {
  if (!std::isfinite(dt) || dt < 0.0F) return;
  for (auto &tween : tweens_) {
    if (!UiMutationQueue::alive(document, tween.node)) continue;
    tween.elapsed += dt;
    const float progress = reducedMotion_ ? 1.0F : std::clamp(tween.elapsed / tween.duration, 0.0F, 1.0F);
    write(*UiStateController{}.find(document, tween.node.id), tween.property,
          tween.from + (tween.to - tween.from) * progress);
  }
  std::erase_if(tweens_, [&](const Tween &tween) {
    return !UiMutationQueue::alive(document, tween.node) || reducedMotion_ ||
           tween.elapsed >= tween.duration;
  });
}
} // namespace demi::runtime::ui
