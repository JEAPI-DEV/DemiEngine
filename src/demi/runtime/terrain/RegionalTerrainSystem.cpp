#include "demi/runtime/terrain/RegionalTerrainSystem.h"
#include "demi/runtime/concurrency/AsyncResidency.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldCommandBuffer.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/EntityLookup.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/RegionalTerrainCover.h"
#include "demi/runtime/terrain/RegionalTerrainMorph.h"
#include "demi/runtime/terrain/RegionalTerrainPresentation.h"
#include "demi/runtime/terrain/RegionalTerrainVegetation.h"
#include "demi/runtime/terrain/TerrainMeshBuilder.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace demi::runtime {
namespace {
struct RegionSurface {
  std::string owner;
  std::uint64_t session = 0;
};
struct PreparedPatch {
  std::shared_ptr<World> world;
  std::vector<std::string> entities;
  std::pair<float, float> heights;
};
using PatchCache = AsyncResidency<TerrainRegionPatchVariant, PreparedPatch>;
using InitialCoverCache =
    AsyncResidency<int, std::vector<TerrainRegionPatchKey>>;
std::string patchId(std::string_view owner, TerrainRegionPatchVariant key) {
  return std::string(owner) + "/__region/" + std::to_string(key.patch.level) +
         "/" + std::to_string(key.patch.x) + "/" + std::to_string(key.patch.z) +
         "/" + std::to_string(key.stitch);
}
std::size_t patchReservation(const TerrainRegionConfig &config) {
  // Mesh + collider arrays, immutable shape preparation and field/scratch.
  const auto cells = std::size_t(config.recipe.chunkCells);
  if (cells == 0 ||
      cells > (std::numeric_limits<std::size_t>::max() - 1024 * 1024) / 2048 /
                  cells) {
    throw std::invalid_argument("Regional patch preparation size overflow");
  }
  return cells * cells * 2048 + 1024 * 1024;
}
PatchCache::Loaded preparePatch(const RegionalTerrainSampler &sampler,
                                TerrainRegionPatchVariant key,
                                const Entity &root, std::uint64_t session,
                                std::stop_token stop, bool physics) {
  const auto start = std::chrono::steady_clock::now();
  auto tile = sampler.samplePatch(key.patch, key.stitch, stop);
  auto world = std::make_shared<World>();
  world->entities.push_back(root);
  const auto patchOwnerId = patchId(root.id, key);
  Entity patchOwner;
  patchOwner.id = patchOwnerId;
  patchOwner.sceneOwner = root.sceneOwner;
  patchOwner.layer = root.layer;
  patchOwner.setComponent(Transform3DComponent{
      .parent = root.id,
      .position = {float(tile.originX), 0, float(tile.originZ)}});
  Terrain3DComponent terrain;
  terrain.generated = std::make_shared<const HeightField>(tile.field);
  // The regional quadtree already selects resolution and stitches its edges.
  // Keep each selected patch at Full locally; a second independent LOD choice
  // would undo those edge constraints.
  terrain.lodErrorPixels = 0;
  patchOwner.setComponent(std::move(terrain));
  patchOwner.setComponent(terrain_detail::TerrainGeneratedRegionPatch{root.id});
  patchOwner.setComponent(RegionSurface{root.id, session});
  const auto renderLod = prepareTerrainRenderLod(
      patchOwner.component<Terrain3DComponent>()->generated,
      tile.field.chunks.front());
  world->entities.push_back(patchOwner);
  const auto triangles = terrain_detail::buildChunkTriangles(
      tile.field, tile.field.chunks.front(), stop);
  std::vector<std::string> ids{patchOwnerId};
  for (const auto &[biome, surface] : triangles) {
    if (stop.stop_requested()) {
      throw std::runtime_error("Regional patch preparation cancelled");
    }
    std::string error;
    const auto id = patchId(root.id, key) + "/" + std::to_string(biome);
    auto uvs = surface.uvs;
    auto entity = buildTerrainMeshEntity(
        patchOwner, id, surface.vertices, surface.normals, uvs,
        tile.field.biomeColors.at(biome), tile.field.biomeMaterial(biome),
        error);
    if (!entity) {
      throw std::runtime_error(error);
    }
    entity->sceneOwner = root.sceneOwner;
    entity->updateComponent<MeshRendererComponent>([&](auto &mesh) {
      mesh.materialNumbers["projection_origin_x"] =
          tile.field.projectionOrigin.x;
      mesh.materialNumbers["projection_origin_z"] =
          tile.field.projectionOrigin.y;
    });
    entity->setComponent(terrain_detail::TerrainGeneratedSurface{
        patchOwnerId, 0, 0, tile.field.biomeIds.at(biome)});
    entity->setComponent(
        TerrainRenderLod{renderLod, tile.field.biomeIds.at(biome), patchOwnerId,
                         entity->component<MeshRendererComponent>()->revision});
    entity->setComponent(RegionSurface{root.id, session});
    ids.push_back(entity->id);
    world->entities.push_back(std::move(*entity));
  }
  if (stop.stop_requested()) {
    throw std::runtime_error("Regional patch preparation cancelled");
  }
  std::string error;
  if (physics && !preparePhysicsShapes3D(*world, error)) {
    throw std::runtime_error(error);
  }
  const auto count = std::size_t(tile.field.cellsX) * tile.field.cellsZ;
  const auto bytes =
      count * 6 * (sizeof(Vec3) * 5 + sizeof(Vec2) * 2) + ids.size() * 8192;
  const auto [low, high] =
      std::minmax_element(tile.field.heights.begin(), tile.field.heights.end());
  return {{std::move(world), std::move(ids), {*low, *high}},
          bytes,
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - start)
              .count(),
          0};
}
void removeSurfaces(World &world, const std::vector<std::string> &ids) {
  if (world.physicsWorld3D) {
    world.physicsWorld3D->removeEntities(ids);
  }
  WorldCommandBuffer commands;
  for (const auto &id : ids) {
    if (findEntity(world, id)) {
      (void)commands.destroy(world, id);
    }
  }
  (void)commands.flush(world);
}
} // namespace

