#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
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
  {
    MeshInstances3DComponent instances;
    instances.setTransform("tree", {.position = {1, 0, 4}});
    const auto initialRevision = instances.contentRevision();
    instances.setTransform("tree", {.position = {1, 0, 4}});
    assert(instances.contentRevision() == initialRevision);
    assert(!instances.eraseTransform("missing"));
    assert(instances.contentRevision() == initialRevision);
    auto copied = instances;
    assert(copied.contentRevision() == initialRevision);
    copied.setTransform("tree", {.position = {2, 0, 4}});
    assert(copied.contentRevision() != initialRevision);
    assert(instances.transforms().at("tree").position.x == 1);
    MeshInstances3DComponent moved(std::move(copied));
    assert(moved.transforms().at("tree").position.x == 2);
    assert(copied.transforms().empty());
    assert(copied.contentRevision() != moved.contentRevision());
    copied = instances;
    assert(copied.contentRevision() == initialRevision);
    assert(copied.eraseTransform("tree"));
    assert(copied.contentRevision() != initialRevision);
    instances.clearTransforms();
    assert(instances.transforms().empty());
    assert(instances.contentRevision() != initialRevision);
  }
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
  // An unresolved model has no trustworthy bounds and stays conservatively
  // visible. A default cube now has known canonical primitive bounds.
  Entity unknown = mesh("unknown", {100.0F, 0.0F, 8.0F}, {}, {});
  unknown.component<MeshRendererComponent>()->hasBounds = false;
  unknown.component<MeshRendererComponent>()->model =
      "asset://unresolved/model";
  world.entities.push_back(std::move(unknown));
  Entity primitive = mesh("offscreen-primitive", {100.0F, 0.0F, 8.0F}, {}, {});
  primitive.component<MeshRendererComponent>()->hasBounds = false;
  world.entities.push_back(std::move(primitive));

  BgfxCameraFrame3D frame{
      .camera = {.fov = 60.0F, .nearClip = 0.1F, .farClip = 100.0F},
      .forward = {0.0F, 0.0F, 1.0F},
      .up = {0.0F, 1.0F, 0.0F},
      .viewportWidth = 160,
      .viewportHeight = 90,
  };
  const SceneVisibility3D serial = extractVisibleMeshes3D(world, frame);
  JobSystem jobs(2);
  const SceneVisibility3D parallel =
      extractVisibleMeshes3D(world, frame, &jobs);
  assert(serial.considered == 6 && serial.culled == 3);
  assert(serial.meshes.size() == 3);
  assert(serial.meshes[0].entity->id == "center");
  assert(serial.meshes[1].entity->id == "edge");
  assert(serial.meshes[2].entity->id == "unknown");
  assert(parallel.considered == serial.considered);
  assert(parallel.culled == serial.culled);
  for (std::size_t index = 0; index < serial.meshes.size(); ++index)
    assert(parallel.meshes[index].entity->id ==
           serial.meshes[index].entity->id);

  // Cross the dispatch threshold so the worker-pool path is covered. Results
  // must retain stable world order despite completing on multiple threads.
  World largeWorld;
  largeWorld.entities.reserve(1200);
  for (int index = 0; index < 1200; ++index) {
    const bool inView = index % 3 != 0;
    largeWorld.entities.push_back(mesh(
        "mesh-" + std::to_string(index), {inView ? 0.0F : 100.0F, 0.0F, 8.0F},
        {-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}));
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

  {
    World copies;
    auto owner = mesh("distance-copies", {10, 0, 0}, {-.5F, -.5F, -.5F},
                      {.5F, .5F, .5F});
    owner.component<Transform3DComponent>()->scale = {2, 2, 2};
    owner.component<MeshRendererComponent>()->cullDistance = 10;
    owner.component<MeshRendererComponent>()->size = {2, 3, 4};
    MeshInstances3DComponent instances;
    instances.setTransform("near-viewer", {.position = {-5, 0, 2.5F}});
    instances.setTransform("far-viewer", {.position = {-5, 0, 12.5F}});
    owner.setComponent(instances);
    copies.entities.push_back(owner);
    auto shadowFrame = frame;
    shadowFrame.position = {0, 0, -100};
    shadowFrame.camera.perspective = false;
    shadowFrame.camera.orthographicSize = 200;
    shadowFrame.camera.farClip = 500;
    // Shadow view distance must use the main camera, retaining nearby casters
    // even when the light's virtual camera sits far away from the player.
    const auto shadow =
        extractVisibleMeshes3D(copies, shadowFrame, nullptr, nullptr, &frame);
    assert(shadow.considered == 2 && shadow.culled == 1);
    assert(shadow.meshes.size() == 1);
    assert(shadow.meshes.front().transform.position.x == 0);
    assert(shadow.meshes.front().transform.position.z == 5);
    shadowFrame.lodPosition = frame.position;
    const auto explicitViewer = extractVisibleMeshes3D(copies, shadowFrame);
    assert(explicitViewer.meshes.size() == 1);

    MeshInstancePreparation3D preparation;
    const auto cachedShadow = extractVisibleMeshes3D(
        copies, shadowFrame, nullptr, nullptr, &frame, nullptr, nullptr,
        nullptr, nullptr, {}, &preparation);
    assert(cachedShadow.meshes.size() == explicitViewer.meshes.size());
    const auto &cachedMesh = cachedShadow.meshes.front();
    assert(cachedMesh.preparedModelMatrix);
    assert(!explicitViewer.meshes.front().preparedModelMatrix);
    assert(
        *cachedMesh.preparedModelMatrix ==
        composeMeshTransform3D(
            cachedMesh.transform,
            copies.entities.front().component<MeshRendererComponent>()->size));
    const auto repeatedShadow = extractVisibleMeshes3D(
        copies, shadowFrame, nullptr, nullptr, &frame, nullptr, nullptr,
        nullptr, nullptr, {}, &preparation);
    assert(repeatedShadow.meshes.front().preparedModelMatrix ==
           cachedMesh.preparedModelMatrix);
    const auto ownerTransform =
        *resolveWorldTransform3D(copies, copies.entities.front());
    auto &prepared = preparation.source(
        copies, copies.entities.front(), ownerTransform,
        *copies.entities.front().component<MeshInstances3DComponent>());
    assert(prepared.copies.size() == 2);
    assert(!prepared.copies[0].world && !prepared.copies[0].bounds &&
           !prepared.copies[0].modelMatrix);
    assert(prepared.copies[1].world && prepared.copies[1].bounds);
    const auto *preparedCopies = prepared.copies.data();
    auto otherViewer = shadowFrame;
    otherViewer.lodPosition = Vec3{0, 0, 25};
    const auto secondView = extractVisibleMeshes3D(
        copies, otherViewer, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, {}, &preparation);
    assert(secondView.meshes.size() == 1);
    assert(secondView.meshes.front().transform.position.z == 25);
    assert(prepared.copies.data() == preparedCopies);
    assert(prepared.copies[0].world && prepared.copies[0].bounds);

    // Unchanged copy values retain owned bounds and matrices across
    // presentations; camera visibility itself is evaluated again.
    const auto retainedRadius = prepared.copies[1].bounds->radius;
    const auto retainedMatrix = prepared.copies[1].modelMatrix;
    preparation.reset();
    auto &retained = preparation.source(
        copies, copies.entities.front(), ownerTransform,
        *copies.entities.front().component<MeshInstances3DComponent>());
    assert(retained.copies[1].bounds->radius == retainedRadius);
    assert(retained.copies[1].modelMatrix == retainedMatrix);

    // A new presentation observes edits even when the component address and
    // number of copies stay unchanged.
    copies.entities.front()
        .component<MeshInstances3DComponent>()
        ->setTransform("near-viewer", {.position = {-5, 0, 3.F}});
    preparation.reset();
    const auto nextPresentation = extractVisibleMeshes3D(
        copies, shadowFrame, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, {}, &preparation);
    assert(nextPresentation.meshes.size() == 1);
    assert(nextPresentation.meshes.front().transform.position.z == 6.F);
    assert(
        *nextPresentation.meshes.front().preparedModelMatrix ==
        composeMeshTransform3D(
            nextPresentation.meshes.front().transform,
            copies.entities.front().component<MeshRendererComponent>()->size));

    // Editor renderFrame calls own separate presentations. Owner edits, like
    // copy edits above, become visible after resetting the immutable snapshot.
    copies.entities.front().component<Transform3DComponent>()->position.x =
        11.F;
    preparation.reset();
    const auto movedOwner = extractVisibleMeshes3D(
        copies, shadowFrame, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, {}, &preparation);
    assert(movedOwner.meshes.size() == 1);
    assert(movedOwner.meshes.front().transform.position.x == 1.F);
    // Retained instance matrices must observe mesh size and local edits even
    // when the instance labels, source address and copy count are unchanged.
    auto *resizedMesh = copies.entities.front().component<MeshRendererComponent>();
    resizedMesh->size = {4.F, 2.F, 3.F};
    preparation.reset();
    const auto resized = extractVisibleMeshes3D(
        copies, shadowFrame, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, {}, &preparation);
    assert(!resized.meshes.empty());
    for (const auto &item : resized.meshes) {
      assert(*item.preparedModelMatrix ==
             composeMeshTransform3D(item.transform, resizedMesh->size));
    }
    resizedMesh->size = {2.F, 3.F, 4.F};
    preparation.reset();
    World otherWorld;
    otherWorld.entities = copies.entities;
    otherWorld.entities.front().component<Transform3DComponent>()->position.x =
        12.F;
    const auto otherScene = extractVisibleMeshes3D(
        otherWorld, shadowFrame, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, {}, &preparation);
    assert(otherScene.meshes.size() == 1);
    assert(otherScene.meshes.front().transform.position.x == 2.F);
  }

  {
    World copies;
    auto owner = mesh("changing-model-bounds", {}, {}, {});
    auto *appearance = owner.component<MeshRendererComponent>();
    appearance->hasBounds = false;
    appearance->model = "asset://changing/model";
    MeshInstances3DComponent instances;
    instances.setTransform("edge", {.position = {30, 0, 8}});
    owner.setComponent(instances);
    copies.entities.push_back(std::move(owner));
    MeshInstancePreparation3D preparation;
    float extent = 1.F;
    const auto bounds = [&](std::string_view) -> std::optional<MeshBounds3D> {
      return MeshBounds3D{{-extent, -extent, -extent}, {extent, extent, extent}};
    };
    const auto extract = [&] {
      return extractVisibleMeshes3D(copies, frame, nullptr, nullptr, nullptr,
                                    nullptr, nullptr, nullptr, nullptr, bounds,
                                    &preparation);
    };
    assert(extract().meshes.empty());
    extent = 40.F;
    preparation.reset();
    assert(extract().meshes.size() == 1);
    extent = 1.F;
    preparation.reset();
    assert(extract().meshes.empty());
  }

  frame.camera.perspective = false;
  frame.camera.orthographicSize = 5.0F;
  const SceneVisibility3D orthographic = extractVisibleMeshes3D(world, frame);
  assert(orthographic.culled >= 3);
  return 0;
}
