#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/ui/UiInteractionController.h"
#include "demi/runtime/ui/UiLayoutEngine.h"
#include "demi/runtime/ui/UiStateController.h"

#include <algorithm>

namespace demi::runtime {
namespace {

void relayoutHud(World &world) {
  ui::UiLayoutEngine{}.layout(world.ui, world.ui.canvasSize);
}

} // namespace

bool LuaScriptHost::setHudText(const std::string &id, const std::string &text) {
  if (world_ == nullptr)
    return false;
  if (ui::UiStateController{}.setText(world_->ui, id, text))
    return true;
  return false;
}

bool LuaScriptHost::setHudButtonLabel(const std::string &id,
                                      const std::string &label) {
  if (world_ == nullptr)
    return false;
  return ui::UiStateController{}.setText(world_->ui, id, label);
}

bool LuaScriptHost::createHudText(const std::string &id,
                                  const std::string &text, float x, float y,
                                  float scale, Color color) {
  if (world_ == nullptr)
    return false;
  ui::UiNode node;
  node.id = id;
  node.type = "label";
  node.text = text;
  node.layout.position = {x, y};
  node.fontSize = scale * 8.0F;
  node.color = color;
  if (auto *existing = ui::UiStateController{}.find(world_->ui, id)) {
    *existing = node;
    relayoutHud(*world_);
    return true;
  }
  world_->ui.nodes.push_back(node);
  relayoutHud(*world_);
  return true;
}

bool LuaScriptHost::setHudTextScale(const std::string &id, float scale) {
  if (world_ == nullptr)
    return false;
  if (auto *node = ui::UiStateController{}.find(world_->ui, id)) {
    node->fontSize = scale * 8.0F;
    return true;
  }
  return false;
}

bool LuaScriptHost::createHudRect(const std::string &id, float x, float y,
                                  float width, float height, Color color) {
  if (world_ == nullptr)
    return false;
  ui::UiNode node;
  node.id = id;
  node.type = "rect";
  node.layout.position = {x, y};
  node.layout.size = {width, height};
  node.backgroundColor = color;
  if (auto *existing = ui::UiStateController{}.find(world_->ui, id)) {
    *existing = node;
    relayoutHud(*world_);
    return true;
  }
  world_->ui.nodes.push_back(node);
  relayoutHud(*world_);
  return true;
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

bool LuaScriptHost::setHudGroupVisible(const std::string &group, bool visible) {
  if (world_ == nullptr)
    return false;
  bool any = false;
  for (ui::UiNode &node : world_->ui.nodes) {
    if (node.parent == group || node.id == group || node.group == group ||
        (node.style == group)) {
      node.visible = visible;
      any = true;
    }
  }
  if (any)
    relayoutHud(*world_);
  return any;
}

std::optional<std::string> LuaScriptHost::hudText(const std::string &id) const {
  if (world_ == nullptr)
    return std::nullopt;
  if (const ui::UiNode *node =
          ui::UiStateController{}.find(world_->ui, id))
    return node->text;
  return std::nullopt;
}

} // namespace demi::runtime