struct RegionalTerrainSystem::Impl {
  struct Region {
    std::shared_ptr<const TerrainRegionConfig> config;
    std::shared_ptr<const RegionalTerrainSampler> sampler;
    std::uint64_t session;
    PatchCache cache;
    InitialCoverCache initialCover;
    std::unique_ptr<RegionalTerrainVegetation> vegetation;
    std::vector<TerrainRegionPatchKey> cover;
    std::vector<TerrainRegionPatchVariant> active;
    std::optional<std::vector<TerrainRegionPatchKey>> proposed;
    std::optional<Vec3> plannedCamera;
    std::optional<Vec3> plannedForward;
    bool needsPlan = true;
    bool initialReady = false;
    std::optional<RegionalTerrainMorph> morph;
    bool morphBeforePublication = false;
    std::vector<RegionalTerrainMorphPatch> stagedMorph;
    EntityLookup renderLookup;
    std::shared_ptr<RegionalTerrainRenderPreparation> renderPreparation =
        std::make_shared<RegionalTerrainRenderPreparation>();
    std::optional<bool> surfaceBlending;
    std::vector<TerrainRegionPatchVariant> target;
    std::vector<TerrainRegionPatchVariant> requestedActive;
    std::vector<TerrainRegionPatchVariant> requestedTarget;
    std::size_t requestedPatchCount = 0;
    Region(JobSystem &jobs, const Entity &owner, WorldTransform3D pose,
           std::shared_ptr<const TerrainRegionConfig> source,
           std::uint64_t serial, Options options, Vec3 initialCamera)
        : config(std::move(source)),
          sampler(std::make_shared<const RegionalTerrainSampler>(*config)),
          session(serial),
          cache(jobs,
                {.maximumBytes = config->streaming.maximumResidentBytes,
                 .maximumPendingJobs = config->streaming.maximumPendingJobs,
                 .maximumStartsPerUpdate = config->streaming.maximumPendingJobs,
                 .maximumPublicationsPerUpdate =
                     config->streaming.maximumPendingJobs},
                [sampler = sampler, root = rootSnapshot(owner, pose), serial,
                 options](const auto &key, std::stop_token stop) {
                  return preparePatch(*sampler, key, root, serial, stop,
                                      options.physics);
                }),
          initialCover(
              jobs,
              {.maximumBytes = config->streaming.maximumResidentBytes,
               .maximumPendingJobs = 1,
               .maximumStartsPerUpdate = 1,
               .maximumPublicationsPerUpdate = 1},
              [sampler = sampler, initialCamera](const int &,
                                                 std::stop_token stop) {
                const auto &config = sampler->config();
                const auto cells = std::size_t(config.recipe.chunkCells);
                const auto patchBytes =
                    cells * cells * 6 * (sizeof(Vec3) * 5 + sizeof(Vec2) * 2) +
                    (config.recipe.biomes.size() + 1) * 8192;
                const auto headroom = patchReservation(config) * 8;
                const auto available =
                    config.streaming.maximumResidentBytes > headroom
                        ? config.streaming.maximumResidentBytes - headroom
                        : 0;
                const auto limit =
                    std::max<std::size_t>(1, available / patchBytes);
                auto cover = planInitialRegionCover(*sampler, initialCamera,
                                                    limit, stop);
                const auto bytes =
                    cover.capacity() * sizeof(TerrainRegionPatchKey);
                return InitialCoverCache::Loaded{std::move(cover), bytes};
              }) {
      if (patchReservation(*config) * 8 >
          config->streaming.maximumResidentBytes) {
        throw std::invalid_argument(
            "Regional streaming budget cannot admit a patch transition; reduce "
            "chunk_cells or raise resident_mib");
      }
      if (options.vegetation && sampler->hasScatter()) {
        vegetation = std::make_unique<RegionalTerrainVegetation>(jobs, sampler,
                                                                 owner.id);
      }
    }
    static Entity rootSnapshot(const Entity &owner, WorldTransform3D pose) {
      Entity root;
      root.id = owner.id;
      root.layer = owner.layer;
      root.sceneOwner = owner.sceneOwner;
      Transform3DComponent transform;
      transform.position = pose.position;
      transform.rotation = pose.rotation;
      transform.scale = pose.scale;
      root.setComponent(std::move(transform));
      return root;
    }
    void demand() {
      if (cover.empty() && !proposed) {
        const std::array<InitialCoverCache::Demand, 1> request{
            {{0, config->streaming.maximumResidentBytes}}};
        initialCover.update(request);
        if (const auto error = initialCover.error(0); !error.empty()) {
          throw std::runtime_error(error);
        }
        if (const auto *planned = initialCover.find(0)) {
          proposed = *planned;
          target = regionCoverVariants(*proposed);
          initialCover.clear();
        }
      }
      const auto statistics = cache.statistics();
      const bool unchanged = active == requestedActive && target == requestedTarget;
      const bool settled = statistics.residentCells == requestedPatchCount &&
                           statistics.pendingJobs == 0 &&
                           statistics.retiringJobs == 0;
      // Pending, retiring and budget-blocked demand still needs polling. A
      // complete unchanged cover has no residency work to repeat each tick.
      if (unchanged && settled) {
        return;
      }
      ProfileScope profile("Terrain.region.residency_update");
      std::set<TerrainRegionPatchVariant> requested(active.begin(),
                                                    active.end());
      requested.insert(target.begin(), target.end());
      std::vector<PatchCache::Demand> demand;
      for (const auto &key : requested) {
        demand.push_back({key, patchReservation(*config)});
      }
      cache.update(demand);
      requestedActive = active;
      requestedTarget = target;
      requestedPatchCount = demand.size();
    }
    bool targetReady(std::string &error) const {
      if (!proposed) {
        return false;
      }
      for (const auto &key : target) {
        error = cache.error(key);
        if (!error.empty()) {
          throw std::runtime_error(error);
        }
        if (!cache.find(key)) {
          return false;
        }
      }
      return true;
    }
    RegionalTerrainRenderPatch renderPatch(
        std::shared_ptr<const HeightField> field) const {
      auto [low, high] = std::minmax_element(field->heights.begin(),
                                            field->heights.end());
      float minimum = *low;
      float maximum = *high;
      if (field->morphFromHeights) {
        const auto [fromLow, fromHigh] = std::minmax_element(
            field->morphFromHeights->begin(), field->morphFromHeights->end());
        minimum = std::min(minimum, *fromLow);
        maximum = std::max(maximum, *fromHigh);
      }
      const float x = float(config->originX) + field->projectionOrigin.x;
      const float z = float(config->originZ) + field->projectionOrigin.y;
      return {field, {x, minimum, z},
              {x + field->size.x, maximum, z + field->size.y}};
    }
    void refreshRenderPreparation(
        World &world, const std::vector<TerrainRegionPatchVariant> &variants) {
      ProfileScope profile("Terrain.region.prepare_render_requests");
      renderPreparation->active.clear();
      renderPreparation->active.reserve(variants.size());
      for (const auto &key : variants) {
        const auto &patch = *cache.find(key);
        // A region can own thousands of patches. Resolve IDs through the shared
        // index rather than scanning the whole World for every active patch.
        const auto *entity =
            renderLookup.find(world.entities, patch.entities.front());
        const auto *terrain =
            entity ? entity->component<Terrain3DComponent>() : nullptr;
        if (!terrain || !terrain->generated) {
          throw std::runtime_error("Regional render patch is missing: " +
                                   patch.entities.front());
        }
        renderPreparation->active.push_back(renderPatch(terrain->generated));
      }
    }
    void transition(World &world, const Entity &owner, std::string &error,
                    float deltaSeconds) {
      if (morph) {
        if (morph->advance(world, deltaSeconds)) {
          if (morphBeforePublication) {
            (void)publish(world, owner, error);
          } else {
            refreshRenderPreparation(world, active);
          }
          morph.reset();
        }
        return;
      }
      if (!targetReady(error)) {
        return;
      }
      if (!renderPreparation->pending.empty()) {
        if (renderPreparation->waiting()) {
          return;
        }
        if (!initialReady || !morphBeforePublication) {
          (void)publish(world, owner, error);
        }
        if (!stagedMorph.empty()) {
          morph.emplace(world, std::move(stagedMorph));
          for (const auto &patch : renderPreparation->pending) {
            // Splits already retained the target cover in publish(). Only its
            // morph fields are additional demand. Merges must also hold the
            // eventual coarse replacement until the fine cover finishes.
            if (morphBeforePublication || patch.field->morphFromHeights) {
              renderPreparation->active.push_back(patch);
            }
          }
        }
        renderPreparation->pending.clear();
        return;
      }
      for (const auto &key : target) {
        const auto &patch = *cache.find(key);
        const auto *entity = findEntity(*patch.world, patch.entities.front());
        renderPreparation->pending.push_back(
            renderPatch(entity->component<Terrain3DComponent>()->generated));
      }
      renderPreparation->prepared = false;
      if (!initialReady) {
        return;
      }
      const auto fields = [&](const auto &variants) {
        std::map<TerrainRegionPatchKey, std::shared_ptr<const HeightField>>
            result;
        for (const auto &variant : variants) {
          const auto &patch = *cache.find(variant);
          const auto *entity = findEntity(*patch.world, patch.entities.front());
          result.emplace(
              variant.patch,
              entity->template component<Terrain3DComponent>()->generated);
        }
        return result;
      };
      const RegionalTerrainCoverSurface from(*config, fields(active));
      const RegionalTerrainCoverSurface to(*config, fields(target));
      morphBeforePublication = target.size() < active.size();
      const auto &geometry = morphBeforePublication ? active : target;
      const auto &other = morphBeforePublication ? target : active;
      std::vector<RegionalTerrainMorphPatch> patches;
      for (const auto &key : geometry) {
        if (std::ranges::find(other, key) != other.end()) {
          continue;
        }
        const auto &patch = *cache.find(key);
        const auto *entity = findEntity(*patch.world, patch.entities.front());
        const auto &grid = *entity->component<Terrain3DComponent>()->generated;
        patches.push_back(
            {patch.entities.front(),
             makeRegionalMorphField(*config, key.patch, grid, from, to),
             patch.entities});
      }
      for (const auto &patch : patches) {
        renderPreparation->pending.push_back(renderPatch(patch.field));
      }
      stagedMorph = std::move(patches);
    }
    bool publish(World &world, const Entity &owner, std::string &error) {
      if (!targetReady(error)) {
        return false;
      }
      ProfileScope profile("Terrain.region.publish");
      WorldCommandBuffer commands;
      std::vector<std::string> removed;
      for (const auto &key : active) {
        if (std::ranges::find(target, key) != target.end()) {
          continue;
        }
        if (const auto *patch = cache.find(key)) {
          for (const auto &id : patch->entities) {
            const auto *entity = findEntity(world, id);
            const auto *marker =
                entity ? entity->component<RegionSurface>() : nullptr;
            if (marker && marker->session == session) {
              (void)commands.destroy(world, id);
              removed.push_back(id);
            }
          }
        }
      }
      for (const auto &key : target) {
        if (std::ranges::find(active, key) != active.end()) {
          continue;
        }
        const auto &patch = *cache.find(key);
        for (const auto &source : patch.world->entities) {
          if (!source.hasComponent<RegionSurface>()) {
            continue;
          }
          Entity entity = source;
          if (entity.hasComponent<Terrain3DComponent>()) {
            const bool blending =
                owner.component<Terrain3DComponent>()->surfaceBlending;
            entity.updateComponent<Terrain3DComponent>(
                [&](auto &terrain) { terrain.surfaceBlending = blending; });
          }
          entity.layer = owner.layer;
          entity.sceneOwner = owner.sceneOwner;
          if (!commands.create(world, std::move(entity))) {
            throw std::runtime_error("Regional terrain entity ID conflict: " +
                                     source.id);
          }
        }
      }
      if (world.physicsWorld3D) {
        world.physicsWorld3D->removeEntities(removed);
      }
      (void)commands.flush(world);
      for (const auto &key : target) {
        if (std::ranges::find(active, key) != active.end()) {
          continue;
        }
        adoptPreparedPhysicsShapes3D(world, *cache.find(key)->world);
      }
      refreshRenderPreparation(world, target);
      active = target;
      cover = std::move(*proposed);
      // Bootstrap already planned a complete camera-demanded cover. Waiting
      // for demand to stop changing can hide a moving camera indefinitely and
      // bypass morphing for every refinement during that interval.
      initialReady = true;
      proposed.reset();
      target.clear();
      needsPlan = true;
      // Shape adoption consumes the prepared native handles. Retire replaced
      // payloads now so an immediate camera reversal prepares them on workers
      // instead of reusing a consumed snapshot and rebuilding on the main
      // thread.
      demand();
      rebuildTerrainOwnership(world);
      return true;
    }
  };
  explicit Impl(Options value) : options(value) {}
  Options options;
  JobSystem jobs{2};
  std::map<std::string, std::unique_ptr<Region>> regions;
  std::vector<std::unique_ptr<Region>> retired;
  std::uint64_t serial = 0;
  Vec3 lastCamera{};

