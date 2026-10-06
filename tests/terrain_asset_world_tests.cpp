#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <iostream>
#include <stdexcept>

using namespace demi::runtime;
using Json = nlohmann::json;

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

Entity owner(const std::string &id, const Json &terrain) {
  std::string error;
  auto parsed = RuntimeObjectModel::buildEntity(
      {{"id", id},
       {"components",
        {{"Transform3D", Json::object()}, {"Terrain3D", terrain}}}},
      error);
  require(bool(parsed), error.c_str());
  return std::move(*parsed);
}

void assetsLoadWithoutProceduralFallback() {
  TerrainRecipe recipe;
  recipe.cellsX = recipe.cellsZ = 8;
  recipe.size = {16, 16};
  recipe.biomes.at("default").material = "asset://ground/stone";
  recipe.biomes.at("default").textureScale = 2;
  auto generated = TerrainGenerator::generate(recipe);
  require(bool(generated), "Could not prepare test field");
  auto field = std::make_shared<const HeightField>(std::move(*generated));
  World world;
  world.entities.push_back(owner("first", {{"asset", "asset://terrain/test"}}));
  world.entities.push_back(
      owner("second", {{"asset", "asset://terrain/test"}}));
  int loads = 0;
  const TerrainInputResolver forbiddenGeneration =
      [](const TerrainRecipe &) -> TerrainGenerationInputs {
    throw std::runtime_error("Asset loading attempted procedural generation");
  };
  const TerrainFieldResolver resolver = [&](std::string_view id, Json &source) {
    require(id == "asset://terrain/test", "Terrain reference changed");
    ++loads;
    source = nullptr;
    return field;
  };
  std::string error;
  require(
      materializeTerrains(world, error, nullptr, forbiddenGeneration, resolver),
      error.c_str());
  require(loads == 2, "Not every placed terrain used its asset");
  for (const auto &entry : world.entities) {
    if (!terrainSurfaceOwner(entry))
      continue;
    const auto *mesh = entry.component<MeshRendererComponent>();
    require(mesh && mesh->material == "asset://ground/stone",
            "Cooked-only terrain lost its native material binding");
    for (std::size_t vertex = 0; vertex < mesh->vertices.size(); ++vertex)
      require(mesh->uvs[vertex].x == mesh->vertices[vertex].x * 2 &&
                  mesh->uvs[vertex].y == mesh->vertices[vertex].z * 2,
              "Cooked-only terrain lost terrain-local texture scale");
  }
  for (const char *id : {"first", "second"}) {
    const auto *terrain =
        findEntity(world, id)->component<Terrain3DComponent>();
    require(terrain->generated == field,
            "Instances did not share prepared data");
    Json serialized;
    require(!Terrain3DComponent::serializeField(*terrain, "recipe", serialized),
            "Resolved recipe leaked into the scene component");
    require(Terrain3DComponent::serializeField(*terrain, "asset", serialized) &&
                serialized == "asset://terrain/test",
            "Asset reference was lost");
  }
  auto edited = recipe;
  edited.edits.push_back({.kind = TerrainEditKind::Raise,
                          .center = {8, 8},
                          .radius = 3,
                          .amount = 1});
  const auto update = updateTerrain(recipe, edited, field);
  require(bool(update), "Could not edit prepared terrain");
  require(updateTerrainWorld(world, "first", edited.toJson(), *update, error),
          error.c_str());
  const auto serialized = Json::parse(
      findEntity(world, "first")->serializedComponents.at("Terrain3D"));
  require(serialized.at("asset") == "asset://terrain/test" &&
              !serialized.contains("recipe"),
          "Terrain editing replaced the serialized asset reference");

  World unprepared;
  unprepared.entities.push_back(
      owner("missing", {{"asset", "asset://terrain/test"}}));
  require(!materializeTerrains(unprepared, error),
          "Missing asset resolver silently generated terrain");
  require(unprepared.entities.size() == 1 &&
              !unprepared.entities.front()
                   .component<Terrain3DComponent>()
                   ->generated,
          "Failed loading partially published terrain");
}

void sourceChoiceIsExplicit() {
  World placeholder;
  placeholder.entities.push_back(owner("empty", Json::object()));
  std::string error;
  require(materializeTerrains(placeholder, error), error.c_str());
  require(placeholder.entities.size() == 1,
          "Empty terrain implicitly generated a surface");
  require(
      !placeholder.entities.front().component<Terrain3DComponent>()->generated,
      "Empty terrain has generated data");
  auto invalid = RuntimeObjectModel::buildEntity(
      {{"id", "invalid"},
       {"components",
        {{"Terrain3D",
          {{"asset", "asset://terrain/test"}, {"recipe", Json::object()}}}}}},
      error);
  require(!invalid, "Asset and procedural sources were accepted together");
}

void emptyAppearanceColumnsUseDefaults() {
  TerrainRecipe recipe;
  recipe.cellsX = recipe.cellsZ = 4;
  recipe.size = {4, 4};
  auto generated = TerrainGenerator::generate(recipe);
  require(bool(generated), "Could not generate default terrain field");
  generated->biomeMaterials.clear();
  generated->biomeTextureScales.clear();
  auto field = std::make_shared<const HeightField>(std::move(*generated));
  World world;
  world.entities.push_back(owner("plain", {{"asset", "asset://terrain/plain"}}));
  const TerrainFieldResolver resolver = [&](std::string_view, Json &source) {
    source = nullptr;
    return field;
  };
  std::string error;
  require(materializeTerrains(world, error, nullptr, {}, resolver),
          error.c_str());
  for (const auto &entry : world.entities) {
    if (!terrainSurfaceOwner(entry))
      continue;
    const auto *mesh = entry.component<MeshRendererComponent>();
    require(mesh && mesh->material.empty(),
            "Empty material column did not use ordinary default");
    for (std::size_t vertex = 0; vertex < mesh->vertices.size(); ++vertex)
      require(mesh->uvs[vertex].x == mesh->vertices[vertex].x &&
                  mesh->uvs[vertex].y == mesh->vertices[vertex].z,
              "Empty texture scale column did not use unit scale");
  }
  auto malformed = std::make_shared<HeightField>(*field);
  malformed->biomeMaterials = {"asset://ground/one", "asset://ground/two"};
  World rejected;
  rejected.entities.push_back(owner("bad", {{"asset", "asset://terrain/plain"}}));
  const TerrainFieldResolver invalidResolver =
      [&](std::string_view, Json &) -> std::shared_ptr<const HeightField> {
    return malformed;
  };
  require(!materializeTerrains(rejected, error, nullptr, {}, invalidResolver) &&
              rejected.entities.size() == 1,
          "Nonempty appearance column with wrong count was published");
}
} // namespace

int main() {
  try {
    assetsLoadWithoutProceduralFallback();
    sourceChoiceIsExplicit();
    emptyAppearanceColumnsUseDefaults();
    std::cout << "Terrain asset world tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
