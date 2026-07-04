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
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.color = color;
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
