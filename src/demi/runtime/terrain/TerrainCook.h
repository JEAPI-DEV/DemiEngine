#pragma once

#include "demi/runtime/terrain/TerrainGeneration.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace demi::runtime {

// Bumped when the on-disk layout changes incompatibly. A file carrying a
// different major version is stale, not an error to guess at.
//
// This version is INDEPENDENT of terrainGeneratorVersion. A generator change
// invalidates cached data without changing how it is stored, and a layout change
// invalidates every file without the generator moving, so folding them together
// would make one of the two reasons for rebuilding unreportable.
inline constexpr int terrainCookVersion = 2;

// Hashes the canonical parsed recipe, including seed, rules, edits and preset
// identity. A field's samples or asset fingerprint cannot prove those inputs.
[[nodiscard]] std::string terrainCookRecipeDigest(const TerrainRecipe &recipe);

struct TerrainCookInput {
  const HeightField *field = nullptr;
  const TerrainMasks *masks = nullptr;   // optional; absent is recorded, not faked
  TerrainWaterLevel water{};
  std::string generatorVersionTag;       // e.g. terrainGeneratorVersion as text
  std::string recipeDigest;              // terrainCookRecipeDigest(parsed recipe)
  std::string inputFingerprint;          // content hash of referenced assets
};

// A packed, little-endian, explicitly sized payload. Floats are written with a
// documented order and the header records it, so a build that changes endianness
// or float handling cannot silently read a file it cannot interpret.
//
// Mask vectors are populated only when masksPresent is true, and then only when
// they cover the whole grid. An absent mask set records its absence instead of
// storing zeros, because a zeroed mask is a claim about the terrain that nothing
// computed.
struct TerrainCookedField {
  int version = terrainCookVersion;
  Vec2 size{}; int cellsX = 0, cellsZ = 0;
  std::vector<float> baseHeights, heights, normalsX, normalsY, normalsZ;
  std::vector<std::uint32_t> biomeIndices, substrates;
  std::vector<float> slope, moisture, waterDistance, flow, sediment, exclusions;
  std::vector<std::string> biomeIds;
  std::vector<Color> biomeColors;
  std::vector<TerrainChunk> chunks;
  // Provenance, so a stale file can be told from a fresh one without a diff.
  std::string generatorVersionTag, recipeDigest, inputFingerprint, stageOrder,
      quality, paletteId;
  float seaLevel = 0;
  std::uint64_t contentHash = 0;   // over every byte above, excluding this field
  bool masksPresent = false;
};

// Cooks a live field (and optionally its derived masks) into a packed payload.
//
// The result is exactly what publishTerrain() would accept: dense sample arrays
// matching the grid, finite heights, upward unit normals, in-range biome indices
// and chunk rectangles inside the grid. A cook that could not be published at
// runtime is not worth writing to disk.
//
// Empty on a null field, on a field that would not survive publication, and on
// masks that only partly cover the grid. Pass `error` to learn which.
[[nodiscard]] std::optional<TerrainCookedField>
cookTerrainField(const TerrainCookInput &input, std::string *error = nullptr);

// Serializes to the packed on-disk form. Every block carries its own count, so
// a struct whose arrays disagree with its grid still produces a readable file;
// it is deserializeTerrainCookedField() that refuses to believe it.
[[nodiscard]] std::vector<std::byte>
serializeTerrainCookedField(const TerrainCookedField &field);

// Reads a packed payload. The file is untrusted input: every count is validated
// against the remaining bytes BEFORE anything is allocated, and a NaN or
// infinite height or mask is refused because it propagates into the mesh and the
// physics. Empty with a specific reason in `error`.
//
// A payload written by a different terrainCookVersion is refused rather than
// reinterpreted: guessing at a layout is how a stale file becomes a corrupt
// heightfield.
[[nodiscard]] std::optional<TerrainCookedField>
deserializeTerrainCookedField(std::span<const std::byte> bytes,
                              std::string *error = nullptr);

// Reloads into a live field. A loader that returns a field with a different
// digest, grid or provenance than the file is a bug, so it is checked: the
// reloaded field is re-cooked and compared against the file, and a mismatch
// throws std::logic_error rather than returning a field nobody can explain.
//
// Derived masks and scatter placements are not part of a HeightField. Masks are
// read back through TerrainCookedField directly, and placements are re-derived
// from the field and the palette rather than restored from the cache.
[[nodiscard]] std::shared_ptr<const HeightField>
loadTerrainCookedField(const TerrainCookedField &field);

// True when the cached file cannot be trusted: version, generator tag, recipe
// digest, input fingerprint, grid, or content hash disagree.
//
// Returns an empty string when the file is fresh, otherwise a stable reason code
// so the editor can say WHY a cache was dropped rather than just dropping it:
// "version", "generator_version", "recipe_digest", "input_fingerprint",
// "grid", "content_hash", "input_field_missing".
//
// This answers "can this file be trusted for these inputs", not "is this file
// the surface the caller wanted". The caller normally asks before generating,
// so there is no field yet to compare samples against, and the recipe the field
// will come from is supplied as an explicit canonical digest. The input
// fingerprint separately ties the recipe to its referenced asset contents.
[[nodiscard]] std::string terrainCookStaleness(const TerrainCookedField &cached,
                                               const TerrainCookInput &input);

// The provenance a cook node needs from this format, so AssetCooker can register
// a cooked heightfield as an ordinary asset with the ordinary graph and cache
// rather than beside them. A cooked terrain has no source file, so the fields
// AssetCookNode derives its key from are the generator tag, canonical recipe
// digest, and input fingerprint. Adding a node here and a payload under its outputs is all a
// terrain cook needs to participate in the existing cache-hit, dependency
// invalidation and package-content audit paths.
struct TerrainCookProvenance {
  std::string generatorVersionTag;
  std::string recipeDigest;
  std::string inputFingerprint;
  std::string recipeKey;
};
// Throws std::invalid_argument when the caller has not supplied a recipe digest.
[[nodiscard]] TerrainCookProvenance
terrainCookProvenance(const TerrainCookInput &input);

} // namespace demi::runtime
