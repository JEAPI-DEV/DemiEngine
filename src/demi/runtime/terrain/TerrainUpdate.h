#pragma once

#include "demi/runtime/terrain/TerrainGenerator.h"
#include <memory>

namespace demi::runtime {
struct TerrainGenerationInputs;

// Inclusive sample coordinates. Empty is represented by maximum < minimum.
struct TerrainRect {
  int minX = 0, minZ = 0, maxX = -1, maxZ = -1;
  bool empty() const { return maxX < minX || maxZ < minZ; }
  bool contains(int x, int z) const;
  bool intersects(const TerrainRect &other) const;
  void include(int x, int z);
  void include(const TerrainRect &other);
  TerrainRect expanded(int samples, int cellsX, int cellsZ) const;
};

struct TerrainInvalidation {
  bool fullGeneration = false;
  bool layoutChanged = false;
  bool materialsChanged = false;
  TerrainRect baseSamples;
  TerrainRect heightSamples;
  TerrainRect normalSamples;
  TerrainRect biomeSamples;
  TerrainRect exclusionSamples;
  std::string reason;
  TerrainRect geometrySamples() const;
};

struct TerrainSampleValue {
  float base = 0, height = 0, exclusion = 0;
  Vec3 normal{};
  std::size_t biome = 0;
};

struct TerrainSampleChange {
  std::size_t index = 0;
  TerrainSampleValue before, after;
};

struct TerrainBiomePalette {
  std::vector<std::string> ids;
  std::vector<Color> colors;
  std::vector<std::string> materials;
  std::vector<float> textureScales;
};

struct TerrainDerivedState {
  std::vector<TerrainScatterPlacement> placements;
  bool scatterTruncated = false;
  std::shared_ptr<const TerrainGraphArtifacts> graphArtifacts;
};

struct TerrainPatch {
  Vec2 size{};
  int cellsX = 0, cellsZ = 0;
  std::vector<TerrainSampleChange> samples;
  std::optional<TerrainBiomePalette> beforePalette, afterPalette;
  std::optional<TerrainDerivedState> beforeDerived, afterDerived;
  // Only global generation/layout changes need whole-field history snapshots.
  // Local commands retain sample, palette and derived placement/water deltas.
  std::shared_ptr<const HeightField> fullBefore, fullAfter;
  TerrainInvalidation invalidation;
  std::size_t retainedBytes() const;
};

struct TerrainUpdateStats {
  std::size_t baseEvaluations = 0;
  std::size_t editEvaluations = 0;
  std::size_t normalEvaluations = 0;
  std::size_t changedSamples = 0;
};

struct TerrainUpdate {
  std::shared_ptr<const HeightField> field;
  std::shared_ptr<const TerrainPatch> patch;
  TerrainInvalidation invalidation;
  TerrainUpdateStats stats;
};

// Exact local replay from the retained base checkpoint. Global recipe changes
// are explicitly classified; stop returns no update and never changes input.
std::optional<TerrainUpdate>
updateTerrain(const TerrainRecipe &before, const TerrainRecipe &after,
              std::shared_ptr<const HeightField> previous,
              std::stop_token stop = {},
              const TerrainGenerator::Progress &progress = {});
// The supplied snapshot belongs to the after-recipe. Changing its fingerprint
// forces a global rebuild; unchanged graph nodes reuse the previous node cache.
std::optional<TerrainUpdate>
updateTerrainWithInputs(const TerrainRecipe &before, const TerrainRecipe &after,
                        std::shared_ptr<const HeightField> previous,
                        const TerrainGenerationInputs &inputs,
                        std::stop_token stop = {},
                        const TerrainGenerator::Progress &progress = {});

TerrainUpdate applyTerrainPatch(std::shared_ptr<const HeightField> current,
                               const TerrainPatch &patch, bool forward);
bool terrainFieldsEqual(const HeightField &left, const HeightField &right);
std::shared_ptr<const TerrainPatch>
mergeTerrainPatches(const TerrainPatch &first, const TerrainPatch &next);

// Future nonlocal simulation stages must request this explicitly rather than
// incorrectly using a local brush footprint for erosion/drainage propagation.
std::optional<TerrainUpdate>
regenerateTerrain(const TerrainRecipe &recipe,
                  std::shared_ptr<const HeightField> previous,
                  std::string reason, std::stop_token stop = {},
                  const TerrainGenerator::Progress &progress = {});
std::optional<TerrainUpdate>
regenerateTerrainWithInputs(const TerrainRecipe &recipe,
                            std::shared_ptr<const HeightField> previous,
                            std::string reason,
                            const TerrainGenerationInputs &inputs,
                            std::stop_token stop = {},
                            const TerrainGenerator::Progress &progress = {});
} // namespace demi::runtime
