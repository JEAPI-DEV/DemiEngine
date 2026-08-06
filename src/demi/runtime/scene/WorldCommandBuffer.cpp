#include "demi/runtime/scene/WorldCommandBuffer.h"

#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <algorithm>

namespace demi::runtime {

bool WorldCommandBuffer::create(const World &world, Entity entity,
                                const bool replace) {
  const bool exists = findEntity(world, entity.id) != nullptr;
  if (entity.id.empty() || (replace ? !exists : exists))
    return false;
  const bool duplicatePending =
      std::ranges::any_of(commands_, [&](const Command &command) {
        const auto *pending = std::get_if<Create>(&command);
        return pending != nullptr && pending->entity.id == entity.id;
      });
  if (duplicatePending)
    return false;
  commands_.push_back(Create{.entity = std::move(entity), .replace = replace});
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
  for (Command &command : commands_) {
    if (auto *create = std::get_if<Create>(&command)) {
      const std::string id = create->entity.id;
      const bool replacing = findEntity(world, id) != nullptr;
      if (RuntimeObjectModel::addEntity(world, std::move(create->entity),
                                        create->replace)) {
        mutations.push_back(
            {.kind = replacing ? WorldMutationKind::Replaced
                               : WorldMutationKind::Created,
             .entityId = id,
             .component = {}});
      }
    } else if (auto *destroy = std::get_if<Destroy>(&command)) {
      const auto before = world.entities.size();
      std::erase_if(world.entities,
                    [&](const Entity &entity) { return entity.id == destroy->id; });
      if (world.entities.size() != before)
        mutations.push_back({.kind = WorldMutationKind::Destroyed,
                             .entityId = destroy->id,
                             .component = {}});
    } else if (auto *add = std::get_if<AddComponent>(&command)) {
      Entity *entity = findEntity(world, add->id);
      if (entity != nullptr &&
          RuntimeObjectModel::addComponent(*entity, add->component,
                                           add->values))
        mutations.push_back({.kind = WorldMutationKind::ComponentAdded,
                             .entityId = add->id,
                             .component = add->component});
    } else if (auto *remove = std::get_if<RemoveComponent>(&command)) {
      Entity *entity = findEntity(world, remove->id);
      if (entity != nullptr &&
          RuntimeObjectModel::removeComponent(*entity, remove->component))
        mutations.push_back({.kind = WorldMutationKind::ComponentRemoved,
                             .entityId = remove->id,
                             .component = remove->component});
    } else if (auto *enabled = std::get_if<SetEnabled>(&command)) {
      Entity *entity = findEntity(world, enabled->id);
      if (entity != nullptr) {
        entity->enabled = enabled->enabled;
        mutations.push_back({.kind = WorldMutationKind::EnabledChanged,
                             .entityId = enabled->id,
                             .component = {}});
      }
    }
  }
  commands_.clear();
  return mutations;
}

Entity *WorldCommandBuffer::pendingEntity(const std::string_view id) {
  for (Command &command : commands_) {
    if (auto *create = std::get_if<Create>(&command);
        create != nullptr && create->entity.id == id)
      return &create->entity;
  }
  return nullptr;
}

const Entity *
WorldCommandBuffer::pendingEntity(const std::string_view id) const {
  for (const Command &command : commands_) {
    if (const auto *create = std::get_if<Create>(&command);
        create != nullptr && create->entity.id == id)
      return &create->entity;
  }
  return nullptr;
}

void WorldCommandBuffer::clear() { commands_.clear(); }
bool WorldCommandBuffer::empty() const { return commands_.empty(); }

} // namespace demi::runtime
