#include "demi/runtime/scene/RuntimePrefabService.h"

#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/assets/MasonryGeneration.h"
#include "demi/assets/AssetHash.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"

#include <algorithm>
#include <fstream>

namespace demi::runtime {
namespace {

bool placementOverrides(const nlohmann::json &overrides) {
  if (!overrides.is_object()) return false;
  for (const auto &entry : overrides) {
    if (!entry.is_object() || entry.size()!=1 || !entry.contains("components")) return false;
    const auto &components=entry["components"];
    if (!components.is_object() || components.size()!=1 || !components.contains("Transform3D")) return false;
    const auto &transform=components["Transform3D"];
    if (!transform.is_object()) return false;
    for (const auto &[key, value] : transform.items()) {
      (void)value;
      if (key!="position" && key!="rotation" && key!="scale") return false;
    }
  }
  return true;
}

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
  templates_.clear();
}

PrefabInstanceResult RuntimePrefabService::build(
    const std::string_view prefab,
    const PrefabInstantiateOptions &options, nlohmann::json &expanded) {
  PrefabInstanceResult result;
  if (projectDirectory_.empty() || options.id.empty()) {
    result.diagnostics.push_back(prefabError(
        "PREFAB_RUNTIME_INVALID_OPTIONS",
        "Runtime prefab instantiate requires a configured project and id."));
    return result;
  }
  std::string cacheKey;
  const auto path=composition::resolvePrefabReference(projectDirectory_/"demi.project.json",prefab);
  if (path && placementOverrides(options.overrides)) {
    std::ifstream file(*path);
    const auto source=nlohmann::json::parse(file,nullptr,false);
    const auto safe=[](const auto &self,const nlohmann::json &value)->bool {
      if (value.is_object()) {
        if (value.contains("instances") || value.contains("prefab") || value.contains("fracture")) return false;
        if (value.contains("components")) {
          const auto &c=value["components"];
          if (c.contains("Fracture3D") && c.contains("MeshRenderer") &&
              c["MeshRenderer"].contains("model") && c["Fracture3D"].value("collider","source")!="box") return false;
        }
        for (const auto &[key,child]:value.items()) if (!self(self,child)) return false;
      } else if (value.is_array()) for (const auto &child:value) if (!self(self,child)) return false;
      return true;
    };
    if (assets::hasMasonryAuthoring(source) && safe(safe,source))
      if (auto hash=assets::hashFile(*path)) cacheKey=std::string(prefab)+"|"+*hash+"|"+options.overrides.dump();
  }
  const nlohmann::json instance = {
      {"id", cacheKey.empty()?options.id:"template"},
      {"prefab", prefab},
      {"overrides", options.overrides},
  };
  if (!cacheKey.empty() && templates_.contains(cacheKey)) {
    expanded=composition::rebasePrefabEntities(templates_.at(cacheKey),"template",options.id);
  } else {
    ProfileScope preparation("Prefab.prepare_template");
    const auto expansion=composition::expandPrefabInstance(projectDirectory_/"demi.project.json",instance);
    result.diagnostics=expansion.diagnostics;
    if (!expansion.document) return result;
    expanded=*expansion.document;
    if (!cacheKey.empty()) {
      if (templates_.size()>=16) templates_.erase(templates_.begin());
      templates_[cacheKey]=expanded;
      RuntimeProfiler::setGauge("Prefab.template_cache_entries",double(templates_.size()));
      expanded=composition::rebasePrefabEntities(std::move(expanded),"template",options.id);
    }
  }

  result.instanceId = options.id;
  for (const nlohmann::json &json : expanded)
    result.entityIds.push_back(json.value("id", std::string{}));
  return result;
}

PrefabInstanceResult RuntimePrefabService::instantiate(
    World &world, WorldCommandBuffer &commands, std::string prefab,
    PrefabInstantiateOptions options) {
  ProfileScope instantiation("Prefab.instantiate");
  if (options.pooled)
    for (auto &[instanceId, instance] : instances_) {
      if (instance.prefab == prefab && instance.available) {
        options.id = instanceId;
        nlohmann::json expansion;
        PrefabInstanceResult result = build(prefab, options, expansion);
        if (!result)
          return result;
        std::vector<Entity> entities;
        std::string error;
        for (const nlohmann::json &json : expansion) {
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
          entity.sceneOwner = world.activeSceneId;
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

  nlohmann::json expansion;
  PrefabInstanceResult result = build(prefab, options, expansion);
  if (!result)
    return result;
  if (instances_.contains(options.id)) {
    result.instanceId.clear();
    result.diagnostics.push_back(prefabError(
        "PREFAB_RUNTIME_DUPLICATE_INSTANCE",
        "Prefab instance already exists: " + options.id));
    return result;
  }
  std::vector<Entity> entities;
  std::string error;
  for (const nlohmann::json &json : expansion) {
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
    entity->sceneOwner = world.activeSceneId;
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
    if ((findEntity(world,id) || commands.pendingEntity(id)) && !commands.destroy(world, id))
      return false;
  instances_.erase(found);
  return true;
}

void RuntimePrefabService::prune(const World &world) {
  std::erase_if(instances_, [&](const auto &entry) {
    return std::ranges::none_of(entry.second.entityIds,
                                [&](const std::string &id) {
                                  return findEntity(world, id) != nullptr;
                                });
  });
}

std::size_t
RuntimePrefabService::pooledCount(const std::string_view prefab) const {
  return static_cast<std::size_t>(std::ranges::count_if(
      instances_, [&](const auto &entry) {
        return entry.second.prefab == prefab && entry.second.available;
      }));
}

} // namespace demi::runtime
