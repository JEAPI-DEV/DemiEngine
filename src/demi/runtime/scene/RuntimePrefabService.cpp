#include "demi/runtime/scene/RuntimePrefabService.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/composition/PrefabResolutionContext.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

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
  if (const auto *transform = entity.component<Transform3DComponent>();
      transform != nullptr && !isInstanceChild(transform->parent)) {
    entity.updateComponent<Transform3DComponent>(
        [&](Transform3DComponent &value) { value.position = position; });
    transform = entity.component<Transform3DComponent>();
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

RuntimePrefabService::PreparationBatch::PreparationBatch(
    RuntimePrefabService &owner,
    std::shared_ptr<composition::PrefabResolutionContext> previous)
    : owner_(&owner), previous_(std::move(previous)),
      context_(owner.resolution_) {}

RuntimePrefabService::PreparationBatch::PreparationBatch(
    PreparationBatch &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      previous_(std::move(other.previous_)),
      context_(std::move(other.context_)) {}

RuntimePrefabService::PreparationBatch::~PreparationBatch() {
  if (owner_ != nullptr) {
    owner_->resolution_ = std::move(previous_);
  }
}

std::size_t RuntimePrefabService::PreparationBatch::packageVerificationCount()
    const noexcept {
  return context_ ? context_->packageVerificationCount() : 0;
}

composition::PrefabResolutionContext &
RuntimePrefabService::PreparationBatch::resolutionContext() const noexcept {
  return *context_;
}

RuntimePrefabService::PreparationBatch
RuntimePrefabService::beginPreparationBatch(
    const AssetRegistry *verifiedRegistry) {
  auto previous = resolution_;
  if (!resolution_) {
    resolution_ = verifiedRegistry
                      ? std::make_shared<composition::PrefabResolutionContext>(
                            *verifiedRegistry)
                      : std::make_shared<composition::PrefabResolutionContext>(
                            projectDirectory_);
  }
  return PreparationBatch(*this, std::move(previous));
}

void RuntimePrefabService::configure(std::filesystem::path projectDirectory) {
  projectDirectory_ = std::move(projectDirectory);
  projectRoot_ =
      projectDirectory_.empty()
          ? std::filesystem::path{}
          : std::filesystem::absolute(projectDirectory_).lexically_normal();
  instances_.clear();
  templates_.configure(projectDirectory_);
  resolution_.reset();
}

PrefabInstanceResult
RuntimePrefabService::build(const std::string_view prefab,
                            const PrefabInstantiateOptions &options,
                            nlohmann::json &expanded) {
  PrefabInstanceResult result;
  if (projectDirectory_.empty() || options.id.empty()) {
    result.diagnostics.push_back(prefabError(
        "PREFAB_RUNTIME_INVALID_OPTIONS",
        "Runtime prefab instantiate requires a configured project and id."));
    return result;
  }
  std::optional<PreparationBatch> preparationBatch;
  if (resolution_ && resolution_->packageFiles(projectDirectory_) == nullptr) {
    result.diagnostics = resolution_->diagnostics();
    return result;
  }
  const std::string reference(prefab);
  constexpr std::string_view TemplateId = "template";
  if (const auto *cached = templates_.find(reference, options.overrides)) {
    expanded =
        composition::rebasePrefabEntities(*cached, TemplateId, options.id);
  } else {
    preparationBatch.emplace(beginPreparationBatch());
    if (resolution_->packageFiles(projectDirectory_) == nullptr) {
      result.diagnostics = resolution_->diagnostics();
      return result;
    }
    ProfileScope preparation("Prefab.prepare_template");
    const auto dependencies = templates_.dependencies(
        reference, options.overrides, resolution_.get());
    const nlohmann::json instance = {{"id", TemplateId},
                                     {"prefab", reference},
                                     {"overrides", options.overrides}};
    const auto expansion = composition::expandPrefabInstance(
        projectDirectory_ / "demi.project.json", instance, resolution_.get());
    result.diagnostics = expansion.diagnostics;
    if (!expansion.document) {
      return result;
    }
    templates_.store(reference, options.overrides, *expansion.document,
                     dependencies, resolution_.get());
    expanded = composition::rebasePrefabEntities(*expansion.document,
                                                 TemplateId, options.id);
  }
  RuntimeProfiler::setGauge(
      "Prefab.package_verifications",
      resolution_ ? static_cast<double>(resolution_->packageVerificationCount())
                  : 0.0);
  const auto cache = templates_.statistics();
  RuntimeProfiler::setGauge("Prefab.template_cache_entries",
                            double(cache.entries));
  RuntimeProfiler::setGauge("Prefab.template_cache_hits", double(cache.hits));
  RuntimeProfiler::setGauge("Prefab.template_cache_misses",
                            double(cache.misses));
  RuntimeProfiler::setGauge("Prefab.template_cache_bypasses",
                            double(cache.bypasses));

  result.instanceId = options.id;
  for (const nlohmann::json &json : expanded)
    result.entityIds.push_back(json.value("id", std::string{}));
  return result;
}

std::optional<std::vector<Entity>>
RuntimePrefabService::prepareEntities(std::string_view prefab,
                                      Diagnostics &diagnostics) {
  nlohmann::json expanded;
  const auto result = build(
      prefab, PrefabInstantiateOptions{.id = "scatter_template"}, expanded);
  diagnostics = result.diagnostics;
  if (!result) {
    return std::nullopt;
  }
  std::vector<Entity> entities;
  entities.reserve(expanded.size());
  for (const auto &document : expanded) {
    std::string error;
    auto entity = RuntimeObjectModel::buildEntity(document, error);
    if (!entity) {
      diagnostics.push_back(
          prefabError("PREFAB_RUNTIME_ENTITY_INVALID", error));
      return std::nullopt;
    }
    entities.push_back(std::move(*entity));
  }
  return entities;
}

PrefabInstanceResult
RuntimePrefabService::instantiate(World &world, WorldCommandBuffer &commands,
                                  std::string prefab,
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
          entity.prefabLocalId = entity.id.substr(
              std::min(entity.id.size(), options.id.size() + 1));
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

  return adoptPrepared(world, commands,
                       prepareInstance(std::move(prefab), options));
}

PreparedPrefabInstance
RuntimePrefabService::prepareInstance(std::string prefab,
                                      const PrefabInstantiateOptions &options) {
  ProfileScope preparation("Prefab.prepare_instance");
  PreparedPrefabInstance prepared;
  prepared.prefab_ = std::move(prefab);
  prepared.projectRoot_ = projectRoot_;
  prepared.pooled_ = options.pooled;
  nlohmann::json expansion;
  prepared.result_ = build(prepared.prefab_, options, expansion);
  if (!prepared.result_) {
    return prepared;
  }
  prepared.entities_.reserve(expansion.size());
  std::unordered_set<std::string> ids;
  for (const auto &document : expansion) {
    std::string error;
    auto entity = RuntimeObjectModel::buildEntity(document, error);
    if (!entity || !ids.insert(entity->id).second) {
      prepared.result_.instanceId.clear();
      prepared.result_.diagnostics.push_back(prefabError(
          "PREFAB_RUNTIME_ENTITY_INVALID",
          entity ? "Duplicate prefab entity ID: " + entity->id : error));
      prepared.entities_.clear();
      return prepared;
    }
    entity->prefabInstance = options.id;
    entity->prefabLocalId =
        entity->id.substr(std::min(entity->id.size(), options.id.size() + 1));
    if (options.position) {
      applyRootPosition(*entity, *options.position, prepared.result_.entityIds);
    }
    prepared.entities_.push_back(std::move(*entity));
  }
  return prepared;
}

PrefabInstanceResult
RuntimePrefabService::adoptPrepared(World &world, WorldCommandBuffer &commands,
                                    const PreparedPrefabInstance &prepared) {
  ProfileScope adoption("Prefab.adopt_prepared");
  auto result = prepared.result_;
  if (!result) {
    return result;
  }
  const auto fail = [&](std::string code, std::string message) {
    result.instanceId.clear();
    result.diagnostics.push_back(
        prefabError(std::move(code), std::move(message)));
    return result;
  };
  if (projectRoot_.empty() || prepared.projectRoot_ != projectRoot_) {
    return fail("PREFAB_RUNTIME_PROJECT_MISMATCH",
                "Prepared prefab belongs to another project");
  }
  if (instances_.contains(result.instanceId)) {
    return fail("PREFAB_RUNTIME_DUPLICATE_INSTANCE",
                "Prefab instance already exists: " + result.instanceId);
  }
  for (const auto &entity : prepared.entities_) {
    if (findEntity(world, entity.id) || commands.pendingEntity(entity.id)) {
      return fail("PREFAB_RUNTIME_CREATE_FAILED",
                  "Prefab entity id already exists: " + entity.id);
    }
  }
  for (const auto &source : prepared.entities_) {
    Entity entity = source;
    entity.sceneOwner = world.activeSceneId;
    if (!commands.create(world, std::move(entity))) {
      return fail("PREFAB_RUNTIME_CREATE_FAILED",
                  "Could not queue prepared prefab entity creation");
    }
  }
  instances_.emplace(result.instanceId, Instance{.prefab = prepared.prefab_,
                                                 .entityIds = result.entityIds,
                                                 .pooled = prepared.pooled_});
  return result;
}

std::optional<std::string_view>
RuntimePrefabService::sourcePrefab(std::string_view instanceId) const {
  const auto instance = instances_.find(std::string(instanceId));
  if (instance == instances_.end()) {
    return std::nullopt;
  }
  return instance->second.prefab;
}

PrefabInstanceResult
RuntimePrefabService::replace(World &world, WorldCommandBuffer &commands,
                              std::string prefab,
                              PrefabInstantiateOptions options) {
  const auto previous = instances_.find(options.id);
  if (previous == instances_.end() || previous->second.available) {
    PrefabInstanceResult result;
    result.diagnostics.push_back(
        prefabError("PREFAB_RUNTIME_INSTANCE_MISSING",
                    "Cannot replace missing prefab instance: " + options.id));
    return result;
  }
  nlohmann::json expansion;
  auto result = build(prefab, options, expansion);
  if (!result) {
    return result;
  }
  const std::unordered_set<std::string> oldIds(
      previous->second.entityIds.begin(), previous->second.entityIds.end());
  std::string sceneOwner = world.activeSceneId;
  for (const auto &id : previous->second.entityIds) {
    const auto *owned = findEntity(world, id);
    if (!owned) {
      continue;
    }
    if (owned->prefabInstance != options.id) {
      result.instanceId.clear();
      result.diagnostics.push_back(
          prefabError("PREFAB_RUNTIME_REPLACE_FAILED",
                      "Previous prefab entity is no longer owned: " + id));
      return result;
    }
    sceneOwner = owned->sceneOwner;
  }
  std::vector<Entity> entities;
  for (const auto &document : expansion) {
    std::string error;
    auto entity = RuntimeObjectModel::buildEntity(document, error);
    const auto *existing = entity ? findEntity(world, entity->id) : nullptr;
    if (!entity || commands.pendingEntity(entity->id) ||
        (existing && !oldIds.contains(existing->id))) {
      result.instanceId.clear();
      result.diagnostics.push_back(prefabError(
          "PREFAB_RUNTIME_REPLACE_FAILED",
          entity ? "Replacement entity ID is already owned: " + entity->id
                 : error));
      return result;
    }
    entity->prefabInstance = options.id;
    entity->sceneOwner = sceneOwner;
    entity->prefabLocalId = entity->id.substr(options.id.size() + 1);
    if (options.position) {
      applyRootPosition(*entity, *options.position, result.entityIds);
    }
    entities.push_back(std::move(*entity));
  }
  WorldCommandBuffer staged = commands;
  for (auto &entity : entities) {
    const bool exists = findEntity(world, entity.id) != nullptr;
    if (!staged.create(world, std::move(entity), exists)) {
      result.instanceId.clear();
      result.diagnostics.push_back(
          prefabError("PREFAB_RUNTIME_REPLACE_FAILED",
                      "Could not stage replacement prefab entities."));
      return result;
    }
  }
  for (const auto &id : oldIds) {
    if (std::ranges::find(result.entityIds, id) == result.entityIds.end() &&
        findEntity(world, id) && !staged.destroy(world, id)) {
      result.instanceId.clear();
      result.diagnostics.push_back(
          prefabError("PREFAB_RUNTIME_REPLACE_FAILED",
                      "Could not retire previous prefab entity: " + id));
      return result;
    }
  }
  Instance replacement{.prefab = std::move(prefab),
                       .entityIds = result.entityIds,
                       .pooled = options.pooled};
  previous->second = std::move(replacement);
  commands = std::move(staged);
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
    if ((findEntity(world, id) || commands.pendingEntity(id)) &&
        !commands.destroy(world, id))
      return false;
  instances_.erase(found);
  return true;
}

void RuntimePrefabService::prune(const World &world) {
  ProfileScope pruning("Prefab.prune");
  // The world is not mutated during pruning, so borrowed ID views remain valid.
  // Index once rather than scanning the world for every tracked instance.
  std::unordered_set<std::string_view> liveIds;
  liveIds.reserve(world.entities.size());
  for (const auto &entity : world.entities)
    liveIds.insert(entity.id);
  std::erase_if(instances_, [&](const auto &entry) {
    return std::ranges::none_of(
        entry.second.entityIds,
        [&](const std::string &id) { return liveIds.contains(id); });
  });
}

std::size_t
RuntimePrefabService::pooledCount(const std::string_view prefab) const {
  return static_cast<std::size_t>(
      std::ranges::count_if(instances_, [&](const auto &entry) {
        return entry.second.prefab == prefab && entry.second.available;
      }));
}

} // namespace demi::runtime