  void retire(World &world, Region &region, RuntimePrefabService *prefabs,
              std::string &error) {
    if (region.vegetation && !region.vegetation->clear(world, prefabs, error)) {
      throw std::runtime_error(error);
    }
    std::vector<std::string> ids;
    for (const auto &entity : world.entities) {
      const auto *marker = entity.component<RegionSurface>();
      if (marker && marker->session == region.session) {
        ids.push_back(entity.id);
      }
    }
    removeSurfaces(world, ids);
    region.cache.clear();
    rebuildTerrainOwnership(world);
  }
  bool update(World &world, Vec3 camera, std::optional<Vec3> forward,
              std::string &error, RuntimePrefabService *prefabs,
              float deltaSeconds) {
    ProfileScope profile("Terrain.region.update");
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0) {
      throw std::invalid_argument(
          "Regional terrain delta must be finite and nonnegative");
    }
    lastCamera = camera;
    std::map<std::string, std::shared_ptr<const TerrainRegionConfig>> sources;
    for (const auto &entity : world.entities) {
      const auto *terrain = entity.component<Terrain3DComponent>();
      if (entity.enabled && terrain && terrain->region) {
        sources.emplace(entity.id, terrain->region);
      }
    }
    for (auto it = regions.begin(); it != regions.end();) {
      const auto source = sources.find(it->first);
      if (source == sources.end() || source->second != it->second->config) {
        retire(world, *it->second, prefabs, error);
        retired.push_back(std::move(it->second));
        it = regions.erase(it);
      } else {
        ++it;
      }
    }
    for (auto &region : retired) {
      region->cache.clear();
      region->initialCover.clear();
      if (region->vegetation) {
        region->vegetation->drain();
      }
    }
    std::erase_if(retired, [](const auto &region) {
      const auto stats = region->cache.statistics();
      const auto planning = region->initialCover.statistics();
      const auto plants = region->vegetation ? region->vegetation->statistics()
                                             : ResidencyStatistics{};
      return stats.pendingJobs == 0 && stats.retiringJobs == 0 &&
             planning.pendingJobs == 0 && planning.retiringJobs == 0 &&
             plants.pendingJobs == 0 && plants.retiringJobs == 0;
    });
    std::size_t activeCount = 0, pending = 0, bytes = 0;
    std::size_t vegetationCells = 0, vegetationBytes = 0, vegetationPending = 0;
    std::size_t budgetMisses = 0;
    std::size_t morphingRegions = 0;
    std::size_t renderWaitingRegions = 0;
    double finestSpacing = std::numeric_limits<double>::max();
    for (const auto &[id, source] : sources) {
      const auto *owner = findEntity(world, id);
      const auto pose = resolveWorldTransform3D(world, *owner);
      if (!pose) {
        throw std::runtime_error(
            "Regional terrain owner has an invalid hierarchy");
      }
      auto &region = regions[id];
      if (!region) {
        region = std::make_unique<Region>(
            jobs, *owner, *pose, source, ++serial, options,
            inverseTransformPoint3D(*pose, camera));
      }
      const bool blending =
          owner->component<Terrain3DComponent>()->surfaceBlending;
      if (region->surfaceBlending != blending) {
        for (auto &entity : world.entities) {
          const auto *marker = entity.component<RegionSurface>();
          if (marker && marker->session == region->session &&
              entity.hasComponent<Terrain3DComponent>()) {
            entity.updateComponent<Terrain3DComponent>(
                [&](auto &terrain) { terrain.surfaceBlending = blending; });
          }
        }
        region->surfaceBlending = blending;
      }
      const auto local = inverseTransformPoint3D(*pose, camera);
      std::optional<Vec3> localForward;
      if (forward) {
        auto direction = inverseTransformVector3D(*pose, *forward);
        const float extent =
            std::hypot(std::hypot(direction.x, direction.z), direction.y);
        if (std::isfinite(extent) && extent > 0) {
          localForward = Vec3{direction.x / extent, direction.y / extent,
                              direction.z / extent};
        }
      }
      const bool facingChanged =
          localForward.has_value() != region->plannedForward.has_value() ||
          (localForward && region->plannedForward &&
           std::hypot(std::hypot(localForward->x - region->plannedForward->x,
                                 localForward->z - region->plannedForward->z),
                      localForward->y - region->plannedForward->y) > .01F);
      const bool cameraMoved =
          !region->plannedCamera ||
          std::hypot(std::hypot(local.x - region->plannedCamera->x,
                                local.z - region->plannedCamera->z),
                     local.y - region->plannedCamera->y) >=
              source->baseSampleSpacing;
      if (!region->morph && region->renderPreparation->pending.empty() &&
          !region->cover.empty() &&
          (region->needsPlan || cameraMoved || facingChanged)) {
        region->plannedCamera = local;
        region->plannedForward = localForward;
        region->needsPlan = false;
        std::map<TerrainRegionPatchKey, std::pair<float, float>> heights;
        for (const auto &variant : region->active) {
          heights.emplace(variant.patch, region->cache.find(variant)->heights);
        }
        const TerrainRegionHeightBounds bounds =
            [&](TerrainRegionPatchKey key) {
              if (const auto entry = heights.find(key);
                  entry != heights.end()) {
                return entry->second;
              }
              // A merge candidate has four resident children. Use their actual
              // geometry bounds, not the height underneath the camera
              // elsewhere.
              std::pair<float, float> range{
                  std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::lowest()};
              for (std::uint32_t z = 0; z < 2; ++z) {
                for (std::uint32_t x = 0; x < 2; ++x) {
                  const auto child =
                      heights.at({key.level + 1, key.x * 2 + x, key.z * 2 + z});
                  range.first = std::min(range.first, child.first);
                  range.second = std::max(range.second, child.second);
                }
              }
              return range;
            };
        region->proposed = advanceRegionCover(
            *source, region->cover, local, bounds,
            source->streaming.lodDistanceRatio, localForward);
        if (region->proposed &&
            region->proposed->size() > region->cover.size()) {
          const auto variants = regionCoverVariants(*region->proposed);
          const auto missing =
              std::ranges::count_if(variants, [&](const auto &key) {
                return region->cache.find(key) == nullptr;
              });
          const auto resident = region->cache.statistics().residentBytes;
          const auto reserve = patchReservation(*source);
          if (std::size_t(missing) >
              (source->streaming.maximumResidentBytes - resident) / reserve) {
            // Make room through distant coarsening before admitting an entire
            // replacement. Never stall halfway through a split at the budget.
            region->proposed = advanceRegionCover(
                *source, region->cover, local, bounds,
                source->streaming.lodDistanceRatio, localForward, false);
          }
        }
        region->target = region->proposed
                             ? regionCoverVariants(*region->proposed)
                             : std::vector<TerrainRegionPatchVariant>{};
      }
      region->demand();
      region->transition(world, *owner, error, deltaSeconds);
      morphingRegions += region->morph.has_value() ? 1U : 0U;
      renderWaitingRegions += region->renderPreparation->waiting() ? 1U : 0U;
      const auto stats = region->cache.statistics();
      auto *liveOwner = findEntity(world, id);
      const auto *presentation =
          liveOwner->component<RegionalTerrainPresentation>();
      if (!presentation || presentation->ready != region->initialReady ||
          presentation->render != region->renderPreparation) {
        liveOwner->setComponent(RegionalTerrainPresentation{
            region->initialReady, region->renderPreparation});
      }
      if (region->vegetation &&
          !region->vegetation->update(world, local, prefabs, error,
                                      region->initialReady)) {
        throw std::runtime_error(error);
      }
      if (region->vegetation) {
        const auto plants = region->vegetation->statistics();
        vegetationCells += plants.residentCells;
        vegetationBytes +=
            plants.residentBytes + plants.pendingBytes + plants.retiringBytes;
        vegetationPending += plants.pendingJobs;
      }
      for (const auto &variant : region->active) {
        const auto rectangle = regionPatchRectangle(*source, variant.patch);
        finestSpacing =
            std::min(finestSpacing, std::max(rectangle.width, rectangle.depth) /
                                        source->recipe.chunkCells);
      }
      budgetMisses += stats.budgetMisses;
      activeCount += region->active.size();
      pending += stats.pendingJobs;
      bytes += stats.residentBytes + stats.pendingBytes + stats.retiringBytes;
    }
    RuntimeProfiler::setGauge("Terrain.region.vegetation_cells",
                              vegetationCells);
    RuntimeProfiler::setGauge("Terrain.region.vegetation_accounted_bytes",
                              vegetationBytes);
    RuntimeProfiler::setGauge("Terrain.region.vegetation_pending_jobs",
                              vegetationPending);
    RuntimeProfiler::setGauge("Terrain.region.budget_misses", budgetMisses);
    RuntimeProfiler::setGauge("Terrain.region.morphing_regions", morphingRegions);
    RuntimeProfiler::setGauge("Terrain.region.render_waiting_regions",
                              renderWaitingRegions);
    RuntimeProfiler::setGauge("Terrain.region.finest_spacing",
                              activeCount ? finestSpacing : 0);
    RuntimeProfiler::setGauge("Terrain.region.active_tiles", activeCount);
    RuntimeProfiler::setGauge("Terrain.region.pending_jobs", pending);
    RuntimeProfiler::setGauge("Terrain.region.accounted_bytes", bytes);
    return true;
  }
};
RegionalTerrainSystem::RegionalTerrainSystem() = default;
RegionalTerrainSystem::RegionalTerrainSystem(Options options)
    : options_(options) {}
