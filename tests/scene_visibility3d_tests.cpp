#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
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

void retainedSourcesObserveComponentReplacement() {
  BgfxCameraFrame3D frame;
  World changing;
  changing.entities.push_back(mesh("replacement", {0, 0, 8}, {}, {}));
  MeshInstancePreparation3D preparation;
  const auto extract = [&] {
    return extractVisibleMeshes3D(changing, frame, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, {},
                                  &preparation);
  };
  assert(extract().meshes.size() == 1);
  auto replacement = *changing.entities.front().component<MeshRendererComponent>();
  replacement.color = {1.F, 0.F, 0.F, 1.F};
  changing.entities.front().setComponent(std::move(replacement));
  preparation.reset();
  const auto replaced = extract();
  assert(replaced.meshes.size() == 1);
  assert(replaced.meshes.front().preparedSource->mesh ==
         changing.entities.front().component<MeshRendererComponent>());
  assert(replaced.meshes.front().preparedSource->mesh->color.g == 0.F);
  changing.entities.front().removeComponent<MeshRendererComponent>();
  preparation.reset();
  assert(extract().meshes.empty());
  changing.entities.front().setComponent(MeshRendererComponent{});
  preparation.reset();
  assert(extract().meshes.size() == 1);
}

void retainedPrimitiveBoundsFollowLiveEdits() {
  World world;
  world.entities.push_back(mesh("editable", {30, 0, 10}, {-.5F, -.5F, -.5F},
                                {.5F, .5F, .5F}));
  BgfxCameraFrame3D frame;
  frame.viewportWidth = 100;
  frame.viewportHeight = 100;
  MeshInstancePreparation3D preparation;
  const auto visibleCount = [&] {
    preparation.reset();
    return extractVisibleMeshes3D(world, frame, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, {},
                                  &preparation).meshes.size();
  };
  auto *appearance = world.entities.front().component<MeshRendererComponent>();
  assert(visibleCount() == 0);
  appearance->size = {100, 100, 100};
  assert(visibleCount() == 1);
  appearance->size = {1, 1, 1};
  assert(visibleCount() == 0);
  appearance->boundsMin = {-40, -40, -40};
  appearance->boundsMax = {40, 40, 40};
  assert(visibleCount() == 1);
  appearance->hasBounds = false;
  assert(visibleCount() == 0);
  world.entities.front().component<Transform3DComponent>()->position.x = 0;
  assert(visibleCount() == 1);
}

void orthographicCullingMatchesExactProjectionEdges() {
  World world;
  const std::array<Vec3, 12> positions{{{10, 0, 10},
                                        {-10, 0, 10},
                                        {0, 5, 10},
                                        {0, -5, 10},
                                        {0, 0, 1},
                                        {0, 0, 20},
                                        {10.01F, 0, 10},
                                        {-10.01F, 0, 10},
                                        {0, 5.01F, 10},
                                        {0, -5.01F, 10},
                                        {0, 0, 0.99F},
                                        {0, 0, 20.01F}}};
  for (std::size_t index = 0; index < positions.size(); ++index) {
    world.entities.push_back(
        mesh(std::to_string(index), positions[index], {}, {}));
  }
  BgfxCameraFrame3D frame;


  frame.camera.perspective = false;
  frame.camera.orthographicSize = 10;
  frame.camera.nearClip = 1;
  frame.camera.farClip = 20;
  frame.viewportWidth = 200;
  frame.viewportHeight = 100;
  frame.forward = {0, 0, 1};
  frame.up = {0, 1, 0};
  const auto visible = extractVisibleMeshes3D(world, frame);
  assert(visible.meshes.size() == 6);
  for (std::size_t index = 0; index < visible.meshes.size(); ++index) {
    assert(visible.meshes[index].entity->id == std::to_string(index));
  }
}

