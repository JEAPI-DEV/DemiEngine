#pragma once

#include <cstdint>
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
  LandformWarp,
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

// Immutable per-rule prefix of the canonical scatter seed. Construct once per
// rule, then derive independent candidate/sample streams without rehashing the
// world channel or stable rule ID. Integer mixing matches the legacy API
// exactly.
class TerrainScatterRuleSeed {
public:
  TerrainScatterRuleSeed(int worldSeed, std::uint64_t ruleHash) noexcept;
  [[nodiscard]] std::uint64_t candidate(std::uint64_t key,
                                        std::uint32_t sample) const noexcept {
    return prefix_ ^ (key * 0x9E3779B97F4A7C15ull) ^
           (std::uint64_t(sample) * 0x165667B19E3779F9ull);
  }

private:
  std::uint64_t prefix_ = 0;
};

// Shared physical-site stream for selection, spacing, variation and rendering.
[[nodiscard]] std::uint64_t terrainScatterCandidateKey(int x, int z) noexcept;
[[nodiscard]] std::uint64_t
deriveTerrainScatterSeed(int worldSeed, std::uint64_t ruleHash,
                         std::uint64_t candidateKey,
                         std::uint32_t sample) noexcept;

} // namespace demi::runtime
