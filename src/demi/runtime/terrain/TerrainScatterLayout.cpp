#include "demi/runtime/terrain/TerrainScatterLayout.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>

namespace demi::runtime {
namespace {

// FNV-1a over the key's text, matching the derivation TerrainSeed.cpp uses, so
// the two agree on what "a stable hash of a name" means.
constexpr std::uint64_t fnvOffset = 1469598103934665603ull;
constexpr std::uint64_t fnvPrime = 1099511628211ull;

void absorbText(std::uint64_t &hash, std::string_view text) noexcept {
  for (const char character : text) {
    hash ^= std::uint64_t(static_cast<unsigned char>(character));
    hash *= fnvPrime;
  }
  // Length is mixed in so that concatenations cannot collide: "ab" + "c" and
  // "a" + "bc" hash the same bytes without it.
  hash ^= static_cast<std::uint64_t>(text.size());
  hash *= fnvPrime;
}

void absorb(std::uint64_t &hash, std::uint64_t value) noexcept {
  absorbText(hash, std::to_string(value));
}

// splitmix64 finalizer: a full-avalanche mix with no platform-dependent width.
std::uint64_t mix(std::uint64_t value) noexcept {
  value += 0x9E3779B97F4A7C15ull;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
  return value ^ (value >> 31);
}

// Canonical bit pattern for a float, so the hash of an instance never depends on
// a NaN payload, on a sign bit a comparison would ignore, or on the order the
// arithmetic was folded in.
std::uint32_t canonicalFloat(float value) noexcept {
  if (std::isnan(value))
    return 0x7FC00000U;
  if (std::isinf(value))
    return value > 0.F ? 0x7F800000U : 0xFF800000U;
  // -0.0 and 0.0 compare equal, so they must hash equal.
  return std::bit_cast<std::uint32_t>(value == 0.F ? 0.F : value);
}

void absorbFloat(std::uint64_t &hash, float value) noexcept {
  absorb(hash, canonicalFloat(value));
}

constexpr char keySeparator = '|';

// Escapes the separator and the escape itself, so joining escaped segments is
// injective: no authored id can forge another set of inputs.
std::string escapeSegment(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char character : text) {
    if (character == keySeparator || character == '\\')
      escaped.push_back('\\');
    escaped.push_back(character);
  }
  return escaped;
}

std::string drawableOf(const TerrainScatterInstance &instance) {
  // A prefab-only role still draws something, and grouping it under an empty
  // asset would batch every prefab role together with every other.
  return instance.asset.empty() ? instance.prefab : instance.asset;
}

} // namespace

std::string terrainScatterInstanceKey(std::string_view paletteId,
                                      TerrainPaletteRole role,
                                      std::size_t cell, std::string_view regionId,
                                      std::string_view layerId) {
  std::string key;
  key.reserve(paletteId.size() + regionId.size() + layerId.size() + 32);
  key += "scatter";
  key += keySeparator;
  key += escapeSegment(paletteId);
  key += keySeparator;
  // Names, never ordinals: adding a role must not change any existing role's
  // identity, which is the same rule TerrainSeed follows for channels.
  key += escapeSegment(terrainPaletteRoleName(role));
  key += keySeparator;
  key += escapeSegment(regionId);
  key += keySeparator;
  key += escapeSegment(layerId);
  key += keySeparator;
  key += std::to_string(cell);
  return key;
}

std::uint64_t terrainScatterInstanceSeed(int worldSeed, TerrainPaletteRole role,
                                         std::size_t cell) {
  const auto channel =
      deriveTerrainSubSeed(worldSeed, TerrainSeedChannel::Scatter);
  // Identical to the cellRandom() derivation in TerrainScatter.cpp for sample
  // slot 1, which is the slot that draws yaw and scale. Duplicated rather than
  // called because scatterTerrain's helper is file-local; the exact patch that
  // moves this single definition into scatterTerrain is in the change report.
  std::uint64_t state = static_cast<std::uint64_t>(std::uint32_t(channel));
  state ^= static_cast<std::uint64_t>(cell) * 0x9E3779B97F4A7C15ull;
  state ^= static_cast<std::uint64_t>(role) * 0xC2B2AE3D27D4EB4Full;
  state ^= static_cast<std::uint64_t>(1) * 0x165667B19E3779F9ull;
  return state;
}

