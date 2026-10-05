#include "demi/runtime/terrain/TerrainCookNode.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include <cassert>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

using namespace demi::runtime;

namespace {
TerrainRecipe recipe() {
  TerrainRecipe r;
  r.size = {32, 32};
  r.cellsX = r.cellsZ = 16;
  r.landforms.at("default").baseHeight = 4;
  return r;
}

// The field must outlive every request that points at it, so it is owned once
// here rather than produced per call and left dangling.
std::shared_ptr<const HeightField> sharedField() {
  static const std::shared_ptr<const HeightField> held = [] {
    auto generated = TerrainGenerator::generate(recipe());
    assert(generated.has_value());
    return std::make_shared<const HeightField>(std::move(*generated));
  }();
  return held;
}

TerrainCookRequest request() {
  TerrainCookRequest r;
  r.assetId = "asset://terrain/recipes/valley";
  r.input.field = sharedField().get();
  r.input.generatorVersionTag = "3";
  r.input.recipeDigest = terrainCookRecipeDigest(recipe());
  r.input.inputFingerprint = "fnv1a64:aaaa";
  r.platform = "linux";
  return r;
}

// The node must key on the recipe, not on a file path, and must carry the
// generator version and fingerprint so a change to either rebuilds.
void nodeKeysOnProvenance() {
  const auto first = buildTerrainCookNode(request());
  const auto again = buildTerrainCookNode(request());
  assert(first.key == again.key);
  assert(first.node.assetId == "asset://terrain/recipes/valley");
  assert(first.node.importer == "terrain_heightfield");
  assert(first.node.importerVersion == terrainCookVersion);
  assert(first.node.normalizedSettings == "3");
  // Two hashes: the canonical recipe and the referenced asset content.
  assert(first.node.sourceHashes.size() == 2);
  assert(first.node.sourceHashes.front() == request().input.recipeDigest);

  // A changed generator tag or fingerprint must move the key, or a stale field
  // would be served across an engine or palette change.
  auto moved = request();
  moved.input.generatorVersionTag = "4";
  assert(buildTerrainCookNode(moved).key != first.key);
  auto reinput = request();
  reinput.input.inputFingerprint = "fnv1a64:bbbb";
  assert(buildTerrainCookNode(reinput).key != first.key);
  auto reseeded = recipe();
  ++reseeded.seed;
  auto changedRecipe = request();
  changedRecipe.input.recipeDigest = terrainCookRecipeDigest(reseeded);
  assert(changedRecipe.input.inputFingerprint == request().input.inputFingerprint);
  assert(buildTerrainCookNode(changedRecipe).key != first.key);
}

// A recipe that is not an asset still gets a stable key.
void keyWorksWithoutAnAssetId() {
  auto bare = request();
  bare.assetId.clear();
  const auto built = buildTerrainCookNode(bare);
  assert(!built.key.empty());
  assert(buildTerrainCookNode(bare).key == built.key);
  assert(built.node.assetId.empty());
}

// The palette is a dependency, so editing it rebuilds the terrain rather than
// serving a field scattered against the old palette.
void paletteIsADependency() {
  auto withPalette = request();
  withPalette.paletteId = "asset://terrain/palettes/meadow";
  assert(buildTerrainCookNode(withPalette).node.dependencies.size() == 1);
  assert(buildTerrainCookNode(withPalette).node.dependencies.front() ==
         withPalette.paletteId);
  // The default request names no palette, so it has no dependencies.
  assert(buildTerrainCookNode(request()).node.dependencies.empty());

  // A palette the registry does not hold is not recorded, because a node naming
  // a missing asset would make the graph unsatisfiable and take the cook with it.
  demi::AssetRegistry empty;
  const auto unresolved =
      terrainCookDependencies(withPalette, empty);
  assert(unresolved.empty());
}

// Cooking through the node path must produce the same payload as cooking
// directly, or routing through the graph would change the bytes.
void cookThroughTheNodeMatchesDirectly() {
  const auto cooked = cookTerrainField(request().input);
  assert(cooked.has_value());
  std::string error;
  const auto throughNode = cookTerrainAsset(request(), error);
  assert(throughNode.has_value());
  assert(error.empty());
  assert(serializeTerrainCookedField(*cooked) ==
         serializeTerrainCookedField(*throughNode));
  assert(throughNode->contentHash == cooked->contentHash);
}

// A request with no field is refused with a reason, not silently cooked empty.
void missingFieldIsRefused() {
  auto broken = request();
  broken.input.field = nullptr;
  std::string error;
  const auto result = cookTerrainAsset(broken, error);
  assert(!result.has_value());
  assert(!error.empty());
}

void missingRecipeProofIsRefused() {
  auto broken = request();
  broken.input.recipeDigest.clear();
  bool rejectedNode = false;
  try {
    (void)buildTerrainCookNode(broken);
  } catch (const std::invalid_argument &) {
    rejectedNode = true;
  }
  assert(rejectedNode);
  std::string error;
  assert(!cookTerrainAsset(broken, error));
  assert(error.find("recipe digest") != std::string::npos);
}
} // namespace

int main() {
  nodeKeysOnProvenance();
  keyWorksWithoutAnAssetId();
  paletteIsADependency();
  cookThroughTheNodeMatchesDirectly();
  missingFieldIsRefused();
  missingRecipeProofIsRefused();
  std::cout << "Terrain cook node checks passed\n";
}
