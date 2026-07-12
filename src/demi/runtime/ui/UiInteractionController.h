#pragma once
#include "demi/runtime/ui/UiModel.h"
#include <optional>
#include <string>
namespace demi::runtime::ui {
class UiInteractionController {
public:
  bool focusNext(UiDocument &document, bool reverse = false) const;
  bool capturePointer(UiDocument &document, Vec2 position) const;
  bool updatePointer(UiDocument &document, Vec2 position) const;
  void releasePointer(UiDocument &document) const;
  [[nodiscard]] std::optional<std::string>
  activateFocused(UiDocument &document) const;
};
} // namespace demi::runtime::ui
