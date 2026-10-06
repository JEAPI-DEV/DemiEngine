#include "demi/runtime/terrain/TerrainSeed.h"
#include <cstdint>

namespace demi::runtime {
namespace {

// FNV-1a over the channel name, so the salt is independent of declaration
// order and of how many channels exist.
std::uint64_t nameHash(std::string_view name) noexcept {
  std::uint64_t hash = 1469598103934665603ull;
  for (const char character : name) {
    hash ^= std::uint64_t(static_cast<unsigned char>(character));
    hash *= 1099511628211ull;
  }
  return hash;
}

// splitmix64 finalizer: a full-avalanche mix with no platform-dependent width.
std::uint64_t mix(std::uint64_t value) noexcept {
  value += 0x9E3779B97F4A7C15ull;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
  return value ^ (value >> 31);
}

} // namespace

std::string_view terrainSeedChannelName(TerrainSeedChannel channel) noexcept {
  switch (channel) {
  case TerrainSeedChannel::Landform:
    return "landform";
  case TerrainSeedChannel::Erosion:
    return "erosion";
  case TerrainSeedChannel::Hydrology:
    return "hydrology";
  case TerrainSeedChannel::BiomePlacement:
    return "biome_placement";
  case TerrainSeedChannel::Scatter:
    return "scatter";
  }
  return "unknown";
}

int deriveTerrainSubSeed(int worldSeed, TerrainSeedChannel channel) noexcept {
  const auto world = std::uint64_t(static_cast<std::uint32_t>(worldSeed));
  const auto seed = mix(world ^ nameHash(terrainSeedChannelName(channel)));
  // Fold to a non-zero 30-bit value. FastNoiseLite takes a signed int, and
  // reserving 0 keeps a derived seed distinguishable from a default one.
  return static_cast<int>((seed >> 17) & 0x3FFFFFFFull) + 1;
}

} // namespace demi::runtime
