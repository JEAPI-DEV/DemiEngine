#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/render/bgfx3d/TerrainCameraDemand3D.h"
#include "demi/runtime/render/bgfx3d/TerrainDrawSelection3D.h"
#include "demi/runtime/render/bgfx3d/TerrainPatchBatch3D.h"
#include "demi/runtime/render/bgfx3d/VisibleMeshGeometry3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/terrain/TerrainLodResidency.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"

#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace demi::runtime::render {
namespace {

float dot(const Vec3 left, const Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 cross(const Vec3 left, const Vec3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

Vec3 normalized(const Vec3 value, const Vec3 fallback) {
  const float lengthSquared = dot(value, value);
  if (lengthSquared <= 0.000001F) {
    return fallback;
  }
  const float inverseLength = 1.0F / std::sqrt(lengthSquared);
  return {value.x * inverseLength, value.y * inverseLength,
          value.z * inverseLength};
}

struct Sphere {
  Vec3 center;
  float radius = 0.0F;
};

Sphere worldBounds(const MeshRendererComponent &mesh,
                   WorldTransform3D transform,
                   std::span<const MeshDent3D> dents,
                   const PreparedTerrainChunkLod *terrain = nullptr,
                   std::optional<Vec3> sizeOverride = std::nullopt) {
  const auto size = sizeOverride.value_or(mesh.size);
  transform.scale = {transform.scale.x * size.x, transform.scale.y * size.y,
                     transform.scale.z * size.z};
  Vec3 minimum;
  Vec3 maximum;
  bool first = true;
  const Vec3 expansion = meshDentBoundsExpansion3D(dents);
  const auto boundsMin = terrain ? terrain->boundsMin : mesh.boundsMin;
  const auto boundsMax = terrain ? terrain->boundsMax : mesh.boundsMax;
  for (const float x : {boundsMin.x - expansion.x, boundsMax.x + expansion.x}) {
    for (const float y :
         {boundsMin.y - expansion.y, boundsMax.y + expansion.y}) {
      for (const float z :
           {boundsMin.z - expansion.z, boundsMax.z + expansion.z}) {
        const Vec3 point = transformPoint3D(transform, {x, y, z});
        if (first) {
          minimum = maximum = point;
          first = false;
        } else {
          minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y),
                     std::min(minimum.z, point.z)};
          maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y),
                     std::max(maximum.z, point.z)};
        }
      }
    }
  }
  const Vec3 center{(minimum.x + maximum.x) * 0.5F,
                    (minimum.y + maximum.y) * 0.5F,
                    (minimum.z + maximum.z) * 0.5F};
  const Vec3 extent{maximum.x - center.x, maximum.y - center.y,
                    maximum.z - center.z};
  return {.center = center, .radius = std::sqrt(dot(extent, extent))};
}

bool sphereVisible(const Sphere sphere, const BgfxCameraFrame3D &frame) {
  const Vec3 forward = normalized(frame.forward, {0.0F, 0.0F, 1.0F});
  const Vec3 right = normalized(cross(frame.up, forward), {1.0F, 0.0F, 0.0F});
  const Vec3 up = normalized(cross(forward, right), {0.0F, 1.0F, 0.0F});
  const Vec3 relative{sphere.center.x - frame.position.x,
                      sphere.center.y - frame.position.y,
                      sphere.center.z - frame.position.z};
  const float depth = dot(relative, forward);
  if (depth + sphere.radius < frame.camera.nearClip ||
      depth - sphere.radius > frame.camera.farClip) {
    return false;
  }

  const float aspect =
      static_cast<float>(std::max(frame.viewportWidth, std::uint16_t{1})) /
      static_cast<float>(std::max(frame.viewportHeight, std::uint16_t{1}));
  float halfWidth = std::max(frame.camera.orthographicSize, 0.001F) * aspect;
  float halfHeight = std::max(frame.camera.orthographicSize, 0.001F);
  float horizontalRadius = sphere.radius;
  float verticalRadius = sphere.radius;
  if (frame.camera.perspective) {
    constexpr float DegreesToRadians = 0.01745329251994329577F;
    const float tangent = std::tan(std::clamp(frame.camera.fov, 1.0F, 179.0F) *
                                   DegreesToRadians * 0.5F);
    halfHeight = std::max(depth, 0.0F) * tangent;
    halfWidth = halfHeight * aspect;
    verticalRadius *= std::sqrt(1.0F + tangent * tangent);
    const float horizontalTangent = tangent * aspect;
    horizontalRadius *= std::sqrt(1.0F + horizontalTangent * horizontalTangent);
  }
  return std::abs(dot(relative, right)) <= halfWidth + horizontalRadius &&
         std::abs(dot(relative, up)) <= halfHeight + verticalRadius;
}

