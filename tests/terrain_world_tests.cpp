#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace demi::runtime;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

TerrainRecipe smallRecipe() {
  TerrainRecipe recipe;
  recipe.size = {4, 4};
  recipe.cellsX = recipe.cellsZ = 4;
  recipe.chunkCells = 2;
  recipe.landforms.at("default").heightVariation = 0;
  recipe.landforms.at("default").baseHeight = 1;
  return recipe;
}

void testWorld() {
  World world;
  Entity owner;
  owner.id = "land";
  owner.sceneOwner = "scene://terrain";
  owner.setComponent(Transform3DComponent{
      .position = {10, 3, 20}, .rotation = {0, .5F, 0}, .scale = {2, 3, 2}});
  Terrain3DComponent terrain;
  terrain.recipe = smallRecipe().toJson();
  const auto recipe = terrain.recipe;
  owner.setComponent(std::move(terrain));
  world.entities.push_back(std::move(owner));
  std::string error;
  check(materializeTerrains(world, error), error.c_str());
  check(world.entities.size() == 5, "Expected four generated chunks");
  const auto *retained = world.entities[0].component<Terrain3DComponent>();
  check(retained->generated && retained->generated == findTerrain(recipe),
        "World did not retain the shared cache field");
  check(retained->recipe == recipe, "Materialization changed authored recipe");
  std::vector<std::string> ids;
  for (std::size_t index = 1; index < world.entities.size(); ++index) {
    const Entity &child = world.entities[index];
    ids.push_back(child.id);
    const auto *mesh = child.component<MeshRendererComponent>();
    const auto *triangles = resolvedTriangleCollider3D(world, child);
    check(mesh && triangles && triangles->size() * 3 == mesh->vertices.size(),
          "Collision does not match render triangle count");
    check(!child.hasComponent<Rigidbody3DComponent>(), "Terrain is not static");
    check(child.sceneOwner == "scene://terrain" &&
              child.component<Transform3DComponent>()->parent == "land" &&
              terrainSurfaceOwner(child) == "land",
          "Generated child lost scene or parent ownership");
    check(mesh->normals.size() == mesh->vertices.size() &&
              mesh->uvs.size() == mesh->vertices.size(),
          "Generated normals or UVs missing");
    for (std::size_t vertex = 0; vertex < mesh->vertices.size(); ++vertex) {
      const auto point = mesh->vertices[vertex];
      const auto sample = retained->generated->index(static_cast<int>(point.x),
                                                     static_cast<int>(point.z));
      const auto expectedNormal = retained->generated->normals[sample];
      check(point.y == retained->generated->heights[sample] &&
                mesh->normals[vertex].x == expectedNormal.x &&
                mesh->normals[vertex].y == expectedNormal.y &&
                mesh->normals[vertex].z == expectedNormal.z,
            "Chunk borders diverged from shared height/normal samples");
    }
    for (std::size_t triangle = 0; triangle < triangles->size(); ++triangle) {
      const auto &collision = (*triangles)[triangle];
      const Vec3 points[]{collision.a, collision.b, collision.c};
      for (std::size_t corner = 0; corner < 3; ++corner) {
        const auto vertex = mesh->vertices[triangle * 3 + corner];
        check(points[corner].x == vertex.x && points[corner].y == vertex.y &&
                  points[corner].z == vertex.z,
              "Collider triangle differs from mesh triangle");
      }
    }
    const auto transform = resolveWorldTransform3D(world, child);
    check(transform && transform->position.x == 10 &&
              transform->position.y == 3 && transform->scale.y == 3,
          "Terrain child did not inherit owner transform");
  }
  auto &physics = ensurePhysicsWorld3D(world);
  if (physics.available()) {
    physics.step(world, 1.F / 60);
    const auto transform = resolveWorldTransform3D(world, world.entities[1]);
    const auto point = transformPoint3D(*transform, {.25F, 1, .25F});
    const auto hit =
        physics.raycast({point.x, point.y + 10, point.z}, {0, -1, 0}, 20);
    check(hit && std::abs(hit->distance - 10) < .01F,
          "Static triangle collision missed transformed terrain surface");
  }
  check(materializeTerrains(world, error), error.c_str());
  check(world.entities.size() == 5, "Regeneration duplicated children");
  for (std::size_t index = 1; index < world.entities.size(); ++index)
    check(world.entities[index].id == ids[index - 1], "Generated ID changed");
  Entity ancestor;
  ancestor.id = "container";
  ancestor.enabled = false;
  ancestor.setComponent(Transform3DComponent{});
  world.entities[0].component<Transform3DComponent>()->parent = "container";
  world.entities.push_back(std::move(ancestor));
  synchronizeTerrainVisibility(world);
  check(!world.entities[1].enabled, "Disabled ancestor left terrain visible");
  world.entities.back().enabled = true;
  synchronizeTerrainVisibility(world);
  check(world.entities[1].enabled, "Enabling ancestor did not restore terrain");
  world.entities[0].enabled = false;
  synchronizeTerrainVisibility(world);
  check(!world.entities[1].enabled, "Disabled owner left terrain visible");
  world.entities[0].enabled = true;
  world.entities[0].component<Transform3DComponent>()->parent.clear();
  world.entities.pop_back();
  const auto previous =
      world.entities[0].component<Terrain3DComponent>()->generated;
  world.entities[0].component<Terrain3DComponent>()->recipe["resolution"] = {0,
                                                                             4};
  check(!materializeTerrains(world, error), "Invalid recipe was accepted");
  check(world.entities.size() == 5 &&
            world.entities[0].component<Terrain3DComponent>()->generated ==
                previous,
        "Failed generation replaced the previous terrain");
  world.entities.erase(world.entities.begin());
  check(materializeTerrains(world, error) && world.entities.empty(),
        "Deleted terrain owner left orphan children");
}

