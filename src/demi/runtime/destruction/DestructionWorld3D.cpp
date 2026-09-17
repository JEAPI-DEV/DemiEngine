#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/destruction/ColliderFractureFamily3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace demi::runtime {
namespace {
struct FragmentOwner {
  std::string root;
  std::uint64_t key;
};
struct GroupBody {
  DestructionGroup3D group;
  std::string body;
};
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
bool contains(const DestructionGroup3D &group, const std::string &part) {
  return std::ranges::find(group.chunks, part) != group.chunks.end();
}
bool colliderPresent(const Entity &entity) {
  return entity.hasComponent<ModelCollider3DComponent>() ||
         entity.hasComponent<BoxCollider3DComponent>() ||
         entity.hasComponent<SphereCollider3DComponent>() ||
         entity.hasComponent<CapsuleCollider3DComponent>() ||
         entity.hasComponent<ConvexCollider3DComponent>();
}
} // namespace

struct DestructionWorld3D::Impl {
  struct Assembly {
    std::uint64_t key = 0;
    ColliderAsset3D asset;
    std::string sourceAsset;
    std::unique_ptr<BlastFamily3D> family;
    std::map<std::string, std::string> visuals;
    std::vector<GroupBody> groups;
    std::map<std::string, float> pending;
    std::map<std::string, float> pendingAnchors;
    DestructionState3D state;
    float totalVolume = 0;
    std::set<std::string> privateAssets;
  };
  std::map<std::string, Assembly> assemblies;
  std::uint64_t nextKey = 1;

  Assembly *find(const std::string &entity) {
    if (auto it = assemblies.find(entity); it != assemblies.end())
      return &it->second;
    for (auto &[id, assembly] : assemblies)
      for (const auto &group : assembly.groups)
        if (group.body == entity)
          return &assembly;
    return nullptr;
  }

  void attach(World &world, Entity &root, Assembly &assembly) {
    auto *config = root.component<Destructible3DComponent>();
    config->attachmentKey = assembly.key = nextKey++;
    assembly.state.root = root.id;
    assembly.state.status = "failed";
    const auto *transform = root.component<Transform3DComponent>();
    const auto *collider = root.component<ModelCollider3DComponent>();
    const auto *body = root.component<Rigidbody3DComponent>();
    require(transform && transform->parent.empty() && collider && body &&
                body->bodyEnabled && body->bodyType != "kinematic" &&
                !root.persistent && !collider->isTrigger &&
                !root.hasComponent<MeshRendererComponent>() &&
                !root.hasComponent<CharacterController3DComponent>() &&
                !root.hasComponent<BoxCollider3DComponent>() &&
                !root.hasComponent<SphereCollider3DComponent>() &&
                !root.hasComponent<CapsuleCollider3DComponent>() &&
                !root.hasComponent<ConvexCollider3DComponent>(),
            "Destructible3D requires an unparented non-trigger compound "
            "rigidbody with separate part visuals");
    const auto *asset = resolvedColliderAsset3D(world, root);
    require(asset && asset->fracture.has_value(),
            "Destructible3D requires a loaded collider fracture graph");
    require(config->parts.size() == asset->parts.size(),
            "Every fracture part needs one visual mapping");
    std::set<std::string> mappedVisuals;
    for (const auto &part : asset->parts) {
      const auto mapping = config->parts.find(part.id);
      require(mapping != config->parts.end(),
              "Missing fracture visual: " + part.id);
      require(mappedVisuals.insert(mapping->second).second,
              "Fracture visuals must be unique");
      const auto *visual = findEntity(world, mapping->second);
      const auto *local =
          visual ? visual->component<Transform3DComponent>() : nullptr;
      require(visual && !visual->persistent && local &&
                  local->parent == root.id &&
                  visual->hasComponent<MeshRendererComponent>() &&
                  !colliderPresent(*visual) &&
                  !visual->hasComponent<Rigidbody3DComponent>(),
              "Fracture visuals must be non-persistent direct renderer "
              "children without "
              "physics bodies");
    }
    require(
        body->bodyType ==
            (asset->fracture->anchors.empty() ? "dynamic" : "static"),
        "Anchored destructibles must start static; unanchored destructibles "
        "must start dynamic");
    std::string error;
    auto family = createColliderFractureFamily3D(*asset, error);
    require(bool(family), error);
    assembly.asset = *asset;
    assembly.sourceAsset = collider->asset;
    assembly.visuals = config->parts;
    for (const auto &chunk : family->chunks())
      assembly.totalVolume += chunk.volume;
    require(std::isfinite(assembly.totalVolume) && assembly.totalVolume > 0,
            "Invalid fracture volume");
    assembly.groups.push_back({family->groups().front(), root.id});
    assembly.family = std::move(family);
    assembly.state.status = "ready";
    assembly.state.bodies = 1;
  }

