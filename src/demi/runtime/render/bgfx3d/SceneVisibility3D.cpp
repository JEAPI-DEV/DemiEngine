#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/TerrainCameraDemand3D.h"
#include "demi/runtime/render/bgfx3d/TerrainDrawSelection3D.h"
#include "demi/runtime/render/bgfx3d/TerrainPatchBatch3D.h"
#include "demi/runtime/render/bgfx3d/VisibleMeshGeometry3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
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
                   std::optional<Vec3> sizeOverride = std::nullopt,
                   const std::optional<MeshBounds3D> &bounds = std::nullopt) {
  const auto size = sizeOverride.value_or(mesh.size);
  const auto boundsMin = terrain  ? terrain->boundsMin
                         : bounds ? bounds->minimum
                                  : mesh.boundsMin;
  const auto boundsMax = terrain  ? terrain->boundsMax
                         : bounds ? bounds->maximum
                                  : mesh.boundsMax;
  const Vec3 expansion = meshDentBoundsExpansion3D(dents);
  const Vec3 center{(boundsMin.x + boundsMax.x) * .5F,
                    (boundsMin.y + boundsMax.y) * .5F,
                    (boundsMin.z + boundsMax.z) * .5F};
  const Vec3 extent{(boundsMax.x - boundsMin.x) * .5F + expansion.x,
                    (boundsMax.y - boundsMin.y) * .5F + expansion.y,
                    (boundsMax.z - boundsMin.z) * .5F + expansion.z};
  transform.scale = {transform.scale.x * size.x, transform.scale.y * size.y,
                     transform.scale.z * size.z};
  const Vec3 scaledExtent{extent.x * transform.scale.x,
                          extent.y * transform.scale.y,
                          extent.z * transform.scale.z};
  // Rotation preserves this circumsphere's radius. Transforming eight corners
  // only to enclose the resulting AABB repeats trigonometry and enlarges it.
  return {transformPoint3D(transform, center),
          std::sqrt(dot(scaledExtent, scaledExtent))};
}

struct ViewFrustum {
  Vec3 position;
  Vec3 forward;
  Vec3 right;
  Vec3 up;
  float nearClip;
  float farClip;
  float halfWidth;
  float halfHeight;
  float horizontalRadiusScale = 1;
  float verticalRadiusScale = 1;
  bool perspective;

  explicit ViewFrustum(const BgfxCameraFrame3D &frame)
      : position(frame.position),
        forward(normalized(frame.forward, {0.F, 0.F, 1.F})),
        right(normalized(cross(frame.up, forward), {1.F, 0.F, 0.F})),
        up(normalized(cross(forward, right), {0.F, 1.F, 0.F})),
        nearClip(frame.camera.nearClip), farClip(frame.camera.farClip),
        perspective(frame.camera.perspective) {
    const float aspect =
        float(std::max(frame.viewportWidth, std::uint16_t{1})) /
        float(std::max(frame.viewportHeight, std::uint16_t{1}));
    halfHeight = std::max(frame.camera.orthographicSize, .001F);
    if (perspective) {
      constexpr float DegreesToRadians = .01745329251994329577F;
      halfHeight = std::tan(std::clamp(frame.camera.fov, 1.F, 179.F) *
                            DegreesToRadians * .5F);
    }
    halfWidth = halfHeight * aspect;
    if (perspective) {
      verticalRadiusScale = std::sqrt(1.F + halfHeight * halfHeight);
      horizontalRadiusScale = std::sqrt(1.F + halfWidth * halfWidth);
    }
  }

  bool visible(Sphere sphere) const {
    const Vec3 relative{sphere.center.x - position.x,
                        sphere.center.y - position.y,
                        sphere.center.z - position.z};
    const float depth = dot(relative, forward);
    if (depth + sphere.radius < nearClip || depth - sphere.radius > farClip) {
      return false;
    }
    const float depthScale = perspective ? std::max(depth, 0.F) : 1.F;
    return std::abs(dot(relative, right)) <=
               halfWidth * depthScale + sphere.radius * horizontalRadiusScale &&
           std::abs(dot(relative, up)) <=
               halfHeight * depthScale + sphere.radius * verticalRadiusScale;
  }
};

