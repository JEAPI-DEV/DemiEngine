#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <string_view>
#include <vector>

namespace demi::runtime::ui {

class UiEventQueue {
public:
  static void push(UiDocument &document, UiEvent event);
  [[nodiscard]] static std::vector<UiEvent> take(UiDocument &document);
  static void valueChanged(UiDocument &document, const UiNode &node,
                           std::string_view source);
  static void cancelSubtree(UiDocument &document, std::string_view root,
                            std::string_view source);
};

} // namespace demi::runtime::ui
