#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/render/bgfx3d/UnderwaterCamera3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainWaterMesh.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

World waterWorld() {
  TerrainRecipe recipe;
  recipe.size = {8, 8};
  recipe.cellsX = recipe.cellsZ = 4;
  recipe.landforms.at("default").baseHeight = 0;
  recipe.landforms.at("default").heightVariation = 0;
  auto field = TerrainGenerator::generate(recipe);
  assert(field);
  auto artifacts = std::make_shared<TerrainGraphArtifacts>();
  artifacts->water.authored = true;
  TerrainWaterBodySpec body;
  body.id = "lake";
  body.kind = TerrainWaterBody::Lake;
  body.level = 2;
  body.center = {4, 4};
  body.appearance.absorptionDistance = 5;
  body.appearance.deepColor = {0.1F, 0.2F, 0.3F, 0.8F};
  artifacts->water.bodies.push_back(body);
  artifacts->waterResult = carveTerrainWater(*field, artifacts->water, nullptr);
  assert(artifacts->waterResult);
  field->heights = artifacts->waterResult->carvedHeights;
  field->graphArtifacts = artifacts;
  World world;
  Entity owner;
  owner.id = "terrain";
  owner.setComponent(
      Transform3DComponent{.position = {10, 5, 20}, .scale = {2, 3, 4}});
  Terrain3DComponent terrain;
  terrain.generated = std::make_shared<const HeightField>(std::move(*field));
  owner.setComponent(std::move(terrain));
  world.entities.push_back(std::move(owner));
  world.terrainOwners.push_back({.id = "terrain"});
  return world;
}

void onlyWetCameraPointsReceiveUnderwaterAppearance() {
  auto world = waterWorld();
  UnderwaterCamera3D camera;
  const auto transform = resolveWorldTransform3D(world, world.entities.front());
  assert(transform);
  const auto at = [&](Vec3 local) {
    return transformPoint3D(*transform, local);
  };
  const auto submerged = camera.sample(world, at({4, 1, 4}));
  assert(submerged && submerged->immersion == 1);
  assert(submerged->absorptionDistance == 5 && submerged->color.a == 0.8F);
  assert(std::abs(submerged->surfaceRelative.y - 3) < 0.00001F);
  assert(submerged->normal.y == 1);
  assert(submerged->inverseBasis[0].x == 0.5F);
  assert(std::abs(submerged->inverseBasis[1].y - 1.F / 3) < 0.00001F);
  assert(submerged->inverseBasis[2].z == 0.25F);
  assert(!submerged->linearDepth);
  assert(!camera.sample(world, at({4, 3, 4})));
  assert(!camera.sample(world, at({4, 2, 4})));
  assert(!camera.sample(world, at({4, -1, 4})));
  assert(!camera.sample(world, at({-1, 1, 4})));
  const auto transition = camera.sample(world, at({4, 1.95F, 4}));
  assert(transition && std::abs(transition->immersion - 1.F / 3) < 0.0001F);
  world.entities.front().enabled = false;
  assert(!camera.sample(world, at({4, 1, 4})));
}

void transformedWaterAndSceneChangesDoNotRetainCameraState() {
  auto world = waterWorld();
  world.entities.front().updateComponent<Transform3DComponent>(
      [](Transform3DComponent &transform) {
        transform.rotation = {0.3F, 0.2F, 0.1F};
      });
  const auto transform = resolveWorldTransform3D(world, world.entities.front());
  assert(transform);
  UnderwaterCamera3D camera;
  const auto underwaterPoint = transformPoint3D(*transform, {4, 1, 4});
  const auto submerged = camera.sample(world, underwaterPoint);
  assert(submerged && submerged->immersion == 1);
  const auto expectedSurface = transformPoint3D(*transform, {4, 2, 4});
  assert(
      std::hypot(
          submerged->surfaceRelative.x - expectedSurface.x + underwaterPoint.x,
          submerged->surfaceRelative.y - expectedSurface.y + underwaterPoint.y,
          submerged->surfaceRelative.z - expectedSurface.z +
              underwaterPoint.z) < 0.0001F);
  const auto abovePoint = transformPoint3D(*transform, {4, 3, 4});
  assert(!camera.sample(world, abovePoint));
  World empty;
  assert(!camera.sample(empty, underwaterPoint));
  assert(camera.sample(world, underwaterPoint));
  world.entities.front().component<Terrain3DComponent>()->generated.reset();
  assert(!camera.sample(world, underwaterPoint));
}

void waterVisibilityDoesNotRetirePreparedQueries() {
  auto world = waterWorld();
  const auto *terrain = world.entities.front().component<Terrain3DComponent>();
  auto meshes =
      buildTerrainWaterMeshes(world.entities.front(), *terrain->generated);
  assert(meshes.size() == 1);
  world.entities.push_back(std::move(meshes.front()));
  const auto *mesh = world.entities.back().component<MeshRendererComponent>();
  assert(mesh && mesh->hasBounds);
  for (const auto &vertex : mesh->vertices) {
    assert(vertex.x >= mesh->boundsMin.x && vertex.x <= mesh->boundsMax.x);
    assert(vertex.y >= mesh->boundsMin.y && vertex.y <= mesh->boundsMax.y);
    assert(vertex.z >= mesh->boundsMin.z && vertex.z <= mesh->boundsMax.z);
  }
  BgfxCameraFrame3D frame;
  frame.position = {18, 11, -20};
  frame.forward = {0, 0, 1};
  frame.viewportWidth = 800;
  frame.viewportHeight = 600;
  auto transform = resolveWorldTransform3D(world, world.entities.back());
  assert(transform && meshBoundsVisible3D(*mesh, *transform, frame));
  UnderwaterCamera3D camera;
  const auto wetPoint = transformPoint3D(*transform, {4, 1, 4});
  assert(camera.sample(world, wetPoint));
  frame.forward = {0, 0, -1};
  assert(!meshBoundsVisible3D(*mesh, *transform, frame));
  assert(camera.sample(world, wetPoint));
  frame.forward = {0, 0, 1};
  world.entities.front().updateComponent<Transform3DComponent>(
      [](Transform3DComponent &transform) { transform.position.z = -200; });
  transform = resolveWorldTransform3D(world, world.entities.back());
  assert(transform && !meshBoundsVisible3D(*mesh, *transform, frame));
  assert(camera.sample(world, transformPoint3D(*transform, {4, 1, 4})));
}

} // namespace

int main() {
  onlyWetCameraPointsReceiveUnderwaterAppearance();
  transformedWaterAndSceneChangesDoNotRetainCameraState();
  waterVisibilityDoesNotRetirePreparedQueries();
  std::cout << "Underwater cameras use transformed shared water containment\n";
}
