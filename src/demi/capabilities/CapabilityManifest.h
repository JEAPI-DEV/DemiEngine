#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <span>
#include <string>

namespace demi::capabilities {

[[nodiscard]] nlohmann::json
buildManifest(std::span<const std::string> luaApi);

[[nodiscard]] Diagnostics
comparePublicApi(const nlohmann::json &baseline,
                 const nlohmann::json &current);

[[nodiscard]] Diagnostics
verifyReferenceGates(const std::filesystem::path &sourceRoot,
                     const nlohmann::json &document);

} // namespace demi::capabilities
