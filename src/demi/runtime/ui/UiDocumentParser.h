#pragma once
#include "demi/runtime/ui/UiModel.h"
#include <nlohmann/json_fwd.hpp>
namespace demi::runtime::ui {
[[nodiscard]] UiDocument parseUiDocument(const nlohmann::json &document);
} // namespace demi::runtime::ui
