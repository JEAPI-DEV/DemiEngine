#pragma once

#include "demi/runtime/ui/UiModel.h"

namespace demi::runtime::ui {

class UiLayoutEngine {
public:
  // Explicit rebuild for authored document construction/editing.
  void layout(const UiDocument &document, Vec2 viewportSize) const;
  // Runtime read barrier: unchanged documents do no geometry work.
  void ensure(const UiDocument &document, Vec2 viewportSize) const;
};

} // namespace demi::runtime::ui
