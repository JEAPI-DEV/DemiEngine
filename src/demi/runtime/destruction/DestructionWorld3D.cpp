#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/destruction/DeferredFractureVisuals3D.h"
#include "demi/runtime/destruction/ColliderFractureFamily3D.h"
#include "demi/runtime/destruction/DestructionCheckpoint3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
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
  bool retired = false;
};
struct PartImpulse {
  std::string part;
  Vec3 point; // Assembly-local while queued; world-space when prepared.
  Vec3 impulse;
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
    std::string geometryHash;
    Vec3 scale;
    std::string sourceAsset;
    std::unique_ptr<BlastFamily3D> family;
    std::map<std::string, std::string> visuals;
    DeferredFractureVisuals3D deferred;
    std::vector<GroupBody> groups;
    std::map<std::string, float> pending;
    std::map<std::string, float> pendingAnchors;
    std::vector<PartImpulse> pendingImpulses;
    DestructionState3D state;
    float totalVolume = 0;
    std::map<std::string, double> massWeights;
    std::map<std::string,float> appliedBonds, appliedAnchors;
    std::optional<DestructionCheckpoint3D> restore;
    bool retireRequested = false;
    std::set<std::string> privateAssets;
  };
  std::map<std::string, Assembly> assemblies;
  std::uint64_t nextKey = 1;

  Assembly *find(const std::string &entity) {
    if (entity.empty()) return nullptr;
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
    assembly.deferred.configure(world,root,*config);
    for (const auto &part : asset->parts) {
      const auto mapping = config->parts.find(part.id);
      require(mapping != config->parts.end(),
              "Missing fracture visual: " + part.id);
      require(mappedVisuals.insert(mapping->second).second,
              "Fracture visuals must be unique");
      const auto *visual = findEntity(world, mapping->second);
      if(!visual && !assembly.deferred.dormantOwner(mapping->second).empty())continue;
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
    assembly.scale=transform->scale;
    assembly.geometryHash=destructionGeometryHash(*asset)+":"+
        nlohmann::json({transform->scale.x,transform->scale.y,transform->scale.z,body->mass}).dump();
    assembly.sourceAsset = collider->asset;
    assembly.visuals = config->parts;
    for (const auto &chunk : family->chunks()) {
      assembly.totalVolume += chunk.volume;
      const auto part =
          std::ranges::find(asset->parts, chunk.id, &ColliderPart3D::id);
      require(part != asset->parts.end() && std::isfinite(part->density) &&
                  part->density >= 0.001F && part->density <= 1000000,
              "Invalid fracture density");
      assembly.massWeights.emplace(chunk.id,
                                   double(chunk.volume) * part->density);
    }
    require(std::isfinite(assembly.totalVolume) && assembly.totalVolume > 0,
            "Invalid fracture volume");
    assembly.groups.push_back({family->groups().front(), root.id});
    assembly.family = std::move(family);
    assembly.state.status = "ready";
    assembly.state.bodies = 1;
  }

  bool retire(World &world, PhysicsWorld3D &physics, Assembly &assembly) {
    std::vector<std::string> sources;
    std::set<std::string> removed;
    for (const auto &group : assembly.groups)
      if (!group.retired && !group.group.anchored &&
          group.body != assembly.state.root) {
        sources.push_back(group.body);
        removed.insert(group.body);
      }
    if (sources.empty())
      return false;
    World candidate;
    candidate.entities = world.entities;
    candidate.colliderAssets3D = world.colliderAssets3D;
    auto privateAssets = assembly.privateAssets;
    for (const auto &id : sources) {
      const auto *entity = findEntity(world, id);
      require(entity && entity->component<ModelCollider3DComponent>(),
              "Retired body unavailable");
      const auto asset = entity->component<ModelCollider3DComponent>()->asset;
      candidate.colliderAssets3D.erase(asset);
      privateAssets.erase(asset);
    }
    for (;;) {
      const auto before = removed.size();
      for (const auto &entity : candidate.entities) {
        const auto *transform = entity.component<Transform3DComponent>();
        if (transform && removed.contains(transform->parent))
          removed.insert(entity.id);
      }
      if (removed.size() == before)
        break;
    }
    std::erase_if(candidate.entities, [&](const Entity &entity) {
      return removed.contains(entity.id);
    });
    std::string error;
    require(physics.replaceBodies(world, candidate, sources, {}, error), error);
    for (auto &group : assembly.groups)
      if (removed.contains(group.body)) {
        group.retired = true;
        group.body.clear();
      }
    assembly.privateAssets.swap(privateAssets);
    return true;
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
          collider && collider->asset == assembly.sourceAsset && source &&
              source->revision == assembly.asset.revision,
          "Fracture source changed; reload the scene before applying damage");
    }
    std::vector<BondDamage3D> commands;
    for (const auto &[id, damage] : assembly.pending)
      commands.push_back({id, damage});
    std::vector<AnchorDamage3D> anchors;
    for (const auto &[id, damage] : assembly.pendingAnchors)
      anchors.push_back({id, damage});
    if (commands.empty() && anchors.empty() && !assembly.restore)
      return false;
    const auto token = commands.empty() && anchors.empty() ? 0 : assembly.family->stage(commands, anchors);
    try {
      const auto &partition = token ? assembly.family->stagedGroups(token) : assembly.family->groups();
      if (assembly.restore) {
        require(assembly.restore->groups.size()==partition.size(),"Checkpoint partition mismatch");
        for (const auto &group:partition) {
          auto parts=group.chunks; std::ranges::sort(parts);
          const auto saved=std::ranges::find(assembly.restore->groups,parts,&DestructionGroupCheckpoint3D::parts);
          require(saved!=assembly.restore->groups.end() && !(group.anchored && saved->retired),
                  "Checkpoint partition/support mismatch");
        }
      }
      const auto liveGroups=std::ranges::count_if(partition,[&](const DestructionGroup3D &group) {
        if (assembly.restore) {
          auto parts=group.chunks; std::ranges::sort(parts);
          return !std::ranges::find(assembly.restore->groups,parts,&DestructionGroupCheckpoint3D::parts)->retired;
        }
        return std::ranges::none_of(assembly.groups,[&](const GroupBody &old){return old.retired && old.group==group;});
      });
      require(liveGroups <= config->maxBodies,
              "Destruction max_bodies budget exceeded");
      if (!assembly.restore && partition == assembly.family->groups()) {
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
        if (unchanged != partition.end() && !assembly.restore)
          continue;
        const auto *entity = findEntity(world, old.body);
        const auto *transform =
            entity ? entity->component<Transform3DComponent>() : nullptr;
        require(entity && transform && transform->parent.empty(),
                "Fracture source transform unavailable");
        for (const auto &part : old.group.chunks) {
          auto *visual = findEntity(world, assembly.visuals.at(part));
          if(!visual) {
            const auto owner=assembly.deferred.dormantOwner(assembly.visuals.at(part));
            visual=findEntity(world,owner);
          }
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
        if (unchanged != assembly.groups.end() && !assembly.restore) {
          nextGroups.push_back(*unchanged);
          continue;
        }
        const auto source =
            std::ranges::find_if(assembly.groups, [&](const GroupBody &old) {
              return contains(old.group, group.chunks.front());
            });
        require(source != assembly.groups.end(),
                "Fracture partition lost its physical owner");
        const DestructionGroupCheckpoint3D *saved = nullptr;
        if (assembly.restore) {
          auto parts=group.chunks; std::ranges::sort(parts);
          saved=&*std::ranges::find(assembly.restore->groups,parts,&DestructionGroupCheckpoint3D::parts);
        }
        if (saved && saved->retired) {
          nextGroups.push_back({group,{},true});
          for (const auto &part:group.chunks) {
            const auto visual=assembly.visuals.at(part);
            std::erase_if(candidate.entities,[&](const Entity &entity) {
              const auto *t=entity.component<Transform3DComponent>();
              return entity.id==visual || (t && t->parent==visual);
            });
          }
          continue;
        }
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
        double massWeight = 0;
        for (const auto &part : assembly.asset.parts)
          if (contains(group, part.id))
            geometry.parts.push_back(part);
        for (const auto &chunk : assembly.family->chunks())
          if (contains(group, chunk.id))
            massWeight += assembly.massWeights.at(chunk.id);
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
        double sourceMassWeight = 0;
        for (const auto &chunk : assembly.family->chunks())
          if (contains(source->group, chunk.id))
            sourceMassWeight += assembly.massWeights.at(chunk.id);
        body.bodyType = group.anchored ? "static" : "dynamic";
        body.mass *= massWeight / sourceMassWeight;
        body.bodyEnabled = body.awake = true;
        if (saved) {
          auto *transform=fragment.component<Transform3DComponent>();
          transform->position=saved->position; transform->rotation=saved->rotation;
          body.velocity=saved->velocity; body.angularVelocity=saved->angularVelocity;
        }
        fragment.setComponent(std::move(body));
        fragment.setComponent(FragmentOwner{rootId, assembly.key});
        candidate.entities.push_back(std::move(fragment));
        replacements.push_back({id, source->body, saved!=nullptr});
        nextGroups.push_back({group, id});
        for (const auto &part : group.chunks)
          parentForPart.emplace(part, id);
      }
      std::map<std::string,std::string> visualOwners;
      for(const auto &group:nextGroups)
        for(const auto &part:group.group.chunks)
          visualOwners[assembly.visuals.at(part)]=group.retired?std::string{}:group.body;
      auto refined=assembly.deferred.prepare(candidate,visualOwners);
      for (const auto &[part, parent] : parentForPart)
        if(auto *visual=findEntity(candidate,assembly.visuals.at(part)))
          visual->component<Transform3DComponent>()->parent = parent;
      std::string error;
      require(
          physics.replaceBodies(world, candidate, sources, replacements, error),
          error);
      // Single-thread ownership guarantees this token is still current.
      if (token) (void)assembly.family->commit(token);
      assembly.deferred.commit(std::move(refined));
      assembly.groups.swap(nextGroups);
      assembly.privateAssets.swap(privateAssets);
      return true;
    } catch (...) {
      if (token) (void)assembly.family->discard(token);
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
        (assembly.pending.empty() && assembly.pendingAnchors.empty() &&
         assembly.pendingImpulses.empty() && !assembly.restore && !assembly.retireRequested))
      continue;
    try {
      require(assembly.state.revision != UINT64_MAX,
              "Destruction revision exhausted");
      auto impulses = assembly.pendingImpulses;
      for (auto &impulse : impulses) {
        const auto group =
            std::ranges::find_if(assembly.groups, [&](const GroupBody &g) {
              return contains(g.group, impulse.part);
            });
        require(group != assembly.groups.end(), "Impact part lost its owner");
        const auto *body = findEntity(world, group->body);
        const auto *transform =
            body ? body->component<Transform3DComponent>() : nullptr;
        const auto *rigidbody =
            body ? body->component<Rigidbody3DComponent>() : nullptr;
        require(transform && transform->parent.empty() && rigidbody &&
                    rigidbody->bodyEnabled &&
                    rigidbody->bodyType ==
                        (group->group.anchored ? "static" : "dynamic") &&
                    physics.velocity(group->body).has_value(),
                "Impact body is unavailable");
        impulse.point = transformPoint3D(
            {transform->position, transform->rotation, transform->scale},
            impulse.point);
        require(std::isfinite(impulse.point.x) &&
                    std::isfinite(impulse.point.y) &&
                    std::isfinite(impulse.point.z),
                "Impact point overflow");
      }
      changed = impl_->apply(world, physics, assembly);
      const auto record=[](auto &applied,const auto &pending) {
        for (const auto &[id,amount]:pending)
          applied[id]=float(std::min(1e30,double(applied[id])+amount));
      };
      record(assembly.appliedBonds,assembly.pending);
      record(assembly.appliedAnchors,assembly.pendingAnchors);
      // No impulse is applied until the entire family's topology proposal
      // commits.
      for (const auto &impulse : impulses) {
        const auto group =
            std::ranges::find_if(assembly.groups, [&](const GroupBody &g) {
              return contains(g.group, impulse.part);
            });
        if (group != assembly.groups.end() && !group->group.anchored)
          (void)physics.addImpulseAtPosition(group->body, impulse.impulse,
                                             impulse.point);
      }
      if (assembly.retireRequested)
        changed = impl_->retire(world,physics,assembly) || changed;
      assembly.state.status = "applied";
      assembly.state.error.clear();
      ++assembly.state.revision;
      assembly.state.bodies = std::ranges::count_if(assembly.groups,[](const GroupBody &g){return !g.retired;});
    } catch (const std::exception &error) {
      assembly.state.status = "failed";
      assembly.state.error = error.what();
    }
    assembly.pending.clear();
    assembly.pendingAnchors.clear();
    assembly.pendingImpulses.clear();
    assembly.restore.reset();
    assembly.retireRequested=false;
    break;
  }
  return changed;
}
bool DestructionWorld3D::damagePart(const std::string &entity,
                                    const std::string &part, float amount,
                                    std::string &error) {
  error.clear();
  auto *assembly = impl_->find(entity);
  if (!assembly || !assembly->family || assembly->restore) {
    error = assembly && assembly->restore ? "Destruction is restoring a checkpoint" :
        (assembly ? assembly->state.error : "No attached destructible");
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
    if (assembly->family->bondIntact(bond.id) &&
        (bond.firstPart == part || bond.secondPart == part) &&
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
  if (pending == assembly->pending &&
      pendingAnchors == assembly->pendingAnchors) {
    error =
        "Part is already detached; no bonds or foundation attachment remain";
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
    if (!group.retired)
    for (const auto &part : group.group.chunks)
      result.parts.emplace(part, group.body);
  return result;
}

nlohmann::json DestructionWorld3D::checkpoint(const World &world,
                                              const std::string &entity,
                                              std::string &error) const {
  error.clear();
  try {
    const auto *assembly = impl_->find(entity);
    require(assembly && assembly->family, "No attached destructible");
    require(assembly->pending.empty() && assembly->pendingAnchors.empty() &&
                assembly->pendingImpulses.empty() && !assembly->restore &&
                !assembly->retireRequested,
            "Wait for queued destruction before checkpointing");
    DestructionCheckpoint3D saved;
    saved.geometryHash = assembly->geometryHash;
    saved.bonds = assembly->appliedBonds;
    saved.anchors = assembly->appliedAnchors;
    for (const auto &group : assembly->groups) {
      DestructionGroupCheckpoint3D item;
      item.parts = group.group.chunks;
      std::ranges::sort(item.parts);
      item.retired = group.retired;
      if (!item.retired) {
        const auto *body = findEntity(world, group.body);
        require(body && body->component<Transform3DComponent>() &&
                    body->component<Rigidbody3DComponent>(),
                "Checkpoint body unavailable");
        const auto *t = body->component<Transform3DComponent>();
        require(t->scale.x==assembly->scale.x && t->scale.y==assembly->scale.y &&
                    t->scale.z==assembly->scale.z,"Reload the scene after changing destructible scale");
        const auto *b = body->component<Rigidbody3DComponent>();
        item.position = t->position;
        item.rotation = t->rotation;
        item.velocity = b->velocity;
        item.angularVelocity = b->angularVelocity;
      }
      saved.groups.push_back(std::move(item));
    }
    return writeDestructionCheckpoint(saved);
  } catch (const std::exception &exception) {
    error = exception.what();
    return nullptr;
  }
}
bool DestructionWorld3D::restore(const std::string &entity,
                                 const nlohmann::json &json,
                                 std::string &error) {
  error.clear();
  try {
    auto *assembly = impl_->find(entity);
    require(assembly && assembly->family && assembly->state.revision == 0 &&
                assembly->pending.empty() && assembly->pendingAnchors.empty() &&
                assembly->pendingImpulses.empty() && !assembly->restore &&
                !assembly->retireRequested,
            "Restore requires a fresh attached assembly without queued damage");
    auto saved = readDestructionCheckpoint(json, assembly->asset, assembly->geometryHash);
    assembly->pending = saved.bonds;
    assembly->pendingAnchors = saved.anchors;
    assembly->restore = std::move(saved);
    assembly->state.status = "queued";
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}
bool DestructionWorld3D::retireDebris(const std::string &entity,
                                      std::string &error) {
  error.clear();
  auto *assembly = impl_->find(entity);
  if (!assembly || !assembly->family || assembly->restore) {
    error = "No ready destructible for debris cleanup";
    return false;
  }
  assembly->retireRequested = true;
  assembly->state.status = "queued";
  return true;
}

bool DestructionWorld3D::impact(World &world, PhysicsWorld3D &physics,
                                const DestructionImpact3D &hit,
                                std::size_t &affectedAssemblies,
                                std::string &error) {
  ProfileScope scope("Destruction3D.impact");
  affectedAssemblies = 0;
  if (!validateDestructionImpact3D(hit, error))
    return false;
  auto *target = hit.entity.empty() ? nullptr : impl_->find(hit.entity);
  if (!hit.entity.empty() && (!target || !target->family)) {
    error = "Impact target is not an attached destructible";
    return false;
  }
  try {
    struct Contact {
      PhysicsQueryHit3D hit;
      float weight;
    };
    struct Proposal {
      std::string status = "queued";
      Impl::Assembly *assembly = nullptr;
      float energyPerHealth = 1000;
      std::map<std::string, Contact> contacts;
      std::map<std::string, float> bondWeights, anchorWeights;
      std::map<std::string, float> bonds, anchors;
      std::vector<PartImpulse> impulses;
    };
    std::map<std::string, Proposal> proposals;
    double impulseWeight = 0, energyWeight = 0;
    const auto contacts =
        physics.overlapSphere(hit.position, hit.radius, {}, {}, true);
    require(contacts.size() <= 8192,
            "Impact query exceeds 8192 collider parts; reduce radius");
    for (const auto &contact : contacts) {
      if (contact.colliderPartId.empty() || contact.isTrigger)
        continue;
      auto *assembly = impl_->find(contact.entityId);
      if (!assembly || !assembly->family || (target && target != assembly))
        continue;
      require(!assembly->restore,"Impact target is restoring a checkpoint");
      const auto *root = findEntity(world, assembly->state.root);
      const auto *config =
          root ? root->component<Destructible3DComponent>() : nullptr;
      if (!root || !root->enabled || !config)
        continue;
      require(std::isfinite(config->energyPerHealth) &&
                  config->energyPerHealth >= 0.000001F &&
                  config->energyPerHealth <= 1e12F,
              "Invalid energy_per_health");
      const float weight =
          destructionImpactWeight(contact.distance, hit.radius);
      if (weight <= 0)
        continue;
      auto &proposal = proposals[assembly->state.root];
      require(proposals.size() <= 32,
              "Impact exceeds 32 affected assemblies; reduce radius");
      proposal.assembly = assembly;
      proposal.energyPerHealth = config->energyPerHealth;
      if (proposal.contacts
              .emplace(contact.colliderPartId, Contact{contact, weight})
              .second)
        impulseWeight += weight;
    }
    for (auto &[root, p] : proposals) {
      const auto weight = [&](const std::string &part) {
        const auto found = p.contacts.find(part);
        return found == p.contacts.end() ? 0.0F : found->second.weight;
      };
      for (const auto &bond : p.assembly->asset.fracture->bonds) {
        if (!p.assembly->family->bondIntact(bond.id))
          continue;
        const float w =
            std::max(weight(bond.firstPart), weight(bond.secondPart));
        if (w <= 0)
          continue;
        const bool connected =
            std::ranges::any_of(p.assembly->groups, [&](const GroupBody &g) {
              return contains(g.group, bond.firstPart) &&
                     contains(g.group, bond.secondPart);
            });
        if (connected) {
          p.bondWeights.emplace(bond.id, w);
          energyWeight += w;
        }
      }
      for (const auto &[part, contact] : p.contacts)
        if (p.assembly->family->anchored(part)) {
          p.anchorWeights.emplace(part, contact.weight);
          energyWeight += contact.weight;
        }
    }
    for (auto &[root, p] : proposals) {
      p.bonds = p.assembly->pending;
      p.anchors = p.assembly->pendingAnchors;
      p.impulses = p.assembly->pendingImpulses;
      const auto accumulate = [&](auto &pending, const auto &weights) {
        if (hit.energy == 0)
          return;
        for (const auto &[id, weight] : weights) {
          const double amount = double(hit.energy) * weight /
                                std::max(1.0, energyWeight) / p.energyPerHealth;
          require(float(amount) > 0,
                  "Impact damage is below supported precision");
          const double sum = pending[id] + amount;
          require(std::isfinite(sum) &&
                      sum <= std::numeric_limits<float>::max(),
                  "Impact damage batch overflow");
          pending[id] = float(sum);
        }
      };
      accumulate(p.bonds, p.bondWeights);
      accumulate(p.anchors, p.anchorWeights);
      if (hit.impulse > 0)
        for (const auto &[part, contact] : p.contacts) {
          require(p.impulses.size() < 512,
                  "Impact queue exceeds 512 part impulses for one assembly");
          const auto *body = findEntity(world, contact.hit.entityId);
          const auto transform =
              body ? resolveWorldTransform3D(world, *body) : std::nullopt;
          require(bool(transform), "Impact body transform unavailable");
          const Vec3 point =
              inverseTransformPoint3D(*transform, contact.hit.point);
          require(std::isfinite(point.x) && std::isfinite(point.y) &&
                      std::isfinite(point.z),
                  "Impact local point overflow");
          const float amount = float(double(hit.impulse) * contact.weight /
                                     std::max(1.0, impulseWeight));
          const auto direction =
              destructionImpactDirection(hit, contact.hit.point);
          p.impulses.push_back({part,
                                point,
                                {direction.x * amount, direction.y * amount,
                                 direction.z * amount}});
        }
    }
    // Validate and allocate every proposal before publishing any queue changes.
    for (auto &[root, p] : proposals) {
      const bool damage = hit.energy > 0 &&
                          (!p.bondWeights.empty() || !p.anchorWeights.empty());
      if (!damage && hit.impulse == 0)
        continue;
      p.assembly->pending.swap(p.bonds);
      p.assembly->pendingAnchors.swap(p.anchors);
      p.assembly->pendingImpulses.swap(p.impulses);
      p.assembly->state.status.swap(p.status);
      p.assembly->state.error.clear();
      ++affectedAssemblies;
    }
    if (affectedAssemblies == 0) {
      error = "No destructible bonds, anchors or movable fragments in impact "
              "radius";
      return false;
    }
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}
} // namespace demi::runtime
