#include "demi/runtime/terrain/TerrainCellPreparation.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/terrain/TerrainScatterBatches.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace demi::runtime {
namespace {
std::size_t retainedBytes(const PreparedTerrainCell &cell) {
  std::size_t bytes = sizeof(cell) + cell.batches.capacity() * sizeof(Entity);
  const auto add = [&](std::size_t count) {
    if (count > std::numeric_limits<std::size_t>::max() - bytes) {
      throw std::overflow_error("Prepared vegetation payload overflow");
    }
    bytes += count;
  };
  const auto text = [&](const std::string &value) {
    add(value.capacity() + 1);
  };
  const auto accountEntity = [&](const Entity &entity) {
    for (const auto *value :
         {&entity.id, &entity.name, &entity.layer, &entity.sceneOwner,
          &entity.prefabInstance, &entity.prefabLocalId}) {
      text(*value);
    }
    for (const auto &[name, value] : entity.serializedComponents) {
      text(name);
      text(value);
      add(sizeof(std::pair<std::string, std::string>) + 3 * sizeof(void *));
    }
    if (const auto *instances = entity.component<MeshInstances3DComponent>()) {
      add(sizeof(MeshInstances3DComponent));
      for (const auto &[id, transform] : instances->transforms()) {
        text(id);
        add(sizeof(std::pair<const std::string, WorldTransform3D>) +
            4 * sizeof(void *));
      }
    }
    if (const auto *mesh = entity.component<MeshRendererComponent>()) {
      add(sizeof(*mesh));
      add(mesh->vertices.capacity() * sizeof(Vec3));
      add(mesh->normals.capacity() * sizeof(Vec3));
      add(mesh->uvs.capacity() * sizeof(Vec2));
      add(mesh->vertexColors.capacity() * sizeof(Color));
      for (const auto *value :
           {&mesh->model, &mesh->mediumLodModel, &mesh->lowLodModel,
            &mesh->shape, &mesh->texture, &mesh->material,
            &mesh->renderLayer}) {
        text(*value);
      }
      for (const auto &[name, value] : mesh->materialNumbers) {
        text(name);
        add(sizeof(value) + sizeof(std::string) + 3 * sizeof(void *));
      }
      for (const auto &[name, value] : mesh->materialColors) {
        text(name);
        add(sizeof(value) + sizeof(std::string) + 3 * sizeof(void *));
      }
    }
  };
  for (const auto &entity : cell.batches) {
    accountEntity(entity);
  }
  for (const auto &[id, prepared] : cell.prefabInstances) {
    text(id);
    text(prepared.prefab());
    text(prepared.result().instanceId);
    add(sizeof(PreparedPrefabInstance) + 4 * sizeof(void *));
    for (const auto &entityId : prepared.result().entityIds) {
      text(entityId);
      add(sizeof(std::string));
    }
    for (const auto &entity : prepared.entities()) {
      add(sizeof(Entity));
      accountEntity(entity);
    }
  }
  return bytes;
}
} // namespace

std::shared_ptr<const PreparedTerrainCell>
prepareTerrainCell(std::vector<TerrainScatterPlacement> &placements,
                   std::string_view ownerId, std::string_view paletteId,
                   const std::filesystem::path &projectDirectory,
                   std::stop_token stop) {
  ScopedProfileSuppression suppressMainThreadScopes;
  if (stop.stop_requested()) {
    return {};
  }
  if (projectDirectory.empty() &&
      std::ranges::any_of(placements,
                          [](const auto &p) { return !p.prefab.empty(); })) {
    // Existing native clients without a project context retain the complete
    // normal publisher; never turn visual prefabs into individual entities.
    return {};
  }
  RuntimePrefabService prefabs;
  if (!projectDirectory.empty()) {
    prefabs.configure(projectDirectory);
  }
  auto preparation = prefabs.beginPreparationBatch();
  World staging;
  Entity owner;
  owner.id = ownerId;
  owner.setComponent(Transform3DComponent{});
  staging.entities.push_back(std::move(owner));
  WorldCommandBuffer commands;
  std::string error;
  const auto result = syncTerrainScatterBatches(
      staging, commands, ownerId, {paletteId, placements, {}},
      [&]() -> RuntimePrefabService * {
        return projectDirectory.empty() ? nullptr : &prefabs;
      },
      error, stop);
  if (!error.empty()) {
    throw std::runtime_error(error);
  }
  if (stop.stop_requested()) {
    return {};
  }
  (void)commands.flush(staging);
  auto cell = std::make_shared<PreparedTerrainCell>();
  for (auto &entity : staging.entities) {
    if (entity.hasComponent<TerrainScatterBatch>()) {
      cell->batches.push_back(std::move(entity));
    }
  }

  std::vector<TerrainScatterPlacement> interactive;
  interactive.reserve(std::count(result.handled.begin(), result.handled.end(),
                                 std::uint8_t{0}));
  for (std::size_t index = 0; index < placements.size(); ++index) {
    if (!result.handled[index]) {
      interactive.push_back(placements[index]);
    }
  }
  for (const auto &placement : interactive) {
    if (stop.stop_requested()) {
      return {};
    }
    if (placement.prefab.empty()) {
      continue;
    }
    const auto id =
        terrainScatterInstanceId(ownerId, paletteId, placement.ruleId,
                                 placement.candidateX, placement.candidateZ);
    auto prepared = prefabs.prepareInstance(
        placement.prefab,
        PrefabInstantiateOptions{.id = id, .position = placement.position});
    if (!prepared) {
      throw std::runtime_error(
          "Cannot prepare terrain prefab " + id + ": " +
          (prepared.result().diagnostics.empty()
               ? "unknown error"
               : prepared.result().diagnostics.front().message));
    }
    if (!cell->prefabInstances.emplace(id, std::move(prepared)).second) {
      throw std::runtime_error("Duplicate prepared terrain instance: " + id);
    }
  }
  cell->accountedBytes = retainedBytes(*cell);
  placements = std::move(interactive);
  return cell;
}

void publishTerrainVisuals(World &world, WorldCommandBuffer &commands,
                           const Entity &owner, const PreparedTerrainCell &cell,
                           std::span<const std::string> exclusions) {
  ProfileScope profile("Terrain.visual_adoption");
  std::unordered_set<std::string> wanted;
  for (const auto &source : cell.batches) {
    Entity batch = source;
    batch.layer = owner.layer;
    batch.sceneOwner = owner.sceneOwner;
    auto *instances = batch.component<MeshInstances3DComponent>();
    for (const auto &id : exclusions) {
      instances->eraseTransform(id);
    }
    if (instances->transforms().empty()) {
      continue;
    }
    wanted.insert(batch.id);
    const bool replacing = findEntity(world, batch.id) != nullptr;
    if (!commands.create(world, std::move(batch), replacing)) {
      throw std::runtime_error("Cannot adopt vegetation batch " + source.id);
    }
  }
  const std::string prefix = owner.id + "/__scatter/";
  for (const auto &entity : world.entities) {
    if (entity.hasComponent<TerrainScatterBatch>() &&
        entity.id.starts_with(prefix) && !wanted.contains(entity.id)) {
      if (!commands.destroy(world, entity.id)) {
        throw std::runtime_error("Cannot retire vegetation batch " + entity.id);
      }
    }
  }
}
} // namespace demi::runtime
