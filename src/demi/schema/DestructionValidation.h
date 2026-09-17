#pragma once
#include "demi/diagnostics/Diagnostic.h"
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
namespace demi {
struct AssetRegistry;
void validateDestruction3D(Diagnostics &, const std::filesystem::path &,
                           const nlohmann::json &document,
                           const AssetRegistry &);
} // namespace demi
