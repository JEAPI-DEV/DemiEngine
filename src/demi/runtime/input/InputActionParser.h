#pragma once

#include "demi/runtime/input/InputActionMap.h"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace demi::runtime::input {

[[nodiscard]] InputActionMap
parseInputActions(const nlohmann::json &projectDocument);

[[nodiscard]] std::vector<std::string> knownInputPresets();

} // namespace demi::runtime::input