bool sphereVisible(Sphere sphere, const BgfxCameraFrame3D &frame) {
  return ViewFrustum(frame).visible(sphere);
}

std::optional<MeshBounds3D>
localMeshBounds(const Entity &entity, const ModelBoundsResolver3D &models) {
  const auto &mesh = *entity.component<MeshRendererComponent>();
  if (mesh.hasBounds) {
    return MeshBounds3D{mesh.boundsMin, mesh.boundsMax};
  }
  if (!mesh.model.empty()) {
    // Bind-pose bounds do not conservatively describe animated characters.
    if (!models || entity.hasComponent<AnimationPlayer3DComponent>()) {
      return std::nullopt;
    }
    auto bounds = models(mesh.model);
    if (!bounds) {
      return std::nullopt;
    }
    // Any authored LOD may be selected later; use their union to avoid clipping
    // larger reduced meshes at a switching threshold.
    for (const auto &model : {mesh.mediumLodModel, mesh.lowLodModel}) {
      if (model.empty()) {
        continue;
      }
      const auto level = models(model);
      if (!level) {
        return std::nullopt;
      }
      bounds->minimum = {std::min(bounds->minimum.x, level->minimum.x),
                         std::min(bounds->minimum.y, level->minimum.y),
                         std::min(bounds->minimum.z, level->minimum.z)};
      bounds->maximum = {std::max(bounds->maximum.x, level->maximum.x),
                         std::max(bounds->maximum.y, level->maximum.y),
                         std::max(bounds->maximum.z, level->maximum.z)};
    }
    return bounds;
  }
  if (mesh.shape == "cube" || mesh.shape == "sphere" ||
      mesh.shape == "cylinder" || mesh.shape == "plane") {
    // Canonical primitives fit the unit cube. A conservative cube also keeps
    // this path independent of sphere/cylinder tessellation details.
    return MeshBounds3D{{-.5F, -.5F, -.5F}, {.5F, .5F, .5F}};
  }
  return std::nullopt;
}

float windBoundsExpansion(const Entity &entity,
                          const MaterialLibrary *materials) {
  if (!materials) {
    return 0;
  }
  const auto strength = [&](std::string_view id) {
    const auto *material = materials->find(id);
    return material && !material->program ? material->surface.windStrength
                                          : 0.F;
  };
  const auto *mesh = entity.component<MeshRendererComponent>();
  float expansion = strength(mesh->material);
  if (const auto *sections = entity.component<MeshMaterialSections3D>()) {
    for (const auto &section : sections->sections) {
      expansion = std::max(expansion, strength(section.material));
    }
  }
  return expansion;
}

