#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace demi::assets::terrain_storage {

struct PreparedField {
  std::shared_ptr<const runtime::HeightField> field;
  std::string recipeDigest;
  std::string inputFingerprint;
};

std::filesystem::path cachePath(const AssetRegistry &registry,
                                std::string_view assetId,
                                std::string_view recipeDigest,
                                std::string_view inputFingerprint);
std::string
assetInputFingerprint(const AssetRegistry &registry,
                      const runtime::TerrainRecipe &recipe,
                      const runtime::TerrainGenerationInputs &generationInputs);
PreparedField read(const std::filesystem::path &path);
void writeAtomically(const std::filesystem::path &path,
                     std::span<const std::byte> bytes);

} // namespace demi::assets::terrain_storage
