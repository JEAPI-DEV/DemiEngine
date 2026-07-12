#pragma once

#include "demi/runtime/input/InputActionMap.h"

#include <nlohmann/json_fwd.hpp>

namespace demi::runtime::input {

[[nodiscard]] InputActionMap
parseInputActions(const nlohmann::json &projectDocument);

} // namespace demi::runtime::input
