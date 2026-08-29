#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/ui/UiInteractionController.h"
#include "demi/runtime/ui/UiLayoutEngine.h"
#include "demi/runtime/ui/UiLocalization.h"
#include "demi/runtime/ui/UiMutationQueue.h"
#include "demi/runtime/ui/UiStateController.h"
#include "demi/runtime/ui/UiVariables.h"

#include <algorithm>

namespace demi::runtime {
namespace {

void relayoutHud(World &world) {
  ui::UiLayoutEngine{}.layout(world.ui, world.ui.canvasSize);
}

void discardInvalidRecyclers(World &world) {
  std::erase_if(world.uiVirtualRecyclers, [&](auto &entry) {
    if (entry.second->valid(world.ui))
      return false;
    entry.second->clear(world.ui, world.uiTweens);
    return true;
  });
}

} // namespace

bool LuaScriptHost::setHudText(const std::string &id, const std::string &text) {
  if (world_ == nullptr)
    return false;
  if (ui::UiStateController{}.setText(world_->ui, id, text))
    return true;
  return false;
}

bool LuaScriptHost::setHudVariable(const std::string &name,
                                   const std::string &value,
                                   std::string &error) {
  return world_ != nullptr &&
         ui::UiVariables{}.set(world_->ui, name, value, error);
}

bool LuaScriptHost::setHudVariables(
    std::unordered_map<std::string, std::string> variables,
    std::string &error) {
  return world_ != nullptr &&
         ui::UiVariables{}.setMany(world_->ui, std::move(variables), error);
}

bool LuaScriptHost::hudVariableDeclared(const std::string &name) const {
  return world_ != nullptr && world_->ui.declaredVariables.contains(name);
}

std::optional<std::string>
LuaScriptHost::hudVariable(const std::string &name) const {
  if (world_ == nullptr)
    return std::nullopt;
  const auto found = world_->ui.variables.find(name);
  return found == world_->ui.variables.end()
             ? std::nullopt
             : std::optional<std::string>(found->second);
}

void LuaScriptHost::resetHudVariables() {
  if (world_ != nullptr)
    ui::UiVariables{}.reset(world_->ui);
}

bool LuaScriptHost::setHudFont(const std::string &id, std::string font) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->font = std::move(font);
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudFontSize(const std::string &id, float fontSize) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->fontSize = std::max(fontSize, 0.0F);
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudRect(const std::string &id, float x, float y,
                               float width, float height) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->layout.position = {x, y};
    node->layout.size = {width, height};
    relayoutHud(*world_);
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudImage(const std::string &id, std::string texture,
                                float sourceX, float sourceY, float sourceWidth,
                                float sourceHeight) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->texture = std::move(texture);
    node->animation.clear();
    node->animationFrame = 0;
    node->sourcePosition = {sourceX, sourceY};
    node->sourceSize = {sourceWidth, sourceHeight};
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudImageAnimationFrame(const std::string &id,
                                              std::string animation,
                                              int frame) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->texture = animation;
    node->animation = std::move(animation);
    node->animationFrame = std::max(frame, 0);
    node->sourcePosition = {};
    node->sourceSize = {};
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudPosition(const std::string &id, float x, float y) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->layout.position = {x, y};
    relayoutHud(*world_);
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudSize(const std::string &id, float width,
                               float height) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->layout.size = {width, height};
    relayoutHud(*world_);
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudColor(const std::string &id, Color color) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->color = color;
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudBackgroundColor(const std::string &id, Color color) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->backgroundColor = color;
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudOpacity(const std::string &id, float opacity) {
  if (world_ == nullptr)
    return false;
  const float alpha = std::clamp(opacity, 0.0F, 1.0F);
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->color.a = alpha;
    node->backgroundColor.a = alpha;
    node->hoverColor.a = alpha;
    node->textColor.a = alpha;
    node->borderColor.a = alpha;
    return true;
  }
  return false;
}

bool LuaScriptHost::setHudVisible(const std::string &id, bool visible) {
  if (world_ == nullptr)
    return false;
  if (!ui::UiStateController{}.setVisible(world_->ui, id, visible))
    return false;
  relayoutHud(*world_);
  return true;
}

