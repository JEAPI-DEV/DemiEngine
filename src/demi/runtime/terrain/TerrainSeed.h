#pragma once

#include <string_view>

namespace demi::runtime {

// Channels that derive an independent sub-seed from the recipe's world seed.
//
// A channel is a pure function of the world seed, so a given channel produces
// the same sub-seed no matter how many workers run, in what order chunks load,
// or how many times a recipe is regenerated. That is what lets later stages be
// added, parallelised or replayed incrementally without reshuffling results.
//
// Erosion and hydrology are reserved for milestone 3. Declaring them now keeps
// the channel set stable: see terrainSeedChannelName for why names, not enum
// ordinals, are hashed.
enum class TerrainSeedChannel {
  Landform,
  Erosion,
  Hydrology,
  BiomePlacement,
  Scatter,
};

// Stable across platforms and compilers. Never 0, so a derived seed is
// distinguishable from an unset/authored-zero seed.
int deriveTerrainSubSeed(int worldSeed, TerrainSeedChannel channel) noexcept;

// The name is part of the seed derivation. Adding a channel must not change an
// existing channel's sub-seed, so the name is hashed instead of the ordinal.
std::string_view terrainSeedChannelName(TerrainSeedChannel channel) noexcept;

} // namespace demi::runtime
