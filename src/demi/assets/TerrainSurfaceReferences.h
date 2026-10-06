#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>

namespace demi {
struct AssetRegistry;
namespace runtime {
struct TerrainRecipe;
}
namespace assets {
Diagnostics
validateTerrainSurfaceReferences(const AssetRegistry &registry,
                                 const runtime::TerrainRecipe &recipe,
                                 const std::filesystem::path &source = {});
}
} // namespace demi
