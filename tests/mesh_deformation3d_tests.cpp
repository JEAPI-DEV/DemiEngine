#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/schema/Validation.h"
#include "demi/runtime/geometry/MeshImpact3D.h"
#include "demi/runtime/geometry/MeshRefinement3D.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/bgfx3d/DeformedMeshCache3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/World.h"
#include <cassert>
#include <cmath>
#include <limits>

using namespace demi::runtime;
using namespace demi::runtime::render;
int main() {
  // Energy scales linearly with effective mass and quadratically with normal
  // speed.
  assert(normalImpactEnergy3D({0, 0, 8}, {0, 0, 1}, 0.5F) == 64);
  assert(normalImpactEnergy3D({0, 0, 16}, {0, 0, 1}, 0.5F) == 256);
  assert(normalImpactEnergy3D({0, 0, 8}, {0, 0, 1}, 0.25F) == 128);
  assert(normalImpactEnergy3D({8, 0, 0}, {0, 0, 1}, 0.5F) == 0);
  assert(normalImpactEnergy3D({0, 0, -8}, {0, 0, 1}, 0.5F) == 0);
  MeshImpactMaterial3D material;
  const auto low = meshImpactDepth3D(16, material);
  const auto high = meshImpactDepth3D(64, material);
  assert(low.accepted && high.accepted && high.depth > low.depth);
  assert(meshImpactDepth3D(1, material).depth == 0);
  material.stiffness *= 2;
  assert(meshImpactDepth3D(64, material).depth < high.depth);
  assert(meshImpactDepth3D(1e9F, material).depth == material.maximumDepth);
  material.stiffness = 0;
  assert(!meshImpactDepth3D(64, material).accepted);

  // A tiny brush between original vertices gains geometry without a new asset.
  MeshGeometry3D refined;
  std::string refinementError;
  const std::vector<Vec3> sparse{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  const std::vector<Vec2> uv{{0, 0}, {1, 0}, {0, 1}};
  const std::vector<std::uint32_t> colors(3, 0xff112233U);
  const std::vector<MeshDent3D> smallDent{{.center = {0.25F, 0.25F, 0},
                                           .displacement = {0, 0, -0.03F},
                                           .radius = 0.1F}};
  assert(refineMeshForDents3D(sparse, uv, {}, colors, smallDent, refined,
                              refinementError));
  assert(refined.positions.size() > sparse.size());
  assert(refined.indices.size() / 3 <= MaximumRefinedMeshTriangles3D);
  assert(refined.uvs.size() == refined.positions.size());
  assert(refined.colors.size() == refined.positions.size());
  deformMeshPositions3D(refined.positions, smallDent);
  bool movedInterior = false;
  for (std::size_t i = 0; i < refined.positions.size(); ++i) {
    assert(refined.colors[i] == 0xff112233U);
    if (refined.positions[i].z < -0.02F)
      movedInterior = true;
  }
  assert(movedInterior && sparse[0].z == 0);
  const std::vector<Vec3> original{
      {0, 0, 0}, {0.5F, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 2}};
  World world;
  Entity entity;
  entity.id = "panel";
  entity.setComponent(Transform3DComponent{});
  entity.setComponent(MeshRendererComponent{.vertices = original});
  entity.setComponent(BoxCollider3DComponent{.size = {2, 2, 0.1F}});
  world.entities.push_back(entity);
  entity.id = "reference";
  world.entities.push_back(entity);
  std::string error;
  assert(!dentMesh3D(world, "panel", {}, {0,0,-1}, 1, 0.2F, error));
  assert(!impactMesh3D(world, "panel", {}, {0,0,-1}, 0, {}).accepted);
  assert(!resetMeshDents3D(world, "panel"));
  world.entities[0].setComponent(Dentable3DComponent{});
  auto &dents = world.entities[0].component<Dentable3DComponent>()->dents;
  assert(dentMesh3D(world, "panel", {}, {0, 0, -1}, 1, 0.2F, error));
  auto *mesh = world.entities[0].component<MeshRendererComponent>();
  auto positions = original;
  deformMeshPositions3D(positions, dents);
  assert(std::abs(positions[0].z + 0.2F) < 1e-6F);
  assert(positions[1].z < 0 && positions[1].z > -0.2F);
  assert(positions[2].z == 0 && positions[3].z == 0 && positions[4].z == 2);
  assert(mesh->vertices[0].z == 0);
  assert(!world.entities[1].hasComponent<Dentable3DComponent>());
  assert(world.entities[0].component<BoxCollider3DComponent>()->size.z == 0.1F);
  const auto signature = meshDentsSignature3D(dents);
  assert(!dentMesh3D(world, "panel", {}, {}, 1, 0.2F, error));
  assert(!dentMesh3D(world, "panel", {}, {0, 0, -1}, -1, 0.2F, error));
  assert(!dentMesh3D(world, "panel", {}, {0, 0, -1}, 1, 1, error));
  assert(!dentMesh3D(world, "panel",
                     {std::numeric_limits<float>::quiet_NaN(), 0, 0},
                     {0, 0, -1}, 1, 0.2F, error));
  assert(meshDentsSignature3D(dents) == signature);
  assert(!dentMesh3D(world, "missing", {}, {0, 0, -1}, 1, 0.2F, error));
  assert(resetMeshDents3D(world, "panel"));
  positions = original;
  deformMeshPositions3D(positions, dents);
  assert(positions[0].z == 0);

  world.entities[1].component<Transform3DComponent>()->rotation = {0, 0.7F, 0};
  world.entities[1].component<Transform3DComponent>()->scale = {2, 3, 4};
  world.entities[0].component<Transform3DComponent>()->parent = "reference";
  mesh->size = {0.5F, 1, 2};
  auto transform = *resolveWorldTransform3D(world, world.entities[0]);
  transform.scale = {transform.scale.x * mesh->size.x,
                     transform.scale.y * mesh->size.y,
                     transform.scale.z * mesh->size.z};
  const auto origin = transformPoint3D(transform, {});
  assert(dentMesh3D(world, "panel", origin, {0, 0, -1}, 1, 0.2F, error));
  positions = original;
  deformMeshPositions3D(positions, dents);
  const auto moved = transformPoint3D(transform, positions[0]);
  assert(std::abs(moved.x - origin.x) < 1e-5F);
  assert(std::abs(moved.z - origin.z + 0.2F) < 1e-5F);
  for (std::size_t i = 1; i < MaximumMeshDents3D; ++i)
    assert(dentMesh3D(world, "panel", origin, {0, 0, -1}, 1, 0.2F, error));
  assert(!dentMesh3D(world, "panel", origin, {0, 0, -1}, 1, 0.2F, error));
  assert(dents.size() == MaximumMeshDents3D);
  assert(resetMeshDents3D(world, "panel"));
  world.entities[0].setComponent(AnimationPlayer3DComponent{});
  assert(!dentMesh3D(world, "panel", origin, {0, 0, -1}, 1, 0.2F, error));
  assert(world.entities[0].removeComponent<Dentable3DComponent>());
  assert(entityMeshDents3D(world.entities[0]).empty());
  const auto *descriptor = scene_loading::findComponentDescriptor("Dentable3D");
  assert(descriptor);
  const auto defaults = scene_loading::componentDefaults(*descriptor);
  assert(defaults["stiffness"] == 4000.0F);
  assert(defaults["yield_energy"] == 4.0F);
  Entity parsed;
  scene_loading::parseComponent<Dentable3DComponent>({{"stiffness", 8000}}, parsed);
  assert(parsed.component<Dentable3DComponent>()->material.stiffness == 8000);
  assert(parsed.component<Dentable3DComponent>()->material.radius == 0.32F);
  const auto invalidValues = scene_loading::validateComponent(
      *descriptor, {{"absorption", 2}, {"stiffness", 0}});
  assert(!invalidValues.empty());
  const auto invalidDocument = demi::validateSceneDocument("dentable.scene.json",
      {{"format_version", 1}, {"id", "scene://dentable-test"},
       {"entities", {{{"id", "missing_mesh"},
         {"components", {{"Dentable3D", nlohmann::json::object()}}}}}}});
  assert(std::ranges::any_of(invalidDocument, [](const auto &issue) {
    return issue.code == "DENTABLE3D_MESH_REQUIRED";
  }));

  BgfxGraphicsDevice graphics;
  error.clear();
  assert(graphics.initialize(
      {.api = GraphicsApi::Noop, .width = 64, .height = 64}, error));
  auto resources = createBgfxGpuResources();
  {
    DeformedMeshCache3D cache(*resources);
    const std::vector<Vec3> triangle{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const std::vector<MeshDent3D> dents{
        {.displacement = {0, 0, -0.2F}, .radius = 1}};
    const auto *first =
        cache.get("one", "model", 1, triangle, {}, {}, {}, dents, error);
    assert(first && cache.uploads() == 1);
    assert(cache.get("one", "model", 1, triangle, {}, {}, {}, dents, error) ==
           first);
    assert(cache.uploads() == 1);
    assert(cache.get("two", "model", 1, triangle, {}, {}, {}, dents, error) !=
           first);
    assert(cache.uploads() == 2 && cache.size() == 2);
    assert(cache.get("one", "model", 2, triangle, {}, {}, {}, dents, error));
    assert(cache.uploads() == 3);
    cache.retain({"one"});
    assert(cache.size() == 1);
    cache.retain({});
    assert(cache.size() == 0 && triangle[0].z == 0);
  }
  resources->clear();
  resources.reset();
  graphics.shutdown();
}