void appendVisibleInstances(
    const World &world, const VisibleMesh3D &source,
    const MeshInstances3DComponent &instances, SceneVisibility3D &result,
    const MaterialLibrary *materials, const ModelBoundsResolver3D &models,
    const ViewFrustum &frustum, Vec3 lodPosition,
    MeshInstancePreparation3D *presentationPreparation) {
  ProfileScope instanceScope("Renderer3D.extract_instance_copies");
  const auto &mesh = *source.entity->component<MeshRendererComponent>();
  MeshInstancePreparation3D uncached;
  auto &preparation =
      presentationPreparation ? *presentationPreparation : uncached;
  auto &prepared =
      preparation.source(world, *source.entity, source.transform, instances);
  if (!prepared.geometryPrepared) {
    const auto oldSize = prepared.meshSize;
    const bool sizeChanged = !oldSize || oldSize->x != mesh.size.x ||
                             oldSize->y != mesh.size.y || oldSize->z != mesh.size.z;
    if (sizeChanged) {
      for (auto &copy : prepared.copies) {
        copy.modelMatrix.reset();
      }
    }
    prepared.meshSize = mesh.size;
    prepared.localBounds = localMeshBounds(*source.entity, models);
    prepared.windExpansion = windBoundsExpansion(*source.entity, materials);
    if (prepared.localBounds) {
      const auto minimum = prepared.localBounds->minimum;
      const auto maximum = prepared.localBounds->maximum;
      const auto expansion =
          meshDentBoundsExpansion3D(entityMeshDents3D(*source.entity));
      prepared.localCenter = {(minimum.x + maximum.x) * .5F,
                              (minimum.y + maximum.y) * .5F,
                              (minimum.z + maximum.z) * .5F};
      prepared.localExtent = {(maximum.x - minimum.x) * .5F + expansion.x,
                              (maximum.y - minimum.y) * .5F + expansion.y,
                              (maximum.z - minimum.z) * .5F + expansion.z};
    }
    prepared.geometryPrepared = true;
  }
  const float cullDistanceSquared = mesh.cullDistance * mesh.cullDistance;
  const bool unrotatedOwner = source.transform.rotation.x == 0 &&
                              source.transform.rotation.y == 0 &&
                              source.transform.rotation.z == 0;
  for (auto &copy : prepared.copies) {
    ++result.considered;
    const Vec3 distance{copy.position.x - lodPosition.x,
                        copy.position.y - lodPosition.y,
                        copy.position.z - lodPosition.z};
    if (mesh.cullDistance > 0 &&
        dot(distance, distance) >= cullDistanceSquared) {
      ++result.culled;
      continue;
    }
    if (!copy.world) {
      if (unrotatedOwner) {
        copy.world = copy.local;
        copy.world->position = copy.position;
        copy.world->scale = {source.transform.scale.x * copy.local.scale.x,
                             source.transform.scale.y * copy.local.scale.y,
                             source.transform.scale.z * copy.local.scale.z};
      } else {
        copy.world = composeWorldTransform3D(source.transform, copy.local);
      }
    }
    if (prepared.localBounds) {
      if (!copy.bounds) {
        auto transform = *copy.world;
        transform.scale = {transform.scale.x * mesh.size.x,
                           transform.scale.y * mesh.size.y,
                           transform.scale.z * mesh.size.z};
        const Vec3 extent{prepared.localExtent.x * transform.scale.x,
                          prepared.localExtent.y * transform.scale.y,
                          prepared.localExtent.z * transform.scale.z};
        copy.bounds = MeshInstancePreparation3D::Sphere{
            transformPoint3D(transform, prepared.localCenter),
            std::sqrt(dot(extent, extent)) + prepared.windExpansion};
      }
      if (!frustum.visible({copy.bounds->center, copy.bounds->radius})) {
        ++result.culled;
        continue;
      }
    }
    const std::array<float, 16> *modelMatrix = nullptr;
    if (presentationPreparation) {
      if (!copy.modelMatrix) {
        copy.modelMatrix = composeMeshTransform3D(*copy.world, mesh.size);
      }
      modelMatrix = &*copy.modelMatrix;
    }
    result.meshes.push_back({.entity = source.entity,
                             .transform = *copy.world,
                             .preparedModelMatrix = modelMatrix,
                             .preparedSource = source.preparedSource});
  }
}

} // namespace