void spatialInstancesMatchReferenceAndKeepOffscreenShadowCasters() {
  World instanced;
  Entity owner;
  owner.id = "vegetation";
  owner.setComponent(Transform3DComponent{});
  owner.setComponent(MeshRendererComponent{.shape = "cube"});
  MeshInstances3DComponent instances;
  for (unsigned index = 0; index < 4096; ++index) {
    instances.setTransform(
        std::to_string(index),
        {.position = {float(index % 64) * 10, 0, float(index / 64) * 10}});
  }
  owner.setComponent(std::move(instances));
  instanced.entities.push_back(std::move(owner));
  MeshInstancePreparation3D preparation;
  BgfxCameraFrame3D camera;
  camera.position = {5, 0, -10};
  camera.forward = {0, 0, 1};
  camera.up = {0, 1, 0};
  camera.camera.fov = 60;
  camera.camera.farClip = 40;
  camera.viewportWidth = camera.viewportHeight = 128;
  const auto reference = [&](const BgfxCameraFrame3D &view) {
    World ordinary;
    const auto transform =
        *resolveWorldTransform3D(instanced, instanced.entities.front());
    for (const auto &[id, local] : instanced.entities.front()
                                       .component<MeshInstances3DComponent>()
                                       ->transforms()) {
      const auto world = composeWorldTransform3D(transform, local);
      auto entity =
          mesh(id, world.position, {-.5F, -.5F, -.5F}, {.5F, .5F, .5F});
      entity.component<Transform3DComponent>()->rotation = world.rotation;
      entity.component<Transform3DComponent>()->scale = world.scale;
      ordinary.entities.push_back(std::move(entity));
    }
    const auto expected = extractVisibleMeshes3D(ordinary, view);
    const auto actual = extractVisibleMeshes3D(
        instanced, view, nullptr, nullptr, &camera, nullptr, nullptr, nullptr,
        nullptr, {}, &preparation);
    assert(actual.meshes.size() == expected.meshes.size());
    for (std::size_t index = 0; index < actual.meshes.size(); ++index) {
      assert(actual.meshes[index].transform.position.x ==
             expected.meshes[index].transform.position.x);
      assert(actual.meshes[index].transform.position.z ==
             expected.meshes[index].transform.position.z);
    }
    assert(actual.considered == 4096);
    assert(actual.instanceCopiesExamined < 512);
    assert(actual.instanceCopiesRejectedByIndex > 3500);
    assert(actual.instanceNodesTested > 0);
    return actual;
  };
  assert(!reference(camera).meshes.empty());
  auto shadow = camera;
  shadow.viewId = 1;
  shadow.position = {150, 100, 150};
  shadow.forward = {0, -1, 0};
  shadow.up = {0, 0, 1};
  shadow.camera.perspective = false;
  shadow.camera.orthographicSize = 60;
  shadow.camera.farClip = 200;
  const auto casters = reference(shadow);
  assert(!casters.meshes.empty());
  assert(casters.meshes.front().transform.position.x > 100);
  // Content edits and transformed owners rebuild the position index. Bounds
  // retain nonuniform/mirrored scale and rotations through the ordinary path.
  instanced.entities.front()
      .component<MeshInstances3DComponent>()
      ->setTransform("4095", {.position = {5, 0, 5}});
  preparation.reset();
  reference(camera);
  auto *transform =
      instanced.entities.front().component<Transform3DComponent>();
  transform->position = {-10, 0, 0};
  transform->rotation = {0, .1F, 0};
  transform->scale = {-1, 2, .5F};
  preparation.reset();
  reference(camera);
}

void spatialIndexExpandsForChangingLodBounds() {
  World world;
  Entity owner;
  owner.id = "lod-instances";
  owner.setComponent(Transform3DComponent{});
  MeshRendererComponent geometry;
  geometry.model = "asset://base";
  geometry.mediumLodModel = "asset://larger-lod";
  owner.setComponent(geometry);
  MeshInstances3DComponent instances;
  instances.setTransform("edge", {.position = {10, 0, 10}});
  for (unsigned index = 0; index < 128; ++index) {
    instances.setTransform("far" + std::to_string(index),
                           {.position = {1000, 0, 10}});
  }
  owner.setComponent(std::move(instances));
  world.entities.push_back(std::move(owner));
  BgfxCameraFrame3D camera;
  camera.forward = {0, 0, 1};
  camera.up = {0, 1, 0};
  camera.viewportWidth = camera.viewportHeight = 128;
  camera.camera.perspective = false;
  camera.camera.orthographicSize = 10;
  camera.camera.farClip = 50;
  float lodExtent = .5F;
  const ModelBoundsResolver3D bounds = [&](std::string_view model) {
    const auto extent = model == "asset://larger-lod" ? lodExtent : .5F;
    return std::optional<MeshBounds3D>{
        {{-extent, -extent, -extent}, {extent, extent, extent}}};
  };
  MeshInstancePreparation3D preparation;
  const auto extract = [&] {
    return extractVisibleMeshes3D(world, camera, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, bounds,
                                  &preparation);
  };
  assert(extract().meshes.empty());
  // Resource bounds can change without an instance-content revision. The
  // cached position hierarchy must accept the new geometry expansion.
  lodExtent = 5;
  preparation.reset();
  const auto larger = extract();
  assert(larger.meshes.size() == 1);
  assert(larger.meshes.front().transform.position.x == 10);
}

int main() {
  retainedPrimitiveBoundsFollowLiveEdits();
  retainedSourcesObserveComponentReplacement();
  orthographicCullingMatchesExactProjectionEdges();
  spatialIndexExpandsForChangingLodBounds();
  spatialInstancesMatchReferenceAndKeepOffscreenShadowCasters();
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
    copies.entities.front().component<MeshInstances3DComponent>()->setTransform(
        "near-viewer", {.position = {-5, 0, 3.F}});
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
    auto *resizedMesh =
        copies.entities.front().component<MeshRendererComponent>();
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
      return MeshBounds3D{{-extent, -extent, -extent},
                          {extent, extent, extent}};
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
