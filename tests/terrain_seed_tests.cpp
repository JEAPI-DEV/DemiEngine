#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainSeed.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace demi::runtime;

namespace {
constexpr TerrainSeedChannel channels[]{
    TerrainSeedChannel::Landform, TerrainSeedChannel::Erosion,
    TerrainSeedChannel::Hydrology, TerrainSeedChannel::BiomePlacement,
    TerrainSeedChannel::Scatter};

constexpr int sampleSeeds[]{-2147483648, -70000, -1, 0, 1, 7, 1337, 2147483647};

// Derivation must be a pure function of (world seed, channel).
void derivationIsDeterministic() {
  for (const int world : sampleSeeds)
    for (const auto channel : channels) {
      const int first = deriveTerrainSubSeed(world, channel);
      const int second = deriveTerrainSubSeed(world, channel);
      assert(first == second);
      // Never zero, so a derived seed is distinguishable from a default one.
      assert(first != 0);
      // Must be representable as the signed int FastNoiseLite accepts.
      assert(first > 0);
    }
}

// Distinct channels must not collide, or landform work would reuse the scatter
// field and later stages would silently correlate.
void channelsAreDistinct() {
  for (const int world : sampleSeeds) {
    std::set<int> seen;
    for (const auto channel : channels)
      assert(seen.insert(deriveTerrainSubSeed(world, channel)).second);
  }
}

// Different world seeds must diverge. Nearby seeds are the common failure case
// for weak mixes, which would make neighbouring worlds look almost identical.
void worldSeedsDiverge() {
  for (const auto channel : channels) {
    std::set<int> seen;
    for (int world = -64; world <= 64; ++world)
      assert(seen.insert(deriveTerrainSubSeed(world, channel)).second);
    // Adjacent seeds must differ substantially rather than by a small delta.
    const int a = deriveTerrainSubSeed(1000, channel);
    const int b = deriveTerrainSubSeed(1001, channel);
    const int difference = a > b ? a - b : b - a;
    assert(difference > 1024);
  }
}

// The sub-seed is derived from the channel NAME, not its ordinal, so inserting
// or reordering channels cannot change an existing channel's value. These
// expected values pin the derivation; changing it is a generator change and
// must be accompanied by a terrainGeneratorVersion bump.
void derivationIsPinned() {
  assert(deriveTerrainSubSeed(1337, TerrainSeedChannel::Landform) == 239678297);
  assert(deriveTerrainSubSeed(1337, TerrainSeedChannel::Erosion) == 244313309);
  assert(deriveTerrainSubSeed(1337, TerrainSeedChannel::Hydrology) == 773731491);
  assert(deriveTerrainSubSeed(1337, TerrainSeedChannel::BiomePlacement) ==
         669271665);
  assert(deriveTerrainSubSeed(1337, TerrainSeedChannel::Scatter) == 560400873);
  assert(deriveTerrainSubSeed(0, TerrainSeedChannel::Landform) == 363374772);
  assert(deriveTerrainSubSeed(-1, TerrainSeedChannel::Landform) == 384420008);
  for (const auto channel : channels)
    assert(!terrainSeedChannelName(channel).empty());
}

// A generator version change must move the cache key, so a heightfield produced
// by an older engine can never be served by a newer one.
void cacheKeyCarriesGeneratorVersion() {
  TerrainRecipe recipe;
  const auto key = terrainGenerationCacheKey(recipe.toJson());
  assert(!key.empty());
  assert(key.rfind(std::to_string(terrainGeneratorVersion) + "\n", 0) == 0);
  // Equal recipes produce equal keys, including after a JSON round trip.
  assert(terrainGenerationCacheKey(recipe.toJson()) == key);
  assert(terrainGenerationCacheKey(TerrainRecipe::parse(recipe.toJson()).toJson()) ==
         key);
  // A real recipe difference still separates entries.
  auto other = recipe;
  other.seed += 1;
  assert(terrainGenerationCacheKey(other.toJson()) != key);
}

// An asset id does not change when the file's contents do. Without a content
// fingerprint in the key, editing a palette's weights or spacing would leave the
// previous heightfield -- carrying the previous placements -- reachable under the
// same key, and nothing would ever notice.
void inputFingerprintMovesTheCacheKey() {
  TerrainRecipe recipe;
  recipe.paletteId = "asset://terrain/palettes/meadow";
  const nlohmann::json json = recipe.toJson();
  const auto base = terrainGenerationCacheKey(json);
  assert(!terrainGenerationCacheKey(json, "fnv1a64:aaaa").empty());
  // Any difference in the referenced content is a different key.
  assert(terrainGenerationCacheKey(json, "fnv1a64:aaaa") != base);
  assert(terrainGenerationCacheKey(json, "fnv1a64:bbbb") !=
         terrainGenerationCacheKey(json, "fnv1a64:aaaa"));
  // The same content reproduces the same key, so the cache still hits.
  assert(terrainGenerationCacheKey(json, "fnv1a64:aaaa") ==
         terrainGenerationCacheKey(json, "fnv1a64:aaaa"));
  // A recipe with no palette is given an empty fingerprint by the caller, and
  // that must still key stably so the ordinary path keeps hitting the cache.
  const nlohmann::json plain = TerrainRecipe::defaults();
  assert(terrainGenerationCacheKey(plain) == terrainGenerationCacheKey(plain));
  assert(terrainGenerationCacheKey(plain, {}) ==
         terrainGenerationCacheKey(plain));
}

// Generation is unchanged by the number of evaluators built over one recipe, so
// repeated construction reproduces the same field.
void repeatedEvaluationIsStable() {
  TerrainRecipe recipe;
  recipe.size = {32, 32};
  recipe.cellsX = recipe.cellsZ = 32;
  const auto first = TerrainGenerator::generate(recipe);
  const auto second = TerrainGenerator::generate(recipe);
  assert(first && second);
  assert(first->heights == second->heights);
  assert(first->baseHeights == second->baseHeights);
  assert(first->biomeIndices == second->biomeIndices);
  for (std::size_t i = 0; i < first->normals.size(); ++i) {
    const Vec3 a = first->normals[i];
    const Vec3 b = second->normals[i];
    assert(a.x == b.x && a.y == b.y && a.z == b.z);
  }
}
} // namespace

int main() {
  derivationIsDeterministic();
  channelsAreDistinct();
  worldSeedsDiverge();
  derivationIsPinned();
  cacheKeyCarriesGeneratorVersion();
  repeatedEvaluationIsStable();
  inputFingerprintMovesTheCacheKey();
  std::cout << "Terrain seed checks passed\n";
}
