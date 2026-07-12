#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <string_view>

namespace demi::runtime::ui {

class UiActionController {
public:
  bool apply(UiDocument &document, std::string_view action) const;
};

} // namespace demi::runtime::ui
