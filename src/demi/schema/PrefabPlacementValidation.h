#pragma once
#include "demi/diagnostics/Diagnostic.h"
#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace demi {
void validatePrefabPlacements3D(Diagnostics &diagnostics,
                                const std::filesystem::path &source,
                                const nlohmann::json &expandedDocument);
}