TerrainScatterInstance
terrainScatterInstanceFrom(const TerrainScatterPlacement &placement,
                           std::string_view paletteId, int worldSeed,
                           std::string_view regionId, std::string_view layerId) {
  TerrainScatterInstance instance;
  instance.id = terrainScatterInstanceKey(paletteId, placement.role,
                                          placement.cell, regionId, layerId);
  instance.seed =
      terrainScatterInstanceSeed(worldSeed, placement.role, placement.cell);
  instance.cell = placement.cell;
  instance.role = placement.role;
  instance.biome = placement.biome;
  instance.asset = placement.asset;
  instance.prefab = placement.prefab;
  instance.position = placement.position;
  instance.yaw = placement.yaw;
  instance.scale = placement.scale;
  instance.collision = placement.collision;
  instance.lod = placement.lod;
  return instance;
}

std::uint64_t terrainScatterInstanceHash(const TerrainScatterInstance &instance) {
  std::uint64_t hash = fnvOffset;
  absorbText(hash, instance.id);
  absorb(hash, instance.seed);
  absorb(hash, instance.cell);
  // Enum names rather than ordinals, for the reason given on the key.
  absorbText(hash, terrainPaletteRoleName(instance.role));
  absorb(hash, instance.biome);
  absorbText(hash, instance.asset);
  absorbText(hash, instance.prefab);
  absorbFloat(hash, instance.position.x);
  absorbFloat(hash, instance.position.y);
  absorbFloat(hash, instance.position.z);
  absorbFloat(hash, instance.yaw);
  absorbFloat(hash, instance.scale);
  absorbText(hash, terrainCollisionPolicyName(instance.collision));
  absorb(hash, static_cast<std::uint64_t>(instance.lod));
  // instanceGroup is intentionally absent: it is derived from the rest of the
  // set, so folding it in would break "an unrelated placement changes nothing".
  return mix(hash);
}

std::vector<TerrainScatterInstanceGroup>
terrainScatterGroupInstances(std::vector<TerrainScatterInstance> &instances) {
  std::vector<TerrainScatterInstanceGroup> groups;
  // Sorted by drawable, then by id: the group number depends only on which
  // drawables exist, and a member's position inside its batch depends only on
  // its own identity.
  std::vector<std::size_t> order(instances.size());
  for (std::size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    const auto &left = instances[a];
    const auto &right = instances[b];
    const auto leftAsset = drawableOf(left);
    const auto rightAsset = drawableOf(right);
    if (leftAsset != rightAsset)
      return leftAsset < rightAsset;
    return left.id < right.id;
  });

  std::string current;
  bool open = false;
  for (const auto index : order) {
    const auto drawable = drawableOf(instances[index]);
    if (!open || drawable != current) {
      groups.push_back({drawable, 0, {}});
      current = drawable;
      open = true;
    }
    const auto groupIndex = groups.size() - 1;
    groups[groupIndex].instances += 1;
    groups[groupIndex].members.push_back(index);
    instances[index].instanceGroup = groupIndex;
  }
  return groups;
}

bool terrainScatterWantsCollision(const TerrainScatterInstance &instance) {
  return instance.collision != TerrainCollisionPolicy::None;
}

std::string_view
terrainScatterCollisionSkipReason(const TerrainScatterInstance &instance) {
  if (terrainScatterWantsCollision(instance))
    return {};
  return "palette collision policy is none";
}

int terrainScatterLodFor(const TerrainScatterInstance &instance, float distance,
                         float lodDistance, float cullDistance) {
  const auto levels = std::max(1, instance.lod + 1);
  const auto coarsest = levels - 1;
  // No LOD distance means no distance switching, not "already at the far end".
  if (!(lodDistance > 0.F))
    return 0;
  const auto reach = std::max(0.F, distance);
  if (reach <= lodDistance)
    return 0;
  const auto span = std::max(0.F, cullDistance) - lodDistance;
  if (!(span > 0.F))
    return coarsest;
  const auto t = (reach - lodDistance) / span;
  // Clamped on both sides, so no camera position, however wrong, can produce an
  // index a mesh array does not have.
  return std::clamp(static_cast<int>(t * float(levels)), 0, coarsest);
}

} // namespace demi::runtime
