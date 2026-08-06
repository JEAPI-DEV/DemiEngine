#include "demi/runtime/scene/SceneFlow.h"

#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace demi::runtime {

void SceneFlow::configure(ProjectData project) {
  project_ = std::move(project);
  state_ = ScenePreparationState::Idle;
  prepared_.reset();
  error_.clear();
}

bool SceneFlow::prepare(std::string sceneId, const bool additive) {
  if (state_ == ScenePreparationState::Loading || sceneId.empty())
    return false;
  state_ = ScenePreparationState::Loading;
  prepared_.reset();
  error_.clear();
  const ProjectData project = project_;
  future_ = std::async(std::launch::async,
                       [project, sceneId = std::move(sceneId), additive] {
                         Prepared result{.sceneId = sceneId,
                                         .additive = additive,
                                         .world = std::nullopt,
                                         .error = {}};
                         result.world =
                             loadScene(project, sceneId, result.error);
                         return result;
                       });
  return true;
}

bool SceneFlow::cancel() {
  if (state_ != ScenePreparationState::Loading &&
      state_ != ScenePreparationState::Ready)
    return false;
  prepared_.reset();
  error_.clear();
  state_ = ScenePreparationState::Cancelled;
  return true;
}

void SceneFlow::poll() {
  if (state_ != ScenePreparationState::Loading || !future_.valid() ||
      future_.wait_for(std::chrono::seconds(0)) !=
          std::future_status::ready)
    return;
  prepared_ = future_.get();
  if (!prepared_->world) {
    error_ = prepared_->error;
    state_ = ScenePreparationState::Failed;
    return;
  }
  state_ = ScenePreparationState::Ready;
}

ScenePreparationState SceneFlow::state() const { return state_; }

float SceneFlow::progress() const {
  return state_ == ScenePreparationState::Ready ? 1.0F : 0.0F;
}

const std::string &SceneFlow::error() const { return error_; }

bool SceneFlow::preparedAdditive() const {
  return prepared_.has_value() && prepared_->additive;
}

std::string SceneFlow::activationError(const World &world) const {
  if (state_ != ScenePreparationState::Ready || !prepared_ ||
      !prepared_->world)
    return "No prepared scene is ready for activation.";

  const World &incoming = *prepared_->world;
  if (prepared_->additive &&
      world.loadedSceneIds.contains(prepared_->sceneId))
    return "Scene is already loaded: " + prepared_->sceneId;

  std::unordered_set<std::string> entityIds;
  for (const Entity &entity : incoming.entities) {
    if (!entityIds.insert(entity.id).second)
      return "Prepared scene contains duplicate entity id: " + entity.id;
    const Entity *existing = findEntity(world, entity.id);
    if (existing != nullptr &&
        (prepared_->additive || existing->persistent))
      return "Prepared scene entity id conflicts with a live entity: " +
             entity.id;
  }

  std::unordered_set<std::string> uiIds;
  for (const ui::UiNode &node : incoming.ui.nodes) {
    if (!uiIds.insert(node.id).second)
      return "Prepared scene contains duplicate UI id: " + node.id;
    if (prepared_->additive &&
        std::ranges::any_of(world.ui.nodes, [&](const ui::UiNode &existing) {
          return existing.id == node.id;
        }))
      return "Prepared scene UI id conflicts with a live node: " + node.id;
  }
  return {};
}

std::optional<SceneTransition>
SceneFlow::activate(World &world, ResourceLifetimeRegistry &resources) {
  poll();
  if (const std::string activationFailure = activationError(world);
      !activationFailure.empty()) {
    error_ = activationFailure;
    state_ = ScenePreparationState::Failed;
    return std::nullopt;
  }

  World incoming = std::move(*prepared_->world);
  const std::string sceneId = prepared_->sceneId;
  SceneTransition transition{.previousActiveScene = world.activeSceneId,
                             .activeScene = sceneId,
                             .unloadingEntities = {},
                             .loadedEntities = {},
                             .unloadingUiNodes = {},
                             .loadedUiNodes = {},
                             .additive = prepared_->additive};
  for (Entity &entity : incoming.entities) {
    entity.sceneOwner = sceneId;
    transition.loadedEntities.push_back(entity.id);
  }
  for (ui::UiNode &node : incoming.ui.nodes) {
    node.sceneOwner = sceneId;
    transition.loadedUiNodes.push_back(node.id);
  }

  if (prepared_->additive) {
    for (Entity &entity : incoming.entities)
      world.entities.push_back(std::move(entity));
    for (ui::UiNode &node : incoming.ui.nodes)
      world.ui.nodes.push_back(std::move(node));
    world.loadedSceneIds.insert(sceneId);
    world.activeSceneId = sceneId;
    resources.capture(sceneId, world.entities);
  } else {
    std::vector<Entity> persistent;
    for (Entity &entity : world.entities) {
      if (entity.persistent) {
        entity.sceneOwner = "persistent";
        persistent.push_back(std::move(entity));
      } else {
        transition.unloadingEntities.push_back(entity.id);
      }
    }
    for (const ui::UiNode &node : world.ui.nodes)
      transition.unloadingUiNodes.push_back(node.id);
    world = std::move(incoming);
    world.activeSceneId = sceneId;
    world.loadedSceneIds = {sceneId};
    for (Entity &entity : persistent)
      world.entities.push_back(std::move(entity));
    resources.clear();
    resources.capture(sceneId, world.entities);
    resources.capture("persistent", world.entities);
  }

  prepared_.reset();
  state_ = ScenePreparationState::Idle;
  return transition;
}

std::optional<SceneTransition>
SceneFlow::unload(World &world, const std::string_view sceneId,
                  ResourceLifetimeRegistry &resources) {
  if (!world.loadedSceneIds.contains(std::string(sceneId)))
    return std::nullopt;
  SceneTransition transition{.previousActiveScene = world.activeSceneId,
                             .activeScene = world.activeSceneId,
                             .unloadingEntities = {},
                             .loadedEntities = {},
                             .unloadingUiNodes = {},
                             .loadedUiNodes = {},
                             .additive = true};
  std::erase_if(world.entities, [&](const Entity &entity) {
    if (entity.sceneOwner != sceneId || entity.persistent)
      return false;
    transition.unloadingEntities.push_back(entity.id);
    return true;
  });
  std::erase_if(world.ui.nodes, [&](const ui::UiNode &node) {
    if (node.sceneOwner != sceneId)
      return false;
    transition.unloadingUiNodes.push_back(node.id);
    return true;
  });
  world.loadedSceneIds.erase(std::string(sceneId));
  resources.release(sceneId);
  if (world.activeSceneId == sceneId) {
    world.activeSceneId =
        world.loadedSceneIds.empty()
            ? std::string{}
            : *std::ranges::min_element(world.loadedSceneIds);
    transition.activeScene = world.activeSceneId;
  }
  return transition;
}

bool SceneFlow::setPersistent(World &world, const std::string_view entityId,
                              const bool persistent) {
  Entity *entity = findEntity(world, std::string(entityId));
  if (entity == nullptr)
    return false;
  entity->persistent = persistent;
  return true;
}

} // namespace demi::runtime
