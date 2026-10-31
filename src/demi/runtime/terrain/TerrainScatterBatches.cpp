#include "demi/runtime/terrain/TerrainScatterBatches.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainScatterCollision.h"
#include "demi/runtime/terrain/TerrainScatterLayout.h"
#include <algorithm>
#include <map>
#include <unordered_map>

namespace demi::runtime {
namespace {

struct DrawablePart {
  Entity source;
  WorldTransform3D transform;
};

struct PreparedSource {
  bool batchable = false;
  std::vector<DrawablePart> parts;
};

bool isVisualComponent(std::string_view name) {
  return name == "Transform3D" || name == "MeshRenderer" ||
         name == "SurfaceRelief3D";
}

PreparedSource prepareSource(const TerrainScatterPlacement &placement,
                             const TerrainPrefabServiceProvider &provider) {
  PreparedSource result;
  if (placement.prefab.empty()) {
    Entity source;
    source.id = "model";
    source.setComponent(MeshRendererComponent{.model = placement.model});
    result.batchable = true;
    result.parts.push_back({std::move(source), {}});
    return result;
  }
  auto *prefabs = provider ? provider() : nullptr;
  if (!prefabs) {
    return result;
  }
  Diagnostics diagnostics;
  auto entities = prefabs->prepareEntities(placement.prefab, diagnostics);
  if (!entities) {
    // Ordinary instantiation retains the existing detailed failure diagnostics.
    return result;
  }
  World sourceWorld;
  sourceWorld.entities = std::move(*entities);
  for (auto &entity : sourceWorld.entities) {
    applyTerrainScatterCollision(entity, TerrainCollisionPolicy::None);
  }
  for (const auto &entity : sourceWorld.entities) {
    for (const auto &[name, unused] : entity.serializedComponents) {
      if (!isVisualComponent(name)) {
        return {};
      }
    }
    if (!entity.enabled || !entity.component<MeshRendererComponent>()) {
      continue;
    }
    const auto transform = resolveWorldTransform3D(sourceWorld, entity);
    if (!transform) {
      return {};
    }
    result.parts.push_back({entity, *transform});
  }
  result.batchable = !result.parts.empty();
  return result;
}

bool sameTransform(const WorldTransform3D &left,
                   const WorldTransform3D &right) {
  return left.position.x == right.position.x &&
         left.position.y == right.position.y &&
         left.position.z == right.position.z &&
         left.rotation.x == right.rotation.x &&
         left.rotation.y == right.rotation.y &&
         left.rotation.z == right.rotation.z && left.scale.x == right.scale.x &&
         left.scale.y == right.scale.y && left.scale.z == right.scale.z;
}

} // namespace

TerrainScatterBatchResult
syncTerrainScatterBatches(World &world, WorldCommandBuffer &commands,
                          std::string_view ownerId, TerrainScatterView view,
                          const TerrainPrefabServiceProvider &prefabs,
                          std::string &error, std::stop_token stop) {
  ProfileScope profile("Terrain.scatter_batches");
  TerrainScatterBatchResult result;
  result.handled.resize(view.placements.size());
  const auto *pendingOwner = commands.pendingEntity(ownerId);
  // Command creation can reallocate its storage. Never retain a borrowed
  // pending-entity pointer across further create() calls.
  const std::optional<Entity> ownerSnapshot =
      pendingOwner ? std::optional<Entity>(*pendingOwner) : std::nullopt;
  const auto *owner =
      ownerSnapshot ? &*ownerSnapshot : findEntity(world, std::string(ownerId));
  if (!owner) {
    error =
        "Terrain scatter batch owner was not found: " + std::string(ownerId);
    return result;
  }
  const std::string prefix =
      std::string(ownerId) + "/__scatter/" + std::string(view.paletteId) + "/";
  std::unordered_map<std::string_view, WorldTransform3D> previous;
  std::vector<std::string> oldBatches;
  for (const auto &entity : world.entities) {
    if (!entity.hasComponent<TerrainScatterBatch>() ||
        !entity.id.starts_with(prefix)) {
      continue;
    }
    oldBatches.push_back(entity.id);
    const auto *instances = entity.component<MeshInstances3DComponent>();
    if (instances) {
      for (const auto &[id, transform] : instances->transforms()) {
        previous.try_emplace(id, transform);
      }
    }
  }

  std::map<std::string, PreparedSource> sources;
  std::map<std::string, Entity> batches;
  std::unordered_map<std::string, WorldTransform3D> current;
  current.reserve(view.placements.size());
  struct BatchPart {
    MeshInstances3DComponent *instances = nullptr;
    WorldTransform3D transform;
  };
  struct BatchGroup {
    bool batchable = false;
    std::vector<BatchPart> parts;
  };
  std::map<std::pair<std::string, std::string>, BatchGroup> groups;
  const auto groupFor =
      [&](const TerrainScatterPlacement &placement) -> BatchGroup & {
    const std::string sourceKey = placement.prefab.empty()
                                      ? "model:" + placement.model
                                      : "prefab:" + placement.prefab;
    auto [group, newGroup] = groups.try_emplace({sourceKey, placement.ruleId});
    if (!newGroup) {
      return group->second;
    }
    auto [source, newSource] = sources.try_emplace(sourceKey);
    if (newSource) {
      source->second = prepareSource(placement, prefabs);
    }
    group->second.batchable = source->second.batchable;
    for (const auto &part : source->second.parts) {
      const auto batchId = prefix + "__batches/" +
                           terrainScatterInstanceKey(sourceKey, part.source.id,
                                                     0, 0, placement.ruleId);
      auto [batch, created] = batches.try_emplace(batchId);
      if (created) {
        batch->second = part.source;
        batch->second.id = batchId;
        batch->second.name = "Scatter batch: " + placement.ruleId;
        batch->second.layer = owner->layer;
        batch->second.sceneOwner = owner->sceneOwner;
        batch->second.prefabInstance.clear();
        batch->second.prefabLocalId.clear();
        batch->second.setComponent(
            Transform3DComponent{.parent = std::string(ownerId)});
        batch->second.setComponent(MeshInstances3DComponent{});
        batch->second.setComponent(TerrainScatterBatch{});
        batch->second.removeComponent<TerrainScatterCollisionState>();
        batch->second.serializedComponents.erase("Transform3D");
      }
      group->second.parts.push_back(
          {batch->second.component<MeshInstances3DComponent>(),
           part.transform});
    }
    return group->second;
  };
  // Scatter normally emits consecutive rules. Unsorted inputs still resolve
  // through the group map; only invariant source/part setup is reused.
  BatchGroup *group = nullptr;
  std::string_view lastRule, lastModel, lastPrefab;
  const auto *terrain = owner->component<Terrain3DComponent>();
  for (std::size_t index = 0; index < view.placements.size(); ++index) {
    if ((index & 255U) == 0 && stop.stop_requested()) {
      return {};
    }
    const auto &placement = view.placements[index];
    if (placement.collision != TerrainCollisionPolicy::None) {
      continue;
    }
    if (!group || lastRule != placement.ruleId ||
        lastModel != placement.model || lastPrefab != placement.prefab) {
      group = &groupFor(placement);
      lastRule = placement.ruleId;
      lastModel = placement.model;
      lastPrefab = placement.prefab;
    }
    if (!group->batchable) {
      continue;
    }
    const auto placementId =
        terrainScatterInstanceId(ownerId, view.paletteId, placement.ruleId,
                                 placement.candidateX, placement.candidateZ);
    if (std::ranges::find(view.exclusions, placementId) !=
            view.exclusions.end() ||
        (terrain &&
         std::ranges::find(terrain->scatterExclusions, placementId) !=
             terrain->scatterExclusions.end())) {
      continue;
    }
    result.handled[index] = 1;
    const WorldTransform3D placementTransform{
        placement.position,
        {0, placement.yaw, 0},
        {placement.scale, placement.scale, placement.scale}};
    for (const auto &part : group->parts) {
      const auto transform =
          composeWorldTransform3D(placementTransform, part.transform);
      part.instances->setTransform(placementId, transform);
      current.try_emplace(placementId, transform);
    }
  }

  // A prepared group can be fully excluded. Do not publish an empty owner.
  std::erase_if(batches, [](const auto &item) {
    return item.second.template component<MeshInstances3DComponent>()
        ->transforms()
        .empty();
  });

  for (auto &[id, batch] : batches) {
    const auto *existing = findEntity(world, id);
    const auto *oldInstances =
        existing ? existing->component<MeshInstances3DComponent>() : nullptr;
    const auto &newInstances =
        batch.component<MeshInstances3DComponent>()->transforms();
    const bool unchanged =
        oldInstances &&
        existing->serializedComponents == batch.serializedComponents &&
        oldInstances->transforms().size() == newInstances.size() &&
        std::equal(oldInstances->transforms().begin(),
                   oldInstances->transforms().end(), newInstances.begin(),
                   [](const auto &left, const auto &right) {
                     return left.first == right.first &&
                            sameTransform(left.second, right.second);
                   });
    if (unchanged) {
      continue;
    }
    if (!commands.create(world, std::move(batch), existing != nullptr)) {
      error = "Terrain scatter could not publish render batch: " + id;
      return result;
    }
  }
  for (const auto &id : oldBatches) {
    if (!batches.contains(id) && !commands.destroy(world, id)) {
      error = "Terrain scatter could not remove render batch: " + id;
      return result;
    }
  }
  for (const auto &[id, transform] : current) {
    const auto previousTransform = previous.find(id);
    if (previousTransform == previous.end()) {
      ++result.statistics.created;
    } else if (sameTransform(previousTransform->second, transform)) {
      ++result.statistics.retained;
    } else {
      ++result.statistics.updated;
    }
  }
  for (const auto &[id, unused] : previous) {
    if (!current.contains(std::string(id))) {
      ++result.statistics.removed;
    }
  }
  return result;
}

} // namespace demi::runtime