SceneVisibility3D extractVisibleMeshes3D(
    const World &world, const BgfxCameraFrame3D &frame, JobSystem *jobs,
    TerrainDrawSelection3D *terrainSelection,
    const BgfxCameraFrame3D *lodCamera, TerrainLodResidency *terrainResidency,
    TerrainCameraDemand3D *terrainDemand,
    const TerrainPatchBatch3D *terrainPatches, const MaterialLibrary *materials,
    const ModelBoundsResolver3D &modelBounds,
    MeshInstancePreparation3D *instancePreparation) {
  const ViewFrustum frustum(frame);
  const Vec3 instanceLodPosition = frame.lodPosition.value_or(
      lodCamera ? lodCamera->position : frame.position);
  MeshInstancePreparation3D uncachedPreparation;
  auto &preparation =
      instancePreparation ? *instancePreparation : uncachedPreparation;
  auto &sources = preparation.worldSources(world);
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
    auto &source = sources[index];
    const auto *mesh = source.mesh;
    if (mesh == nullptr) {
      return;
    }
    const auto *terrainMarker = source.terrain;
    if (terrainPatches && terrainMarker &&
        terrainPatches->suppressesOwner(terrainMarker->owner)) {
      return;
    }
    if (!frame.camera.renderMask.empty() && !mesh->renderLayer.empty() &&
        frame.camera.renderMask != mesh->renderLayer) {
      return;
    }
    considered[index] = 1;
    if (!source.transform) {
      return;
    }
    const auto &transform = *source.transform;
    const auto *terrain = source.terrain;
    const auto *chunk =
        terrain && terrain->chunk ? terrain->chunk.get() : nullptr;
    const bool instanced = source.instances != nullptr;
    if (!source.geometryPrepared) {
      const auto localBounds =
          instanced ? std::nullopt : localMeshBounds(entity, modelBounds);
      const auto bounds = worldBounds(*mesh, transform, entityMeshDents3D(entity),
                                     chunk, std::nullopt, localBounds);
      source.bounds = {bounds.center,
                       bounds.radius + windBoundsExpansion(entity, materials)};
      source.bounded = localBounds.has_value() || chunk != nullptr;
      source.geometryPrepared = true;
    }
    if (!instanced && source.bounded &&
        !frustum.visible({source.bounds.center, source.bounds.radius})) {
      culled[index] = 1;
      return;
    }
    if (instancePreparation && !instanced && !terrain && !source.modelMatrix) {
      source.modelMatrix = composeMeshTransform3D(transform, mesh->size);
    }
    extracted[index] = VisibleMesh3D{
        .entity = &entity,
        .transform = transform,
        .preparedModelMatrix = instancePreparation && source.modelMatrix
                                   ? &*source.modelMatrix
                                   : nullptr,
        .preparedSource = instancePreparation ? &source : nullptr};
  };
  {
    ProfileScope cullScope("Renderer3D.cull_sources");
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
  }

  SceneVisibility3D result;
  result.meshes.reserve(world.entities.size());
  {
    ProfileScope gatherScope("Renderer3D.gather_instances");
    for (std::size_t index = 0; index < extracted.size(); ++index) {
      if (extracted[index]) {
        const auto &source = *extracted[index];
        const auto sourceIndex = source.entity - world.entities.data();
        const auto *instances = sources[sourceIndex].instances;
        if (instances) {
          appendVisibleInstances(world, source, *instances, result, materials,
                                 modelBounds, frustum, instanceLodPosition,
                                 instancePreparation);
          continue;
        }
      }
      result.considered += considered[index] ? 1U : 0U;
      result.culled += culled[index] ? 1U : 0U;
      if (extracted[index]) {
        result.meshes.push_back(std::move(*extracted[index]));
      }
    }
  }
  ProfileScope sectionScope("Renderer3D.select_sections");
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
    const auto sourceIndex = visible.entity - world.entities.data();
    const auto &source = sources[sourceIndex];
    const auto *terrain = source.terrain;
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
      const auto *appearance = source.mesh;
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
    const auto *sections =
        visible.terrainSections ? visible.terrainSections : source.sections;
    const auto &mesh =
        visible.terrainGeometry ? *visible.terrainGeometry : *source.mesh;
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

bool meshBoundsVisible3D(const MeshRendererComponent &mesh,
                         const WorldTransform3D &transform,
                         const BgfxCameraFrame3D &frame) {
  return !mesh.hasBounds ||
         sphereVisible(worldBounds(mesh, transform, {}), frame);
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
