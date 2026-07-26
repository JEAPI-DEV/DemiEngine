#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/gameplay/LuaScriptComponent.h"

#include <iostream>
#include <unordered_set>

namespace demi::runtime {

std::optional<std::string> LuaScriptHost::instantiatePrefab(
    const std::string &prefab, const PrefabInstantiateOptions &options) {
  if (world_ == nullptr)
    return std::nullopt;
  PrefabInstanceResult result =
      prefabService_.instantiate(*world_, worldCommands_, prefab, options);
  if (!result) {
    if (!result.diagnostics.empty())
      std::cerr << "Prefab instantiate failed: "
                << result.diagnostics.front().message << '\n';
    return std::nullopt;
  }
  return result.instanceId;
}

bool LuaScriptHost::releasePrefab(const std::string &instanceId) {
  return world_ != nullptr &&
         prefabService_.release(*world_, worldCommands_, instanceId);
}

std::size_t
LuaScriptHost::pooledPrefabCount(const std::string &prefab) const {
  return prefabService_.pooledCount(prefab);
}

bool LuaScriptHost::entityExists(const std::string &entityId) const {
  return world_ != nullptr &&
         (findEntity(*world_, entityId) != nullptr ||
          worldCommands_.pendingEntity(entityId) != nullptr);
}

bool LuaScriptHost::createEntity(Entity entity) {
  return world_ != nullptr &&
         worldCommands_.create(*world_, std::move(entity), false);
}

bool LuaScriptHost::replaceEntity(Entity entity) {
  return world_ != nullptr &&
         worldCommands_.create(*world_, std::move(entity), true);
}

bool LuaScriptHost::cloneEntity(const std::string &sourceId,
                                const std::string &newId) {
  return world_ != nullptr && worldCommands_.clone(*world_, sourceId, newId);
}

bool LuaScriptHost::destroyEntity(const std::string &entityId) {
  return world_ != nullptr && worldCommands_.destroy(*world_, entityId);
}

int LuaScriptHost::destroyEntities(
    const std::vector<std::string> &entityIds) {
  if (world_ == nullptr)
    return 0;
  std::unordered_set<std::string> unique;
  int accepted = 0;
  for (const std::string &id : entityIds) {
    if (!id.empty() && unique.insert(id).second &&
        worldCommands_.destroy(*world_, id))
      ++accepted;
  }
  return accepted;
}

bool LuaScriptHost::setEntityEnabled(const std::string &entityId,
                                     const bool enabled) {
  return world_ != nullptr &&
         worldCommands_.setEnabled(*world_, entityId, enabled);
}

bool LuaScriptHost::isEntityEnabled(const std::string &entityId) const {
  const Entity *entity =
      world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  if (entity == nullptr)
    entity = worldCommands_.pendingEntity(entityId);
  return entity != nullptr && entity->enabled;
}

bool LuaScriptHost::addEntityComponent(const std::string &entityId,
                                       const std::string &component,
                                       const nlohmann::json &values) {
  return world_ != nullptr && worldCommands_.addComponent(
                                  *world_, entityId, component, values);
}

bool LuaScriptHost::removeEntityComponent(const std::string &entityId,
                                          const std::string &component) {
  return world_ != nullptr &&
         worldCommands_.removeComponent(*world_, entityId, component);
}

bool LuaScriptHost::hasEntityComponent(const std::string &entityId,
                                       const std::string &component) const {
  const Entity *entity =
      world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  if (entity == nullptr)
    entity = worldCommands_.pendingEntity(entityId);
  return entity != nullptr &&
         RuntimeObjectModel::hasComponent(*entity, component);
}

std::optional<nlohmann::json> LuaScriptHost::entityComponentField(
    const std::string &entityId, const std::string &component,
    const std::string &field) const {
  const Entity *entity =
      world_ == nullptr ? nullptr : findEntity(*world_, entityId);
  if (entity == nullptr)
    entity = worldCommands_.pendingEntity(entityId);
  return entity == nullptr
             ? std::nullopt
             : RuntimeObjectModel::componentField(*entity, component, field);
}

bool LuaScriptHost::setEntityComponentField(
    const std::string &entityId, const std::string &component,
    const std::string &field, const nlohmann::json &value) {
  if (world_ == nullptr)
    return false;
  if (Entity *pending = worldCommands_.pendingEntity(entityId))
    return RuntimeObjectModel::setComponentField(*pending, component, field,
                                                 value)
        .ok;
  return RuntimeObjectModel::setComponentField(*world_, entityId, component,
                                               field, value)
      .ok;
}

std::vector<std::string>
LuaScriptHost::queryEntities(const EntityQuery &query) const {
  return world_ == nullptr ? std::vector<std::string>{}
                           : RuntimeObjectModel::query(*world_, query);
}

bool LuaScriptHost::setEntityParent(
    const std::string &entityId,
    const std::optional<std::string> &parentId) {
  return world_ != nullptr &&
         RuntimeObjectModel::setParent(*world_, entityId, parentId);
}

std::optional<std::string>
LuaScriptHost::entityParent(const std::string &entityId) const {
  return world_ == nullptr ? std::nullopt
                           : RuntimeObjectModel::parent(*world_, entityId);
}

std::vector<std::string>
LuaScriptHost::entityChildren(const std::string &entityId) const {
  return world_ == nullptr ? std::vector<std::string>{}
                           : RuntimeObjectModel::children(*world_, entityId);
}

std::optional<nlohmann::json>
LuaScriptHost::entityLocalPosition(const std::string &entityId) const {
  return world_ == nullptr
             ? std::nullopt
             : RuntimeObjectModel::localPosition(*world_, entityId);
}

std::optional<nlohmann::json>
LuaScriptHost::entityWorldPosition(const std::string &entityId) const {
  return world_ == nullptr
             ? std::nullopt
             : RuntimeObjectModel::worldPosition(*world_, entityId);
}

void LuaScriptHost::flushWorldCommands() {
  if (world_ == nullptr)
    return;
  const std::vector<WorldMutation> mutations = worldCommands_.flush(*world_);
  for (const WorldMutation &mutation : mutations) {
    const bool scriptRemoved =
        mutation.kind == WorldMutationKind::Destroyed ||
        mutation.kind == WorldMutationKind::Replaced ||
        (mutation.kind == WorldMutationKind::ComponentRemoved &&
         mutation.component == LuaScriptComponent::typeName);
    if (scriptRemoved)
      unloadEntityScript(mutation.entityId);

    const bool scriptMayHaveBeenAdded =
        mutation.kind == WorldMutationKind::Created ||
        mutation.kind == WorldMutationKind::Replaced ||
        mutation.kind == WorldMutationKind::EnabledChanged ||
        (mutation.kind == WorldMutationKind::ComponentAdded &&
         mutation.component == LuaScriptComponent::typeName);
    const Entity *entity = findEntity(*world_, mutation.entityId);
    if (!scriptMayHaveBeenAdded || entity == nullptr ||
        !entity->enabled || !entity->hasComponent<LuaScriptComponent>())
      continue;
    const auto loaded =
        std::ranges::find(scripts_, mutation.entityId,
                          &ScriptInstance::entityId);
    if (loaded != scripts_.end()) {
      startScriptInstance(*loaded);
    } else {
      std::string error;
      if (!loadDynamicEntityScript(mutation.entityId, error))
        std::cerr << "Dynamic LuaScript lifecycle failed: " << error << '\n';
    }
  }
}

} // namespace demi::runtime
