#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/PreparedInstanceView3D.h"
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

void recordInstanceCullingStatistics(const SceneVisibility3D &result,
                                     std::uint16_t viewId) {
  if (RuntimeProfiler::enabled()) {
    const auto prefix =
        "Renderer3D.view." + std::to_string(viewId) + ".instances.";
    const auto sectionsPrefix =
        "Renderer3D.view." + std::to_string(viewId) + ".sections.";
    RuntimeProfiler::setGauge(prefix + "hull_sources_rejected",
                              result.shadowHullSourcesRejected);
    RuntimeProfiler::setGauge(prefix + "hull_copies_rejected_by_index",
                              result.shadowHullCopiesRejectedByIndex);
    RuntimeProfiler::setGauge(prefix + "hull_copies_rejected",
                              result.shadowHullCopiesRejected);
    RuntimeProfiler::setGauge(sectionsPrefix + "sources_examined",
                              result.sectionSourcesExamined);
    RuntimeProfiler::setGauge(sectionsPrefix + "copies_examined",
                              result.sectionCopiesExamined);
    RuntimeProfiler::setGauge(sectionsPrefix + "draws_emitted",
                              result.sectionDrawsEmitted);
    RuntimeProfiler::setGauge(prefix + "sources_reused",
                              result.instanceSourcesReused);
    RuntimeProfiler::setGauge(prefix + "copies_reused",
                              result.instanceCopiesReused);
    RuntimeProfiler::setGauge(prefix + "nodes_tested",
                              result.instanceNodesTested);
    RuntimeProfiler::setGauge(prefix + "copies_examined",
                              result.instanceCopiesExamined);
    RuntimeProfiler::setGauge(prefix + "copies_rejected_by_index",
                              result.instanceCopiesRejectedByIndex);
  }
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
  const ShadowCasterHull3D &hull;

  explicit ViewFrustum(const BgfxCameraFrame3D &frame)
      : position(frame.position),
        forward(normalized(frame.forward, {0.F, 0.F, 1.F})),
        right(normalized(cross(frame.up, forward), {1.F, 0.F, 0.F})),
        up(normalized(cross(forward, right), {0.F, 1.F, 0.F})),
        nearClip(frame.camera.nearClip), farClip(frame.camera.farClip),
        perspective(frame.camera.perspective), hull(frame.shadowCasterHull) {
    const float aspect =
        float(std::max(frame.viewportWidth, std::uint16_t{1})) /
        float(std::max(frame.viewportHeight, std::uint16_t{1}));
    halfHeight = frame.camera.orthographicSize * .5F;
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
    return projectionVisible(sphere) &&
           hull.intersectsSphere(sphere.center, sphere.radius);
  }

  bool projectionVisible(Sphere sphere) const {
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
    const auto oldBounds = prepared.localBounds;
    const auto oldCenter = prepared.localCenter;
    const auto oldExtent = prepared.localExtent;
    const auto oldWindExpansion = prepared.windExpansion;
    const auto oldSize = prepared.meshSize;
    const bool sizeChanged = !oldSize || oldSize->x != mesh.size.x ||
                             oldSize->y != mesh.size.y ||
                             oldSize->z != mesh.size.z;
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
    const auto sameVector = [](Vec3 left, Vec3 right) {
      return left.x == right.x && left.y == right.y && left.z == right.z;
    };
    const bool boundsChanged =
        sizeChanged ||
        oldBounds.has_value() != prepared.localBounds.has_value() ||
        !sameVector(oldCenter, prepared.localCenter) ||
        !sameVector(oldExtent, prepared.localExtent) ||
        oldWindExpansion != prepared.windExpansion;
    if (boundsChanged) {
      for (auto &copy : prepared.copies) {
        copy.bounds.reset();
      }
    }
    prepared.geometryPrepared = true;
  }
  const float cullDistanceSquared = mesh.cullDistance * mesh.cullDistance;
  const bool unrotatedOwner = source.transform.rotation.x == 0 &&
                              source.transform.rotation.y == 0 &&
                              source.transform.rotation.z == 0;
  const Vec3 scaledCenter{prepared.localCenter.x * mesh.size.x,
                          prepared.localCenter.y * mesh.size.y,
                          prepared.localCenter.z * mesh.size.z};
  const Vec3 scaledExtent{prepared.localExtent.x * mesh.size.x,
                          prepared.localExtent.y * mesh.size.y,
                          prepared.localExtent.z * mesh.size.z};
  const float geometryExpansion = std::sqrt(dot(scaledCenter, scaledCenter)) +
                                  std::sqrt(dot(scaledExtent, scaledExtent));
  result.instanceNodesTested += prepared.spatialIndex.query(
      [&](const InstanceSpatialIndex3D::Node &node) {
        const Vec3 distance{std::max({node.minimum.x - lodPosition.x, 0.F,
                                      lodPosition.x - node.maximum.x}),
                            std::max({node.minimum.y - lodPosition.y, 0.F,
                                      lodPosition.y - node.maximum.y}),
                            std::max({node.minimum.z - lodPosition.z, 0.F,
                                      lodPosition.z - node.maximum.z})};
        if (mesh.cullDistance > 0 &&
            dot(distance, distance) >= cullDistanceSquared) {
          return false;
        }
        if (!prepared.localBounds) {
          return true;
        }
        const Vec3 center{node.minimum.x * .5F + node.maximum.x * .5F,
                          node.minimum.y * .5F + node.maximum.y * .5F,
                          node.minimum.z * .5F + node.maximum.z * .5F};
        const Vec3 extent{(node.maximum.x - node.minimum.x) * .5F,
                          (node.maximum.y - node.minimum.y) * .5F,
                          (node.maximum.z - node.minimum.z) * .5F};
        const float radius = std::sqrt(dot(extent, extent)) +
                             node.maximumScale * geometryExpansion +
                             prepared.windExpansion;
        if (!frustum.projectionVisible({center, radius})) {
          return false;
        }
        const float geometryPadding =
            node.maximumScale * geometryExpansion + prepared.windExpansion;
        if (!frustum.hull.intersectsAabb(node.minimum, node.maximum,
                                         geometryPadding)) {
          result.shadowHullCopiesRejectedByIndex += node.count;
          return false;
        }
        return true;
      },
      prepared.visibleCandidates, prepared.visibilityWords);
  const auto rejected =
      prepared.copies.size() - prepared.visibleCandidates.size();
  result.considered += prepared.copies.size();
  result.culled += rejected;
  result.instanceCopiesRejectedByIndex += rejected;
  result.instanceCopiesExamined += prepared.visibleCandidates.size();
  for (const auto index : prepared.visibleCandidates) {
    auto &copy = prepared.copies[index];
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
      if (!frustum.projectionVisible(
              {copy.bounds->center, copy.bounds->radius})) {
        ++result.culled;
        continue;
      }
      if (!frustum.hull.intersectsSphere(copy.bounds->center,
                                         copy.bounds->radius)) {
        ++result.culled;
        ++result.shadowHullCopiesRejected;
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

void PreparedInstanceView3D::reset() {
  opaquePlan_.reset();
  key_.reset();
  indices_.clear();
  usedSources_ = 0;
  for (auto &source : sources_) {
    source.ready = false;
    auto retained = std::move(source.visibility.meshes);
    retained.clear();
    source.visibility = SceneVisibility3D{.meshes = std::move(retained)};
  }
}

void PreparedInstanceView3D::beginView(const World &world,
                                       const BgfxCameraFrame3D &frame,
                                       Vec3 lodPosition) {
  const Key key{
      .world = &world,
      .cameraId = frame.cameraId,
      .renderMask = frame.camera.renderMask,
      .projectionAndPose = {frame.position.x, frame.position.y,
                            frame.position.z, frame.forward.x, frame.forward.y,
                            frame.forward.z, frame.up.x, frame.up.y, frame.up.z,
                            lodPosition.x, lodPosition.y, lodPosition.z,
                            frame.camera.nearClip, frame.camera.farClip,
                            frame.camera.fov, frame.camera.orthographicSize},
      .viewport = {frame.viewportX, frame.viewportY, frame.viewportWidth,
                   frame.viewportHeight},
      .perspective = frame.camera.perspective,
      .shadowCasterHull = frame.shadowCasterHull,
      .shadowPass = frame.pass == BgfxCameraPass3D::Shadow};
  if (!key_ || *key_ != key) {
    reset();
    key_ = key;
  }
}

PreparedInstanceView3D::Source &
PreparedInstanceView3D::source(const Entity &entity) {
  const auto [found, inserted] = indices_.try_emplace(&entity, usedSources_);
  if (inserted) {
    if (usedSources_ == sources_.size()) {
      sources_.emplace_back();
    }
    ++usedSources_;
  }
  return sources_[found->second];
}

std::size_t PreparedInstanceView3D::capacityHint() const {
  std::size_t capacity = 0;
  for (const auto &source : sources_) {
    capacity += source.visibility.meshes.capacity();
  }
  return capacity;
}
SceneVisibility3D extractVisibleMeshes3D(
    const World &world, const BgfxCameraFrame3D &frame, JobSystem *jobs,
    TerrainDrawSelection3D *terrainSelection,
    const BgfxCameraFrame3D *lodCamera, TerrainLodResidency *terrainResidency,
    TerrainCameraDemand3D *terrainDemand,
    const TerrainPatchBatch3D *terrainPatches, const MaterialLibrary *materials,
    const ModelBoundsResolver3D &modelBounds,
    MeshInstancePreparation3D *instancePreparation,
    PreparedInstanceView3D *preparedInstances,
    const ModelSectionsResolver3D &modelSections) {
  const ViewFrustum frustum(frame);
  const Vec3 instanceLodPosition = frame.lodPosition.value_or(
      lodCamera ? lodCamera->position : frame.position);
  // Reused records may borrow renderer-owned source and matrix addresses.
  // Standalone extraction without that owner must remain independent.
  if (instancePreparation == nullptr) {
    preparedInstances = nullptr;
  }
  if (preparedInstances != nullptr) {
    preparedInstances->beginView(world, frame, instanceLodPosition);
  }
  MeshInstancePreparation3D uncachedPreparation;
  auto &preparation =
      instancePreparation ? *instancePreparation : uncachedPreparation;
  auto &sources = preparation.worldSources(world, jobs);
  enum class Visibility : std::uint8_t {
    Ignored,
    MissingTransform,
    Culled,
    HullCulled,
    Visible,
  };
  // Workers write independent bytes. Full draw records are constructed only
  // for accepted sources, rather than initialized for every entity per view.
  std::vector<Visibility> visibility(world.entities.size(),
                                     Visibility::Ignored);
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
    if (frame.pass == BgfxCameraPass3D::Shadow && !mesh->castsShadows) {
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
    visibility[index] = Visibility::MissingTransform;
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
      const auto bounds =
          worldBounds(*mesh, transform, entityMeshDents3D(entity), chunk,
                      std::nullopt, localBounds);
      source.bounds = {bounds.center,
                       bounds.radius + windBoundsExpansion(entity, materials)};
      source.bounded = localBounds.has_value() || chunk != nullptr;
      source.geometryPrepared = true;
    }
    if (!instanced && source.bounded) {
      if (!frustum.projectionVisible(
              {source.bounds.center, source.bounds.radius})) {
        visibility[index] = Visibility::Culled;
        return;
      }
      if (!frustum.hull.intersectsSphere(source.bounds.center,
                                         source.bounds.radius)) {
        visibility[index] = Visibility::HullCulled;
        return;
      }
    }
    if (instancePreparation && !instanced && !terrain && !source.modelMatrix) {
      source.modelMatrix = composeMeshTransform3D(transform, mesh->size);
    }
    visibility[index] = Visibility::Visible;
  };
  {
    ProfileScope cullScope("Renderer3D.cull_sources");
    // Transform hierarchy lookup is cheap for ordinary scenes. Dispatch only
    // when extraction is large enough to amortize wake-up and synchronization.
    constexpr std::size_t ParallelExtractionThreshold = 1024;
    if (jobs != nullptr &&
        world.entities.size() >= ParallelExtractionThreshold) {
      jobs->parallelFor(world.entities.size(), 64, extract);
    } else {
      for (std::size_t index = 0; index < world.entities.size(); ++index) {
        extract(index);
      }
    }
  }

  SceneVisibility3D result;
  const auto instanceCapacity = preparedInstances != nullptr
                                    ? preparedInstances->capacityHint()
                                    : std::size_t{0};
  result.meshes.reserve(world.entities.size() + instanceCapacity);
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
  const auto appendSelectedDraw =
      [&](VisibleMesh3D visible,
          const MeshInstancePreparation3D::EntitySource &source) {
        const auto *terrain = source.terrain;
        if (terrain && terrain->chunk) {
          auto [quality, inserted] =
              ownerQuality.try_emplace(terrain->owner, 0.0F);
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
                    ? terrainSelection->select(
                          terrain->chunk, visible.transform, appearance->size,
                          selectionCamera, quality->second, terrain->owner)
                    : selectTerrainDrawLevel3D(
                          *terrain->chunk, visible.transform, appearance->size,
                          selectionCamera, quality->second);
          }
          if (terrainPatches &&
              terrainPatches->forcesFullOwner(terrain->owner)) {
            decision->second.level = 0;
          }
          if (firstSurface && decision->second.level != 0) {
            if (terrainDemand) {
              terrainDemand->record(selectionCamera, visible.entity->id,
                                    terrain->owner, terrain->chunk,
                                    decision->second.level);
            }
            decision->second.prepared =
                terrainResidency ? terrainResidency->request(
                                       terrain->chunk, decision->second.level)
                                 : nullptr;
            if (!decision->second.prepared) {
              decision->second.level = 0;
            }
          }
          visible.terrainLevel = decision->second.level;
          visible.terrainPreparedLevel = decision->second.prepared;
          if (visible.terrainLevel != 0) {
            const auto surface =
                visible.terrainPreparedLevel->find(terrain->biome);
            if (surface == visible.terrainPreparedLevel->end()) {
              return;
            }
            visible.terrainGeometry = surface->second.geometry;
            visible.terrainSections = &surface->second.sections;
          }
        }
        const auto &mesh =
            visible.terrainGeometry ? *visible.terrainGeometry : *source.mesh;
        if (modelSections && !mesh.model.empty() && mesh.material.empty()) {
          visible.modelSections = modelSections(visible, instanceLodPosition);
        }
        const auto *sections = visible.modelSections ? visible.modelSections
                               : visible.terrainSections
                                   ? visible.terrainSections
                                   : source.sections;
        if (visible.terrainSurface && mesh.hasBounds) {
          const auto *appearance =
              visible.entity->component<MeshRendererComponent>();
          const auto bounds = worldBounds(mesh, visible.transform,
                                          entityMeshDents3D(*visible.entity),
                                          nullptr, appearance->size);
          if (!sphereVisible(bounds, frame)) {
            ++result.culled;
            return;
          }
        }
        if ((!visible.modelSections && mesh.vertices.empty()) || !sections ||
            sections->sections.empty()) {
          result.meshes.push_back(std::move(visible));
          return;
        }
        for (std::size_t index = 0; index < sections->sections.size();
             ++index) {
          auto draw = visible;
          draw.materialSection = index;
          result.meshes.push_back(std::move(draw));
        }
      };

  const auto appendSourceDraws =
      [&](std::span<const VisibleMesh3D> draws,
          const MeshInstancePreparation3D::EntitySource &source,
          bool needsSelection) {
        if (!needsSelection) {
          result.meshes.insert(result.meshes.end(), draws.begin(), draws.end());
          return;
        }
        ProfileScope sectionScope("Renderer3D.select_sections");
        ++result.sectionSourcesExamined;
        result.sectionCopiesExamined += draws.size();
        const auto firstDraw = result.meshes.size();
        for (const auto &draw : draws) {
          appendSelectedDraw(draw, source);
        }
        result.sectionDrawsEmitted += result.meshes.size() - firstDraw;
      };
  const auto appendInstanceCounters = [&](const SceneVisibility3D &prepared) {
    result.considered += prepared.considered;
    result.culled += prepared.culled;
    result.instanceNodesTested += prepared.instanceNodesTested;
    result.shadowHullCopiesRejectedByIndex +=
        prepared.shadowHullCopiesRejectedByIndex;
    result.shadowHullCopiesRejected += prepared.shadowHullCopiesRejected;
    result.instanceCopiesExamined += prepared.instanceCopiesExamined;
    result.instanceCopiesRejectedByIndex +=
        prepared.instanceCopiesRejectedByIndex;
  };

  {
    ProfileScope gatherScope("Renderer3D.gather_instances");
    for (std::size_t index = 0; index < visibility.size(); ++index) {
      const auto decision = visibility[index];
      if (decision == Visibility::Ignored) {
        continue;
      }
      if (decision != Visibility::Visible) {
        ++result.considered;
        result.culled +=
            decision == Visibility::Culled || decision == Visibility::HullCulled
                ? 1U
                : 0U;
        result.shadowHullSourcesRejected +=
            decision == Visibility::HullCulled ? 1U : 0U;
        continue;
      }
      const auto &metadata = sources[index];
      VisibleMesh3D draw{
          .entity = metadata.entity,
          .transform = *metadata.transform,
          .preparedModelMatrix = instancePreparation && metadata.modelMatrix
                                     ? &*metadata.modelMatrix
                                     : nullptr,
          .preparedSource = instancePreparation ? &metadata : nullptr};
      const bool hasInlineSections = metadata.sections &&
                                     !metadata.sections->sections.empty() &&
                                     !metadata.mesh->vertices.empty();
      const bool needsSectionSelection =
          metadata.terrain || hasInlineSections ||
          (modelSections && !metadata.mesh->model.empty() &&
           metadata.mesh->material.empty());
      if (metadata.instances) {
        auto *shared =
            preparedInstances != nullptr && metadata.terrain == nullptr
                ? &preparedInstances->source(*metadata.entity)
                : nullptr;
        if (shared != nullptr) {
          const bool reused = shared->ready;
          if (!shared->ready) {
            appendVisibleInstances(
                world, draw, *metadata.instances, shared->visibility, materials,
                modelBounds, frustum, instanceLodPosition, instancePreparation);
            shared->ready = true;
          }
          const auto &prepared = shared->visibility;
          appendSourceDraws(prepared.meshes, metadata, needsSectionSelection);
          appendInstanceCounters(prepared);
          if (reused) {
            ++result.instanceSourcesReused;
            result.instanceCopiesReused += prepared.meshes.size();
          }
        } else if (needsSectionSelection) {
          SceneVisibility3D sourceInstances;
          appendVisibleInstances(
              world, draw, *metadata.instances, sourceInstances, materials,
              modelBounds, frustum, instanceLodPosition, instancePreparation);
          appendSourceDraws(sourceInstances.meshes, metadata, true);
          appendInstanceCounters(sourceInstances);
        } else {
          appendVisibleInstances(world, draw, *metadata.instances, result,
                                 materials, modelBounds, frustum,
                                 instanceLodPosition, instancePreparation);
        }
        continue;
      }
      ++result.considered;
      if (needsSectionSelection) {
        appendSourceDraws(std::span(&draw, 1), metadata, true);
      } else {
        result.meshes.push_back(std::move(draw));
      }
    }
  }

  recordInstanceCullingStatistics(result, frame.viewId);
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