  bool apply(World &world, PhysicsWorld3D &physics, Assembly &assembly) {
    const auto rootId = assembly.state.root;
    auto *root = findEntity(world, rootId);
    auto *config = root ? root->component<Destructible3DComponent>() : nullptr;
    require(config && config->parts == assembly.visuals,
            "Fracture mapping changed; reload the scene");
    if (assembly.groups.size() == 1 && assembly.groups[0].body == rootId) {
      const auto *collider = root->component<ModelCollider3DComponent>();
      const auto *source = resolvedColliderAsset3D(world, *root);
      require(
          collider && collider->asset == assembly.sourceAsset &&
              source && source->revision == assembly.asset.revision,
          "Fracture source changed; reload the scene before applying damage");
    }
    std::vector<BondDamage3D> commands;
    for (const auto &[id, damage] : assembly.pending)
      commands.push_back({id, damage});
    std::vector<AnchorDamage3D> anchors;
    for (const auto &[id, damage] : assembly.pendingAnchors)
      anchors.push_back({id, damage});
    const auto token = assembly.family->stage(commands, anchors);
    try {
      const auto &partition = assembly.family->stagedGroups(token);
      require(partition.size() <= static_cast<std::size_t>(config->maxBodies),
              "Destruction max_bodies budget exceeded");
      if (partition == assembly.family->groups()) {
        require(assembly.family->commit(token), "Stale destruction token");
        return false;
      }
      World candidate;
      candidate.entities = world.entities;
      candidate.colliderAssets3D = world.colliderAssets3D;
      std::vector<GroupBody> nextGroups;
      std::vector<std::string> sources;
      std::vector<ReplacementBody3D> replacements;
      auto privateAssets = assembly.privateAssets;
      std::map<std::string, std::string> parentForPart;
      for (const auto &old : assembly.groups) {
        const auto unchanged = std::ranges::find(partition, old.group);
        if (unchanged != partition.end())
          continue;
        const auto *entity = findEntity(world, old.body);
        const auto *transform =
            entity ? entity->component<Transform3DComponent>() : nullptr;
        require(entity && transform && transform->parent.empty(),
                "Fracture source transform unavailable");
        for (const auto &part : old.group.chunks) {
          auto *visual = findEntity(world, assembly.visuals.at(part));
          require(visual && visual->component<Transform3DComponent>() &&
                      visual->component<Transform3DComponent>()->parent ==
                          old.body,
                  "Fracture visual ownership changed; reload the scene");
        }
        sources.push_back(old.body);
        if (old.body == rootId) {
          auto *container = findEntity(candidate, rootId);
          container->removeComponent<Rigidbody3DComponent>();
          container->removeComponent<ModelCollider3DComponent>();
          container->serializedComponents.erase("Rigidbody3D");
          container->serializedComponents.erase("ModelCollider3D");
        } else {
          const auto *collider = entity->component<ModelCollider3DComponent>();
          require(collider != nullptr, "Fracture body lost its collider");
          candidate.colliderAssets3D.erase(collider->asset);
          privateAssets.erase(collider->asset);
          std::erase_if(candidate.entities, [&](const Entity &value) {
            return value.id == old.body;
          });
        }
      }
      for (const auto &group : partition) {
        const auto unchanged =
            std::ranges::find_if(assembly.groups, [&](const GroupBody &old) {
              return old.group == group;
            });
        if (unchanged != assembly.groups.end()) {
          nextGroups.push_back(*unchanged);
          continue;
        }
        const auto source =
            std::ranges::find_if(assembly.groups, [&](const GroupBody &old) {
              return contains(old.group, group.chunks.front());
            });
        require(source != assembly.groups.end(),
                "Fracture partition lost its physical owner");
        const auto *oldEntity = findEntity(world, source->body);
        const auto *oldTransform = oldEntity->component<Transform3DComponent>();
        const std::string id = rootId + "/fracture/" + group.chunks.front();
        const std::string assetId =
            "runtime-fracture://" + std::to_string(assembly.key) + "/" + id;
        require(!findEntity(candidate, id) &&
                    !candidate.colliderAssets3D.contains(assetId),
                "Fracture output identity collision");
        ColliderAsset3D geometry;
        geometry.resident =
            false; // Native users retain it; scene reset retires it.
        geometry.revision = assembly.family->revision() + 1;
        float volume = 0;
        for (const auto &part : assembly.asset.parts)
          if (contains(group, part.id))
            geometry.parts.push_back(part);
        for (const auto &chunk : assembly.family->chunks())
          if (contains(group, chunk.id))
            volume += chunk.volume;
        // Bounds are shared asset-space; preserve each visual's existing local
        // pose.
        Vec3 minimum{std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max()};
        Vec3 maximum{-minimum.x, -minimum.y, -minimum.z};
        for (const auto &part : geometry.parts)
          for (const auto &point : part.points) {
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
          }
        geometry.size = {maximum.x - minimum.x, maximum.y - minimum.y,
                         maximum.z - minimum.z};
        geometry.offset = {minimum.x + geometry.size.x * 0.5F,
                           minimum.y + geometry.size.y * 0.5F,
                           minimum.z + geometry.size.z * 0.5F};
        candidate.colliderAssets3D.emplace(assetId, std::move(geometry));
        privateAssets.insert(assetId);
        Entity fragment;
        fragment.id = id;
        fragment.name = group.chunks.front();
        fragment.sceneOwner = oldEntity->sceneOwner;
        fragment.layer = oldEntity->layer;
        fragment.persistent = oldEntity->persistent;
        fragment.setComponent(Transform3DComponent(*oldTransform));
        fragment.setComponent(ModelCollider3DComponent{
            .asset = assetId,
            .layer = oldEntity->component<ModelCollider3DComponent>()->layer});
        require(oldEntity->component<Rigidbody3DComponent>() != nullptr,
                "Fracture source lost its rigidbody");
        auto body = *oldEntity->component<Rigidbody3DComponent>();
        require(body.bodyEnabled && body.bodyType != "kinematic",
                "Fracture source must be enabled and non-kinematic");
        float sourceVolume = 0;
        for (const auto &chunk : assembly.family->chunks())
          if (contains(source->group, chunk.id))
            sourceVolume += chunk.volume;
        body.bodyType = group.anchored ? "static" : "dynamic";
        body.mass *= volume / sourceVolume;
        body.bodyEnabled = body.awake = true;
        fragment.setComponent(std::move(body));
        fragment.setComponent(FragmentOwner{rootId, assembly.key});
        candidate.entities.push_back(std::move(fragment));
        replacements.push_back({id, source->body});
        nextGroups.push_back({group, id});
        for (const auto &part : group.chunks)
          parentForPart.emplace(part, id);
      }
      for (const auto &[part, parent] : parentForPart)
        findEntity(candidate, assembly.visuals.at(part))
            ->component<Transform3DComponent>()
            ->parent = parent;
      std::string error;
      require(
          physics.replaceBodies(world, candidate, sources, replacements, error),
          error);
      // Single-thread ownership guarantees this token is still current.
      (void)assembly.family->commit(token);
      assembly.groups.swap(nextGroups);
      assembly.privateAssets.swap(privateAssets);
      return true;
    } catch (...) {
      (void)assembly.family->discard(token);
      throw;
    }
  }
};

