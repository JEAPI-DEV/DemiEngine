#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace demi::filesystem {

[[nodiscard]] nlohmann::json
normalizeAuthoredValue(nlohmann::json value,
                             const nlohmann::json *previous = nullptr,
                             int decimalPlaces = 3);

// Applies replacement-only JSON differences to their exact source spans.
// Returns no value for structural changes that require a different writer.
[[nodiscard]] std::optional<std::string>
patchAuthoredJsonSource(const std::string &source, const nlohmann::json &before,
                      const nlohmann::json &after);

} // namespace demi::filesystem
