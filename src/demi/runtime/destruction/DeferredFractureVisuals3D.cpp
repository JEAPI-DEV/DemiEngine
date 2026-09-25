#include "demi/runtime/destruction/DeferredFractureVisuals3D.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Destructible3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SurfaceRelief3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include <algorithm>
#include <stdexcept>

namespace demi::runtime {
void DeferredFractureVisuals3D::configure(
    const World &world, const Entity &root,
    const Destructible3DComponent &config) {
  templates_ = config.deferredVisuals;
  root_ = root.id;
  intactVisuals_ = config.intactVisuals;
  for (const auto &[regionId, visualIds] : intactVisuals_) {
    if (!templates_ || !templates_->contains(regionId)) {
      throw std::runtime_error("Missing deferred region for intact batches: " +
                               regionId);
    }
    for (const std::string &visualId : visualIds) {
      const Entity *visual = findEntity(world, visualId);
      const auto *transform =
          visual ? visual->component<Transform3DComponent>() : nullptr;
      if (!transform || transform->parent != regionId ||
          !visual->hasComponent<MeshRendererComponent>() ||
          !visual->hasComponent<MeshInstances3DComponent>()) {
        throw std::runtime_error("Invalid intact instance batch: " + visualId);
      }
    }
  }
  if (!templates_)
    return;
  std::set<std::string> mapped;
  for (const auto &[part, id] : config.parts)
    mapped.insert(id);
  for (const auto &[region, items] : templates_->items()) {
    std::map<std::string, std::string> parents;
    for (const auto &item : items) {
      const auto id = item.at("id").get<std::string>();
      if (!parents
               .emplace(
                   id,
                   item.at("components").at("Transform3D").value("parent", ""))
               .second)
        throw std::runtime_error("Duplicate deferred visual: " + id);
    }
    const auto *intact = findEntity(world, region);
    const auto *pose =
        intact ? intact->component<Transform3DComponent>() : nullptr;
    if (!pose || pose->parent != root.id ||
        !intact->hasComponent<MeshRendererComponent>())
      throw std::runtime_error(
          "Deferred region must be an intact renderer child: " + region);
    for (const auto &item : items) {
      const auto id = item.at("id").get<std::string>();
      const auto &c = item.at("components");
      if (findEntity(world, id) || !c.contains("MeshRenderer") ||
          !c.contains("Transform3D") || item.value("persistent", false))
        throw std::runtime_error("Invalid deferred leaf mapping: " + id);
      auto leaf = id;
      std::set<std::string> visited;
      while (!mapped.contains(leaf)) {
        if (!visited.insert(leaf).second || !parents.contains(leaf) ||
            !parents.contains(parents.at(leaf)))
          throw std::runtime_error(
              "Deferred visual must descend from a mapped leaf: " + id);
        leaf = parents.at(leaf);
      }
      if (!parents.contains(leaf) || parents.at(leaf) != root.id)
        throw std::runtime_error(
            "Deferred primary leaf must belong to assembly: " + leaf);
      if (!leafOwners_.emplace(id, leaf).second)
        throw std::runtime_error(
            "Deferred visual belongs to multiple regions: " + id);
      regions_.emplace(id, region);
    }
  }
}
std::string
DeferredFractureVisuals3D::dormantOwner(const std::string &visual) const {
  const auto found = regions_.find(visual);
  return found != regions_.end() && !refined_.contains(found->second)
             ? found->second
             : std::string{};
}
std::set<std::string> DeferredFractureVisuals3D::prepare(
    World &candidate, const std::map<std::string, std::string> &owners) const {
  std::set<std::string> refined;
  if (!templates_)
    return refined;
  for (const auto &[region, items] : templates_->items()) {
    if (refined_.contains(region))
      continue;
    std::set<std::string> parents;
    for (const auto &item : items)
      parents.insert(
          owners.at(leafOwners_.at(item.at("id").get<std::string>())));
    auto *intact = findEntity(candidate, region);
    if (parents.size() == 1 && !parents.begin()->empty()) {
      if (!intact)
        throw std::runtime_error("Deferred region disappeared: " + region);
      intact->component<Transform3DComponent>()->parent = *parents.begin();
      continue;
    }
    // A retired whole region needs no leaves. A split region creates only its
    // surviving leaves; unchanged regions stay compact, even in this assembly.
    const auto sceneOwner = intact ? intact->sceneOwner : std::string{};
    if (intact) {
      // Preserve the authored region's identity and any attached gameplay.
      intact->removeComponent<MeshRendererComponent>();
      intact->removeComponent<MeshInstances3DComponent>();
      intact->removeComponent<SurfaceRelief3DComponent>();
      intact->serializedComponents.erase("MeshRenderer");
      intact->serializedComponents.erase("MeshInstances3D");
      intact->serializedComponents.erase("SurfaceRelief3D");
      intact->component<Transform3DComponent>()->parent = root_;
    }
    if (const auto batch = intactVisuals_.find(region);
        batch != intactVisuals_.end())
      std::erase_if(candidate.entities, [&](const Entity &entity) {
        return std::ranges::find(batch->second, entity.id) !=
               batch->second.end();
      });
    for (const auto &item : items) {
      const auto id = item.at("id").get<std::string>();
      const auto &parent = owners.at(leafOwners_.at(id));
      if (parent.empty())
        continue;
      if (findEntity(candidate, id))
        throw std::runtime_error("Deferred leaf identity collision: " + id);
      std::string error;
      auto entity = RuntimeObjectModel::buildEntity(item, error);
      if (!entity)
        throw std::runtime_error(error);
      entity->sceneOwner = sceneOwner;
      if (id == leafOwners_.at(id))
        entity->component<Transform3DComponent>()->parent = parent;
      candidate.entities.push_back(std::move(*entity));
    }
    refined.insert(region);
  }
  return refined;
}
void DeferredFractureVisuals3D::commit(std::set<std::string> regions) {
  refined_.merge(regions);
}
} // namespace demi::runtime