void appendVisibleInstances(const VisibleMesh3D &source,
                            const MeshInstances3DComponent &instances,
                            const BgfxCameraFrame3D &frame,
                            SceneVisibility3D &result) {
  const auto &mesh = *source.entity->component<MeshRendererComponent>();
  const auto &dents = entityMeshDents3D(*source.entity);

  for (const auto &[instanceId, localTransform] : instances.transforms) {
    ++result.considered;
    const auto worldTransform =
        composeWorldTransform3D(source.transform, localTransform);
    if (mesh.hasBounds &&
        !sphereVisible(worldBounds(mesh, worldTransform, dents), frame)) {
      ++result.culled;
      continue;
    }
    result.meshes.push_back({source.entity, worldTransform});
  }
}

} // namespace

SceneVisibility3D extractVisibleMeshes3D(
    const World &world, const BgfxCameraFrame3D &frame, JobSystem *jobs,
    TerrainDrawSelection3D *terrainSelection,
    const BgfxCameraFrame3D *lodCamera, TerrainLodResidency *terrainResidency,
    TerrainCameraDemand3D *terrainDemand,
    const TerrainPatchBatch3D *terrainPatches) {
  std::vector<std::optional<VisibleMesh3D>> extracted(world.entities.size());
  // byte-per-entry storage lets workers write independent indices without the
  // packed-bit races of std::vector<bool>.
  std::vector<std::uint8_t> considered(world.entities.size(), 0);
  std::vector<std::uint8_t> culled(world.entities.size(), 0);
  const auto extract = [&](const std::size_t index) {
    const Entity &entity = world.entities[index];
    if (!entity.enabled) {
      return;
    }
    const auto *mesh = entity.component<MeshRendererComponent>();
    if (mesh == nullptr) {
      return;
    }
    const auto *terrainMarker = entity.component<TerrainRenderLod>();
    if (terrainPatches && terrainMarker &&
        terrainPatches->suppressesOwner(terrainMarker->owner)) {
      return;
    }
    if (!frame.camera.renderMask.empty() && !mesh->renderLayer.empty() &&
        frame.camera.renderMask != mesh->renderLayer) {
      return;
    }
    considered[index] = 1;
    const auto transform = resolveWorldTransform3D(world, entity);
    if (!transform) {
      return;
    }
    const auto *terrain = entity.component<TerrainRenderLod>();
    const auto *chunk =
        terrain && terrain->chunk ? terrain->chunk.get() : nullptr;
    if (!entity.hasComponent<MeshInstances3DComponent>() &&
        (mesh->hasBounds || chunk) &&
        !sphereVisible(
            worldBounds(*mesh, *transform, entityMeshDents3D(entity), chunk),
            frame)) {
      culled[index] = 1;
      return;
    }
    extracted[index] =
        VisibleMesh3D{.entity = &entity, .transform = *transform};
  };
  // Transform hierarchy lookup is cheap for ordinary scenes. Dispatch only
  // when extraction is large enough to amortize wake-up and synchronization.
  constexpr std::size_t ParallelExtractionThreshold = 1024;
  if (jobs != nullptr && world.entities.size() >= ParallelExtractionThreshold) {
    jobs->parallelFor(world.entities.size(), 64, extract);
  } else {
    for (std::size_t index = 0; index < world.entities.size(); ++index) {
      extract(index);
    }
  }

  SceneVisibility3D result;
  result.meshes.reserve(world.entities.size());
  for (std::size_t index = 0; index < extracted.size(); ++index) {
    if (extracted[index]) {
      const auto &source = *extracted[index];
      const auto *instances =
          source.entity->component<MeshInstances3DComponent>();
      if (instances) {
        appendVisibleInstances(source, *instances, frame, result);
        continue;
      }
    }
    result.considered += considered[index] ? 1U : 0U;
    result.culled += culled[index] ? 1U : 0U;
    if (extracted[index]) {
      result.meshes.push_back(std::move(*extracted[index]));
    }
  }
  std::vector<VisibleMesh3D> draws;
  draws.reserve(result.meshes.size());
  std::unordered_map<std::string, float> ownerQuality;
  struct ChunkDrawSelection {
    std::size_t level = 0;
    std::shared_ptr<const PreparedTerrainLodLevel> prepared;
  };
  std::unordered_map<
      std::string,
      std::unordered_map<const PreparedTerrainChunkLod *, ChunkDrawSelection>>
      chunkLevels;
  const auto &selectionCamera = lodCamera ? *lodCamera : frame;
  for (auto visible : result.meshes) {
    const auto *terrain = visible.entity->component<TerrainRenderLod>();
    if (terrain && terrain->chunk) {
      auto [quality, inserted] = ownerQuality.try_emplace(terrain->owner, 0.0F);
      if (inserted) {
        const auto *owner = findEntity(world, terrain->owner);
        const auto *component =
            owner ? owner->component<Terrain3DComponent>() : nullptr;
        if (component) {
          quality->second = component->lodErrorPixels;
        }
      }
      const auto *appearance =
          visible.entity->component<MeshRendererComponent>();
      visible.terrainSurface = true;
      auto [decision, firstSurface] =
          chunkLevels[terrain->owner].try_emplace(terrain->chunk.get());
      if (firstSurface) {
        decision->second.level =
            terrainSelection
                ? terrainSelection->select(terrain->chunk, visible.transform,
                                           appearance->size, selectionCamera,
                                           quality->second, terrain->owner)
                : selectTerrainDrawLevel3D(*terrain->chunk, visible.transform,
                                           appearance->size, selectionCamera,
                                           quality->second);
      }
      if (terrainPatches && terrainPatches->forcesFullOwner(terrain->owner)) {
        decision->second.level = 0;
      }
      if (firstSurface && decision->second.level != 0) {
        if (terrainDemand) {
          terrainDemand->record(selectionCamera, visible.entity->id,
                                terrain->owner, terrain->chunk,
                                decision->second.level);
        }
        decision->second.prepared =
            terrainResidency ? terrainResidency->request(terrain->chunk,
                                                         decision->second.level)
                             : nullptr;
        if (!decision->second.prepared) {
          decision->second.level = 0;
        }
      }
      visible.terrainLevel = decision->second.level;
      visible.terrainPreparedLevel = decision->second.prepared;
      if (visible.terrainLevel != 0) {
        const auto surface = visible.terrainPreparedLevel->find(terrain->biome);
        if (surface == visible.terrainPreparedLevel->end()) {
          continue;
        }
        visible.terrainGeometry = surface->second.geometry;
        visible.terrainSections = &surface->second.sections;
      }
    }
    const auto *sections = visibleMeshSections3D(visible);
    const auto &mesh = visibleMeshGeometry3D(visible);
    if (visible.terrainSurface && mesh.hasBounds) {
      const auto *appearance =
          visible.entity->component<MeshRendererComponent>();
      const auto bounds = worldBounds(mesh, visible.transform,
                                      entityMeshDents3D(*visible.entity),
                                      nullptr, appearance->size);
      if (!sphereVisible(bounds, frame)) {
        ++result.culled;
        continue;
      }
    }
    if (mesh.vertices.empty() || !sections || sections->sections.empty()) {
      draws.push_back(std::move(visible));
      continue;
    }
    for (std::size_t index = 0; index < sections->sections.size(); ++index) {
      auto draw = visible;
      draw.materialSection = index;
      draws.push_back(std::move(draw));
    }
  }
  result.meshes = std::move(draws);
  return result;
}

bool terrainChunkVisible3D(const PreparedTerrainChunkLod &chunk,
                           const WorldTransform3D &transform,
                           const BgfxCameraFrame3D &frame) {
  const Vec3 localCenter{(chunk.boundsMin.x + chunk.boundsMax.x) * 0.5F,
                         (chunk.boundsMin.y + chunk.boundsMax.y) * 0.5F,
                         (chunk.boundsMin.z + chunk.boundsMax.z) * 0.5F};
  const Vec3 extent{(chunk.boundsMax.x - chunk.boundsMin.x) * 0.5F,
                    (chunk.boundsMax.y - chunk.boundsMin.y) * 0.5F,
                    (chunk.boundsMax.z - chunk.boundsMin.z) * 0.5F};
  const auto center = transformPoint3D(transform, localCenter);
  const float radius =
      std::hypot(extent.x * transform.scale.x, extent.y * transform.scale.y,
                 extent.z * transform.scale.z);
  return sphereVisible({center, radius}, frame);
}

} // namespace demi::runtime::render
