#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/render/bgfx3d/DebugGeometry3D.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cassert>
#include <cmath>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

bool close(const float left, const float right) {
  return std::abs(left - right) < 0.0001F;
}

Entity transformedEntity(const std::string &id, const Vec3 position = {},
                         const Vec3 scale = {1.0F, 1.0F, 1.0F}) {
  Entity entity{.id = id};
  entity.setComponent(
      Transform3DComponent{.position = position, .scale = scale});
  return entity;
}

} // namespace

int main() {
  World world;
  Entity box = transformedEntity("box", {3.0F, 0.0F, 0.0F}, {2.0F, 1.0F, 0.5F});
  box.setComponent(BoxCollider3DComponent{.size = {2.0F, 4.0F, 6.0F},
                                          .offset = {1.0F, 0.0F, 0.0F}});
  world.entities.push_back(box);
  assert(buildDebugGeometry3D(world).empty());

  world.debug.colliders = true;
  auto lines = buildDebugGeometry3D(world);
  assert(lines.size() == 12);
  float minimumX = lines.front().start.x;
  float maximumX = minimumX;
  float minimumY = lines.front().start.y;
  float maximumY = minimumY;
  float minimumZ = lines.front().start.z;
  float maximumZ = minimumZ;
  for (const DebugLine3D &line : lines)
    for (const Vec3 point : {line.start, line.end}) {
      minimumX = std::min(minimumX, point.x);
      maximumX = std::max(maximumX, point.x);
      minimumY = std::min(minimumY, point.y);
      maximumY = std::max(maximumY, point.y);
      minimumZ = std::min(minimumZ, point.z);
      maximumZ = std::max(maximumZ, point.z);
    }
  assert(close(minimumX, 3.0F));
  assert(close(maximumX, 7.0F));
  assert(close(minimumY, -2.0F));
  assert(close(maximumY, 2.0F));
  assert(close(minimumZ, -1.5F));
  assert(close(maximumZ, 1.5F));

  World sphereWorld;
  sphereWorld.debug.colliders = true;
  Entity sphere = transformedEntity("sphere", {}, {1.0F, 2.0F, 0.5F});
  sphere.setComponent(SphereCollider3DComponent{
      .radius = 2.0F, .offset = {1.0F, 0.0F, 0.0F}, .isTrigger = true});
  sphereWorld.entities.push_back(sphere);
  lines = buildDebugGeometry3D(sphereWorld);
  assert(lines.size() == 3U * 24U);
  assert(std::ranges::all_of(lines, [](const DebugLine3D &line) {
    return close(line.color.r, 1.0F) && close(line.color.g, 0.78F);
  }));
  minimumX = lines.front().start.x;
  maximumX = minimumX;
  for (const DebugLine3D &line : lines)
    for (const Vec3 point : {line.start, line.end}) {
      minimumX = std::min(minimumX, point.x);
      maximumX = std::max(maximumX, point.x);
    }
  assert(close(minimumX, -3.0F));
  assert(close(maximumX, 5.0F));

  World capsuleWorld;
  capsuleWorld.debug.colliders = true;
  Entity capsule = transformedEntity("capsule");
  capsule.setComponent(
      CapsuleCollider3DComponent{.radius = 0.5F, .height = 2.0F});
  capsuleWorld.entities.push_back(capsule);
  assert(buildDebugGeometry3D(capsuleWorld).size() == 100);

  World characterWorld;
  characterWorld.debug.colliders = true;
  Entity character =
      transformedEntity("character", {2.0F, 3.0F, 4.0F}, {2.0F, 1.0F, 0.5F});
  character.setComponent(CharacterController3DComponent{});
  character.setComponent(BoxCollider3DComponent{.size = {1.0F, 2.0F, 2.0F}});
  characterWorld.entities.push_back(std::move(character));
  lines = buildDebugGeometry3D(characterWorld);
  assert(lines.size() == 12);
  minimumX = lines.front().start.x;
  maximumX = minimumX;
  minimumY = lines.front().start.y;
  maximumY = minimumY;
  for (const DebugLine3D &line : lines)
    for (const Vec3 point : {line.start, line.end}) {
      minimumX = std::min(minimumX, point.x);
      maximumX = std::max(maximumX, point.x);
      minimumY = std::min(minimumY, point.y);
      maximumY = std::max(maximumY, point.y);
    }
  assert(close(minimumX, 1.0F));
  assert(close(maximumX, 3.0F));
  assert(close(minimumY, 2.0F));
  assert(close(maximumY, 4.0F));

  World convexWorld;
  convexWorld.debug.colliders = true;
  Entity convex = transformedEntity("convex");
  convex.setComponent(ConvexCollider3DComponent{
      .points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});
  convexWorld.entities.push_back(convex);
  assert(buildDebugGeometry3D(convexWorld).size() == 6);

  World modelWorld;
  modelWorld.debug.colliders = true;
  modelWorld.colliderAssets3D["asset://triangle"] =
      ColliderAsset3D{.triangles = {{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}};
  Entity model = transformedEntity("model", {2.0F, 3.0F, 4.0F});
  model.setComponent(ModelCollider3DComponent{.asset = "asset://triangle"});
  modelWorld.entities.push_back(model);
  lines = buildDebugGeometry3D(modelWorld);
  assert(lines.size() == 3);
  assert(close(lines.front().start.x, 2.0F));
  assert(close(lines.front().start.y, 3.0F));
  assert(close(lines.front().start.z, 4.0F));

  World visibilityWorld;
  Entity hidden = transformedEntity("hidden");
  hidden.enabled = false;
  hidden.setComponent(BoxCollider3DComponent{});
  visibilityWorld.entities.push_back(hidden);
  Entity individuallyVisible = transformedEntity("individual");
  individuallyVisible.setComponent(
      CapsuleCollider3DComponent{.debugVisible = true});
  visibilityWorld.entities.push_back(individuallyVisible);
  assert(buildDebugGeometry3D(visibilityWorld).size() == 100);

  World gridWorld;
  gridWorld.debug.grid = true;
  assert(buildDebugGeometry3D(gridWorld).size() == 82);

  World requestedWorld;
  Entity requestedCollider = transformedEntity("requested-collider");
  requestedCollider.setComponent(BoxCollider3DComponent{});
  requestedWorld.entities.push_back(requestedCollider);
  assert(buildDebugGeometry3D(requestedWorld,
                              {.forceColliders = true, .bounds = false})
             .size() == 12);

  World boundsWorld;
  Entity bounded = transformedEntity("bounded", {1.0F, 2.0F, 3.0F});
  bounded.setComponent(
      MeshRendererComponent{.size = {2.0F, 4.0F, 6.0F},
                            .boundsMin = {-0.5F, -0.25F, -1.0F},
                            .boundsMax = {0.5F, 0.25F, 1.0F},
                            .hasBounds = true});
  boundsWorld.entities.push_back(std::move(bounded));
  lines = buildDebugGeometry3D(boundsWorld,
                               {.forceColliders = false, .bounds = true});
  assert(lines.size() == 12);
  minimumX = lines.front().start.x;
  maximumX = minimumX;
  for (const DebugLine3D &line : lines)
    for (const Vec3 point : {line.start, line.end}) {
      minimumX = std::min(minimumX, point.x);
      maximumX = std::max(maximumX, point.x);
    }
  assert(close(minimumX, 0.0F));
  assert(close(maximumX, 2.0F));

  World overlayWorld;
  Entity pointLight =
      transformedEntity("point-light", {2.0F, 3.0F, 4.0F}, {4.0F, 4.0F, 4.0F});
  pointLight.setComponent(PointLightComponent{.range = 1.5F});
  overlayWorld.entities.push_back(std::move(pointLight));
  Entity camera = transformedEntity("camera", {-2.0F, 1.0F, 0.0F});
  camera.setComponent(Camera3DComponent{});
  overlayWorld.entities.push_back(std::move(camera));
  assert(buildDebugGeometry3D(overlayWorld).empty());
  assert(buildDebugGeometry3D(overlayWorld, {.lights = true}).size() ==
         3U + 3U * 24U);
  lines = buildDebugGeometry3D(overlayWorld, {.lights = true});
  minimumX = lines.front().start.x;
  maximumX = minimumX;
  for (const DebugLine3D &line : lines)
    for (const Vec3 point : {line.start, line.end}) {
      minimumX = std::min(minimumX, point.x);
      maximumX = std::max(maximumX, point.x);
    }
  assert(close(minimumX, 0.5F));
  assert(close(maximumX, 3.5F));
  assert(buildDebugGeometry3D(overlayWorld, {.cameras = true}).size() == 8U);
  assert(buildDebugGeometry3D(overlayWorld, {.lights = true, .cameras = true})
             .size() == 3U + 3U * 24U + 8U);
  return 0;
}