bool LuaScriptHost::setHudValue(const std::string &id, const float value) {
  if (world_ == nullptr)
    return false;
  if (!ui::UiStateController{}.setValue(world_->ui, id, value))
    return false;
  relayoutHud(*world_);
  return true;
}

bool LuaScriptHost::setHudChecked(const std::string &id, const bool checked) {
  if (world_ == nullptr)
    return false;
  return ui::UiStateController{}.setChecked(world_->ui, id, checked);
}

bool LuaScriptHost::setHudDisabled(const std::string &id, const bool disabled) {
  if (world_ == nullptr)
    return false;
  return ui::UiStateController{}.setDisabled(world_->ui, id, disabled);
}

bool LuaScriptHost::focusNextHudControl(const bool reverse) {
  return world_ != nullptr &&
         ui::UiInteractionController{}.focusNext(world_->ui, reverse);
}

std::string LuaScriptHost::focusedHudControl() const {
  return world_ == nullptr ? std::string{} : world_->ui.focusedId;
}

Vec2 LuaScriptHost::hudCanvasSize() const {
  return world_ == nullptr ? Vec2{} : world_->ui.canvasSize;
}

std::optional<std::string> LuaScriptHost::hudText(const std::string &id) const {
  if (world_ == nullptr)
    return std::nullopt;
  if (const ui::UiNode *node = ui::UiStateController{}.find(world_->ui, id))
    return node->text;
  return std::nullopt;
}

std::optional<ui::UiNodeHandle>
LuaScriptHost::createHudNode(const std::string &parent, ui::UiNode node,
                             std::string &error) {
  if (world_ == nullptr) {
    error = "HUD is unavailable.";
    return std::nullopt;
  }
  const std::string id = node.id;
  ui::UiMutationQueue queue;
  queue.create(parent, std::move(node));
  const auto result = queue.apply(world_->ui);
  if (!result.applied) {
    error = result.error;
    return std::nullopt;
  }
  discardInvalidRecyclers(*world_);
  relayoutHud(*world_);
  return ui::UiMutationQueue::handle(world_->ui, id);
}

std::optional<ui::UiNodeHandle>
LuaScriptHost::hudNodeHandle(const std::string &id) const {
  return world_ ? ui::UiMutationQueue::handle(world_->ui, id) : std::nullopt;
}

std::optional<ui::UiNodeHandle>
LuaScriptHost::cloneHudNode(const ui::UiNodeHandle &source,
                            const std::string &newRootId,
                            const std::string &parent, std::string &error) {
  if (world_ == nullptr) {
    error = "HUD is unavailable.";
    return std::nullopt;
  }
  ui::UiMutationQueue queue;
  queue.clone(source, newRootId, parent);
  const auto result = queue.apply(world_->ui);
  if (!result.applied) {
    error = result.error;
    return std::nullopt;
  }
  discardInvalidRecyclers(*world_);
  relayoutHud(*world_);
  return ui::UiMutationQueue::handle(world_->ui, newRootId);
}

bool LuaScriptHost::removeHudNode(const ui::UiNodeHandle &node,
                                  std::string &error) {
  if (world_ == nullptr) {
    error = "HUD is unavailable.";
    return false;
  }
  ui::UiMutationQueue queue;
  queue.remove(node);
  const auto result = queue.apply(world_->ui);
  if (!result.applied) {
    error = result.error;
    return false;
  }
  discardInvalidRecyclers(*world_);
  relayoutHud(*world_);
  return true;
}

bool LuaScriptHost::reparentHudNode(const ui::UiNodeHandle &node,
                                    const std::string &parent,
                                    std::string &error) {
  if (world_ == nullptr) {
    error = "HUD is unavailable.";
    return false;
  }
  ui::UiMutationQueue queue;
  queue.reparent(node, parent);
  const auto result = queue.apply(world_->ui);
  if (!result.applied) {
    error = result.error;
    return false;
  }
  relayoutHud(*world_);
  return true;
}

