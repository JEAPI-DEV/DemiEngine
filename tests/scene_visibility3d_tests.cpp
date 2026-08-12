#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"

#include <cassert>
#include <string>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

Entity mesh(std::string id, const Vec3 position, const Vec3 minimum,
            const Vec3 maximum) {
  Entity result;
  result.id = std::move(id);
  result.setComponent(Transform3DComponent{.position = position});
  result.setComponent(MeshRendererComponent{
      .boundsMin = minimum, .boundsMax = maximum, .hasBounds = true});
  return result;
}

} // namespace

int main() {
  World world;
  world.entities.push_back(mesh("center", {0.0F, 0.0F, 8.0F},
                                {-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}));
  world.entities.push_back(mesh("behind", {0.0F, 0.0F, -8.0F},
                                {-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}));
  world.entities.push_back(mesh("right", {30.0F, 0.0F, 8.0F},
                                {-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}));
  // A large bound intersecting the right plane must remain visible.
  world.entities.push_back(mesh("edge", {8.0F, 0.0F, 8.0F},
                                {-4.0F, -1.0F, -1.0F}, {4.0F, 1.0F, 1.0F}));
  // Meshes without trustworthy bounds preserve legacy always-visible behavior.
  Entity unknown = mesh("unknown", {100.0F, 0.0F, 8.0F}, {}, {});
  unknown.component<MeshRendererComponent>()->hasBounds = false;
  world.entities.push_back(std::move(unknown));

  BgfxCameraFrame3D frame{
      .camera = {.fov = 60.0F, .nearClip = 0.1F, .farClip = 100.0F},
      .forward = {0.0F, 0.0F, 1.0F},
      .up = {0.0F, 1.0F, 0.0F},
      .viewportWidth = 160,
      .viewportHeight = 90,
  };
  const SceneVisibility3D serial = extractVisibleMeshes3D(world, frame);
  JobSystem jobs(2);
  const SceneVisibility3D parallel = extractVisibleMeshes3D(world, frame, &jobs);
  assert(serial.considered == 5 && serial.culled == 2);
  assert(serial.meshes.size() == 3);
  assert(serial.meshes[0].entity->id == "center");
  assert(serial.meshes[1].entity->id == "edge");
  assert(serial.meshes[2].entity->id == "unknown");
  assert(parallel.considered == serial.considered);
  assert(parallel.culled == serial.culled);
  for (std::size_t index = 0; index < serial.meshes.size(); ++index)
    assert(parallel.meshes[index].entity->id == serial.meshes[index].entity->id);

  // Cross the dispatch threshold so the worker-pool path is covered. Results
  // must retain stable world order despite completing on multiple threads.
  World largeWorld;
  largeWorld.entities.reserve(1200);
  for (int index = 0; index < 1200; ++index) {
    const bool inView = index % 3 != 0;
    largeWorld.entities.push_back(mesh(
        "mesh-" + std::to_string(index),
        {inView ? 0.0F : 100.0F, 0.0F, 8.0F}, {-0.5F, -0.5F, -0.5F},
        {0.5F, 0.5F, 0.5F}));
  }
  const SceneVisibility3D largeSerial =
      extractVisibleMeshes3D(largeWorld, frame);
  const SceneVisibility3D largeParallel =
      extractVisibleMeshes3D(largeWorld, frame, &jobs);
  assert(largeSerial.considered == 1200);
  assert(largeSerial.culled == 400);
  assert(largeParallel.meshes.size() == largeSerial.meshes.size());
  for (std::size_t index = 0; index < largeSerial.meshes.size(); ++index)
    assert(largeParallel.meshes[index].entity->id ==
           largeSerial.meshes[index].entity->id);

  frame.camera.perspective = false;
  frame.camera.orthographicSize = 5.0F;
  const SceneVisibility3D orthographic = extractVisibleMeshes3D(world, frame);
  assert(orthographic.culled >= 2);
  return 0;
}
