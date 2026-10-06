#pragma once

#include "demi/assets/TerrainAsset.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demi::assets::terrain_payload {

inline constexpr int Version = terrainAssetPayloadVersion;

// A shipped terrain contains the cooked surface plus its derived runtime and
// editor checkpoints. It contains neither the source recipe nor graph nodes.
std::vector<std::byte> encode(const runtime::HeightField &field,
                              std::string_view recipeDigest,
                              std::string_view inputFingerprint);
std::shared_ptr<const runtime::HeightField>
decode(std::span<const std::byte> bytes, std::string *recipeDigest = nullptr,
       std::string *inputFingerprint = nullptr);

} // namespace demi::assets::terrain_payload