bool LuaScriptHost::clearHudChildren(const std::string &parent,
                                     std::string &error) {
  if (world_ == nullptr) {
    error = "HUD is unavailable.";
    return false;
  }
  ui::UiMutationQueue queue;
  queue.clearChildren(parent);
  const auto result = queue.apply(world_->ui);
  if (!result.applied) {
    error = result.error;
    return false;
  }
  discardInvalidRecyclers(*world_);
  relayoutHud(*world_);
  return true;
}

std::vector<std::string>
LuaScriptHost::hudChildren(const std::string &parent) const {
  std::vector<std::string> result;
  if (world_)
    for (const auto &node : world_->ui.nodes)
      if (node.parent == parent)
        result.push_back(node.id);
  return result;
}

ui::UiVirtualReconcileResult LuaScriptHost::reconcileHudRows(
    const std::string &collectionId, const ui::UiNodeHandle &rowTemplate,
    const std::vector<std::string> &stableKeys,
    const std::vector<float> &rowExtents, const float scrollOffset,
    const float viewportExtent, const std::size_t overscan) {
  if (world_ == nullptr)
    return {.applied = false,
            .error = "HUD is unavailable.",
            .range = {},
            .rows = {}};
  auto &owner = world_->uiVirtualRecyclers[collectionId];
  if (!owner)
    owner = std::make_unique<ui::UiVirtualRecycler>(collectionId);
  auto result =
      owner->reconcile(world_->ui, world_->uiTweens, rowTemplate, stableKeys,
                       rowExtents, scrollOffset, viewportExtent, overscan);
  if (result.applied)
    relayoutHud(*world_);
  return result;
}

bool LuaScriptHost::clearHudRows(const std::string &collectionId) {
  if (world_ == nullptr)
    return false;
  const auto found = world_->uiVirtualRecyclers.find(collectionId);
  if (found == world_->uiVirtualRecyclers.end())
    return false;
  found->second->clear(world_->ui, world_->uiTweens);
  world_->uiVirtualRecyclers.erase(found);
  relayoutHud(*world_);
  return true;
}

std::vector<ui::UiAccessibilityNode>
LuaScriptHost::hudAccessibilitySnapshot() const {
  return world_ ? ui::UiAccessibilityTree::snapshot(world_->ui)
                : std::vector<ui::UiAccessibilityNode>{};
}

std::uint64_t LuaScriptHost::startHudTween(const ui::UiNodeHandle &node,
                                           const std::string &property,
                                           const float target,
                                           const float duration,
                                           std::string &error) {
  if (world_ == nullptr) {
    error = "HUD is unavailable.";
    return 0;
  }
  const auto parsed =
      property == "opacity" ? std::optional{ui::UiTweenProperty::Opacity}
      : property == "x"     ? std::optional{ui::UiTweenProperty::PositionX}
      : property == "y"     ? std::optional{ui::UiTweenProperty::PositionY}
      : property == "scale" ? std::optional{ui::UiTweenProperty::Scale}
                            : std::nullopt;
  if (!parsed) {
    error = "Unknown HUD tween property: " + property;
    return 0;
  }
  const auto handle =
      world_->uiTweens.start(world_->ui, node, *parsed, target, duration);
  if (!handle)
    error = "HUD tween target is stale or values are invalid.";
  return handle.value;
}
bool LuaScriptHost::cancelHudTween(const std::uint64_t handle) {
  return world_ && world_->uiTweens.cancel({handle});
}
void LuaScriptHost::setHudReducedMotion(const bool enabled) {
  if (world_)
    world_->uiTweens.setReducedMotion(enabled);
}
bool LuaScriptHost::setHudLocale(const std::string &locale,
                                 std::string &error) {
  if (!world_) {
    error = "HUD is unavailable.";
    return false;
  }
  if (!ui::UiLocalization{}.setLocale(world_->ui, locale, error))
    return false;
  relayoutHud(*world_);
  return true;
}
void LuaScriptHost::setHudPseudoLocale(const bool enabled) {
  if (world_) {
    ui::UiLocalization{}.setPseudoLocale(world_->ui, enabled);
    relayoutHud(*world_);
  }
}

} // namespace demi::runtime