bool RegionalTerrainSystem::hasPendingWork() const {
  if (!impl_) {
    return false;
  }
  for (const auto &[id, region] : impl_->regions) {
    const auto state = region->cache.statistics();
    const bool vegetationPending =
        region->vegetation && region->vegetation->hasPendingWork();
    if (region->morph || region->needsPlan || region->proposed ||
        state.pendingJobs || state.retiringJobs || vegetationPending) {
      return true;
    }
  }
  return !impl_->retired.empty();
}
RegionalTerrainSystem::~RegionalTerrainSystem() = default;
bool RegionalTerrainSystem::update(World &world, Vec3 camera,
                                   std::string &error,
                                   RuntimePrefabService *prefabs,
                                   float deltaSeconds) {
  return updateView(world, camera, {}, error, prefabs, deltaSeconds);
}
bool RegionalTerrainSystem::updateView(World &world, Vec3 camera,
                                       std::optional<Vec3> forward,
                                       std::string &error,
                                       RuntimePrefabService *prefabs,
                                       float deltaSeconds) {
  error.clear();
  try {
    if (!impl_) {
      const bool present =
          std::ranges::any_of(world.entities, [](const auto &entity) {
            const auto *terrain =
                entity.template component<Terrain3DComponent>();
            return terrain && terrain->region;
          });
      if (!present) {
        return true;
      }
      impl_ = std::make_unique<Impl>(options_);
    }
    return impl_->update(world, camera, forward, error, prefabs, deltaSeconds);
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
}
bool RegionalTerrainSystem::update(World &world, std::string &error,
                                   RuntimePrefabService *prefabs,
                                   float deltaSeconds) {
  const Entity *camera = nullptr;
  for (const auto &entity : world.entities) {
    const auto *view = entity.component<Camera3DComponent>();
    if (entity.enabled && view && (!camera || view->primary)) {
      camera = &entity;
    }
  }
  if (!camera) {
    return impl_
               ? update(world, impl_->lastCamera, error, prefabs, deltaSeconds)
               : true;
  }
  const auto transform = resolveWorldTransform3D(world, *camera);
  if (!transform) {
    error = "Regional terrain camera hierarchy is invalid";
    return false;
  }
  return updateView(
      world, transform->position,
      transformDirection3D(
          *transform, camera->component<Camera3DComponent>()->targetOffset),
      error, prefabs, deltaSeconds);
}
} // namespace demi::runtime
