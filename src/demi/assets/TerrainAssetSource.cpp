#include "demi/assets/TerrainAsset.h"

#include "demi/runtime/terrain/TerrainRecipe.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace demi::assets {
namespace {

void require(bool condition, std::string_view reason) {
  if (!condition)
    throw std::invalid_argument("Terrain asset: " + std::string(reason));
}

} // namespace

TerrainAssetSource parseTerrainAssetSource(const nlohmann::json &document) {
  require(document.is_object(), "source must be an object");
  for (const auto &[name, ignored] : document.items()) {
    (void)ignored;
    require(name == "format_version" || name == "id" || name == "name" ||
                name == "recipe",
            "unknown source field " + name);
  }
  require(document.contains("format_version") &&
              document.at("format_version").is_number_integer() &&
              document.at("format_version") == 1,
          "format_version must be 1");
  require(document.contains("id") && document.at("id").is_string(),
          "id must be an asset:// string");
  TerrainAssetSource source;
  source.id = document.at("id").get<std::string>();
  require(source.id.starts_with("asset://") && source.id.size() > 8,
          "id must be a nonempty asset:// reference");
  if (document.contains("name")) {
    require(document.at("name").is_string(), "name must be a string");
    source.name = document.at("name").get<std::string>();
  }
  require(document.contains("recipe") && document.at("recipe").is_object(),
          "recipe must be an object");
  source.recipe = document.at("recipe");
  (void)runtime::TerrainRecipe::parse(source.recipe);
  return source;
}

TerrainAssetSource loadTerrainAssetSource(const AssetManifest &manifest) {
  require(manifest.type == "Terrain", "manifest type must be Terrain");
  require(manifest.importer == "terrain_heightfield",
          "manifest importer must be terrain_heightfield");
  require(manifest.sourcePath.filename().string().ends_with(".terrain.json"),
          "editable source must end in .terrain.json");
  std::ifstream input(manifest.sourcePath, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot read terrain source " +
                             manifest.sourcePath.string());
  nlohmann::json document;
  input >> document;
  if (!input && !input.eof())
    throw std::runtime_error("Cannot read complete terrain source " +
                             manifest.sourcePath.string());
  auto source = parseTerrainAssetSource(document);
  require(source.id == manifest.id,
          "source id differs from its asset manifest id");
  return source;
}

nlohmann::json terrainAssetSourceJson(const TerrainAssetSource &source) {
  auto document = nlohmann::json{{"format_version", source.formatVersion},
                                 {"id", source.id},
                                 {"recipe", source.recipe}};
  if (!source.name.empty())
    document["name"] = source.name;
  (void)parseTerrainAssetSource(document);
  return document;
}

std::vector<std::string>
terrainAssetDependencies(const TerrainAssetSource &source) {
  std::set<std::string> dependencies;
  for (const auto &reference : extractAssetReferences(source.recipe.dump()))
    if (reference != source.id)
      dependencies.insert(reference);
  return {dependencies.begin(), dependencies.end()};
}

} // namespace demi::assets
