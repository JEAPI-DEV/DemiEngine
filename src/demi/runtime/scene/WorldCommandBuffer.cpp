#include "demi/runtime/scene/WorldCommandBuffer.h"

#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <algorithm>

namespace demi::runtime {

bool WorldCommandBuffer::create(const World &world, Entity entity,
                                const bool replace) {
  if (entity.id.empty())
    return false;
  const bool exists = findEntity(world, entity.id) != nullptr;
  if ((replace ? !exists : exists) || pendingCreates_.contains(entity.id))
    return false;
  const std::string id = entity.id;
  commands_.push_back(Create{.entity = std::move(entity), .replace = replace});
  try {
    pendingCreates_.emplace(id, commands_.size() - 1);
  } catch (...) {
    commands_.pop_back();
    throw;
  }
  return true;
}

bool WorldCommandBuffer::clone(const World &world,
                               const std::string_view sourceId,
                               std::string newId) {
  const Entity *source = findEntity(world, std::string(sourceId));
  if (source == nullptr)
    return false;
  Entity clone = *source;
  clone.id = std::move(newId);
  clone.name = clone.id;
  return create(world, std::move(clone));
}

bool WorldCommandBuffer::destroy(const World &world, std::string entityId) {
  if (findEntity(world, entityId) == nullptr)
    return false;
  commands_.push_back(Destroy{.id = std::move(entityId)});
  return true;
}

bool WorldCommandBuffer::addComponent(const World &world, std::string entityId,
                                      std::string component,
                                      nlohmann::json values) {
  Entity *pending = pendingEntity(entityId);
  if (pending != nullptr)
    return RuntimeObjectModel::addComponent(*pending, component, values).ok;
  const Entity *entity = findEntity(world, entityId);
  if (entity == nullptr ||
      RuntimeObjectModel::hasComponent(*entity, component))
    return false;
  Entity probe = *entity;
  if (!RuntimeObjectModel::addComponent(probe, component, values))
    return false;
  commands_.push_back(AddComponent{.id = std::move(entityId),
                                   .component = std::move(component),
                                   .values = std::move(values)});
  return true;
}

bool WorldCommandBuffer::removeComponent(const World &world,
                                         std::string entityId,
                                         std::string component) {
  Entity *pending = pendingEntity(entityId);
  if (pending != nullptr)
    return RuntimeObjectModel::removeComponent(*pending, component).ok;
  const Entity *entity = findEntity(world, entityId);
  if (entity == nullptr ||
      !RuntimeObjectModel::hasComponent(*entity, component))
    return false;
  commands_.push_back(RemoveComponent{
      .id = std::move(entityId), .component = std::move(component)});
  return true;
}

bool WorldCommandBuffer::setEnabled(const World &world, std::string entityId,
                                    const bool enabled) {
  if (Entity *pending = pendingEntity(entityId)) {
    pending->enabled = enabled;
    return true;
  }
  if (findEntity(world, entityId) == nullptr)
    return false;
  commands_.push_back(
      SetEnabled{.id = std::move(entityId), .enabled = enabled});
  return true;
}

std::vector<WorldMutation> WorldCommandBuffer::flush(World &world) {
  std::vector<WorldMutation> mutations;
  if (commands_.empty()) {
    pendingCreates_.clear();
    return mutations;
  }
  mutations.reserve(commands_.size());
  EntityIndex worldIndex;
  worldIndex.reserve(world.entities.size() + commands_.size());
  const auto rebuildIndex = [&] {
    worldIndex.clear();
    for (std::size_t index = 0; index < world.entities.size(); ++index)
      worldIndex.try_emplace(world.entities[index].id, index);
  };
  rebuildIndex();
  for (Command &command : commands_) {
    if (auto *create = std::get_if<Create>(&command)) {
      const std::string id = create->entity.id;
      const auto existing = worldIndex.find(id);
      const bool replacing = existing != worldIndex.end();
      Entity *existingEntity =
          replacing ? &world.entities[existing->second] : nullptr;
      if (!RuntimeObjectModel::insertEntity(
              world, std::move(create->entity), create->replace,
              existingEntity))
        continue;
      if (!replacing)
        worldIndex.emplace(id, world.entities.size() - 1);
      mutations.push_back({.kind = replacing ? WorldMutationKind::Replaced
                                             : WorldMutationKind::Created,
                           .entityId = id,
                           .component = {}});
    } else if (auto *destroy = std::get_if<Destroy>(&command)) {
      if (!worldIndex.contains(destroy->id))
        continue;
      std::erase_if(world.entities, [&](const Entity &entity) {
        return entity.id == destroy->id;
      });
      // Erase compacts the vector. Rebuild only on this path, preserving the
      // first matching ID if malformed input contained duplicate world IDs.
      // Repeated destroys still scan/compact the world; this index targets the
      // bulk-create path, not bulk deletion.
      rebuildIndex();
      mutations.push_back({.kind = WorldMutationKind::Destroyed,
                           .entityId = destroy->id,
                           .component = {}});
    } else if (auto *add = std::get_if<AddComponent>(&command)) {
      const auto found = worldIndex.find(add->id);
      if (found != worldIndex.end() &&
          RuntimeObjectModel::addComponent(world.entities[found->second],
                                           add->component, add->values))
        mutations.push_back({.kind = WorldMutationKind::ComponentAdded,
                             .entityId = add->id,
                             .component = add->component});
    } else if (auto *remove = std::get_if<RemoveComponent>(&command)) {
      const auto found = worldIndex.find(remove->id);
      if (found != worldIndex.end() &&
          RuntimeObjectModel::removeComponent(world.entities[found->second],
                                              remove->component))
        mutations.push_back({.kind = WorldMutationKind::ComponentRemoved,
                             .entityId = remove->id,
                             .component = remove->component});
    } else if (auto *enabled = std::get_if<SetEnabled>(&command)) {
      const auto found = worldIndex.find(enabled->id);
      if (found != worldIndex.end()) {
        world.entities[found->second].enabled = enabled->enabled;
        mutations.push_back({.kind = WorldMutationKind::EnabledChanged,
                             .entityId = enabled->id,
                             .component = {}});
      }
    }
  }
  commands_.clear();
  pendingCreates_.clear();
  return mutations;
}

Entity *WorldCommandBuffer::pendingEntity(const std::string_view id) {
  const auto found = pendingCreates_.find(id);
  if (found == pendingCreates_.end())
    return nullptr;
  return &std::get<Create>(commands_[found->second]).entity;
}

const Entity *
WorldCommandBuffer::pendingEntity(const std::string_view id) const {
  const auto found = pendingCreates_.find(id);
  if (found == pendingCreates_.end())
    return nullptr;
  return &std::get<Create>(commands_[found->second]).entity;
}

void WorldCommandBuffer::clear() {
  commands_.clear();
  pendingCreates_.clear();
}
std::vector<std::string> WorldCommandBuffer::affectedEntityIds() const {
  std::vector<std::string> ids;
  ids.reserve(commands_.size());
  for (const auto &command : commands_)
    std::visit([&](const auto &value) {
      if constexpr (std::is_same_v<std::decay_t<decltype(value)>, Create>)
        ids.push_back(value.entity.id);
      else
        ids.push_back(value.id);
    }, command);
  std::ranges::sort(ids);
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}
bool WorldCommandBuffer::empty() const { return commands_.empty(); }

} // namespace demi::runtime
