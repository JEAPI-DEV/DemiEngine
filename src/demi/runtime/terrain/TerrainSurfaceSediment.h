#pragma once
#include "demi/runtime/terrain/TerrainHeightField.h"
#include <optional>
#include <stop_token>

namespace demi::runtime {
// These are selected-surface balances, not a sum of arbitrary graph ancestors.
std::optional<std::shared_ptr<const TerrainSamples<float>>>
blendTerrainSurfaceSediment(const HeightField &a, const HeightField &b,
                            const TerrainSamples<float> *mask, double amount,
                            std::stop_token stop);
std::optional<std::shared_ptr<const TerrainSamples<float>>>
accumulateTerrainSurfaceSediment(const HeightField &source,
                                 const TerrainSamples<float> &delta,
                                 std::stop_token stop);
} // namespace demi::runtime
