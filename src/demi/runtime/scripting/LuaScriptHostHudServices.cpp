#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scene/hud/HudElementTraversal.h"
#include "demi/runtime/ui/UiInteractionController.h"
#include "demi/runtime/ui/UiLegacyProjection.h"
#include "demi/runtime/ui/UiStateController.h"

#include <algorithm>

namespace demi::runtime {

bool LuaScriptHost::setHudText(const std::string &id, const std::string &text) {
  if (world_ == nullptr)
    return false;
  if (ui::UiStateController{}.setText(world_->ui, id, text)) {
    ui::projectUiDocument(*world_);
    return true;
  }
  for (HudTextElement &element : world_->hudText) {
    if (element.id == id) {
      element.text = text;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudButtonLabel(const std::string &id,
                                      const std::string &label) {
  if (world_ == nullptr)
    return false;
  if (ui::UiStateController{}.setText(world_->ui, id, label)) {
    ui::projectUiDocument(*world_);
    return true;
  }
  for (HudButtonElement &element : world_->hudButtons) {
    if (element.id == id) {
      element.label = label;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::createHudText(const std::string &id,
                                  const std::string &text, float x, float y,
                                  float scale, Color color) {
  if (world_ == nullptr)
    return false;
  for (HudTextElement &element : world_->hudText) {
    if (element.id == id) {
      element.text = text;
      element.position = {x, y};
      element.scale = scale;
      element.color = color;
      element.visible = true;
      return true;
    }
  }
  world_->hudText.push_back({.id = id,
                             .group = {},
                             .text = text,
                             .position = {x, y},
                             .scale = scale,
                             .color = color});
  return true;
}

bool LuaScriptHost::setHudTextScale(const std::string &id, float scale) {
  if (world_ == nullptr)
    return false;
  for (HudTextElement &element : world_->hudText) {
    if (element.id == id) {
      element.scale = scale;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::createHudRect(const std::string &id, float x, float y,
                                  float width, float height, Color color) {
  if (world_ == nullptr)
    return false;
  for (HudRectElement &element : world_->hudRects) {
    if (element.id == id) {
      element.position = {x, y};
      element.size = {width, height};
      element.color = color;
      element.visible = true;
      return true;
    }
  }
  world_->hudRects.push_back({.id = id,
                              .group = {},
                              .position = {x, y},
                              .size = {width, height},
                              .color = color});
  return true;
}

bool LuaScriptHost::setHudRect(const std::string &id, float x, float y,
                               float width, float height) {
  if (world_ == nullptr)
    return false;
  const Vec2 position{x, y};
  const Vec2 size{width, height};
  bool supported = false;
  const bool found = visitHudElement(*world_, id, [&](auto &element) {
    if constexpr (requires { element.setRect(position, size); }) {
      element.setRect(position, size);
      supported = true;
    }
  });
  return found && supported;
}

bool LuaScriptHost::setHudImage(const std::string &id, std::string texture,
                                float sourceX, float sourceY, float sourceWidth,
                                float sourceHeight) {
  if (world_ == nullptr)
    return false;
  for (HudImageElement &element : world_->hudImages) {
    if (element.id == id) {
      element.texture = std::move(texture);
      element.animation.clear();
      element.animationFrame = 0;
      element.sourcePosition = {sourceX, sourceY};
      element.sourceSize = {sourceWidth, sourceHeight};
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudImageAnimationFrame(const std::string &id,
                                              std::string animation,
                                              int frame) {
  if (world_ == nullptr)
    return false;
  for (HudImageElement &element : world_->hudImages) {
    if (element.id == id) {
      element.texture = animation;
      element.animation = std::move(animation);
      element.animationFrame = std::max(frame, 0);
      element.sourcePosition = {};
      element.sourceSize = {};
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudPosition(const std::string &id, float x, float y) {
  return world_ != nullptr && visitHudElement(*world_, id, [&](auto &element) {
           element.setPosition({x, y});
         });
}

bool LuaScriptHost::setHudSize(const std::string &id, float width,
                               float height) {
  if (world_ == nullptr)
    return false;
  bool supported = false;
  const bool found = visitHudElement(*world_, id, [&](auto &element) {
    if constexpr (requires { element.setSize(Vec2{}); }) {
      element.setSize({width, height});
      supported = true;
    }
  });
  return found && supported;
}

bool LuaScriptHost::setHudColor(const std::string &id, Color color) {
  return world_ != nullptr && visitHudElement(*world_, id, [&](auto &element) {
           element.setColor(color);
         });
}

bool LuaScriptHost::setHudOpacity(const std::string &id, float opacity) {
  const float alpha = std::clamp(opacity, 0.0F, 1.0F);
  return world_ != nullptr && visitHudElement(*world_, id, [&](auto &element) {
           element.setOpacity(alpha);
         });
}

bool LuaScriptHost::setHudVisible(const std::string &id, bool visible) {
  if (world_ != nullptr &&
      ui::UiStateController{}.setVisible(world_->ui, id, visible)) {
    ui::projectUiDocument(*world_);
    return true;
  }
  return world_ != nullptr && visitHudElement(*world_, id, [&](auto &element) {
           element.visible = visible;
         });
}

bool LuaScriptHost::setHudValue(const std::string &id, const float value) {
  if (world_ == nullptr ||
      !ui::UiStateController{}.setValue(world_->ui, id, value))
    return false;
  ui::projectUiDocument(*world_);
  return true;
}

bool LuaScriptHost::setHudChecked(const std::string &id, const bool checked) {
  if (world_ == nullptr ||
      !ui::UiStateController{}.setChecked(world_->ui, id, checked))
    return false;
  ui::projectUiDocument(*world_);
  return true;
}

bool LuaScriptHost::setHudDisabled(const std::string &id, const bool disabled) {
  if (world_ == nullptr ||
      !ui::UiStateController{}.setDisabled(world_->ui, id, disabled))
    return false;
  ui::projectUiDocument(*world_);
  return true;
}

bool LuaScriptHost::focusNextHudControl(const bool reverse) {
  return world_ != nullptr &&
         ui::UiInteractionController{}.focusNext(world_->ui, reverse);
}

std::string LuaScriptHost::focusedHudControl() const {
  return world_ == nullptr ? std::string{} : world_->ui.focusedId;
}

bool LuaScriptHost::setHudGroupVisible(const std::string &group, bool visible) {
  return world_ != nullptr && visitHudGroup(*world_, group, [&](auto &element) {
           element.visible = visible;
         });
}

std::optional<std::string> LuaScriptHost::hudText(const std::string &id) const {
  if (world_ == nullptr)
    return std::nullopt;
  if (const ui::UiNode *node =
          ui::UiStateController{}.find(world_->ui, id))
    return node->text;
  for (const HudTextElement &element : world_->hudText)
    if (element.id == id)
      return element.text;
  return std::nullopt;
}

} // namespace demi::runtime
