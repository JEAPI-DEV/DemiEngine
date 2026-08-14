#pragma once

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>

namespace demi::runtime {

[[nodiscard]] std::optional<nlohmann::json>
resolveScriptProperties(const nlohmann::json &schema,
                        const nlohmann::json &authored, std::string &error);

} // namespace demi::runtime
