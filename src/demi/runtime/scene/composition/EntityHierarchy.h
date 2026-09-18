#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace demi::runtime::composition {

// Source nesting changes ownership, not stable IDs. Throws on malformed or
// conflicting hierarchy. Runtime consumers receive the existing flat format.
nlohmann::json flattenEntityHierarchy(const nlohmann::json &entities);

nlohmann::json *findAuthoredEntity(nlohmann::json &entities,
                                   std::string_view id);
const nlohmann::json *findAuthoredEntity(const nlohmann::json &entities,
                                         std::string_view id);

} // namespace demi::runtime::composition
