#pragma once

#include "demi/runtime/ui/UiModel.h"

namespace demi::runtime::ui {

class UiLayoutEngine {
public:
  void layout(UiDocument &document, Vec2 viewportSize) const;
};

} // namespace demi::runtime::ui
