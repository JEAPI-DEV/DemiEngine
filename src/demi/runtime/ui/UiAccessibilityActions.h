#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <optional>
#include <string>

namespace demi::runtime::ui {

enum class UiAccessibilityActionType {
  Focus,
  Activate,
  Increment,
  Decrement,
  SetValue,
  SetText,
  ScrollForward,
  ScrollBackward,
};

struct UiAccessibilityAction {
  UiAccessibilityActionType type = UiAccessibilityActionType::Activate;
  std::string nodeId;
  float value = 0.0F;
  std::string text;
};

struct UiAccessibilityActionResult {
  bool handled = false;
  std::string error;
  std::optional<std::string> action;
};

class UiAccessibilityActions {
public:
  [[nodiscard]] UiAccessibilityActionResult
  perform(UiDocument &document, const UiAccessibilityAction &request) const;
};

} // namespace demi::runtime::ui
