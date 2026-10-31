#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/ModelLodSampling3D.h"
#include "demi/runtime/render/bgfx3d/PreparedInstanceView3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/scene/model/World.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <string>
#include <utility>
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
  auto replacement =
      *changing.entities.front().component<MeshRendererComponent>();
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
  world.entities.push_back(
      mesh("editable", {30, 0, 10}, {-.5F, -.5F, -.5F}, {.5F, .5F, .5F}));
  BgfxCameraFrame3D frame;
  frame.viewportWidth = 100;
  frame.viewportHeight = 100;
  MeshInstancePreparation3D preparation;
  const auto visibleCount = [&] {
    preparation.reset();
    return extractVisibleMeshes3D(world, frame, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, {},
                                  &preparation)
        .meshes.size();
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
  world.entities.front().updateComponent<Transform3DComponent>(
      [&](auto &transform) { transform.position.x = 0; });
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
      entity.updateComponent<Transform3DComponent>(
          [&](auto &transform) { transform.rotation = world.rotation; });
      entity.updateComponent<Transform3DComponent>(
          [&](auto &transform) { transform.scale = world.scale; });
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
  instanced.entities.front().updateComponent<Transform3DComponent>(
      [&](auto &value) { value.position = {-10, 0, 0}; });
  instanced.entities.front().updateComponent<Transform3DComponent>(
      [&](auto &value) { value.rotation = {0, .1F, 0}; });
  instanced.entities.front().updateComponent<Transform3DComponent>(
      [&](auto &value) { value.scale = {-1, 2, .5F}; });
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

void shadowHullPrunesInstancesConservativelyInAuthoredOrder() {
  World world;
  auto owner =
      mesh("hull-instances", {}, {-.25F, -.25F, -.25F}, {.25F, .25F, .25F});
  MeshInstances3DComponent instances;
  std::vector<std::pair<std::string, float>> expectedByLabel;
  for (unsigned index = 0; index < 128; ++index) {
    const float x = index % 2 == 0 ? 10.F : -10.F;
    const float z = 5.F + float(index) * .1F;
    instances.setTransform(std::to_string(index), {.position = {x, 0, z}});
    if (x < 0) {
      expectedByLabel.emplace_back(std::to_string(index), z);
    }
  }
  // Its circumsphere overlaps the hull even though its centre lies outside.
  instances.setTransform("boundary", {.position = {.25F, 0, 20}});
  expectedByLabel.emplace_back("boundary", 20);
  // MeshInstances is authored as a stable-label object, whose canonical order
  // is lexicographic. Deliberately unpadded numeric labels catch a BVH that
  // returns insertion, numeric or spatial order instead of source order.
  std::sort(expectedByLabel.begin(), expectedByLabel.end());
  assert(expectedByLabel[0].first == "1" && expectedByLabel[1].first == "101");
  owner.setComponent(std::move(instances));
  world.entities.push_back(std::move(owner));
  world.entities.push_back(mesh("known-outside", {10, 0, 10},
                                {-.25F, -.25F, -.25F}, {.25F, .25F, .25F}));
  auto unknown = mesh("unknown-bounds", {10, 0, 10}, {}, {});
  unknown.component<MeshRendererComponent>()->hasBounds = false;
  unknown.component<MeshRendererComponent>()->model = "asset://unknown/bounds";
  world.entities.push_back(std::move(unknown));

  BgfxCameraFrame3D frame;
  frame.viewportWidth = frame.viewportHeight = 100;
  frame.camera.perspective = false;
  frame.camera.orthographicSize = 50;
  frame.camera.farClip = 100;
  frame.shadowCasterHull.count = 1;
  frame.shadowCasterHull.planes[0] = {1, 0, 0, 0};
  MeshInstancePreparation3D preparation;
  PreparedInstanceView3D prepared;
  const auto extract = [&] {
    return extractVisibleMeshes3D(world, frame, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, {},
                                  &preparation, &prepared);
  };
  const auto visible = extract();
  assert(visible.meshes.size() == expectedByLabel.size() + 1);
  for (std::size_t index = 0; index < expectedByLabel.size(); ++index) {
    assert(visible.meshes[index].entity->id == "hull-instances");
    assert(visible.meshes[index].transform.position.z ==
           expectedByLabel[index].second);
  }
  assert(visible.meshes.back().entity->id == "unknown-bounds");
  assert(visible.shadowHullSourcesRejected == 1);
  assert(visible.shadowHullCopiesRejectedByIndex > 0);
  assert(visible.shadowHullCopiesRejectedByIndex +
             visible.shadowHullCopiesRejected ==
         64);
  assert(extract().instanceSourcesReused == 1);

  // Origin and plane changes affect visibility even if camera/projection and
  // source poses are otherwise identical. Neither may reuse old source slices.
  frame.shadowCasterHull.origin[0] = 100;
  const auto movedOrigin = extract();
  assert(movedOrigin.instanceSourcesReused == 0);
  assert(movedOrigin.meshes.size() == 131);
  frame.shadowCasterHull.origin[0] = 0;
  frame.shadowCasterHull.planes[0][0] = -1;
  const auto reversedPlane = extract();
  assert(reversedPlane.instanceSourcesReused == 0);
  assert(reversedPlane.meshes.size() == 67);
  frame.shadowCasterHull.count = 0;
  const auto ordinary = extract();
  assert(ordinary.instanceSourcesReused == 0);
  assert(ordinary.meshes.size() == 131);
}

void pairedInstanceViewsRespectCameraAndPresentationBoundaries() {
  World world;
  auto batch = mesh("instances", {}, {-.25F, -.25F, -.25F}, {.25F, .25F, .25F});
  batch.component<MeshRendererComponent>()->cullDistance = 12.F;
  batch.component<MeshRendererComponent>()->renderLayer = "foliage";
  MeshInstances3DComponent instances;
  instances.setTransform("near", {.position = {0, 0, 5}});
  instances.setTransform("far", {.position = {0, 0, 20}});
  instances.setTransform("side", {.position = {100, 0, 5}});
  batch.setComponent(instances);
  world.entities.push_back(std::move(batch));
  world.entities.push_back(mesh("ordinary", {0, 0, 7}, {}, {}));

  BgfxCameraFrame3D camera;
  camera.cameraId = "tour";
  camera.viewportWidth = 320;
  camera.viewportHeight = 180;
  camera.camera.farClip = 100;
  MeshInstancePreparation3D preparation;
  PreparedInstanceView3D paired;
  const auto extract = [&](const BgfxCameraFrame3D &view,
                           PreparedInstanceView3D *shared) {
    return extractVisibleMeshes3D(world, view, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, {},
                                  &preparation, shared);
  };
  const auto assertParity = [](const SceneVisibility3D &actual,
                               const SceneVisibility3D &expected) {
    assert(actual.meshes.size() == expected.meshes.size());
    assert(actual.considered == expected.considered);
    assert(actual.culled == expected.culled);
    for (std::size_t index = 0; index < actual.meshes.size(); ++index) {
      assert(actual.meshes[index].entity == expected.meshes[index].entity);
      assert(actual.meshes[index].transform.position.x ==
             expected.meshes[index].transform.position.x);
      assert(actual.meshes[index].transform.position.z ==
             expected.meshes[index].transform.position.z);
    }
  };

  auto depthCamera = camera;
  depthCamera.pass = BgfxCameraPass3D::OpaqueDepth;
  auto depth = extract(depthCamera, &paired);
  assert(depth.instanceSourcesReused == 0);
  assert(depth.meshes.size() == 2);
  assert(depth.meshes[0].entity->id == "instances");
  assert(depth.meshes[1].entity->id == "ordinary");
  // A pass owns its result and may compact/order it without editing the slices
  // consumed by the following camera pass.
  depth.meshes.clear();
  const auto color = extract(camera, &paired);
  assertParity(color, extract(camera, nullptr));
  assert(color.instanceSourcesReused == 1);
  assert(color.instanceCopiesReused == 1);

  std::vector<BgfxCameraFrame3D> differentViews;
  auto changed = camera;
  changed.cameraId = "second_camera";
  differentViews.push_back(changed);
  changed = camera;
  changed.position.x = 100;
  differentViews.push_back(changed);
  changed = camera;
  changed.forward = {1, 0, 0};
  differentViews.push_back(changed);
  changed = camera;
  changed.up = {0, 0, 1};
  differentViews.push_back(changed);
  changed = camera;
  changed.viewportWidth = 640;
  differentViews.push_back(changed);
  changed = camera;
  changed.viewportX = 40;
  differentViews.push_back(changed);
  changed = camera;
  changed.camera.fov = 90;
  differentViews.push_back(changed);
  changed = camera;
  changed.camera.nearClip = 6;
  differentViews.push_back(changed);
  changed = camera;
  changed.camera.farClip = 6;
  differentViews.push_back(changed);
  changed = camera;
  changed.camera.perspective = false;
  differentViews.push_back(changed);
  changed = camera;
  changed.camera.orthographicSize = 20;
  differentViews.push_back(changed);
  changed = camera;
  changed.camera.renderMask = "different_layer";
  differentViews.push_back(changed);
  changed = camera;
  changed.lodPosition = Vec3{100, 0, 5};
  differentViews.push_back(changed);
  for (const auto &different : differentViews) {
    const auto result = extract(different, &paired);
    assert(result.instanceSourcesReused == 0);
    assertParity(result, extract(different, nullptr));
  }

  (void)extract(camera, &paired);
  assert(extract(camera, &paired).instanceSourcesReused == 1);
  paired.reset();
  preparation.reset();
  auto *liveInstances =
      world.entities.front().component<MeshInstances3DComponent>();
  liveInstances->setTransform("near", {.position = {0, 0, 50}});
  const auto nextPresentation = extract(camera, &paired);
  assert(nextPresentation.instanceSourcesReused == 0);
  assert(nextPresentation.meshes.size() == 1);
  assertParity(nextPresentation, extract(camera, nullptr));
}
void sectionSelectionTouchesOnlySourcesThatNeedExpansion() {
  for (const bool animated : {false, true}) {
    World world;
    auto grass = mesh("grass", {}, {-.1F, -.1F, -.1F}, {.1F, .1F, .1F});
    grass.component<MeshRendererComponent>()->model = "asset://grass/model";
    grass.component<MeshRendererComponent>()->material =
        "asset://grass/material";
    MeshInstances3DComponent copies;
    constexpr std::size_t GrassCopies = 1024;
    for (std::size_t index = 0; index < GrassCopies; ++index) {
      copies.setTransform(std::to_string(index), {.position = {0, 0, 10}});
    }
    grass.setComponent(copies);
    world.entities.push_back(std::move(grass));
    auto church = mesh("church", {0, 0, 20}, {-1, -1, -1}, {1, 1, 1});
    church.component<MeshRendererComponent>()->model = "asset://church/model";
    if (animated) {
      church.setComponent(AnimationPlayer3DComponent{});
    }
    world.entities.push_back(std::move(church));
    world.entities.push_back(mesh("marker", {0, 0, 30}, {}, {}));

    MeshMaterialSections3D importedSections;
    for (std::size_t index = 0; index < 6; ++index) {
      importedSections.sections.push_back(
          {.id = "part_" + std::to_string(index),
           .material = "asset://church/material_" + std::to_string(index),
           .firstIndex = static_cast<std::uint32_t>(index * 9),
           .indexCount = 9});
    }
    std::size_t resolverCalls = 0;
    const ModelSectionsResolver3D sections = [&](const VisibleMesh3D &draw,
                                                 Vec3) {
      ++resolverCalls;
      assert(draw.entity->id == "church");
      return &importedSections;
    };
    BgfxCameraFrame3D frame;
    MeshInstancePreparation3D preparation;
    const auto visible = extractVisibleMeshes3D(
        world, frame, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, {}, &preparation, nullptr, sections);
    assert(visible.meshes.size() == GrassCopies + 7);
    assert(visible.considered == GrassCopies + 2);
    assert(visible.sectionSourcesExamined == 1);
    assert(visible.sectionCopiesExamined == 1);
    assert(visible.sectionDrawsEmitted == 6);
    assert(resolverCalls == 1);
    for (std::size_t index = 0; index < GrassCopies; ++index) {
      assert(visible.meshes[index].entity->id == "grass");
      assert(!visible.meshes[index].materialSection);
      assert(visible.meshes[index].modelSections == nullptr);
    }
    for (std::size_t index = 0; index < 6; ++index) {
      const auto &draw = visible.meshes[GrassCopies + index];
      assert(draw.entity->id == "church");
      assert(draw.materialSection == index);
      assert(draw.modelSections == &importedSections);
      const auto &section = draw.modelSections->sections[*draw.materialSection];
      assert(section.firstIndex == index * 9);
      assert(section.indexCount == 9);
      assert(section.material ==
             "asset://church/material_" + std::to_string(index));
    }
    assert(visible.meshes.back().entity->id == "marker");
  }
}

void pairedRawInstancesKeepPerCopyLodSectionsPassLocal() {
  World world;
  auto source = mesh("two_lods", {}, {-1, -1, -1}, {1, 1, 1});
  source.component<MeshRendererComponent>()->model = "asset://model/high";
  MeshInstances3DComponent copies;
  copies.setTransform("near", {.position = {0, 0, 10}});
  copies.setTransform("far", {.position = {0, 0, 40}});
  source.setComponent(copies);
  world.entities.push_back(std::move(source));
  MeshMaterialSections3D high;
  high.sections = {{.id = "stone", .firstIndex = 0, .indexCount = 6},
                   {.id = "roof", .firstIndex = 6, .indexCount = 3}};
  MeshMaterialSections3D low;
  low.sections = {{.id = "combined", .firstIndex = 3, .indexCount = 6}};
  const ModelSectionsResolver3D sections = [&](const VisibleMesh3D &draw,
                                               Vec3 viewer) {
    return draw.transform.position.z - viewer.z < 30 ? &high : &low;
  };
  BgfxCameraFrame3D frame;
  PreparedInstanceView3D paired;
  MeshInstancePreparation3D preparation;
  const auto extract = [&] {
    return extractVisibleMeshes3D(world, frame, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, {},
                                  &preparation, &paired, sections);
  };
  frame.pass = BgfxCameraPass3D::OpaqueDepth;
  auto depth = extract();
  assert(depth.sectionSourcesExamined == 1);
  assert(depth.sectionCopiesExamined == 2);
  assert(depth.meshes.size() == 3);
  const auto &raw = paired.source(world.entities.front()).visibility.meshes;
  assert(raw.size() == 2);
  assert(!raw[0].materialSection && raw[0].modelSections == nullptr);
  depth.meshes.clear();
  frame.pass = BgfxCameraPass3D::Color;
  const auto color = extract();
  assert(color.instanceSourcesReused == 1);
  assert(color.sectionCopiesExamined == 2);
  assert(color.meshes.size() == 3);
  std::size_t nearDraws = 0;
  std::size_t farDraws = 0;
  for (const auto &draw : color.meshes) {
    if (draw.transform.position.z == 10) {
      ++nearDraws;
      assert(draw.modelSections == &high);
      const auto &section = draw.modelSections->sections[*draw.materialSection];
      assert(section.firstIndex == (*draw.materialSection == 0 ? 0 : 6));
    } else {
      ++farDraws;
      assert(draw.modelSections == &low);
      assert(draw.modelSections->sections[*draw.materialSection].firstIndex ==
             3);
    }
  }
  assert(nearDraws == 2 && farDraws == 1);
}
void shadowSamplingFloorPreservesDistanceCullingAndDynamicSources() {
  BgfxCameraFrame3D main;
  main.viewportHeight = 1080;
  main.camera.fov = 60;
  const auto height = effectiveModelLodPixelHeight3D(main);
  const float floor = shadowModelLodDistanceFloor3D(0.1F, main, height);
  assert(std::abs(floor - 93.53074F) < .001F);
  assert(std::abs(shadowModelLodDistanceFloor3D(0.05F, main, height) -
                  floor / 2) < .001F);
  assert(std::abs(shadowModelLodDistanceFloor3D(0.1F, main, height * 2) -
                  floor * 2) < .001F);
  main.camera.renderScale = 0.5F;
  assert(effectiveModelLodPixelHeight3D(main) == 540);
  assert(effectiveModelLodPixelHeight3D(main, 720) == 720);
  main.camera.perspective = false;
  assert(shadowModelLodDistanceFloor3D(0.1F, main, height) == 0);
  main.camera.perspective = true;
  assert(shadowModelLodDistanceFloor3D(0, main, height) == 0);
  assert(shadowModelLodDistanceFloor3D(0.1F, main, 0) == 0);

  Entity entity;
  entity.setComponent(MeshRendererComponent{.model = "asset://high",
                                            .mediumLodModel = "asset://medium",
                                            .mediumLodDistance = 12,
                                            .lowLodModel = "asset://low",
                                            .lowLodDistance = 38,
                                            .cullDistance = 50});
  VisibleMesh3D draw{.entity = &entity, .transform = {.position = {0, 0, 5}}};
  BgfxCameraFrame3D cascade;
  cascade.shadowModelLodDistanceFloor = floor;
  const auto select = [&](bool shadowPass, Vec3 position = {0, 0, 5},
                          bool preserveHigh = false) {
    return selectModelLodGeometry3D(
        *entity.component<MeshRendererComponent>(), position, {}, preserveHigh,
        staticShadowModelLodFloor3D(draw, cascade, shadowPass));
  };
  assert(select(false).level == 0);
  assert(select(true).level == 2 && !select(true).isCulled);
  assert(select(true, {0, 0, 60}).isCulled);
  assert(select(true, {0, 0, 5}, true).level == 0);
  entity.setComponent(AnimationPlayer3DComponent{});
  assert(select(true).level == 0);
  entity.removeComponent<AnimationPlayer3DComponent>();
  entity.setComponent(Dentable3DComponent{});
  assert(select(true).level == 0);
  entity.removeComponent<Dentable3DComponent>();
  entity.setComponent(SurfaceRelief3DComponent{});
  assert(select(true).level == 0);
  entity.removeComponent<SurfaceRelief3DComponent>();
  MeshInstancePreparation3D::EntitySource prepared;
  prepared.entity = &entity;
  prepared.mesh = entity.component<MeshRendererComponent>();
  draw.preparedSource = &prepared;
  assert(select(true).level == 2);
  AnimationPlayer3DComponent animated;
  prepared.animation = &animated;
  assert(select(true).level == 0);
}

void shadowSectionSelectionUsesTheSameStaticSamplingFloor() {
  World world;
  auto subject = mesh("subject", {0, 0, 5}, {-1, -1, -1}, {1, 1, 1});
  subject.setComponent(MeshRendererComponent{.model = "asset://high",
                                             .lowLodModel = "asset://low",
                                             .lowLodDistance = 38,
                                             .cullDistance = 50});
  world.entities.push_back(std::move(subject));
  MeshMaterialSections3D high;
  high.sections = {{.id = "high", .firstIndex = 0, .indexCount = 12}};
  MeshMaterialSections3D low;
  low.sections = {{.id = "low_a", .firstIndex = 3, .indexCount = 3},
                  {.id = "low_b", .firstIndex = 6, .indexCount = 3}};
  BgfxCameraFrame3D frame;
  frame.lodPosition = Vec3{};
  frame.shadowModelLodDistanceFloor = 100;
  bool shadowPass = true;
  const ModelSectionsResolver3D sections = [&](const VisibleMesh3D &draw,
                                               Vec3 camera) {
    const auto selected = selectModelLodGeometry3D(
        *draw.preparedSource->mesh, draw.transform.position, camera, false,
        staticShadowModelLodFloor3D(draw, frame, shadowPass));
    assert(!selected.isCulled);
    return *selected.model == "asset://low" ? &low : &high;
  };
  MeshInstancePreparation3D preparation;
  const auto extract = [&] {
    return extractVisibleMeshes3D(world, frame, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, {},
                                  &preparation, nullptr, sections);
  };
  const auto shadow = extract();
  assert(shadow.meshes.size() == 2);
  assert(shadow.meshes[0].modelSections == &low);
  assert(shadow.meshes[1].modelSections == &low);
  assert(low.sections[*shadow.meshes[0].materialSection].firstIndex == 3);
  assert(low.sections[*shadow.meshes[1].materialSection].firstIndex == 6);
  shadowPass = false;
  assert(extract().meshes.front().modelSections == &high);
  shadowPass = true;
  world.entities.front().setComponent(AnimationPlayer3DComponent{});
  preparation.reset();
  assert(extract().meshes.front().modelSections == &high);
}
void meshShadowOptOutSkipsSourcesBeforeInstanceWork() {
  World world;
  auto blade = mesh("single_grass", {0, 0, 5}, {-1, -1, -1}, {1, 1, 1});
  blade.component<MeshRendererComponent>()->castsShadows = false;
  world.entities.push_back(std::move(blade));
  auto grass = mesh("grass", {}, {-1, -1, -1}, {1, 1, 1});
  grass.component<MeshRendererComponent>()->castsShadows = false;
  MeshInstances3DComponent copies;
  copies.setTransform("a", {.position = {0, 0, 5}});
  copies.setTransform("b", {.position = {0, 0, 10}});
  grass.setComponent(copies);
  world.entities.push_back(std::move(grass));
  world.entities.push_back(mesh("tree", {0, 0, 8}, {-1, -1, -1}, {1, 1, 1}));
  assert(
      world.entities.back().component<MeshRendererComponent>()->castsShadows);

  BgfxCameraFrame3D camera;
  camera.viewportWidth = 320;
  camera.viewportHeight = 180;
  MeshInstancePreparation3D preparation;
  PreparedInstanceView3D paired;
  const auto extract = [&](BgfxCameraPass3D pass) {
    camera.pass = pass;
    return extractVisibleMeshes3D(world, camera, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, {},
                                  &preparation, &paired);
  };
  const auto color = extract(BgfxCameraPass3D::Color);
  assert(color.meshes.size() == 4 && color.instanceCopiesExamined == 2);
  const auto cameraDepth = extract(BgfxCameraPass3D::OpaqueDepth);
  assert(cameraDepth.meshes.size() == color.meshes.size());
  assert(cameraDepth.instanceSourcesReused == 1);
  const auto shadow = extract(BgfxCameraPass3D::Shadow);
  assert(shadow.meshes.size() == 1 &&
         shadow.meshes.front().entity->id == "tree");
  assert(shadow.considered == 1);
  assert(shadow.instanceCopiesExamined == 0 && shadow.instanceNodesTested == 0);
  assert(shadow.instanceSourcesReused == 0);

  assert(RuntimeObjectModel::setComponentField(world, "grass", "MeshRenderer",
                                               "casts_shadows", true));
  preparation.reset();
  paired.reset();
  const auto enabled = extract(BgfxCameraPass3D::Shadow);
  assert(enabled.meshes.size() == 3 && enabled.instanceCopiesExamined == 2);
  assert(RuntimeObjectModel::setComponentField(world, "grass", "MeshRenderer",
                                               "casts_shadows", false));
  preparation.reset();
  paired.reset();
  camera.outputAnalyticDepth = true;
  const auto mrt = extract(BgfxCameraPass3D::Color);
  assert(mrt.meshes.size() == 4);
  const auto fallbackDepth = extract(BgfxCameraPass3D::OpaqueDepth);
  assert(fallbackDepth.meshes.size() == 4);
  assert(extract(BgfxCameraPass3D::Shadow).instanceCopiesExamined == 0);
}
void retainedInstancesSurviveUnrelatedEntityCompaction() {
  World world;
  world.entities.push_back(mesh("retiring_terrain", {}, {}, {}));
  auto batch = mesh("vegetation", {}, {}, {});
  MeshInstances3DComponent instances;
  instances.setTransform("tree", {.position = {5, 0, 10}});
  batch.setComponent(instances);
  world.entities.push_back(std::move(batch));
  MeshInstancePreparation3D preparation;
  const WorldTransform3D owner;
  const auto prepare = [&]() -> auto & {
    const auto &entity = world.entities.back();
    return preparation.source(world, entity, owner,
        *entity.component<MeshInstances3DComponent>());
  };
  const auto *retained = prepare().copies.data();
  world.entities.eraseIf([](const auto &entity) { return entity.id == "retiring_terrain"; });
  preparation.reset();
  auto &after = prepare();
  assert(after.copies.data() == retained);
  assert(after.copies.front().position.x == 5);
  world.entities.back().updateComponent<MeshInstances3DComponent>([](auto &value) {
    value.setTransform("tree", {.position = {9, 0, 10}});
  });
  preparation.reset();
  assert(prepare().copies.front().position.x == 9);
  // Replacing the world at the same address must still refresh its data.
  World replacement;
  auto changed = mesh("vegetation", {}, {}, {});
  instances.setTransform("tree", {.position = {20, 0, 10}});
  changed.setComponent(instances);
  replacement.entities.push_back(std::move(changed));
  world.entities = std::move(replacement.entities);
  preparation.reset();
  assert(prepare().copies.front().position.x == 20);
}

void compiledInstancesRetainCopiesWithoutExpandingSections() {
  World world;
  auto owner = mesh("compiled", {}, {-0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, 0.5F});
  owner.component<MeshRendererComponent>()->model = "asset://compiled/model";
  MeshInstances3DComponent instances;
  instances.setTransform("left", {.position = {-1, 0, 8}});
  instances.setTransform("right", {.position = {1, 0, 8}});
  instances.setTransform("outside", {.position = {100, 0, 8}});
  owner.setComponent(instances);
  world.entities.push_back(std::move(owner));
  BgfxCameraFrame3D frame;
  MeshInstancePreparation3D preparation;
  PreparedInstanceView3D view;
  int sectionQueries = 0;
  const ModelSectionsResolver3D sections = [&](const VisibleMesh3D &, Vec3) {
    ++sectionQueries;
    return static_cast<const MeshMaterialSections3D *>(nullptr);
  };
  const auto compiled = extractVisibleMeshes3D(
      world, frame, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, {}, &preparation, &view, sections,
      [](const VisibleMesh3D &) { return true; });
  assert(compiled.considered == 3 && compiled.culled == 1);
  assert(compiled.meshes.size() == 1 && sectionQueries == 0);
  assert(view.source(world.entities.front()).visibility.meshes.size() == 2);
  const auto ordinary = extractVisibleMeshes3D(
      world, frame, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, {}, &preparation, &view, sections);
  assert(ordinary.meshes.size() == 2 && sectionQueries == 2);
}

int main() {
  compiledInstancesRetainCopiesWithoutExpandingSections();
  retainedInstancesSurviveUnrelatedEntityCompaction();
  meshShadowOptOutSkipsSourcesBeforeInstanceWork();
  shadowHullPrunesInstancesConservativelyInAuthoredOrder();
  shadowSamplingFloorPreservesDistanceCullingAndDynamicSources();
  shadowSectionSelectionUsesTheSameStaticSamplingFloor();
  sectionSelectionTouchesOnlySourcesThatNeedExpansion();
  pairedRawInstancesKeepPerCopyLodSectionsPassLocal();
  pairedInstanceViewsRespectCameraAndPresentationBoundaries();
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
    owner.updateComponent<Transform3DComponent>(
        [&](auto &transform) { transform.scale = {2, 2, 2}; });
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
    copies.entities.front().updateComponent<Transform3DComponent>(
        [&](auto &transform) { transform.position.x = 11.F; });
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
    otherWorld.entities.front().updateComponent<Transform3DComponent>(
        [&](auto &transform) { transform.position.x = 12.F; });
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
