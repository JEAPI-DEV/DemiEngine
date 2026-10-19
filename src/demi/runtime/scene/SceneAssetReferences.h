#pragma once

#include "demi/runtime/scene/model/World.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

// Collection counters describe source inspection, not source serialization.
struct SceneAssetReferenceStatistics {
  std::size_t skippedComponents = 0;
  std::size_t parsedPayloads = 0;
  std::size_t parsedPayloadBytes = 0;
};

// Unknown components and dynamic fields retain conservative nested discovery.
// Reflected math/identity-only components never inspect or serialize payloads.
[[nodiscard]] std::vector<std::string> collectEntityAssetReferences(
    std::span<const Entity> entities,
    std::optional<std::string_view> sceneOwner = std::nullopt,
    SceneAssetReferenceStatistics *statistics = nullptr);

// Returns stable asset IDs referenced by the expanded runtime scene. Prefab
// content is included because collection happens after scene expansion.
[[nodiscard]] std::vector<std::string> collectSceneAssetReferences(
    const World &world, std::string_view sceneOwner = {},
    SceneAssetReferenceStatistics *statistics = nullptr);

} // namespace demi::runtime