void testSceneAndBiomeGroups() {
  auto recipe = smallRecipe();
  recipe.chunkCells = 4;
  recipe.biomes.emplace("red/rock", TerrainBiome{.color = {1, 0, 0, 1}});
  auto generated = TerrainGenerator::generate(recipe);
  check(generated.has_value(), "Generation failed");
  // Publish a known worker result to probe grouping/cache consumption directly.
  for (int z = 0; z <= generated->cellsZ; ++z)
    for (int x = 0; x <= generated->cellsX; ++x)
      generated->biomeIndices.set(generated->index(x, z), x < 2 ? 0 : 1);
  auto field = std::make_shared<const HeightField>(std::move(*generated));
  const auto recipeJson = recipe.toJson();
  publishTerrain(recipeJson, "", field);
  ProjectData project;
  project.projectDirectory = DEMI_SOURCE_DIR;
  project.scenes = {{.id = "scene://terrain", .path = "terrain.scene.json"}};
  const nlohmann::json document{
      {"format_version", 1},
      {"id", "scene://terrain"},
      {"entities",
       {{{"id", "land"},
         {"components",
          {{"Transform3D", nlohmann::json::object()},
           {"Terrain3D", {{"recipe", recipeJson}}}}}}}}};
  const auto original = document.dump();
  std::string error;
  auto scene = loadSceneDocument(project, "scene://terrain", document, error);
  auto preview =
      loadSceneDocument(project, "scene://terrain", document, error, false);
  check(scene && preview, error.c_str());
  check(document.dump() == original, "Scene load changed authored JSON");
  check(scene->entities.size() == 3 && preview->entities.size() == 3,
        "One chunk did not preserve both biome groups in runtime/editor");
  check(scene->entities[0].component<Terrain3DComponent>()->generated ==
                field &&
            preview->entities[0].component<Terrain3DComponent>()->generated ==
                field,
        "Runtime/editor did not consume published worker field");
  check(findEntity(*scene, "land/__terrain/0_0/red%2frock") != nullptr,
        "Biome ID was not encoded stably");
  std::size_t triangleCount = 0;
  for (std::size_t index = 1; index < scene->entities.size(); ++index)
    triangleCount +=
        resolvedTriangleCollider3D(*scene, scene->entities[index])->size();
  check(triangleCount == 32, "Biome grouping lost or duplicated triangles");
  auto prefabProject = project;
  prefabProject.scenes = {
      {.id = "scene://editor-prefab-preview", .path = "terrain.prefab.json"}};
  auto prefabDocument = document;
  prefabDocument["id"] = "prefab://terrain";
  const auto prefabSource = prefabDocument.dump();
  auto prefabPreview =
      loadSceneDocument(prefabProject, "scene://editor-prefab-preview",
                        prefabDocument, error, false);
  check(prefabPreview && prefabPreview->entities.size() == 3 &&
            prefabDocument.dump() == prefabSource,
        "Prefab preview failed or changed authored source");
  Entity conflict;
  conflict.id = "land/__terrain/0_0/default";
  scene->entities.push_back(std::move(conflict));
  check(!materializeTerrains(*scene, error) && scene->entities.size() == 4,
        "ID conflict overwrote an authored entity");
  scene->entities.pop_back();
  scene->entities[0].removeComponent<Terrain3DComponent>();
  check(materializeTerrains(*scene, error) && scene->entities.size() == 1,
        "Removing terrain component left orphan children");
}

void testMeshHelper() {
  Entity owner;
  owner.id = "land";
  const std::vector<Vec3> vertices{{0, 0, 0}, {0, 0, 1}, {1, 0, 0}};
  const std::vector<Vec3> normals(3, {0, 1, 0});
  const std::vector<Vec2> uvs{{0, 0}, {0, 1}, {1, 0}};
  std::string error;
  auto entity = buildTerrainMeshEntity(owner, "land/__terrain/test", vertices,
                                       normals, uvs, {1, .5F, 0, 1},
                                       "asset://terrain/material", error);
  check(entity.has_value(), error.c_str());
  const auto *mesh = entity->component<MeshRendererComponent>();
  check(mesh && mesh->material == "asset://terrain/material" &&
            mesh->color.g == .5F && mesh->hasBounds && mesh->boundsMax.x == 1,
        "Mesh helper lost material/tint/bounds");
  check(!buildTerrainMeshEntity(owner, "bad", vertices, {}, uvs, {}, {}, error),
        "Mesh helper accepted mismatched arrays");
}

void testMissingPaletteInputsLeaveWorldIntact() {
  World world;
  Entity owner;
  owner.id = "palette_owner";
  owner.setComponent(Transform3DComponent{});
  Terrain3DComponent terrain;
  auto recipe = smallRecipe();
  recipe.paletteId = "asset://terrain/palettes/missing";
  terrain.recipe = recipe.toJson();
  owner.setComponent(std::move(terrain));
  world.entities.push_back(std::move(owner));
  std::string error;
  check(!materializeTerrains(world, error),
        "Unresolved palette generated a field without scatter inputs");
  check(error.find("must be resolved") != std::string::npos &&
            world.entities.size() == 1 &&
            !world.entities.front().component<Terrain3DComponent>()->generated,
        "Failed palette resolution changed the world");
}
} // namespace

int main() {
  try {
    testWorld();
    testSceneAndBiomeGroups();
    testMeshHelper();
    testMissingPaletteInputsLeaveWorldIntact();
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