DestructionWorld3D::DestructionWorld3D() : impl_(std::make_unique<Impl>()) {}
DestructionWorld3D::~DestructionWorld3D() = default;
void DestructionWorld3D::prune(World &world) {
  for (auto it = impl_->assemblies.begin(); it != impl_->assemblies.end();) {
    const auto *root = findEntity(world, it->first);
    const auto *config =
        root ? root->component<Destructible3DComponent>() : nullptr;
    if (root && root->enabled && config &&
        config->attachmentKey == it->second.key) {
      ++it;
      continue;
    }
    const auto key = it->second.key;
    std::set<std::string> owned;
    for (const auto &entity : world.entities)
      if (const auto *owner = entity.component<FragmentOwner>();
          owner && owner->key == key)
        owned.insert(entity.id);
    for (std::size_t pass = 0; pass < world.entities.size(); ++pass) {
      const auto before = owned.size();
      for (const auto &entity : world.entities)
        if (const auto *transform = entity.component<Transform3DComponent>();
            transform && owned.contains(transform->parent))
          owned.insert(entity.id);
      if (owned.size() == before)
        break;
    }
    std::erase_if(world.entities, [&](const Entity &entity) {
      if (owned.contains(entity.id))
        return true;
      const auto *transform = entity.component<Transform3DComponent>();
      return transform && owned.contains(transform->parent);
    });
    for (const auto &id : it->second.privateAssets)
      world.colliderAssets3D.erase(id);
    it = impl_->assemblies.erase(it);
  }
}
bool DestructionWorld3D::update(World &world, PhysicsWorld3D &physics) {
  for (auto &entity : world.entities) {
    if (!entity.enabled || !entity.hasComponent<Destructible3DComponent>() ||
        impl_->assemblies.contains(entity.id))
      continue;
    auto &assembly = impl_->assemblies[entity.id];
    try {
      impl_->attach(world, entity, assembly);
    } catch (const std::exception &error) {
      assembly.state.error = error.what();
    }
  }
  bool changed = false;
  // One family per step bounds commits; this is not the final time-budgeted
  // scheduler.
  for (auto &[id, assembly] : impl_->assemblies) {
    if (!assembly.family ||
        (assembly.pending.empty() && assembly.pendingAnchors.empty()))
      continue;
    try {
      changed = impl_->apply(world, physics, assembly);
      assembly.state.status = "applied";
      assembly.state.error.clear();
      assembly.state.revision = assembly.family->revision();
      assembly.state.bodies = assembly.groups.size();
    } catch (const std::exception &error) {
      assembly.state.status = "failed";
      assembly.state.error = error.what();
    }
    assembly.pending.clear();
    assembly.pendingAnchors.clear();
    break;
  }
  return changed;
}
bool DestructionWorld3D::damagePart(const std::string &entity,
                                    const std::string &part, float amount,
                                    std::string &error) {
  error.clear();
  auto *assembly = impl_->find(entity);
  if (!assembly || !assembly->family) {
    error = assembly ? assembly->state.error : "No attached destructible";
    return false;
  }
  if (!std::isfinite(amount) || amount <= 0 ||
      !assembly->visuals.contains(part)) {
    error = "Damage requires an existing part and a finite positive amount";
    return false;
  }
  const auto owner =
      std::ranges::find_if(assembly->groups, [&](const GroupBody &group) {
        return contains(group.group, part);
      });
  if (owner == assembly->groups.end() ||
      (entity != assembly->state.root && entity != owner->body)) {
    error = "Part does not belong to this body";
    return false;
  }
  auto pending = assembly->pending;
  auto pendingAnchors = assembly->pendingAnchors;
  if (assembly->family->anchored(part)) {
    const float sum = pendingAnchors[part] + amount;
    if (!std::isfinite(sum)) {
      error = "Damage batch overflow";
      return false;
    }
    pendingAnchors[part] = sum;
  }
  for (const auto &bond : assembly->asset.fracture->bonds) {
    if ((bond.firstPart == part || bond.secondPart == part) &&
        contains(owner->group, bond.firstPart) &&
        contains(owner->group, bond.secondPart)) {
      const float sum = pending[bond.id] + amount;
      if (!std::isfinite(sum)) {
        error = "Damage batch overflow";
        return false;
      }
      pending[bond.id] = sum;
    }
  }
  if (pending == assembly->pending && pendingAnchors == assembly->pendingAnchors) {
    error = "Part is already detached; no bonds or foundation attachment remain";
    return false;
  }
  assembly->pending.swap(pending);
  assembly->pendingAnchors.swap(pendingAnchors);
  assembly->state.status = "queued";
  assembly->state.error.clear();
  return true;
}
DestructionState3D DestructionWorld3D::state(const std::string &entity) const {
  const auto *assembly = impl_->find(entity);
  if (!assembly)
    return {.root = {}, .status = "unattached", .error = {}, .parts = {}};
  auto result = assembly->state;
  for (const auto &group : assembly->groups)
    for (const auto &part : group.group.chunks)
      result.parts.emplace(part, group.body);
  return result;
}
} // namespace demi::runtime
