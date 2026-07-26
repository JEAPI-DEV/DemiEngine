#include "demi/runtime/scene/RuntimePrefabService.h"

#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <algorithm>

namespace demi::runtime {
namespace {

Diagnostic prefabError(std::string code, std::string message,
                       const std::filesystem::path &path = {}) {
  return {.severity = Severity::Error,
          .code = std::move(code),
          .message = std::move(message),
          .path = path.string(),
          .suggestion = "Inspect the prefab reference, instance id, and "
                        "component overrides."};
}

void applyRootPosition(Entity &entity, const Vec3 &position,
                       const std::vector<std::string> &instanceIds) {
  const auto isInstanceChild = [&](const std::string &parent) {
    return !parent.empty() &&
           std::ranges::find(instanceIds, parent) != instanceIds.end();
  };
  if (auto *transform = entity.component<Transform2DComponent>();
      transform != nullptr && !isInstanceChild(transform->parent)) {
    transform->position = {.x = position.x, .y = position.y};
    entity.serializedComponents[std::string(Transform2DComponent::typeName)] =
        nlohmann::json({{"parent", transform->parent},
                        {"position", {position.x, position.y}},
                        {"rotation", transform->rotation},
                        {"scale", {transform->scale.x, transform->scale.y}}})
            .dump();
  }
  if (auto *transform = entity.component<Transform3DComponent>();
      transform != nullptr && !isInstanceChild(transform->parent)) {
    transform->position = position;
    entity.serializedComponents[std::string(Transform3DComponent::typeName)] =
        nlohmann::json(
            {{"parent", transform->parent},
             {"position", {position.x, position.y, position.z}},
             {"rotation",
              {transform->rotation.x, transform->rotation.y,
               transform->rotation.z}},
             {"scale",
              {transform->scale.x, transform->scale.y, transform->scale.z}}})
            .dump();
  }
}

} // namespace

void RuntimePrefabService::configure(std::filesystem::path projectDirectory) {
  projectDirectory_ = std::move(projectDirectory);
  instances_.clear();
}

PrefabInstanceResult RuntimePrefabService::build(
    const std::string_view prefab,
    const PrefabInstantiateOptions &options) const {
  PrefabInstanceResult result;
  if (projectDirectory_.empty() || options.id.empty()) {
    result.diagnostics.push_back(prefabError(
        "PREFAB_RUNTIME_INVALID_OPTIONS",
        "Runtime prefab instantiate requires a configured project and id."));
    return result;
  }
  const nlohmann::json instance = {
      {"id", options.id},
      {"prefab", prefab},
      {"overrides", options.overrides},
  };
  const composition::ExpansionResult expansion =
      composition::expandPrefabInstance(projectDirectory_ / "demi.project.json",
                                        instance);
  result.diagnostics = expansion.diagnostics;
  if (!expansion.document)
    return result;

  result.instanceId = options.id;
  for (const nlohmann::json &json : *expansion.document)
    result.entityIds.push_back(json.value("id", std::string{}));
  return result;
}

PrefabInstanceResult RuntimePrefabService::instantiate(
    World &world, WorldCommandBuffer &commands, std::string prefab,
    PrefabInstantiateOptions options) {
  if (options.pooled)
    for (auto &[instanceId, instance] : instances_) {
      if (instance.prefab == prefab && instance.available) {
        options.id = instanceId;
        PrefabInstanceResult result = build(prefab, options);
        if (!result)
          return result;
        const composition::ExpansionResult expansion =
            composition::expandPrefabInstance(
                projectDirectory_ / "demi.project.json",
                {{"id", options.id},
                 {"prefab", prefab},
                 {"overrides", options.overrides}});
        std::vector<Entity> entities;
        std::string error;
        for (const nlohmann::json &json : *expansion.document) {
          auto entity = RuntimeObjectModel::buildEntity(json, error);
          if (!entity) {
            result.diagnostics.push_back(
                prefabError("PREFAB_RUNTIME_ENTITY_INVALID", error));
            return result;
          }
          entities.push_back(std::move(*entity));
        }
        for (Entity &entity : entities) {
          const std::string entityId = entity.id;
          entity.prefabInstance = options.id;
          entity.prefabLocalId =
              entity.id.substr(std::min(entity.id.size(), options.id.size() + 1));
          if (options.position)
            applyRootPosition(entity, *options.position, result.entityIds);
          if (!commands.create(world, std::move(entity), true)) {
            result.diagnostics.push_back(prefabError(
                "PREFAB_RUNTIME_REPLACE_FAILED",
                "Could not reset pooled prefab entity: " + entityId));
            return result;
          }
        }
        instance.available = false;
        return result;
      }
    }

  PrefabInstanceResult result = build(prefab, options);
  if (!result)
    return result;
  if (instances_.contains(options.id)) {
    result.instanceId.clear();
    result.diagnostics.push_back(prefabError(
        "PREFAB_RUNTIME_DUPLICATE_INSTANCE",
        "Prefab instance already exists: " + options.id));
    return result;
  }
  const composition::ExpansionResult expansion =
      composition::expandPrefabInstance(
          projectDirectory_ / "demi.project.json",
          {{"id", options.id},
           {"prefab", prefab},
           {"overrides", options.overrides}});
  std::vector<Entity> entities;
  std::string error;
  for (const nlohmann::json &json : *expansion.document) {
    auto entity = RuntimeObjectModel::buildEntity(json, error);
    if (!entity) {
      result.instanceId.clear();
      result.diagnostics.push_back(
          prefabError("PREFAB_RUNTIME_ENTITY_INVALID", error));
      return result;
    }
    if (findEntity(world, entity->id) != nullptr ||
        commands.pendingEntity(entity->id) != nullptr) {
      result.instanceId.clear();
      result.diagnostics.push_back(prefabError(
          "PREFAB_RUNTIME_CREATE_FAILED",
          "Prefab entity id already exists: " + entity->id));
      return result;
    }
    entity->prefabInstance = options.id;
    entity->prefabLocalId =
        entity->id.substr(std::min(entity->id.size(), options.id.size() + 1));
    if (options.position)
      applyRootPosition(*entity, *options.position, result.entityIds);
    entities.push_back(std::move(*entity));
  }
  for (Entity &entity : entities) {
    if (!commands.create(world, std::move(entity))) {
      result.instanceId.clear();
      result.diagnostics.push_back(prefabError(
          "PREFAB_RUNTIME_CREATE_FAILED",
          "Could not queue prefab entity creation."));
      return result;
    }
  }
  instances_.emplace(options.id,
                     Instance{.prefab = std::move(prefab),
                              .entityIds = result.entityIds,
                              .pooled = options.pooled});
  return result;
}

bool RuntimePrefabService::release(World &world, WorldCommandBuffer &commands,
                                   const std::string &instanceId) {
  auto found = instances_.find(instanceId);
  if (found == instances_.end()) {
    found = std::ranges::find_if(instances_, [&](const auto &entry) {
      return std::ranges::find(entry.second.entityIds, instanceId) !=
             entry.second.entityIds.end();
    });
  }
  if (found == instances_.end() || found->second.available)
    return false;
  if (found->second.pooled) {
    for (const std::string &id : found->second.entityIds)
      if (!commands.setEnabled(world, id, false))
        return false;
    found->second.available = true;
    return true;
  }
  for (const std::string &id : found->second.entityIds)
    if (!commands.destroy(world, id))
      return false;
  instances_.erase(found);
  return true;
}

std::size_t
RuntimePrefabService::pooledCount(const std::string_view prefab) const {
  return static_cast<std::size_t>(std::ranges::count_if(
      instances_, [&](const auto &entry) {
        return entry.second.prefab == prefab && entry.second.available;
      }));
}

} // namespace demi::runtime
