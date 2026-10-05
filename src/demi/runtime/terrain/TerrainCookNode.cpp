#include "demi/runtime/terrain/TerrainCookNode.h"
#include <algorithm>

namespace demi::runtime {
namespace {

// The recipe is the cooked terrain's source. Its digest, generator version and
// referenced asset fingerprint are independent importer inputs.
constexpr const char *TerrainImporter = "terrain_heightfield";

} // namespace

TerrainCookNodeRequest buildTerrainCookNode(const TerrainCookRequest &request) {
  const auto provenance = terrainCookProvenance(request.input);
  TerrainCookNodeRequest built;
  built.node.assetId = request.assetId;
  built.node.importer = TerrainImporter;
  // The generator version travels as the importer version, so a cooked terrain
  // is invalidated by the same mechanism that invalidates any other asset whose
  // importer changed.
  built.node.importerVersion = terrainCookVersion;
  built.node.sourceHashes = {provenance.recipeDigest,
                             provenance.inputFingerprint};
  built.node.normalizedSettings = provenance.generatorVersionTag;
  built.node.platform = request.platform;
  built.node.profile = request.profile;
  if (!request.paletteId.empty())
    built.node.dependencies.push_back(request.paletteId);
  built.key = request.assetId.empty()
                  ? provenance.recipeKey
                  : request.assetId + "|" + provenance.recipeKey;
  return built;
}

std::vector<std::string>
terrainCookDependencies(const TerrainCookRequest &request,
                        const AssetRegistry &registry) {
  std::vector<std::string> dependencies;
  if (request.paletteId.empty())
    return dependencies;
  // Only a dependency that actually resolves is recorded, because a node naming
  // a missing asset would make the graph unsatisfiable and take the whole cook
  // down with it. The missing reference is the registry's problem to report.
  if (findAsset(registry, request.paletteId) != nullptr)
    dependencies.push_back(request.paletteId);
  return dependencies;
}

std::optional<TerrainCookedField>
cookTerrainAsset(const TerrainCookRequest &request, std::string &error) {
  if (request.input.recipeDigest.empty()) {
    error = "Terrain cook request has no canonical recipe digest.";
    return std::nullopt;
  }
  auto cooked = cookTerrainField(request.input, &error);
  if (!cooked) {
    error = "Terrain cook failed: " + error;
    return std::nullopt;
  }
  return cooked;
}

} // namespace demi::runtime
