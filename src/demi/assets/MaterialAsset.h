#pragma once

#include "demi/assets/DataAsset.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::assets {

// The closed set of texture slots a terrain material may fill. The vocabulary is
// fixed so a misspelled slot is a load error instead of a map that silently
// never reaches the shader. Height is shading only: parallax or displacement is
// a separate concern from surface relief and belongs to the geometry, never to a
// slot that quietly moves vertices.
enum class TerrainMaterialMapSlot {
  BaseColor,
  Normal,
  Roughness,
  Metallic,
  AmbientOcclusion,
  Height,
  Detail,
  Emissive,
};

// One imported PBR terrain material. It is a DataAsset document with
// settings.content_type "terrain_material", not a new document kind, so cook,
// packaging, dependency closure, demi validate, Lua Data.load and hot reload all
// apply without a second implementation.
//
// Maps are stored slot-keyed, so lookup is deterministic and an absent slot is
// simply absent rather than a null texture the renderer has to special-case.
struct TerrainMaterialAsset {
  static constexpr int formatVersion = 1;
  // The asset:// id the material was loaded from.
  std::string id;
  std::string name;
  // Linear base colour. A colour texture is expected to be imported into linear
  // space; this is the tint and the fallback when no map is assigned, not a
  // second sRGB value.
  runtime::Color baseColor{.8F, .8F, .8F, 1.F};
  float roughness = 0.8F;
  float metallic = 0.F;
  float normalStrength = 1.F;
  // Texture repeats across one authored surface unit. 1 keeps authored scale,
  // greater than 1 tiles closer together.
  float tiling = 1.F;
  float detailStrength = 0.F;
  // Samples all three axes by world position, which is what a near-vertical
  // cliff face needs; a flat projection would smear its texture.
  bool triplanar = false;
  std::map<TerrainMaterialMapSlot, std::string, std::less<>> maps;

  // The authored document, with omitted fields left omitted rather than
  // materialized, so a round trip through the parser is lossless.
  [[nodiscard]] nlohmann::json toJson() const;
  // Every asset:// this material depends on, sorted and deduped.
  [[nodiscard]] std::vector<std::string> assetDependencies() const;
};

[[nodiscard]] std::string_view
terrainMaterialMapSlotName(TerrainMaterialMapSlot slot);
[[nodiscard]] std::optional<TerrainMaterialMapSlot>
terrainMaterialMapSlotFromName(std::string_view name);

// Loads and fully validates a terrain_material DataAsset. Throws
// std::invalid_argument with an actionable message on any problem, including a
// missing id or a manifest that is not a terrain_material DataAsset. Manifest
// dependency declaration is left to validateDataAssets(), which owns it.
[[nodiscard]] std::optional<TerrainMaterialAsset>
loadTerrainMaterialAsset(const AssetRegistry &registry, std::string_view id);

// Validates an already-parsed material document. A consumer that already holds
// the document, such as Lua Data.load or a hot-reloaded snapshot, must not have
// to re-read the file to reach the same rules. The registry is required because
// a map that does not resolve is a load error, not a deferred render warning.
[[nodiscard]] std::optional<TerrainMaterialAsset>
parseTerrainMaterialAsset(const DataDocument &document,
                          const AssetRegistry &registry, std::string_view id);

namespace material_detail {

// Shared with MaterialSet.cpp, which binds the same kind of DataAsset. Not part
// of the material contract: duplicating these rules would let the material and
// the set disagree about what a rejected document says.
[[noreturn]] void fail(std::string message);
void require(bool condition, std::string message);
// "<kind> <where>: " so every message names the document and the exact location
// of the mistake.
std::string at(std::string_view kind, std::string_view where);
std::string summarize(const Diagnostics &diagnostics);
// Loads a manifest and asserts it is a DataAsset of `contentType`, so a caller
// cannot accidentally read a different document kind as a material.
[[nodiscard]] std::optional<LoadedDataAsset>
loadDataAssetOfType(const AssetRegistry &registry, std::string_view id,
                    std::string_view kind, std::string_view contentType);
// A required string field, reported separately from an empty one so a wrong type
// and a blank value are different authoring mistakes.
[[nodiscard]] std::string readString(const DataValue &value,
                                     std::string_view kind,
                                     std::string_view where);

} // namespace material_detail

} // namespace demi::assets