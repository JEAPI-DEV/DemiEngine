#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::ui {

enum class UiAccessibilityRole {
  Generic,
  Group,
  StaticText,
  Image,
  Button,
  CheckBox,
  Slider,
  TextField,
  ScrollArea,
  List,
  ProgressBar,
  Dialog,
  Joystick,
};

struct UiAccessibilityNode {
  std::string id;
  std::string parent;
  UiAccessibilityRole role = UiAccessibilityRole::Generic;
  std::string label;
  std::string description;
  std::string valueText;
  Rect bounds{};
  float value = 0.0F;
  float minimum = 0.0F;
  float maximum = 1.0F;
  bool focused = false;
  bool disabled = false;
  bool checked = false;
  bool focusable = false;
  bool offscreen = false;
};

class UiAccessibilityTree {
public:
  [[nodiscard]] static std::vector<UiAccessibilityNode>
  snapshot(const UiDocument &document);
};

[[nodiscard]] std::string_view
uiAccessibilityRoleName(UiAccessibilityRole role);

} // namespace demi::runtime::ui
