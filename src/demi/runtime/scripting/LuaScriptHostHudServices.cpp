#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <algorithm>
#include <optional>

namespace demi::runtime {

bool LuaScriptHost::setHudText(const std::string& id, const std::string& text) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.text = text;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudButtonLabel(const std::string& id, const std::string& label) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.label = label;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::createHudText(const std::string& id, const std::string& text, const float x, const float y, const float scale, const Color color) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.text = text;
      element.position = Vec2{.x = x, .y = y};
      element.scale = scale;
      element.color = color;
      element.visible = true;
      return true;
    }
  }
  world_->hudText.push_back(HudTextElement{.id = id, .group = {}, .text = text, .position = Vec2{.x = x, .y = y}, .scale = scale, .color = color});
  return true;
}

bool LuaScriptHost::setHudTextScale(const std::string& id, const float scale) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.scale = scale;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::createHudRect(const std::string& id, const float x, const float y, const float width, const float height, const Color color) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.position = Vec2{.x = x, .y = y};
      element.size = Vec2{.x = width, .y = height};
      element.color = color;
      element.visible = true;
      return true;
    }
  }
  world_->hudRects.push_back(HudRectElement{.id = id, .group = {}, .position = Vec2{.x = x, .y = y}, .size = Vec2{.x = width, .y = height}, .color = color});
  return true;
}

bool LuaScriptHost::setHudRect(const std::string& id, const float x, const float y, const float width, const float height) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.position = Vec2{.x = x, .y = y};
      element.size = Vec2{.x = width, .y = height};
      return true;
    }
  }
  for (HudPanelElement& element : world_->hudPanels) {
    if (element.id == id) {
      element.position = Vec2{.x = x, .y = y};
      element.size = Vec2{.x = width, .y = height};
      return true;
    }
  }
  for (HudImageElement& element : world_->hudImages) {
    if (element.id == id) {
      element.position = Vec2{.x = x, .y = y};
      element.size = Vec2{.x = width, .y = height};
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudImage(const std::string& id,
                                std::string texture,
                                const float sourceX,
                                const float sourceY,
                                const float sourceWidth,
                                const float sourceHeight) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudImageElement& element : world_->hudImages) {
    if (element.id == id) {
      element.texture = std::move(texture);
      element.animation.clear();
      element.animationFrame = 0;
      element.sourcePosition = Vec2{.x = sourceX, .y = sourceY};
      element.sourceSize = Vec2{.x = sourceWidth, .y = sourceHeight};
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudImageAnimationFrame(const std::string& id, std::string animation, const int frame) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudImageElement& element : world_->hudImages) {
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

bool LuaScriptHost::setHudPosition(const std::string& id, const float x, const float y) {
  if (world_ == nullptr) {
    return false;
  }
  const Vec2 position{.x = x, .y = y};
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.position = position;
      return true;
    }
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.position = position;
      return true;
    }
  }
  for (HudPanelElement& element : world_->hudPanels) {
    if (element.id == id) {
      element.position = position;
      return true;
    }
  }
  for (HudCircleElement& element : world_->hudCircles) {
    if (element.id == id) {
      element.center = position;
      return true;
    }
  }
  for (HudImageElement& element : world_->hudImages) {
    if (element.id == id) {
      element.position = position;
      return true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.position = position;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudSize(const std::string& id, const float width, const float height) {
  if (world_ == nullptr) {
    return false;
  }
  const Vec2 size{.x = width, .y = height};
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.size = size;
      return true;
    }
  }
  for (HudPanelElement& element : world_->hudPanels) {
    if (element.id == id) {
      element.size = size;
      return true;
    }
  }
  for (HudImageElement& element : world_->hudImages) {
    if (element.id == id) {
      element.size = size;
      return true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.size = size;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudColor(const std::string& id, const Color color) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  for (HudPanelElement& element : world_->hudPanels) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  for (HudCircleElement& element : world_->hudCircles) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  for (HudImageElement& element : world_->hudImages) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudOpacity(const std::string& id, const float opacity) {
  if (world_ == nullptr) {
    return false;
  }
  const float alpha = std::clamp(opacity, 0.0F, 1.0F);
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.color.a = alpha;
      return true;
    }
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.color.a = alpha;
      return true;
    }
  }
  for (HudPanelElement& element : world_->hudPanels) {
    if (element.id == id) {
      element.color.a = alpha;
      element.borderColor.a = alpha;
      return true;
    }
  }
  for (HudCircleElement& element : world_->hudCircles) {
    if (element.id == id) {
      element.color.a = alpha;
      return true;
    }
  }
  for (HudImageElement& element : world_->hudImages) {
    if (element.id == id) {
      element.color.a = alpha;
      return true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.color.a = alpha;
      element.hoverColor.a = alpha;
      element.textColor.a = alpha;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudVisible(const std::string& id, const bool visible) {
  if (world_ == nullptr) {
    return false;
  }
  bool changed = false;
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudPanelElement& element : world_->hudPanels) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudCircleElement& element : world_->hudCircles) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudImageElement& element : world_->hudImages) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  return changed;
}

bool LuaScriptHost::setHudGroupVisible(const std::string& group, const bool visible) {
  if (world_ == nullptr) {
    return false;
  }
  bool changed = false;
  for (HudTextElement& element : world_->hudText) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudPanelElement& element : world_->hudPanels) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudCircleElement& element : world_->hudCircles) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudImageElement& element : world_->hudImages) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  return changed;
}

std::optional<std::string> LuaScriptHost::hudText(const std::string& id) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  for (const HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      return element.text;
    }
  }
  return std::nullopt;
}

bool LuaScriptHost::isMouseDown(const std::string& button) const {
  return input_ != nullptr && input_->mouseButtonsDown.contains(normalizedKey(button));
}

Vec2 LuaScriptHost::mousePosition() const {
  return input_ != nullptr ? input_->mousePosition : Vec2{};
}

Vec2 LuaScriptHost::mouseDelta() const {
  return input_ != nullptr ? input_->mouseDelta : Vec2{};
}

Vec2 LuaScriptHost::mouseWorldPosition() const {
  if (input_ == nullptr || world_ == nullptr) {
    return {};
  }
  const Camera2DComponent* camera = activeCamera(*world_);
  const Vec2 cameraPosition = activeCameraPosition(*world_);
  const float orthographicSize = camera != nullptr ? camera->orthographicSize : 10.0F;
  const float pixelsPerUnit = static_cast<float>(viewportHeight_) / std::max(orthographicSize * 2.0F, 1.0F);
  return Vec2{
    .x = cameraPosition.x + (input_->mousePosition.x - static_cast<float>(viewportWidth_) * 0.5F) / pixelsPerUnit,
    .y = cameraPosition.y + (static_cast<float>(viewportHeight_) * 0.5F - input_->mousePosition.y) / pixelsPerUnit,
  };
}

Vec2 LuaScriptHost::viewportSize() const {
  return Vec2{.x = static_cast<float>(viewportWidth_), .y = static_cast<float>(viewportHeight_)};
}

void LuaScriptHost::addDebugLine(const float x1, const float y1, const float x2, const float y2, const float r, const float g, const float b, const float a) {
  if (world_ == nullptr) {
    return;
  }
  world_->debugLines.push_back(DebugLine{.start = Vec2{.x = x1, .y = y1}, .end = Vec2{.x = x2, .y = y2}, .color = Color{.r = r, .g = g, .b = b, .a = a}});
}

void LuaScriptHost::clearDebugLines() {
  if (world_ != nullptr) {
    world_->debugLines.clear();
  }
}

} // namespace demi::runtime
