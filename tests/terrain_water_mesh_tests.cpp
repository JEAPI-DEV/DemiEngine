#include "demi/runtime/terrain/TerrainWaterMesh.h"

#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/CapsuleCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ConvexCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SphereCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainMeshBuilder.h"
#include "demi/runtime/terrain/TerrainWaterDiagnostics.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

using namespace demi::runtime;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool equal(Vec3 left, Vec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

TerrainWaterSurface body(std::string id = "lake") {
  TerrainWaterSurface surface;
  surface.id = std::move(id);
  surface.kind = TerrainWaterBody::Lake;
  surface.level = 2.F;
  surface.vertices = {{-2, 2, 3}, {2, 2, 3}, {-2, 2, 7}, {2, 2, 7}};
  surface.indices = {0, 2, 1, 1, 2, 3};
  surface.normals.assign(surface.vertices.size(), Vec3{0, 1, 0});
  surface.depth = {0, 1, 2, 3};
  return surface;
}

HeightField fieldWith(std::vector<TerrainWaterSurface> surfaces) {
  HeightField field;
  auto artifacts = std::make_shared<TerrainGraphArtifacts>();
  artifacts->waterResult.emplace();
  artifacts->waterResult->surfaces = std::move(surfaces);
  field.graphArtifacts = std::move(artifacts);
  return field;
}

Entity terrainOwner() {
  Entity owner;
  owner.id = "island/land";
  owner.name = "Authored terrain";
  owner.enabled = false;
  owner.tags = {"landscape", "coast"};
  owner.layer = "ground";
  owner.sceneOwner = "scene://island";
  owner.prefabInstance = "island-instance";
  owner.prefabLocalId = "authored-land";
  owner.persistent = true;
  owner.setComponent(Transform3DComponent{.parent = "island",
                                          .position = {10, 20, 30},
                                          .rotation = {0, 1, 0},
                                          .scale = {2, 3, 4}});
  owner.setComponent(BoxCollider3DComponent{});
  owner.setComponent(Rigidbody3DComponent{});
  return owner;
}

void publishesNativeGeometry() {
  const auto owner = terrainOwner();
  const auto surface = body();
  const auto field = fieldWith({surface, body("river")});
  const auto entities = buildTerrainWaterMeshes(owner, field);
  check(entities.size() == 2, "Expected one mesh per drawable water body");
  for (const auto &entity : entities) {
    const auto *mesh = entity.component<MeshRendererComponent>();
    check(mesh && mesh->vertices.size() == surface.indices.size(),
          "Indexed water was not expanded into triangles");
    check(mesh->normals.size() == mesh->vertices.size() &&
              mesh->uvs.size() == mesh->vertices.size(),
          "Expanded attributes do not match the vertex count");
    for (std::size_t index = 0; index < surface.indices.size(); ++index) {
      const auto source = surface.indices[index];
      check(equal(mesh->vertices[index], surface.vertices[source]) &&
                equal(mesh->normals[index], surface.normals[source]),
            "Expansion changed position, normal or triangle winding");
      check(mesh->uvs[index].x == surface.vertices[source].x &&
                mesh->uvs[index].y == surface.vertices[source].z,
            "UVs must repeat in terrain-local units");
    }
    check(mesh->surfaceMode == "transparent" && mesh->roughness == 0.12F &&
              mesh->metallic == 0.F && mesh->opacity == 1.F &&
              mesh->vertexColors.size() == mesh->vertices.size() &&
              mesh->color.r == 1.F && mesh->color.b == 1.F &&
              mesh->color.a == 1.F,
          "Water lost its transparent blue surface settings");
    check(mesh->revision != 0 && mesh->hasBounds &&
              equal(mesh->boundsMin, {-2, 2, 3}) &&
              equal(mesh->boundsMax, {2, 2, 7}),
          "Native geometry revision or bounds were not prepared");
    check(!entity.hasComponent<BoxCollider3DComponent>() &&
              !entity.hasComponent<SphereCollider3DComponent>() &&
              !entity.hasComponent<CapsuleCollider3DComponent>() &&
              !entity.hasComponent<ConvexCollider3DComponent>() &&
              !entity.hasComponent<ModelCollider3DComponent>() &&
              !entity.hasComponent<Rigidbody3DComponent>() &&
              !entity.hasComponent<terrain_detail::TerrainGeneratedSurface>(),
          "Water acquired terrain collision or a ground-surface marker");
  }
  check(field.graphArtifacts->waterResult->surfaces[0].vertices.size() == 4 &&
            field.graphArtifacts->waterResult->surfaces[0].indices ==
                surface.indices,
        "Publication mutated the retained water artifact");
  check(owner.hasComponent<BoxCollider3DComponent>() &&
            owner.hasComponent<Rigidbody3DComponent>(),
        "Publication mutated owner components");
}

void inheritsMetadataAndParent() {
  const auto owner = terrainOwner();
  const auto entities = buildTerrainWaterMeshes(owner, fieldWith({body()}));
  const auto &entity = entities.at(0);
  check(entity.enabled == owner.enabled && entity.tags == owner.tags &&
            entity.layer == owner.layer &&
            entity.sceneOwner == owner.sceneOwner &&
            entity.prefabInstance == owner.prefabInstance &&
            entity.persistent == owner.persistent,
        "Water did not inherit owner metadata");
  const auto *transform = entity.component<Transform3DComponent>();
  check(transform && transform->parent == owner.id &&
            equal(transform->position, {0, 0, 0}) &&
            equal(transform->rotation, {0, 0, 0}) &&
            equal(transform->scale, {1, 1, 1}),
        "Water must inherit the owner's transform through an identity child");
  const auto *marker =
      entity.component<terrain_detail::TerrainGeneratedWaterSurface>();
  check(marker && marker->owner == owner.id,
        "Water ownership marker does not identify the terrain owner");
  check(entity.serializedComponents.empty() &&
            entity.authoredComponents.empty() && entity.prefabLocalId.empty(),
        "Generated water acquired authored document identity");
}

void skipsEmptyBodiesAndAbsentArtifacts() {
  const auto owner = terrainOwner();
  check(buildTerrainWaterMeshes(owner, HeightField{}).empty(),
        "An absent graph produced water");
  HeightField noWater;
  noWater.graphArtifacts = std::make_shared<TerrainGraphArtifacts>();
  check(buildTerrainWaterMeshes(owner, noWater).empty(),
        "An absent water result produced water");
  TerrainWaterSurface empty;
  empty.id = "shadowed";
  const auto entities =
      buildTerrainWaterMeshes(owner, fieldWith({empty, body()}));
  check(entities.size() == 1 && entities[0].id == owner.id + "/__water/lake",
        "An empty body must not produce a fallback primitive");
  check(buildTerrainWaterMeshes(owner, fieldWith({empty})).empty(),
        "An empty body produced a fallback primitive");
  check(buildTerrainWaterMeshes(owner, fieldWith({})).empty(),
        "An empty water result produced water");
}

void escapesStableBodyIds() {
  const auto owner = terrainOwner();
  auto surfaces = std::vector{body("lake/a"), body("lake%2fa"),
                              body("lake.a b\\c"), body("\xc3\xa9")};
  const auto first = buildTerrainWaterMeshes(owner, fieldWith(surfaces));
  check(first[0].id == owner.id + "/__water/lake%2fa" &&
            first[1].id == owner.id + "/__water/lake%252fa" &&
            first[2].id == owner.id + "/__water/lake%2ea%20b%5cc" &&
            first[3].id == owner.id + "/__water/%c3%a9",
        "Body IDs must encode separators, percent signs and UTF-8 reversibly");
  std::reverse(surfaces.begin(), surfaces.end());
  const auto second = buildTerrainWaterMeshes(owner, fieldWith(surfaces));
  for (std::size_t index = 0; index < first.size(); ++index)
    check(first[index].id == second[first.size() - 1 - index].id,
          "Generated identity depends on body ordering");
}

template <typename Mutation> void rejectsSurface(Mutation mutate) {
  auto invalid = body("broken");
  mutate(invalid);
  const auto owner = terrainOwner();
  try {
    (void)buildTerrainWaterMeshes(owner, fieldWith({body(), invalid}));
  } catch (const std::invalid_argument &error) {
    const std::string message = error.what();
    check(message.find(owner.id) != std::string::npos &&
              message.find(invalid.id) != std::string::npos,
          "Validation error lacks owner/body context");
    return;
  }
  throw std::runtime_error("Invalid water artifact was accepted");
}

void rejectsInvalidArtifacts() {
  rejectsSurface([](auto &surface) { surface.normals.pop_back(); });
  rejectsSurface([](auto &surface) { surface.normals.push_back({0, 1, 0}); });
  rejectsSurface([](auto &surface) { surface.depth.pop_back(); });
  rejectsSurface([](auto &surface) { surface.depth.push_back(1); });
  rejectsSurface([](auto &surface) { surface.indices.pop_back(); });
  rejectsSurface(
      [](auto &surface) { surface.indices[0] = surface.vertices.size(); });
  rejectsSurface([](auto &surface) {
    surface.indices[0] = std::numeric_limits<std::size_t>::max();
  });
  rejectsSurface([](auto &surface) {
    surface.vertices.clear();
    surface.normals.clear();
    surface.depth.clear();
  });
  rejectsSurface([](auto &surface) { surface.depth[0] = -1; });
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();
  for (const float value : {nan, infinity, -infinity}) {
    rejectsSurface([=](auto &surface) { surface.level = value; });
    rejectsSurface([=](auto &surface) { surface.depth[0] = value; });
    for (int axis = 0; axis < 3; ++axis) {
      rejectsSurface([=](auto &surface) {
        auto &vertex = surface.vertices[0];
        if (axis == 0)
          vertex.x = value;
        else if (axis == 1)
          vertex.y = value;
        else
          vertex.z = value;
      });
      rejectsSurface([=](auto &surface) {
        auto &normal = surface.normals[0];
        if (axis == 0)
          normal.x = value;
        else if (axis == 1)
          normal.y = value;
        else
          normal.z = value;
      });
    }
  }
  // Unused samples still belong to the artifact and must be validated.
  rejectsSurface([=](auto &surface) {
    surface.vertices.push_back({nan, 2, 0});
    surface.normals.push_back({0, 1, 0});
    surface.depth.push_back(0);
  });
  rejectsSurface([](auto &surface) { surface.id = "lake"; });
  try {
    (void)buildTerrainWaterMeshes(terrainOwner(), fieldWith({body("")}));
    throw std::runtime_error("An empty body ID was accepted");
  } catch (const std::invalid_argument &) {
  }
  auto owner = terrainOwner();
  owner.id.clear();
  try {
    (void)buildTerrainWaterMeshes(owner, fieldWith({body()}));
    throw std::runtime_error("An empty owner ID was accepted");
  } catch (const std::invalid_argument &) {
  }
}

void cancellationPublishesNothing() {
  std::stop_source cancellation;
  cancellation.request_stop();
  const auto owner = terrainOwner();
  const auto field = fieldWith({body(), body("river")});
  check(buildTerrainWaterMeshes(owner, field, cancellation.get_token()).empty(),
        "Cancelled publication returned water entities");
  check(buildTerrainWaterMeshes(owner, field).size() == 2,
        "Cancelled publication damaged the retained artifact");
}

void unchangedWaterRetainsMeshes() {
  const auto before = fieldWith({body()});
  auto sharedArtifacts = before;
  sharedArtifacts.biomeIds = {"changed-biome"};
  check(!terrainWaterMeshesChanged(&before, sharedArtifacts),
        "Shared water artifacts must retain meshes after a biome edit");

  auto copiedArtifacts =
      std::make_shared<TerrainGraphArtifacts>(*before.graphArtifacts);
  copiedArtifacts->warnings = {"Unrelated graph diagnostic"};
  copiedArtifacts->nodes.push_back(
      {.id = "biome", .cached = true, .milliseconds = 12});
  copiedArtifacts->waterResult->dropped = 1;
  copiedArtifacts->waterResult->carvedHeights.resize(1);
  copiedArtifacts->waterResult->carvedHeights.set(0, 42.F);
  auto biomeOnly = before;
  biomeOnly.graphArtifacts = std::move(copiedArtifacts);
  biomeOnly.biomeColors = {{1, 0, 0, 1}};
  biomeOnly.biomeMaterials = {"asset://ground/changed"};
  check(biomeOnly.graphArtifacts != before.graphArtifacts &&
            !terrainWaterMeshesChanged(&before, biomeOnly),
        "Copied artifacts with unchanged water must retain meshes");
  check(!terrainWaterMeshesChanged(&before, fieldWith({body()})),
        "Identical separately allocated water was reported changed");
}

void detectsWaterAdditionAndRemoval() {
  const HeightField noWater;
  const auto water = fieldWith({body(), body("river")});
  check(!terrainWaterMeshesChanged(nullptr, noWater) &&
            !terrainWaterMeshesChanged(&noWater, fieldWith({})),
        "Absent and empty water must compare equal");
  check(terrainWaterMeshesChanged(nullptr, water) &&
            terrainWaterMeshesChanged(&noWater, water),
        "Adding drawable water must require publication");
  check(terrainWaterMeshesChanged(&water, noWater) &&
            terrainWaterMeshesChanged(&water, fieldWith({})) &&
            terrainWaterMeshesChanged(&water, fieldWith({body()})),
        "Removing some or all water must require publication");

  TerrainWaterSurface empty;
  empty.id = "shadowed";
  empty.level = 10;
  check(!terrainWaterMeshesChanged(&noWater, fieldWith({empty})),
        "An empty body must not require publication");
  const auto before = fieldWith({body()});
  check(!terrainWaterMeshesChanged(&before, fieldWith({empty, body()})),
        "Adding an empty body must not rebuild drawable water");
}

template <typename Mutation> void detectsSurfaceChange(Mutation mutate) {
  const auto before = fieldWith({body()});
  auto changed = body();
  mutate(changed);
  check(terrainWaterMeshesChanged(&before, fieldWith({changed})),
        "Changed water surface was reported unchanged");
}

void detectsWaterSurfaceChanges() {
  detectsSurfaceChange([](auto &surface) { surface.id = "renamed"; });
  detectsSurfaceChange(
      [](auto &surface) { surface.kind = TerrainWaterBody::River; });
  detectsSurfaceChange([](auto &surface) { surface.level += 1; });
  detectsSurfaceChange([](auto &surface) { surface.vertices[0].x += 1; });
  detectsSurfaceChange([](auto &surface) { surface.vertices[0].y += 1; });
  detectsSurfaceChange([](auto &surface) { surface.vertices[0].z += 1; });
  detectsSurfaceChange([](auto &surface) { surface.normals[0] = {1, 0, 0}; });
  detectsSurfaceChange(
      [](auto &surface) { std::swap(surface.indices[0], surface.indices[1]); });
  detectsSurfaceChange([](auto &surface) { surface.indices.resize(3); });
  detectsSurfaceChange([](auto &surface) { surface.depth[0] += 1; });
  detectsSurfaceChange([](auto &surface) {
    surface.vertices.push_back({2, 2, 9});
    surface.normals.push_back({0, 1, 0});
    surface.depth.push_back(0);
    surface.indices.insert(surface.indices.end(), {2, 3, 4});
  });
}
void depthAppearanceAndContainment() {
  auto field = fieldWith({body()});
  auto artifacts =
      std::make_shared<TerrainGraphArtifacts>(*field.graphArtifacts);
  TerrainWaterBodySpec spec;
  spec.id = "lake";
  spec.kind = TerrainWaterBody::Lake;
  spec.appearance =
      parseTerrainWaterAppearance({{"shallow_color", {1, 0, 0, 0.1}},
                                   {"deep_color", {0, 0, 1, 0.9}},
                                   {"absorption_distance", 2},
                                   {"roughness", 0.3}});
  artifacts->water.bodies.push_back(spec);
  field.graphArtifacts = artifacts;
  const auto meshes = buildTerrainWaterMeshes(terrainOwner(), field);
  const auto *mesh = meshes.front().component<MeshRendererComponent>();
  check(mesh->roughness == 0.3F && mesh->vertexColors.front().r == 1 &&
            mesh->vertexColors.front().b == 0 &&
            mesh->vertexColors.front().a == 0.1F,
        "Shore depth did not use authored shallow RGBA");
  const auto deep = terrainWaterDepthColor(spec.appearance, 100);
  check(deep.b > 0.99F && deep.a > 0.89F,
        "Deep colour did not approach authored deep RGBA");
  auto edited = field;
  auto changed = std::make_shared<TerrainGraphArtifacts>(*artifacts);
  changed->water.bodies.front().appearance.roughness = 0.7F;
  edited.graphArtifacts = changed;
  check(terrainWaterMeshesChanged(&field, edited),
        "Water appearance edit was invisible to native publication");
  bool rejected = false;
  try {
    (void)parseTerrainWaterAppearance({{"absorption_distance", 0}});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "Invalid water absorption distance was accepted");

  TerrainRecipe recipe;
  recipe.size = {16, 16};
  recipe.cellsX = recipe.cellsZ = 16;
  recipe.landforms.at("default").baseHeight = 3;
  recipe.landforms.at("default").heightVariation = 0;
  auto ground = TerrainGenerator::generate(recipe);
  check(bool(ground), "Could not generate containment fixture");
  TerrainWaterAuthoring authoring;
  authoring.authored = true;
  spec.level = 5;
  spec.center = {8, 8};
  spec.radius = 5;
  authoring.bodies.push_back(spec);
  check(!terrainWaterContainmentWarnings(*ground, authoring).empty(),
        "Raised lake without banks produced no warning");
  for (int z = 0; z <= 16; ++z)
    for (int x = 0; x <= 16; ++x)
      if ((x - 8) * (x - 8) + (z - 8) * (z - 8) >= 16)
        ground->heights.set(ground->index(x, z), 7);
  check(terrainWaterContainmentWarnings(*ground, authoring).empty(),
        "Contained lake was incorrectly reported as spilling");
}
} // namespace

int main() {
  try {
    publishesNativeGeometry();
    inheritsMetadataAndParent();
    skipsEmptyBodiesAndAbsentArtifacts();
    escapesStableBodyIds();
    rejectsInvalidArtifacts();
    cancellationPublishesNothing();
    unchangedWaterRetainsMeshes();
    detectsWaterAdditionAndRemoval();
    detectsWaterSurfaceChanges();
    depthAppearanceAndContainment();
    std::cout << "Terrain water mesh tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Terrain water mesh tests failed: " << error.what() << '\n';
    return 1;
  }
}
