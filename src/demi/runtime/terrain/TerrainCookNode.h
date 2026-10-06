#pragma once

#include "demi/assets/AssetCookGraph.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/terrain/TerrainCook.h"
#include <string>
#include <vector>

namespace demi::runtime {

// Routing a cooked heightfield through the existing cook pipeline.
//
// A cooked terrain has no source file, so nothing in the asset graph would
// produce one on its own. This supplies the seam: the recipe itself is the
// source, its canonical digest, generator version and asset fingerprint are the importer inputs,
// and the palette is the dependency. The payload is registered as an output so
// AssetCookCache stores its hash and the package-content audit picks it up,
// rather than inventing a second packaging path.
struct TerrainCookRequest {
  // The asset the recipe lives on, or empty for a recipe that is not itself an
  // asset. Either way it is what the node is keyed on.
  std::string assetId;
  TerrainCookInput input;
  std::string platform;
  std::string profile = "default";
  // The palette the recipe names, when it names one. Recorded as a dependency so
  // editing the palette invalidates the cooked terrain.
  std::string paletteId;
};

struct TerrainCookNodeRequest {
  assets::AssetCookNode node;
  // The key the cache stores this node's outputs under.
  [[nodiscard]] std::string key;
};

// Builds the cook node for a terrain. Pure: it reads the request and produces a
// node, so the graph can be built and inspected without cooking anything.
[[nodiscard]] TerrainCookNodeRequest
buildTerrainCookNode(const TerrainCookRequest &request);

// Every asset a node must be cooked after, transitively. The palette is
// included, so a palette edit rebuilds the terrain rather than serving a field
// that was scattered against the old one.
[[nodiscard]] std::vector<std::string>
terrainCookDependencies(const TerrainCookRequest &request,
                        const AssetRegistry &registry);

// Cooks the request and reports what was produced. Returns nullopt only when the
// terrain itself cannot be cooked; a cache hit is a success, not a failure.
[[nodiscard]] std::optional<TerrainCookedField>
cookTerrainAsset(const TerrainCookRequest &request, std::string &error);

} // namespace demi::runtime
