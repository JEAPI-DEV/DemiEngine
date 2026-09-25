#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/geometry/MeshImpact3D.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/physics/JoltCompoundShape3D.h"
#include "demi/runtime/physics/JoltLifetime.h"
#include "demi/runtime/physics/JoltBodyBatch3D.h"
#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/physics/PhysicsContactPhases3D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

// Jolt.h defines platform/compiler macros required by every other Jolt header.
#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <tuple>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {
namespace {

constexpr JPH::ObjectLayer StaticLayer = 0;
constexpr JPH::ObjectLayer MovingLayer = 1;

[[nodiscard]] int physicsWorkerCount() {
  const unsigned available = std::thread::hardware_concurrency();
  if (available <= 1)
    return 0;
  return static_cast<int>(std::min(available - 1U, 8U));
}

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
  BroadPhaseLayers() {
    mapping_[StaticLayer] = JPH::BroadPhaseLayer(0);
    mapping_[MovingLayer] = JPH::BroadPhaseLayer(1);
  }
  [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
  [[nodiscard]] JPH::BroadPhaseLayer
  GetBroadPhaseLayer(const JPH::ObjectLayer layer) const override {
    return mapping_[layer];
  }

private:
  JPH::BroadPhaseLayer mapping_[2];
};

class ObjectVsBroadPhaseFilter final
    : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  [[nodiscard]] bool
  ShouldCollide(const JPH::ObjectLayer layer,
                const JPH::BroadPhaseLayer broadPhase) const override {
    return layer == MovingLayer ||
           broadPhase == JPH::BroadPhaseLayer(MovingLayer);
  }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
  [[nodiscard]] bool
  ShouldCollide(const JPH::ObjectLayer first,
                const JPH::ObjectLayer second) const override {
    return first == MovingLayer || second == MovingLayer;
  }
};

[[nodiscard]] JPH::Vec3 jolt(const Vec3 value) {
  return {value.x, value.y, value.z};
}
[[nodiscard]] Vec3 demi(const JPH::Vec3 value) {
  return {value.GetX(), value.GetY(), value.GetZ()};
}
[[nodiscard]] JPH::Quat joltRotation(const Vec3 euler) {
  return JPH::Quat::sEulerAngles(jolt(euler));
}

[[nodiscard]] bool isTrigger(const Entity &entity) {
  if (const auto *value = entity.component<BoxCollider3DComponent>())
    return value->isTrigger;
  if (const auto *value = entity.component<SphereCollider3DComponent>())
    return value->isTrigger;
  if (const auto *value = entity.component<CapsuleCollider3DComponent>())
    return value->isTrigger;
  if (const auto *value = entity.component<ConvexCollider3DComponent>())
    return value->isTrigger;
  if (const auto *value = entity.component<ModelCollider3DComponent>())
    return value->isTrigger;
  return false;
}

[[nodiscard]] bool hasCollider(const Entity &entity) {
  return entity.hasComponent<BoxCollider3DComponent>() ||
         entity.hasComponent<SphereCollider3DComponent>() ||
         entity.hasComponent<CapsuleCollider3DComponent>() ||
         entity.hasComponent<ConvexCollider3DComponent>() ||
         entity.hasComponent<ModelCollider3DComponent>();
}

[[nodiscard]] bool reportsContacts(const Entity &entity) {
  const auto *body = entity.component<Rigidbody3DComponent>();
  return body == nullptr || body->reportContacts;
}

[[nodiscard]] bool hasCharacterCollider(const Entity &entity) {
  return entity.hasComponent<BoxCollider3DComponent>() ||
         entity.hasComponent<SphereCollider3DComponent>() ||
         entity.hasComponent<CapsuleCollider3DComponent>() ||
         entity.hasComponent<ConvexCollider3DComponent>();
}

[[nodiscard]] std::string colliderLayer(const Entity &entity) {
  if (const auto *value = entity.component<BoxCollider3DComponent>())
    return value->layer;
  if (const auto *value = entity.component<SphereCollider3DComponent>())
    return value->layer;
  if (const auto *value = entity.component<CapsuleCollider3DComponent>())
    return value->layer;
  if (const auto *value = entity.component<ConvexCollider3DComponent>())
    return value->layer;
  if (const auto *value = entity.component<ModelCollider3DComponent>())
    return value->layer;
  return {};
}

[[nodiscard]] bool layersCollide(const World &world, const Entity &first,
                                 const Entity &second) {
  const std::string firstLayer = colliderLayer(first);
  const std::string secondLayer = colliderLayer(second);
  if (firstLayer.empty() || secondLayer.empty())
    return true;
  const auto firstCategory = world.physicsCategoryBits.find(firstLayer);
  const auto secondCategory = world.physicsCategoryBits.find(secondLayer);
  const auto firstMask = world.physicsMaskBits.find(firstLayer);
  const auto secondMask = world.physicsMaskBits.find(secondLayer);
  if (firstCategory == world.physicsCategoryBits.end() ||
      secondCategory == world.physicsCategoryBits.end() ||
      firstMask == world.physicsMaskBits.end() ||
      secondMask == world.physicsMaskBits.end())
    return true;
  return (firstMask->second & secondCategory->second) != 0 &&
         (secondMask->second & firstCategory->second) != 0;
}

class CharacterBodyFilter final : public JPH::BodyFilter {
public:
  CharacterBodyFilter(
      const World &world, const Entity &character,
      const std::unordered_map<std::uint32_t, std::string> &bodyIds)
      : world_(world), character_(character), bodyIds_(bodyIds) {}

  bool ShouldCollide(const JPH::BodyID &body) const override {
    const auto found = bodyIds_.find(body.GetIndexAndSequenceNumber());
    if (found == bodyIds_.end())
      return false;
    const Entity *other = findEntity(world_, found->second);
    return other != nullptr && !isTrigger(*other) &&
           layersCollide(world_, character_, *other);
  }

private:
  const World &world_;
  const Entity &character_;
  const std::unordered_map<std::uint32_t, std::string> &bodyIds_;
};

[[nodiscard]] JPH::EAllowedDOFs allowedDofs(const Rigidbody3DComponent &body) {
  JPH::EAllowedDOFs result = JPH::EAllowedDOFs::None;
  if (!body.lockPositionX)
    result |= JPH::EAllowedDOFs::TranslationX;
  if (!body.lockPositionY)
    result |= JPH::EAllowedDOFs::TranslationY;
  if (!body.lockPositionZ)
    result |= JPH::EAllowedDOFs::TranslationZ;
  if (!body.lockRotationX)
    result |= JPH::EAllowedDOFs::RotationX;
  if (!body.lockRotationY)
    result |= JPH::EAllowedDOFs::RotationY;
  if (!body.lockRotationZ)
    result |= JPH::EAllowedDOFs::RotationZ;
  return result == JPH::EAllowedDOFs::None ? JPH::EAllowedDOFs::All : result;
}

[[nodiscard]] JPH::EMotionType motionType(const Rigidbody3DComponent *body) {
  if (body == nullptr || body->bodyType == "static")
    return JPH::EMotionType::Static;
  if (body->bodyType == "kinematic")
    return JPH::EMotionType::Kinematic;
  return JPH::EMotionType::Dynamic;
}

void mix(std::uint64_t &hash, const std::uint64_t value) {
  hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
}
void mix(std::uint64_t &hash, const float value) {
  mix(hash, static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value)));
}
void mix(std::uint64_t &hash, const bool value) {
  mix(hash, static_cast<std::uint64_t>(value));
}

[[nodiscard]] std::uint64_t shapeSignature(const World &world,
                                           const Entity &entity) {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  const auto transform = resolveWorldTransform3D(world, entity);
  if (transform) {
    mix(hash, transform->scale.x);
    mix(hash, transform->scale.y);
    mix(hash, transform->scale.z);
  }
  if (const auto *value = entity.component<BoxCollider3DComponent>()) {
    mix(hash, std::uint64_t{1});
    for (float number : {value->size.x, value->size.y, value->size.z,
                         value->offset.x, value->offset.y, value->offset.z})
      mix(hash, number);
    mix(hash, value->isTrigger);
  } else if (const auto *value =
                 entity.component<SphereCollider3DComponent>()) {
    mix(hash, std::uint64_t{2});
    mix(hash, value->radius);
    mix(hash, value->isTrigger);
  } else if (const auto *value =
                 entity.component<CapsuleCollider3DComponent>()) {
    mix(hash, std::uint64_t{3});
    mix(hash, value->radius);
    mix(hash, value->height);
    mix(hash, value->isTrigger);
  } else if (const auto *value =
                 entity.component<ConvexCollider3DComponent>()) {
    mix(hash, std::uint64_t{4});
    for (const Vec3 point : value->points)
      for (float number : {point.x, point.y, point.z})
        mix(hash, number);
    mix(hash, value->isTrigger);
  } else if (const auto *value = entity.component<ModelCollider3DComponent>()) {
    mix(hash, std::uint64_t{5});
    mix(hash, std::hash<std::string>{}(value->asset));
    if (value->inlineGeometry) mix(hash, value->inlineGeometry->revision);
    mix(hash, value->isTrigger);
    if (const auto asset = world.colliderAssets3D.find(value->asset);
        asset != world.colliderAssets3D.end())
      mix(hash, asset->second.revision);
  }
  if (const auto *body = entity.component<Rigidbody3DComponent>()) {
    mix(hash, std::hash<std::string>{}(body->bodyType));
    for (bool value :
         {body->lockPositionX, body->lockPositionY, body->lockPositionZ,
          body->lockRotationX, body->lockRotationY, body->lockRotationZ})
      mix(hash, value);
    for (float value : {body->mass, body->linearDamping, body->angularDamping,
                        body->friction, body->restitution, body->gravityScale})
      mix(hash, value);
    mix(hash, body->allowSleep);
    mix(hash, static_cast<std::uint64_t>(body->solverVelocitySteps));
    mix(hash, static_cast<std::uint64_t>(body->solverPositionSteps));
  }
  return hash;
}

[[nodiscard]] JPH::ShapeRefC shapeFor(const World &world,
                                      const Entity &entity) {
  const auto transform = resolveWorldTransform3D(world, entity);
  if (!transform)
    return {};
  const Vec3 scale{std::abs(transform->scale.x), std::abs(transform->scale.y),
                   std::abs(transform->scale.z)};
  JPH::ShapeRefC shape;
  Vec3 offset;
  if (const auto *box = entity.component<BoxCollider3DComponent>()) {
    const JPH::Vec3 half(std::max(box->size.x * scale.x * 0.5F, 0.001F),
                         std::max(box->size.y * scale.y * 0.5F, 0.001F),
                         std::max(box->size.z * scale.z * 0.5F, 0.001F));
    shape = new JPH::BoxShape(half);
    offset = {box->offset.x * scale.x, box->offset.y * scale.y,
              box->offset.z * scale.z};
  } else if (const auto *sphere =
                 entity.component<SphereCollider3DComponent>()) {
    const float radius = sphere->radius * std::max({scale.x, scale.y, scale.z});
    shape = new JPH::SphereShape(std::max(radius, 0.001F));
    offset = {sphere->offset.x * scale.x, sphere->offset.y * scale.y,
              sphere->offset.z * scale.z};
  } else if (const auto *capsule =
                 entity.component<CapsuleCollider3DComponent>()) {
    const float radius = capsule->radius * std::max(scale.x, scale.z);
    const float totalHeight = capsule->height * scale.y;
    shape = new JPH::CapsuleShape(std::max(totalHeight * 0.5F - radius, 0.0F),
                                  std::max(radius, 0.001F));
    offset = {capsule->offset.x * scale.x, capsule->offset.y * scale.y,
              capsule->offset.z * scale.z};
  } else if (const auto *convex = entity.component<ConvexCollider3DComponent>();
             (convex != nullptr && convex->points.size() >= 4) ||
             resolvedConvexCollider3D(world, entity) != nullptr) {
    const auto &sourcePoints =
        convex ? convex->points : *resolvedConvexCollider3D(world, entity);
    JPH::Array<JPH::Vec3> points;
    points.reserve(sourcePoints.size());
    for (const Vec3 point : sourcePoints)
      points.emplace_back(point.x * scale.x, point.y * scale.y,
                          point.z * scale.z);
    JPH::ConvexHullShapeSettings settings(points);
    const auto result = settings.Create();
    if (result.HasError())
      return {};
    shape = result.Get();
    if (convex)
      offset = {convex->offset.x * scale.x, convex->offset.y * scale.y,
                convex->offset.z * scale.z};
  } else if (const auto *model = entity.component<ModelCollider3DComponent>()) {
    if (const auto *parts = resolvedCompoundCollider3D(world, entity))
      return createJoltCompoundShape3D(*parts, transform->scale);
    const auto triangles = resolvedTriangleCollider3D(world, entity);
    if (triangles != nullptr && !triangles->empty()) {
      JPH::TriangleList list;
      list.reserve(triangles->size());
      for (const TriangleCollider3D &triangle : *triangles)
        list.emplace_back(
            JPH::Vec3(triangle.a.x * scale.x, triangle.a.y * scale.y,
                      triangle.a.z * scale.z),
            JPH::Vec3(triangle.b.x * scale.x, triangle.b.y * scale.y,
                      triangle.b.z * scale.z),
            JPH::Vec3(triangle.c.x * scale.x, triangle.c.y * scale.y,
                      triangle.c.z * scale.z));
      JPH::MeshShapeSettings settings(list);
      const auto result = settings.Create();
      if (result.HasError())
        return {};
      shape = result.Get();
    } else if (const auto box = resolvedBoxCollider3D(world, entity)) {
      shape = new JPH::BoxShape(
          JPH::Vec3(std::max(box->size.x * scale.x * 0.5F, 0.001F),
                    std::max(box->size.y * scale.y * 0.5F, 0.001F),
                    std::max(box->size.z * scale.z * 0.5F, 0.001F)));
      offset = {box->offset.x * scale.x, box->offset.y * scale.y,
                box->offset.z * scale.z};
    } else {
      return {};
    }
    (void)model;
  }
  if (shape == nullptr)
    return {};
  if (std::abs(offset.x) > 0.000001F || std::abs(offset.y) > 0.000001F ||
      std::abs(offset.z) > 0.000001F) {
    JPH::RotatedTranslatedShapeSettings settings(jolt(offset),
                                                 JPH::Quat::sIdentity(), shape);
    const auto result = settings.Create();
    if (result.HasError())
      return {};
    shape = result.Get();
  }
  return shape;
}

[[nodiscard]] std::uint64_t pairKey(std::uint32_t first, std::uint32_t second) {
  if (second < first)
    std::swap(first, second);
  // Include Jolt's sequence numbers so recycled body slots cannot alias contacts.
  return (std::uint64_t{first} << 32U) | second;
}

} // namespace

struct PhysicsWorld3D::Impl final : JPH::ContactListener {
  struct BodyRecord {
    JPH::BodyID body;
    std::uint64_t signature = 0;
    Vec3 previousPosition;
    Vec3 currentPosition;
    Vec3 lastAuthoredPosition;
    Vec3 lastAuthoredRotation;
    Vec3 publishedVelocity;
    Vec3 publishedAngularVelocity;
    bool publishedAwake = true;
    float gravityFactor = 1.0F;
    bool reportContacts = true;
    bool continuous = false;
    bool added = true;
    std::vector<std::string> partIds;
  };
  struct RawContact {
    Vec3 point;
    Vec3 normal;
    float penetration = 0.0F;
    bool trigger = false;
    std::uint32_t firstBody = 0;
    std::uint32_t secondBody = 0;
    bool sleeping = false;
    float impactEnergy = 0.0F;
    std::uint64_t impactEpoch = 0;
  };
  struct CharacterRecord {
    JPH::Ref<JPH::CharacterVirtual> character;
    std::uint64_t shapeSignature = 0;
    float padding = 0.0F;
    float slopeLimit = 0.0F;
  };
  struct BodyFrame {
    std::uint64_t epoch = 0;
    std::uint32_t bodyKey = 0;
    Entity *entity = nullptr;
    std::string layer;
    bool reports = true;
    bool added = true;
    bool active = false;
  };

  JoltLifetime lifetime;
  BroadPhaseLayers broadPhaseLayers;
  ObjectVsBroadPhaseFilter objectVsBroadPhase;
  ObjectLayerPairFilter layerPairs;
  JPH::TempAllocatorMalloc allocator;
  JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                physicsWorkerCount()};
  JPH::PhysicsSystem physics;
  World *world = nullptr;
  bool replacementPhase = false;
  std::unordered_map<std::string, BodyRecord> bodies;
  std::unordered_map<std::string, CharacterRecord> characters;
  std::unordered_map<std::uint32_t, std::string> ids;
  std::vector<BodyFrame> bodyFrames;
  std::uint64_t frameEpoch = 0;
  std::uint64_t updateErrorSteps = 0;
  std::unordered_map<std::uint64_t, RawContact> contacts;
  std::mutex contactsMutex;
  std::vector<std::pair<JPH::BodyID, JPH::BodyID>> removedContacts;

  void wakeNearBody(JPH::BodyID body) {
    auto &interface = physics.GetBodyInterface();
    JPH::AABox bounds;
    {
      JPH::BodyLockRead lock(physics.GetBodyLockInterface(), body);
      if (!lock.Succeeded() || !lock.GetBody().IsInBroadPhase())
        return;
      bounds = lock.GetBody().GetWorldSpaceBounds();
    }
    bounds.ExpandBy(JPH::Vec3::sReplicate(0.1F));
    interface.ActivateBodiesInAABox(
        bounds, JPH::BroadPhaseLayerFilter{},
        JPH::SpecifiedObjectLayerFilter(MovingLayer));
  }

  void finishContactRemovals() {
    auto &interface = physics.GetBodyInterface();
    // Solver workers have joined. Body access is forbidden in OnContactRemoved.
    for (const auto &[first, second] : removedContacts) {
      const auto found = contacts.find(
          pairKey(first.GetIndexAndSequenceNumber(),
                  second.GetIndexAndSequenceNumber()));
      if (found == contacts.end())
        continue;
      // Removal callbacks may refer to bodies already destroyed or recycled
      // during synchronization. Never query Jolt with those stale handles.
      if (!frameFor(first.GetIndexAndSequenceNumber()) ||
          !frameFor(second.GetIndexAndSequenceNumber())) {
        contacts.erase(found);
        continue;
      }
      auto &raw = found->second;
      if (raw.firstBody != first.GetIndexAndSequenceNumber() ||
          raw.secondBody != second.GetIndexAndSequenceNumber())
        continue;
      if (physics.WereBodiesInContact(first, second))
        continue;
      if (interface.IsAdded(first) && interface.IsAdded(second) &&
          !interface.IsActive(first) && !interface.IsActive(second))
        raw.sleeping = true;
      else
        contacts.erase(found);
    }
    removedContacts.clear();
  }

  Impl() {
    // Dense piles need several contacts per body. The previous 10,240-contact
    // allocation overflowed in the 5,000-body lab; keep errors observable for
    // workloads that exceed this larger, still bounded per-world allocation.
    physics.Init(65536, 0, 65536, 32768, broadPhaseLayers, objectVsBroadPhase,
                 layerPairs);
    physics.SetContactListener(this);
  }

  ~Impl() override {
    auto &interface = physics.GetBodyInterface();
    for (const auto &[id, record] : bodies) {
      (void)id;
      interface.RemoveBody(record.body);
      interface.DestroyBody(record.body);
    }
  }

  [[nodiscard]] std::string idFor(const JPH::BodyID body) const {
    const auto found = ids.find(body.GetIndexAndSequenceNumber());
    return found == ids.end() ? std::string{} : found->second;
  }

  [[nodiscard]] Entity *entityFor(const JPH::BodyID body) const {
    const auto *frame = frameFor(body.GetIndexAndSequenceNumber());
    return frame ? frame->entity : nullptr;
  }

  [[nodiscard]] const BodyFrame *frameFor(std::uint32_t id) const {
    const auto index = JPH::BodyID(id).GetIndex();
    if (index >= bodyFrames.size())
      return nullptr;
    const auto &frame = bodyFrames[index];
    return frame.epoch == frameEpoch && frame.bodyKey == id ? &frame : nullptr;
  }

  JPH::ValidateResult
  OnContactValidate(const JPH::Body &first, const JPH::Body &second,
                    JPH::RVec3Arg, const JPH::CollideShapeResult &) override {
    if (world == nullptr)
      return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
    const Entity *firstEntity = entityFor(first.GetID());
    const Entity *secondEntity = entityFor(second.GetID());
    return firstEntity != nullptr && secondEntity != nullptr &&
                   layersCollide(*world, *firstEntity, *secondEntity)
               ? JPH::ValidateResult::AcceptAllContactsForThisBodyPair
               : JPH::ValidateResult::RejectAllContactsForThisBodyPair;
  }

  void record(const JPH::Body &first, const JPH::Body &second,
              const JPH::ContactManifold &manifold, bool entering) {
    const auto *firstFrame =
        frameFor(first.GetID().GetIndexAndSequenceNumber());
    const auto *secondFrame =
        frameFor(second.GetID().GetIndexAndSequenceNumber());
    if (!firstFrame || !secondFrame)
      return;
    const auto key = pairKey(first.GetID().GetIndexAndSequenceNumber(),
                            second.GetID().GetIndexAndSequenceNumber());
    if (!firstFrame->reports && !secondFrame->reports) {
      std::scoped_lock lock(contactsMutex);
      contacts.erase(key);
      return;
    }
    const JPH::RVec3 point = manifold.mRelativeContactPointsOn1.empty()
                                 ? manifold.mBaseOffset
                                 : manifold.GetWorldSpaceContactPointOn1(0);
    const bool trigger = first.IsSensor() || second.IsSensor();
    float impactEnergy = 0.0F;
    if (entering && !trigger) {
      const float inverseMassSum =
          (first.IsDynamic() ? first.GetMotionProperties()->GetInverseMass()
                             : 0.0F) +
          (second.IsDynamic() ? second.GetMotionProperties()->GetInverseMass()
                              : 0.0F);
      impactEnergy = normalImpactEnergy3D(
          demi(first.GetPointVelocity(point) - second.GetPointVelocity(point)),
          demi(manifold.mWorldSpaceNormal), inverseMassSum);
    }
    std::scoped_lock lock(contactsMutex);
    auto &contact = contacts[key];
    if (contact.impactEpoch == frameEpoch &&
        contact.impactEnergy > impactEnergy)
      return;
    contact = {
        .point = demi(point),
        .normal = demi(manifold.mWorldSpaceNormal),
        .penetration = std::max(manifold.mPenetrationDepth, 0.0F),
        .trigger = first.IsSensor() || second.IsSensor(),
        .firstBody = first.GetID().GetIndexAndSequenceNumber(),
        .secondBody = second.GetID().GetIndexAndSequenceNumber(),
        .impactEnergy = impactEnergy,
        .impactEpoch = frameEpoch,
    };
  }

  void OnContactAdded(const JPH::Body &first, const JPH::Body &second,
                      const JPH::ContactManifold &manifold,
                      JPH::ContactSettings &) override {
    record(first, second, manifold, true);
  }
  void OnContactPersisted(const JPH::Body &first, const JPH::Body &second,
                          const JPH::ContactManifold &manifold,
                          JPH::ContactSettings &) override {
    record(first, second, manifold, false);
  }
  void OnContactRemoved(const JPH::SubShapeIDPair &pair) override {
    std::scoped_lock lock(contactsMutex);
    removedContacts.emplace_back(pair.GetBody1ID(), pair.GetBody2ID());
  }
};

PhysicsWorld3D::PhysicsWorld3D() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld3D::~PhysicsWorld3D() = default;
bool PhysicsWorld3D::available() const { return impl_ != nullptr; }

PhysicsWorld3D &ensurePhysicsWorld3D(World &world) {
  if (world.physicsWorld3D == nullptr)
    world.physicsWorld3D = std::make_shared<PhysicsWorld3D>();
  return *world.physicsWorld3D;
}

bool PhysicsWorld3D::replaceBodies(
    World &world, World &candidate, const std::vector<std::string> &sources,
    const std::vector<ReplacementBody3D> &replacements, std::string &error) {
  error.clear();
  if (!impl_->replacementPhase || impl_->world != &world) {
    error = "Body replacement requires the fixed-step transaction boundary";
    return false;
  }
  auto &interface = impl_->physics.GetBodyInterface();
  try {
    if (sources.empty() || sources.size() > 256 || replacements.size() > 256)
      throw std::runtime_error("Invalid replacement batch size");
    std::unordered_set<std::string> sourceSet(sources.begin(), sources.end());
    if (sourceSet.size() != sources.size())
      throw std::runtime_error("Duplicate replacement source");
    for (const auto &source : sources)
      if (!impl_->bodies.contains(source) || !impl_->bodies.at(source).added)
        throw std::runtime_error("Replacement source body is unavailable");
    auto nextBodies = impl_->bodies;
    auto nextIds = impl_->ids;
    auto nextFrames = impl_->bodyFrames;
    for (const auto &source : sources) {
      const auto id = nextBodies.at(source).body;
      nextBodies.erase(source);
      nextIds.erase(id.GetIndexAndSequenceNumber());
      nextFrames[id.GetIndex()].epoch = 0;
    }
    JoltBodyBatch3D batch(interface, replacements.size());
    for (const auto &replacement : replacements) {
      if (!sourceSet.contains(replacement.sourceId) ||
          nextBodies.contains(replacement.entityId))
        throw std::runtime_error("Invalid replacement identity or source");
      auto *entity = findEntity(candidate, replacement.entityId);
      auto *body = entity ? entity->component<Rigidbody3DComponent>() : nullptr;
      auto *transform =
          entity ? entity->component<Transform3DComponent>() : nullptr;
      if (!entity || !body || !transform || !transform->parent.empty() ||
          !body->bodyEnabled || isTrigger(*entity))
        throw std::runtime_error(
            "Replacement requires an enabled unparented rigid body");
      auto shape = shapeFor(candidate, *entity);
      if (!shape)
        throw std::runtime_error("Replacement collision hull creation failed");
      const auto *parts = resolvedCompoundCollider3D(candidate, *entity);
      if (!parts || !std::isfinite(body->mass) || body->mass <= 0)
        throw std::runtime_error(
            "Replacement requires compound geometry and positive finite mass");
      const auto source = impl_->bodies.at(replacement.sourceId).body;
      const auto checkpointTransform = *transform;
      const auto checkpointVelocity = body->velocity;
      const auto checkpointAngular = body->angularVelocity;
      JPH::RVec3 position;
      JPH::Quat rotation;
      JPH::Vec3 angular, linear;
      {
        JPH::BodyLockRead lock(impl_->physics.GetBodyLockInterface(), source);
        if (!lock.Succeeded())
          throw std::runtime_error("Replacement source lock failed");
        const auto &parent = lock.GetBody();
        transform->position = demi(parent.GetPosition());
        transform->rotation = demi(parent.GetRotation().GetEulerAngles());
        const auto center = parent.GetPosition() +
                            parent.GetRotation() * shape->GetCenterOfMass();
        angular = parent.IsStatic() ? JPH::Vec3::sZero()
                                    : parent.GetAngularVelocity();
        linear = parent.IsStatic()
                     ? JPH::Vec3::sZero()
                     : parent.GetLinearVelocity() +
                           angular.Cross(JPH::Vec3(
                               center - parent.GetCenterOfMassPosition()));
        position = parent.GetPosition();
        rotation = parent.GetRotation();
      }
      if (replacement.restoreState) {
        for (Vec3 v : {checkpointTransform.position, checkpointTransform.rotation,
                       checkpointVelocity, checkpointAngular})
          if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            throw std::runtime_error("Non-finite replacement checkpoint state");
        *transform = checkpointTransform;
        position = JPH::RVec3(transform->position.x,transform->position.y,transform->position.z);
        rotation = joltRotation(transform->rotation);
        linear = jolt(checkpointVelocity);
        angular = jolt(checkpointAngular);
      }
      body->velocity = body->bodyType == "static" ? Vec3{} : demi(linear);
      body->angularVelocity =
          body->bodyType == "static" ? Vec3{} : demi(angular);
      body->awake = true;
      body->accumulatedForce = body->accumulatedImpulse =
          body->accumulatedTorque = {};
      JPH::BodyCreationSettings settings(
          shape, position, rotation, motionType(body),
          body->bodyType == "static" ? StaticLayer : MovingLayer);
      settings.mLinearVelocity = jolt(body->velocity);
      settings.mAngularVelocity = jolt(body->angularVelocity);
      settings.mLinearDamping = body->linearDamping;
      settings.mAngularDamping = body->angularDamping;
      settings.mGravityFactor = body->useGravity ? body->gravityScale : 0;
      settings.mFriction = body->friction;
      settings.mRestitution = body->restitution;
      settings.mAllowSleeping = body->allowSleep;
      settings.mNumVelocityStepsOverride =
          std::clamp(body->solverVelocitySteps, 0, 128);
      settings.mNumPositionStepsOverride =
          std::clamp(body->solverPositionSteps, 0, 128);
      settings.mMotionQuality = body->continuous
                                    ? JPH::EMotionQuality::LinearCast
                                    : JPH::EMotionQuality::Discrete;
      settings.mAllowedDOFs = allowedDofs(*body);
      settings.mOverrideMassProperties =
          JPH::EOverrideMassProperties::CalculateInertia;
      settings.mMassPropertiesOverride.mMass = body->mass;
      auto *created = batch.create(settings);
      if (!created)
        throw std::runtime_error(
            "Native body capacity exhausted while staging split");
      const auto id = created->GetID();
      Impl::BodyRecord record{.body = id,
                              .signature = shapeSignature(candidate, *entity),
                              .previousPosition = transform->position,
                              .currentPosition = transform->position,
                              .lastAuthoredPosition = transform->position,
                              .lastAuthoredRotation = transform->rotation,
                              .publishedVelocity = body->velocity,
                              .publishedAngularVelocity = body->angularVelocity,
                              .publishedAwake = true,
                              .gravityFactor = settings.mGravityFactor,
                              .reportContacts = body->reportContacts,
                              .continuous = body->continuous,
                              .added = true,
                              .partIds = {}};
      for (const auto &part : *parts)
        record.partIds.push_back(part.id);
      candidate.colliderAssets3D
          .at(entity->component<ModelCollider3DComponent>()->asset)
          .lastUsedEpoch = impl_->frameEpoch;
      nextBodies.emplace(entity->id, std::move(record));
      nextIds.emplace(id.GetIndexAndSequenceNumber(), entity->id);
      if (id.GetIndex() >= nextFrames.size())
        nextFrames.resize(id.GetIndex() + 1);
    }
    // These pointers refer to the candidate vector's buffer, which swap
    // preserves.
    for (auto &entity : candidate.entities) {
      const auto found = nextBodies.find(entity.id);
      if (found == nextBodies.end())
        continue;
      const auto id = found->second.body;
      nextFrames[id.GetIndex()] = {.epoch = impl_->frameEpoch,
                                   .bodyKey = id.GetIndexAndSequenceNumber(),
                                   .entity = &entity,
                                   .layer = colliderLayer(entity),
                                   .reports = reportsContacts(entity),
                                   .added = found->second.added};
    }
    impl_->removedContacts.reserve(impl_->removedContacts.size() +
                                   impl_->contacts.size());
    batch.prepare();
    // Commit: all engine allocations and native broadphase preparation
    // succeeded.
    for (const auto &source : sources) {
      const auto id = impl_->bodies.at(source).body;
      impl_->wakeNearBody(id);
      interface.RemoveBody(id);
      interface.DestroyBody(id);
    }
    batch.publish();
    world.entities.swap(candidate.entities);
    world.colliderAssets3D.swap(candidate.colliderAssets3D);
    impl_->bodies.swap(nextBodies);
    impl_->ids.swap(nextIds);
    impl_->bodyFrames.swap(nextFrames);
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

void PhysicsWorld3D::step(World &world, const float fixedDt,
                          const Vec3 gravity) {
  if (fixedDt <= 0.0F)
    return;
  impl_->world = &world;
  impl_->physics.SetGravity(jolt(gravity));
  auto &interface = impl_->physics.GetBodyInterface();
  // Native slots may be reused; validate the generation and current step before
  // reading each snapshot. Contacts never retain entity/component pointers.
  ++impl_->frameEpoch;
  if (world.destruction3D) {
    world.destruction3D->prune(world);
    world.destruction3D->updateCosmetics(world, fixedDt);
    updateDetachedFragmentFades3D(world,fixedDt,gravity);
  }

  {
    ProfileScope scope("Physics3D.sync_bodies");
    for (Entity &entity : world.entities) {
      if (!world.destruction3D && entity.enabled && entity.hasComponent<Destructible3DComponent>())
        world.destruction3D = std::make_shared<DestructionWorld3D>();
      if (!entity.enabled || !entity.hasComponent<Transform3DComponent>() ||
          !hasCollider(entity) ||
          entity.hasComponent<CharacterController3DComponent>())
        continue;
      auto *body = entity.component<Rigidbody3DComponent>();
      if (const auto *model = entity.component<ModelCollider3DComponent>()) {
        if (auto asset = world.colliderAssets3D.find(model->asset);
            asset != world.colliderAssets3D.end())
          asset->second.lastUsedEpoch = impl_->frameEpoch;
      }
      const std::uint64_t signature = shapeSignature(world, entity);
      auto found = impl_->bodies.find(entity.id);
      const bool shapeRebuilt =
          found != impl_->bodies.end() && found->second.signature != signature;
      if (found != impl_->bodies.end() &&
          found->second.signature != signature) {
        impl_->wakeNearBody(found->second.body);
        interface.RemoveBody(found->second.body);
        interface.DestroyBody(found->second.body);
        impl_->ids.erase(found->second.body.GetIndexAndSequenceNumber());
        impl_->bodies.erase(found);
        found = impl_->bodies.end();
      }

      const Transform3DComponent &local =
          *entity.component<Transform3DComponent>();
      const auto resolved = resolveWorldTransform3D(world, entity);
      if (!resolved)
        continue;
      if (found == impl_->bodies.end()) {
        JPH::ShapeRefC shape;
        {
          ProfileScope scope("Physics3D.create_shape");
          shape = shapeFor(world, entity);
        }
        if (shape == nullptr)
          continue;
        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(resolved->position.x, resolved->position.y,
                       resolved->position.z),
            joltRotation(resolved->rotation), motionType(body),
            motionType(body) == JPH::EMotionType::Static ? StaticLayer
                                                         : MovingLayer);
        if (body != nullptr) {
          settings.mLinearVelocity = jolt(body->velocity);
          settings.mAngularVelocity = jolt(body->angularVelocity);
          settings.mLinearDamping = body->linearDamping;
          settings.mAngularDamping = body->angularDamping;
          settings.mGravityFactor =
              body->useGravity ? body->gravityScale : 0.0F;
          settings.mFriction = body->friction;
          settings.mRestitution = body->restitution;
          settings.mAllowSleeping = body->allowSleep;
          settings.mNumVelocityStepsOverride =
              std::clamp(body->solverVelocitySteps, 0, 128);
          settings.mNumPositionStepsOverride =
              std::clamp(body->solverPositionSteps, 0, 128);
          settings.mMotionQuality = body->continuous
                                        ? JPH::EMotionQuality::LinearCast
                                        : JPH::EMotionQuality::Discrete;
          settings.mAllowedDOFs = allowedDofs(*body);
          settings.mOverrideMassProperties =
              JPH::EOverrideMassProperties::CalculateInertia;
          settings.mMassPropertiesOverride.mMass = body->mass;
        }
        settings.mIsSensor = isTrigger(entity);
        JPH::Body *created = interface.CreateBody(settings);
        if (created == nullptr)
          continue;
        const JPH::BodyID bodyId = created->GetID();
        interface.AddBody(bodyId,
                          body != nullptr && (body->awake || shapeRebuilt)
                              ? JPH::EActivation::Activate
                              : JPH::EActivation::DontActivate);
        if (motionType(body) == JPH::EMotionType::Static)
          impl_->wakeNearBody(bodyId);
        if (body != nullptr && !body->bodyEnabled)
          interface.RemoveBody(bodyId);
        impl_->ids[bodyId.GetIndexAndSequenceNumber()] = entity.id;
        found = impl_->bodies
                    .emplace(
                        entity.id,
                        Impl::BodyRecord{
                            .body = bodyId,
                            .signature = signature,
                            .previousPosition = resolved->position,
                            .currentPosition = resolved->position,
                            .lastAuthoredPosition = local.position,
                            .lastAuthoredRotation = local.rotation,
                            .publishedVelocity = body ? body->velocity : Vec3{},
                            .publishedAngularVelocity =
                                body ? body->angularVelocity : Vec3{},
                            .publishedAwake = body && body->awake,
                            .gravityFactor = body && body->useGravity
                                                 ? body->gravityScale
                                                 : 0.0F,
                            .reportContacts =
                                body == nullptr || body->reportContacts,
                            .continuous = body != nullptr && body->continuous,
                            .added = body == nullptr || body->bodyEnabled})
                    .first;
        if (const auto *parts = resolvedCompoundCollider3D(world, entity))
          for (const auto &part : *parts) found->second.partIds.push_back(part.id);
      } else {
        Impl::BodyRecord &record = found->second;
        const bool authoredTransformChanged =
            local.position.x != record.lastAuthoredPosition.x ||
            local.position.y != record.lastAuthoredPosition.y ||
            local.position.z != record.lastAuthoredPosition.z ||
            local.rotation.x != record.lastAuthoredRotation.x ||
            local.rotation.y != record.lastAuthoredRotation.y ||
            local.rotation.z != record.lastAuthoredRotation.z;
        if (authoredTransformChanged ||
            motionType(body) == JPH::EMotionType::Kinematic) {
          if (authoredTransformChanged)
            impl_->wakeNearBody(record.body);
          interface.SetPositionAndRotationWhenChanged(
              record.body,
              JPH::RVec3(resolved->position.x, resolved->position.y,
                         resolved->position.z),
              joltRotation(resolved->rotation), JPH::EActivation::Activate);
          if (authoredTransformChanged)
            impl_->wakeNearBody(record.body);
        }
        if (body != nullptr) {
          if (body->bodyEnabled != record.added) {
            impl_->wakeNearBody(record.body);
            if (body->bodyEnabled)
              interface.AddBody(record.body, JPH::EActivation::Activate);
            else
              interface.RemoveBody(record.body);
            record.added = body->bodyEnabled;
            if (record.added)
              impl_->wakeNearBody(record.body);
          }
          if (body->reportContacts != record.reportContacts) {
            impl_->wakeNearBody(record.body);
            record.reportContacts = body->reportContacts;
          }
          if (motionType(body) != JPH::EMotionType::Static) {
            const auto changed = [](Vec3 a, Vec3 b) {
              return a.x != b.x || a.y != b.y || a.z != b.z;
            };
            // Components contain last step's published state, not a fresh
            // velocity/wake command every frame. Replaying it prevents sleep
            // and can overwrite commands already applied through BodyInterface.
            if (changed(body->velocity, record.publishedVelocity) ||
                changed(body->angularVelocity, record.publishedAngularVelocity))
              interface.SetLinearAndAngularVelocity(
                  record.body, jolt(body->velocity),
                  jolt(body->angularVelocity));
            const float gravityFactor =
                body->useGravity ? body->gravityScale : 0.0F;
            if (gravityFactor != record.gravityFactor) {
              interface.SetGravityFactor(record.body, gravityFactor);
              record.gravityFactor = gravityFactor;
              if (record.added)
                interface.ActivateBody(record.body);
            }
            if (body->continuous != record.continuous) {
              interface.SetMotionQuality(record.body,
                                         body->continuous
                                             ? JPH::EMotionQuality::LinearCast
                                             : JPH::EMotionQuality::Discrete);
              record.continuous = body->continuous;
            }
            if (record.added && body->awake != record.publishedAwake) {
              if (body->awake)
                interface.ActivateBody(record.body);
              else
                interface.DeactivateBody(record.body);
            }
          }
        }
        record.lastAuthoredPosition = local.position;
        record.lastAuthoredRotation = local.rotation;
      }
      if (body != nullptr && found != impl_->bodies.end()) {
        Impl::BodyRecord &record = found->second;
        if (body->hasKinematicTarget && body->bodyType == "kinematic" &&
            body->kinematicTargetDt > 0.0F) {
          interface.MoveKinematic(record.body,
                                  JPH::RVec3(body->kinematicTargetPosition.x,
                                             body->kinematicTargetPosition.y,
                                             body->kinematicTargetPosition.z),
                                  joltRotation(body->kinematicTargetRotation),
                                  body->kinematicTargetDt);
        }
        if (body->accumulatedForce.x != 0.0F ||
            body->accumulatedForce.y != 0.0F ||
            body->accumulatedForce.z != 0.0F)
          interface.AddForce(record.body, jolt(body->accumulatedForce));
        if (body->accumulatedImpulse.x != 0.0F ||
            body->accumulatedImpulse.y != 0.0F ||
            body->accumulatedImpulse.z != 0.0F)
          interface.AddImpulse(record.body, jolt(body->accumulatedImpulse));
        if (body->accumulatedTorque.x != 0.0F ||
            body->accumulatedTorque.y != 0.0F ||
            body->accumulatedTorque.z != 0.0F)
          interface.AddTorque(record.body, jolt(body->accumulatedTorque));
        body->accumulatedForce = {};
        body->accumulatedImpulse = {};
        body->accumulatedTorque = {};
        body->hasKinematicTarget = false;
        body->kinematicTargetDt = 0.0F;
      }
      if (found != impl_->bodies.end()) {
        const auto nativeId = found->second.body;
        if (nativeId.GetIndex() >= impl_->bodyFrames.size())
          impl_->bodyFrames.resize(nativeId.GetIndex() + 1);
        impl_->bodyFrames[nativeId.GetIndex()] = {
            .epoch = impl_->frameEpoch,
            .bodyKey = nativeId.GetIndexAndSequenceNumber(),
            .entity = &entity,
            .layer = colliderLayer(entity),
            .reports = reportsContacts(entity),
            .added = found->second.added};
      }
    }
  }

  {
    ProfileScope scope("Physics3D.remove_bodies");
    for (auto iterator = impl_->bodies.begin();
         iterator != impl_->bodies.end();) {
      if (impl_->frameFor(iterator->second.body.GetIndexAndSequenceNumber())) {
        ++iterator;
        continue;
      }
      impl_->wakeNearBody(iterator->second.body);
      interface.RemoveBody(iterator->second.body);
      interface.DestroyBody(iterator->second.body);
      impl_->ids.erase(iterator->second.body.GetIndexAndSequenceNumber());
      iterator = impl_->bodies.erase(iterator);
    }
  }

  if (world.destruction3D) {
    ProfileScope scope("Destruction3D.update");
    impl_->replacementPhase = true;
    world.destruction3D->update(world, *this);
    impl_->replacementPhase = false;
  }

  std::erase_if(world.colliderAssets3D, [&](const auto &entry) {
    return !entry.second.resident &&
           entry.second.lastUsedEpoch != impl_->frameEpoch;
  });
  {
    ProfileScope scope("Physics3D.simulate");
    const auto error =
        impl_->physics.Update(fixedDt, 1, &impl_->allocator, &impl_->jobs);
    if (error != JPH::EPhysicsUpdateError::None)
      ++impl_->updateErrorSteps;
  }
  RuntimeProfiler::setGauge("Physics3D.update_error_steps",
                            impl_->updateErrorSteps);
  RuntimeProfiler::setGauge("Physics3D.bodies", impl_->physics.GetNumBodies());
  RuntimeProfiler::setGauge(
      "Physics3D.active_bodies",
      impl_->physics.GetNumActiveBodies(JPH::EBodyType::RigidBody));

  std::unordered_set<std::string> liveCharacters;
  std::vector<PhysicsContact3D> characterContacts;
  {
    ProfileScope scope("Physics3D.update_characters");
    for (Entity &entity : world.entities) {
      auto *transform = entity.component<Transform3DComponent>();
      auto *controller = entity.component<CharacterController3DComponent>();
      if (!entity.enabled || transform == nullptr || controller == nullptr)
        continue;
      liveCharacters.insert(entity.id);
      if (!hasCharacterCollider(entity)) {
        impl_->characters.erase(entity.id);
        controller->grounded = false;
        controller->groundEntity.clear();
        controller->desiredVelocity = {};
        controller->requestedJumpSpeed = 0.0F;
        continue;
      }
      auto found = impl_->characters.find(entity.id);
      const std::uint64_t signature = shapeSignature(world, entity);
      const bool settingsChanged =
          found != impl_->characters.end() &&
          (found->second.shapeSignature != signature ||
           found->second.padding != controller->skinWidth ||
           found->second.slopeLimit != controller->slopeLimit);
      if (settingsChanged) {
        impl_->characters.erase(found);
        found = impl_->characters.end();
      }
      if (found == impl_->characters.end()) {
        const JPH::ShapeRefC shape = shapeFor(world, entity);
        if (shape == nullptr)
          continue;
        JPH::Ref<JPH::CharacterVirtualSettings> settings =
            new JPH::CharacterVirtualSettings();
        settings->mShape = shape;
        settings->mMaxSlopeAngle =
            JPH::DegreesToRadians(controller->slopeLimit);
        settings->mCharacterPadding = controller->skinWidth;
        // Controller transforms use their visual origin as the collider center.
        // Contacts on the lower half of any supported shape may support it.
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), 0.0F);
        JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
            settings,
            JPH::RVec3(transform->position.x, transform->position.y,
                       transform->position.z),
            joltRotation(transform->rotation), 0, &impl_->physics);
        found = impl_->characters
                    .emplace(entity.id,
                             Impl::CharacterRecord{
                                 .character = std::move(character),
                                 .shapeSignature = signature,
                                 .padding = controller->skinWidth,
                                 .slopeLimit = controller->slopeLimit})
                    .first;
      }

      JPH::CharacterVirtual &character = *found->second.character;
      character.SetPosition(JPH::RVec3(
          transform->position.x, transform->position.y, transform->position.z));
      character.SetRotation(joltRotation(transform->rotation));
      character.UpdateGroundVelocity();
      const bool grounded = character.GetGroundState() ==
                            JPH::CharacterBase::EGroundState::OnGround;
      JPH::Vec3 velocity = character.GetLinearVelocity();
      const JPH::Vec3 groundVelocity = character.GetGroundVelocity();
      velocity.SetX(controller->desiredVelocity.x +
                    (grounded ? groundVelocity.GetX() : 0.0F));
      velocity.SetZ(controller->desiredVelocity.z +
                    (grounded ? groundVelocity.GetZ() : 0.0F));
      if (grounded && velocity.GetY() < 0.1F)
        velocity.SetY(groundVelocity.GetY());
      if (grounded && controller->requestedJumpSpeed > 0.0F)
        velocity.SetY(controller->requestedJumpSpeed);
      else
        velocity.SetY(velocity.GetY() + controller->gravity * fixedDt);
      character.SetLinearVelocity(velocity);

      JPH::CharacterVirtual::ExtendedUpdateSettings update;
      update.mWalkStairsStepUp = JPH::Vec3(0.0F, controller->stepHeight, 0.0F);
      update.mStickToFloorStepDown =
          JPH::Vec3(0.0F, -controller->stepHeight, 0.0F);
      {
        ProfileScope scope("Physics3D.character_update");
        const CharacterBodyFilter bodyFilter(world, entity, impl_->ids);
        character.ExtendedUpdate(
            fixedDt, JPH::Vec3(0.0F, controller->gravity, 0.0F), update,
            impl_->physics.GetDefaultBroadPhaseLayerFilter(MovingLayer),
            impl_->physics.GetDefaultLayerFilter(MovingLayer), bodyFilter, {},
            impl_->allocator);
      }

      const JPH::RVec3 position = character.GetPosition();
      transform->position = demi(position);
      controller->velocity = demi(character.GetLinearVelocity());
      controller->grounded = character.GetGroundState() ==
                             JPH::CharacterBase::EGroundState::OnGround;
      controller->groundEntity = controller->grounded
                                     ? impl_->idFor(character.GetGroundBodyID())
                                     : std::string{};
      const std::vector<PhysicsQueryHit3D> triggerHits = overlapCollider(
          world, entity, transform->position, transform->rotation, entity.id);
      for (const PhysicsQueryHit3D &hit : triggerHits) {
        const Entity *other = findEntity(world, hit.entityId);
        if (!hit.isTrigger || other == nullptr ||
            !layersCollide(world, entity, *other))
          continue;
        characterContacts.push_back({.entityId = entity.id,
                                     .otherEntityId = hit.entityId,
                                     .otherLayer = hit.layer,
                                     .point = hit.point,
                                     .normal = hit.normal,
                                     .isTrigger = true});
        characterContacts.push_back(
            {.entityId = hit.entityId,
             .otherEntityId = entity.id,
             .otherLayer = colliderLayer(entity),
             .point = hit.point,
             .normal = {-hit.normal.x, -hit.normal.y, -hit.normal.z},
             .isTrigger = true});
      }
      controller->desiredVelocity = {};
      controller->requestedJumpSpeed = 0.0F;
    }
    for (auto iterator = impl_->characters.begin();
         iterator != impl_->characters.end();) {
      if (liveCharacters.contains(iterator->first))
        ++iterator;
      else
        iterator = impl_->characters.erase(iterator);
    }
  }

  {
    ProfileScope scope("Physics3D.sync_components");
    for (auto &[id, record] : impl_->bodies) {
      const auto *frame =
          impl_->frameFor(record.body.GetIndexAndSequenceNumber());
      Entity *entity = frame ? frame->entity : nullptr;
      auto *transform = entity != nullptr
                            ? entity->component<Transform3DComponent>()
                            : nullptr;
      auto *body = entity != nullptr ? entity->component<Rigidbody3DComponent>()
                                     : nullptr;
      if (transform == nullptr || motionType(body) == JPH::EMotionType::Static)
        continue;
      JPH::BodyLockRead bodyLock(impl_->physics.GetBodyLockInterface(),
                                 record.body);
      if (!bodyLock.Succeeded())
        continue;
      const JPH::Body &simulated = bodyLock.GetBody();
      impl_->bodyFrames[record.body.GetIndex()].active = simulated.IsActive();
      record.previousPosition = record.currentPosition;
      const JPH::RVec3 position = simulated.GetPosition();
      const JPH::Quat rotation = simulated.GetRotation();
      record.currentPosition = demi(position);
      if (transform->parent.empty())
        transform->position = record.currentPosition;
      if (transform->parent.empty())
        transform->rotation = demi(rotation.GetEulerAngles());
      record.lastAuthoredPosition = transform->position;
      record.lastAuthoredRotation = transform->rotation;
      if (body != nullptr) {
        body->velocity = demi(simulated.GetLinearVelocity());
        body->angularVelocity = demi(simulated.GetAngularVelocity());
        body->awake = simulated.IsActive();
        record.publishedVelocity = body->velocity;
        record.publishedAngularVelocity = body->angularVelocity;
        record.publishedAwake = body->awake;
      }
    }
  }

  {
    ProfileScope scope("Physics3D.contact_phases");
    {
      ProfileScope removalScope("Physics3D.contacts.remove");
      impl_->finishContactRemovals();
    }
    world.previousPhysicsContacts3D.swap(world.physicsContacts3D);
    world.physicsContacts3D.clear();
    {
      ProfileScope filterScope("Physics3D.contacts.filter");
      std::erase_if(impl_->contacts, [&](const auto &entry) {
        const auto &raw = entry.second;
        const auto *first = impl_->frameFor(raw.firstBody);
        const auto *second = impl_->frameFor(raw.secondBody);
        return !first || !second || !first->added || !second->added ||
               (raw.sleeping && (first->active || second->active)) ||
               (!first->reports && !second->reports);
      });
    }
    struct SortedContact {
      std::pair<std::string_view, std::string_view> key;
      const Impl::RawContact *raw;
    };
    std::vector<SortedContact> sortedContacts;
    {
      ProfileScope sortScope("Physics3D.contacts.sort");
      sortedContacts.reserve(impl_->contacts.size());
      // Publish stable entity-ID order, independent of native slot allocation.
      // Views live only until this joined, non-mutating publication phase ends.
      for (const auto &[bodyPair, raw] : impl_->contacts) {
        const auto *first = impl_->frameFor(raw.firstBody);
        const auto *second = impl_->frameFor(raw.secondBody);
        if (!first || !second)
          continue;
        const std::string_view firstId = first->entity->id;
        const std::string_view secondId = second->entity->id;
        sortedContacts.push_back(
            {{std::min(firstId, secondId), std::max(firstId, secondId)}, &raw});
      }
      std::ranges::sort(sortedContacts, {}, &SortedContact::key);
    }
    {
      ProfileScope emitScope("Physics3D.contacts.emit");
      for (const auto &contact : sortedContacts) {
        const auto &raw = *contact.raw;
        const auto *first = impl_->frameFor(raw.firstBody);
        const auto *second = impl_->frameFor(raw.secondBody);
        if (!first || !second)
          continue;
        const auto append =
            [&](const std::string &entity, const std::string &other,
                const std::string &otherLayer, const Vec3 normal) {
              world.physicsContacts3D.push_back(
                  {.entityId = entity,
                   .otherEntityId = other,
                   .otherLayer = otherLayer,
                   .point = raw.point,
                   .normal = normal,
                   .penetration = raw.penetration,
                   .isTrigger = raw.trigger,
                   .impactEnergy = raw.sleeping ? 0.0F : raw.impactEnergy});
            };
        if (first->reports)
          append(first->entity->id, second->entity->id, second->layer,
                 {-raw.normal.x, -raw.normal.y, -raw.normal.z});
        if (second->reports)
          append(second->entity->id, first->entity->id, first->layer, raw.normal);
      }
      for (auto &contact : characterContacts)
        world.physicsContacts3D.push_back(std::move(contact));
    }
    {
      ProfileScope phaseScope("Physics3D.contacts.assign_phases");
      assignContactPhases3D(world.physicsContacts3D,
                            world.previousPhysicsContacts3D);
    }
    RuntimeProfiler::setGauge("Physics3D.contact_pairs",
                              impl_->contacts.size());
    RuntimeProfiler::setGauge("Physics3D.contact_events",
                              world.physicsContacts3D.size());
  }
}

bool PhysicsWorld3D::setVelocity(const std::string &entityId,
                                 const Vec3 velocity) {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end())
    return false;
  impl_->physics.GetBodyInterface().SetLinearVelocity(found->second.body,
                                                      jolt(velocity));
  return true;
}

std::optional<Vec3>
PhysicsWorld3D::velocity(const std::string &entityId) const {
  const auto found = impl_->bodies.find(entityId);
  return found == impl_->bodies.end()
             ? std::nullopt
             : std::optional{
                   demi(impl_->physics.GetBodyInterface().GetLinearVelocity(
                       found->second.body))};
}

std::optional<PhysicsWorld3D::MotionSnapshot> PhysicsWorld3D::motionSnapshot(const std::string &id) const {
  const auto found=impl_->bodies.find(id);
  if(found==impl_->bodies.end()) return std::nullopt;
  auto &b=impl_->physics.GetBodyInterface();
  return MotionSnapshot{demi(b.GetCenterOfMassPosition(found->second.body)),demi(b.GetLinearVelocity(found->second.body)),demi(b.GetAngularVelocity(found->second.body))};
}
bool PhysicsWorld3D::addForce(const std::string &entityId, const Vec3 force) {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end())
    return false;
  impl_->physics.GetBodyInterface().AddForce(found->second.body, jolt(force));
  return true;
}

bool PhysicsWorld3D::addImpulse(const std::string &entityId,
                                const Vec3 impulse) {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end())
    return false;
  impl_->physics.GetBodyInterface().AddImpulse(found->second.body,
                                               jolt(impulse));
  return true;
}

bool PhysicsWorld3D::addTorque(const std::string &entityId, const Vec3 torque) {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end())
    return false;
  impl_->physics.GetBodyInterface().AddTorque(found->second.body, jolt(torque));
  return true;
}

bool PhysicsWorld3D::addImpulseAtPosition(const std::string &entityId,
                                          Vec3 impulse, Vec3 position) {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end())
    return false;
  impl_->physics.GetBodyInterface().AddImpulse(
      found->second.body, jolt(impulse),
      JPH::RVec3(position.x, position.y, position.z));
  return true;
}

bool PhysicsWorld3D::setAwake(const std::string &entityId, const bool awake) {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end())
    return false;
  if (awake)
    impl_->physics.GetBodyInterface().ActivateBody(found->second.body);
  else
    impl_->physics.GetBodyInterface().DeactivateBody(found->second.body);
  return true;
}

bool PhysicsWorld3D::setEnabled(const std::string &entityId,
                                const bool enabled) {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end())
    return false;
  auto &interface = impl_->physics.GetBodyInterface();
  impl_->wakeNearBody(found->second.body);
  if (enabled)
    interface.AddBody(found->second.body, JPH::EActivation::Activate);
  else
    interface.RemoveBody(found->second.body);
  if (enabled)
    impl_->wakeNearBody(found->second.body);
  found->second.added = enabled;
  return true;
}

bool PhysicsWorld3D::setContinuous(const std::string &entityId,
                                   const bool continuous) {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end())
    return false;
  impl_->physics.GetBodyInterface().SetMotionQuality(
      found->second.body, continuous ? JPH::EMotionQuality::LinearCast
                                     : JPH::EMotionQuality::Discrete);
  found->second.continuous = continuous;
  return true;
}

bool PhysicsWorld3D::setKinematicTarget(const std::string &entityId,
                                        const Vec3 position,
                                        const Vec3 rotation,
                                        const float fixedDt) {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end() || fixedDt <= 0.0F)
    return false;
  impl_->physics.GetBodyInterface().MoveKinematic(
      found->second.body, JPH::RVec3(position.x, position.y, position.z),
      joltRotation(rotation), fixedDt);
  return true;
}

std::optional<Vec3>
PhysicsWorld3D::interpolatedPosition(const std::string &entityId,
                                     const float alpha) const {
  const auto found = impl_->bodies.find(entityId);
  if (found == impl_->bodies.end())
    return std::nullopt;
  const float amount = std::clamp(alpha, 0.0F, 1.0F);
  const Vec3 &from = found->second.previousPosition;
  const Vec3 &to = found->second.currentPosition;
  return Vec3{from.x + (to.x - from.x) * amount,
              from.y + (to.y - from.y) * amount,
              from.z + (to.z - from.z) * amount};
}

namespace {

class DemiQueryBodyFilter final : public JPH::BodyFilter {
public:
  DemiQueryBodyFilter(const World *world,
                      const std::unordered_map<std::uint32_t, std::string> *ids,
                      std::string layer, std::string ignored, bool includeTriggers = true)
      : world_(world), ids_(ids), layer_(std::move(layer)),
        ignored_(std::move(ignored)), includeTriggers_(includeTriggers) {}

  bool ShouldCollide(const JPH::BodyID &body) const override {
    if (world_ == nullptr)
      return false;
    const auto found = ids_->find(body.GetIndexAndSequenceNumber());
    if (found == ids_->end() || found->second == ignored_)
      return false;
    const Entity *entity = findEntity(*world_, found->second);
    return entity != nullptr &&
           (includeTriggers_ || !isTrigger(*entity)) &&
           (layer_.empty() || colliderLayer(*entity) == layer_);
  }

private:
  const World *world_;
  const std::unordered_map<std::uint32_t, std::string> *ids_;
  std::string layer_;
  std::string ignored_;
  bool includeTriggers_;
};

PhysicsQueryHit3D
queryHit(const World *world,
         const std::unordered_map<std::uint32_t, std::string> &ids,
         const JPH::BodyID body, const JPH::RVec3 point, const JPH::Vec3 normal,
         const float distance, const float fraction) {
  PhysicsQueryHit3D hit;
  const auto found = ids.find(body.GetIndexAndSequenceNumber());
  if (found == ids.end() || world == nullptr)
    return hit;
  hit.entityId = found->second;
  const Entity *entity = findEntity(*world, hit.entityId);
  hit.layer = entity != nullptr ? colliderLayer(*entity) : std::string{};
  hit.point = demi(point);
  hit.normal = demi(normal);
  hit.distance = distance;
  hit.fraction = fraction;
  hit.isTrigger = entity != nullptr && isTrigger(*entity);
  return hit;
}

} // namespace

std::vector<PhysicsQueryHit3D>
PhysicsWorld3D::overlapCollider(const World &world, const Entity &entity,
                                const Vec3 position, const Vec3 rotation,
                                const std::string &ignoredEntityId) const {
  if (impl_->world == nullptr)
    return {};
  const JPH::ShapeRefC shape = shapeFor(world, entity);
  if (shape == nullptr)
    return {};

  JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
  const DemiQueryBodyFilter filter(impl_->world, &impl_->ids, {},
                                   ignoredEntityId);
  impl_->physics.GetNarrowPhaseQuery().CollideShape(
      shape, JPH::Vec3::sOne(),
      JPH::RMat44::sRotationTranslation(
          joltRotation(rotation),
          JPH::RVec3(position.x, position.y, position.z)),
      {}, JPH::RVec3::sZero(), collector, {}, {}, filter);

  std::vector<PhysicsQueryHit3D> result;
  for (const JPH::CollideShapeResult &hit : collector.mHits) {
    PhysicsQueryHit3D value = queryHit(
        impl_->world, impl_->ids, hit.mBodyID2, hit.mContactPointOn2,
        -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()), 0.0F, 0.0F);
    if (!value.entityId.empty() &&
        std::ranges::none_of(result, [&](const PhysicsQueryHit3D &existing) {
          return existing.entityId == value.entityId;
        }))
      result.push_back(std::move(value));
  }
  std::ranges::sort(result, {}, &PhysicsQueryHit3D::entityId);
  return result;
}

std::vector<PhysicsQueryHit3D> PhysicsWorld3D::overlapSphere(
    const Vec3 center, const float radius, const std::string &layer,
    const std::string &ignoredEntityId, bool includeParts) const {
  if (radius < 0.0F || impl_->world == nullptr)
    return {};
  const JPH::SphereShape shape(std::max(radius, 0.001F));
  JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
  const DemiQueryBodyFilter filter(impl_->world, &impl_->ids, layer,
                                   ignoredEntityId);
  impl_->physics.GetNarrowPhaseQuery().CollideShape(
      &shape, JPH::Vec3::sOne(),
      JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z)), {},
      JPH::RVec3::sZero(), collector, {}, {}, filter);
  std::vector<PhysicsQueryHit3D> result;
  for (const JPH::CollideShapeResult &hit : collector.mHits) {
    PhysicsQueryHit3D value = queryHit(
        impl_->world, impl_->ids, hit.mBodyID2, hit.mContactPointOn2,
        -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()), 0.0F, 0.0F);
    if (includeParts && !value.entityId.empty()) {
      JPH::BodyLockRead lock(impl_->physics.GetBodyLockInterface(),
                             hit.mBodyID2);
      if (lock.Succeeded()) {
        const auto part =
            lock.GetBody().GetShape()->GetSubShapeUserData(hit.mSubShapeID2);
        const auto record = impl_->bodies.find(value.entityId);
        if (part > 0 && record != impl_->bodies.end() &&
            part <= record->second.partIds.size())
          value.colliderPartId = record->second.partIds[part - 1];
      }
      value.distance = std::max(0.0F, radius - hit.mPenetrationDepth);
      result.push_back(std::move(value));
      continue;
    }
    if (!value.entityId.empty() &&
        std::ranges::none_of(result, [&](const PhysicsQueryHit3D &existing) {
          return existing.entityId == value.entityId;
        }))
      result.push_back(std::move(value));
  }
  std::ranges::sort(result, [](const auto &a, const auto &b) {
    return std::tie(a.entityId, a.colliderPartId, a.distance) <
           std::tie(b.entityId, b.colliderPartId, b.distance);
  });
  if (includeParts)
    result.erase(std::unique(result.begin(), result.end(),
                             [](const auto &a, const auto &b) {
                               return a.entityId == b.entityId &&
                                      a.colliderPartId == b.colliderPartId;
                             }),
                 result.end());
  return result;
}

std::vector<PhysicsQueryHit3D>
PhysicsWorld3D::overlapBox(const Vec3 center, const Vec3 size,
                           const std::string &layer,
                           const std::string &ignoredEntityId) const {
  if (size.x < 0.0F || size.y < 0.0F || size.z < 0.0F ||
      impl_->world == nullptr)
    return {};
  const JPH::BoxShape shape(JPH::Vec3(std::max(size.x * 0.5F, 0.001F),
                                      std::max(size.y * 0.5F, 0.001F),
                                      std::max(size.z * 0.5F, 0.001F)));
  JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
  const DemiQueryBodyFilter filter(impl_->world, &impl_->ids, layer,
                                   ignoredEntityId);
  impl_->physics.GetNarrowPhaseQuery().CollideShape(
      &shape, JPH::Vec3::sOne(),
      JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z)), {},
      JPH::RVec3::sZero(), collector, {}, {}, filter);
  std::vector<PhysicsQueryHit3D> result;
  for (const JPH::CollideShapeResult &hit : collector.mHits) {
    PhysicsQueryHit3D value = queryHit(
        impl_->world, impl_->ids, hit.mBodyID2, hit.mContactPointOn2,
        -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()), 0.0F, 0.0F);
    if (!value.entityId.empty() &&
        std::ranges::none_of(result, [&](const PhysicsQueryHit3D &existing) {
          return existing.entityId == value.entityId;
        }))
      result.push_back(std::move(value));
  }
  std::ranges::sort(result, {}, &PhysicsQueryHit3D::entityId);
  return result;
}

std::vector<PhysicsQueryHit3D>
PhysicsWorld3D::overlapCapsule(const Vec3 center, const float radius,
                               const float height, const std::string &layer,
                               const std::string &ignoredEntityId) const {
  if (radius < 0.0F || height < 2.0F * radius || impl_->world == nullptr)
    return {};
  const JPH::CapsuleShape shape(std::max(height * 0.5F - radius, 0.0F),
                                std::max(radius, 0.001F));
  JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
  const DemiQueryBodyFilter filter(impl_->world, &impl_->ids, layer,
                                   ignoredEntityId);
  impl_->physics.GetNarrowPhaseQuery().CollideShape(
      &shape, JPH::Vec3::sOne(),
      JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z)), {},
      JPH::RVec3::sZero(), collector, {}, {}, filter);
  std::vector<PhysicsQueryHit3D> result;
  for (const JPH::CollideShapeResult &hit : collector.mHits) {
    PhysicsQueryHit3D value = queryHit(
        impl_->world, impl_->ids, hit.mBodyID2, hit.mContactPointOn2,
        -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()), 0.0F, 0.0F);
    if (!value.entityId.empty() &&
        std::ranges::none_of(result, [&](const PhysicsQueryHit3D &existing) {
          return existing.entityId == value.entityId;
        }))
      result.push_back(std::move(value));
  }
  std::ranges::sort(result, {}, &PhysicsQueryHit3D::entityId);
  return result;
}

std::optional<PhysicsQueryHit3D>
PhysicsWorld3D::raycast(const Vec3 origin, const Vec3 direction,
                        const float distance, const std::string &layer,
                        const std::string &ignoredEntityId) const {
  const JPH::Vec3 unit = jolt(direction).NormalizedOr(JPH::Vec3::sZero());
  if (distance < 0.0F || unit.IsNearZero() || impl_->world == nullptr)
    return std::nullopt;
  const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z),
                          unit * distance);
  JPH::RayCastResult result;
  const DemiQueryBodyFilter filter(impl_->world, &impl_->ids, layer,
                                   ignoredEntityId);
  if (!impl_->physics.GetNarrowPhaseQuery().CastRay(ray, result, {}, {},
                                                    filter))
    return std::nullopt;
  const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
  JPH::BodyLockRead lock(impl_->physics.GetBodyLockInterface(), result.mBodyID);
  const JPH::Vec3 normal = lock.Succeeded()
                               ? lock.GetBody().GetWorldSpaceSurfaceNormal(
                                     result.mSubShapeID2, point)
                               : -unit;
  auto hit = queryHit(impl_->world, impl_->ids, result.mBodyID, point, normal,
                      result.mFraction * distance, result.mFraction);
  if (lock.Succeeded()) {
    const auto part = lock.GetBody().GetShape()->GetSubShapeUserData(result.mSubShapeID2);
    if (part > 0) {
      const auto record = impl_->bodies.find(hit.entityId);
      if (record != impl_->bodies.end() && part <= record->second.partIds.size())
        hit.colliderPartId = record->second.partIds[part - 1];
    }
  }
  return hit;
}

std::optional<PhysicsQueryHit3D>
PhysicsWorld3D::castSphere(const Vec3 origin, const float radius,
                           const Vec3 direction, const float distance,
                           const std::string &layer,
                           const std::string &ignoredEntityId, bool includeTriggers) const {
  if (radius < 0.0F)
    return std::nullopt;
  const JPH::SphereShape shape(std::max(radius, 0.001F));
  const JPH::Vec3 unit = jolt(direction).NormalizedOr(JPH::Vec3::sZero());
  if (distance < 0.0F || unit.IsNearZero() || impl_->world == nullptr)
    return std::nullopt;
  const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
      &shape, JPH::Vec3::sOne(),
      JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z)),
      unit * distance);
  JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
  const DemiQueryBodyFilter filter(impl_->world, &impl_->ids, layer,
                                   ignoredEntityId, includeTriggers);
  impl_->physics.GetNarrowPhaseQuery().CastShape(cast, {}, JPH::RVec3::sZero(),
                                                 collector, {}, {}, filter);
  if (!collector.HadHit())
    return std::nullopt;
  const JPH::ShapeCastResult &hit = collector.mHit;
  return queryHit(impl_->world, impl_->ids, hit.mBodyID2, hit.mContactPointOn2,
                  -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()),
                  hit.mFraction * distance, hit.mFraction);
}

std::optional<PhysicsQueryHit3D>
PhysicsWorld3D::castCapsule(const Vec3 origin, const float radius,
                            const float height, const Vec3 direction,
                            const float distance, const std::string &layer,
                            const std::string &ignoredEntityId) const {
  if (radius < 0.0F || height < 2.0F * radius)
    return std::nullopt;
  const JPH::CapsuleShape shape(std::max(height * 0.5F - radius, 0.0F),
                                std::max(radius, 0.001F));
  const JPH::Vec3 unit = jolt(direction).NormalizedOr(JPH::Vec3::sZero());
  if (distance < 0.0F || unit.IsNearZero() || impl_->world == nullptr)
    return std::nullopt;
  const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
      &shape, JPH::Vec3::sOne(),
      JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z)),
      unit * distance);
  JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
  const DemiQueryBodyFilter filter(impl_->world, &impl_->ids, layer,
                                   ignoredEntityId);
  impl_->physics.GetNarrowPhaseQuery().CastShape(cast, {}, JPH::RVec3::sZero(),
                                                 collector, {}, {}, filter);
  if (!collector.HadHit())
    return std::nullopt;
  const JPH::ShapeCastResult &hit = collector.mHit;
  return queryHit(impl_->world, impl_->ids, hit.mBodyID2, hit.mContactPointOn2,
                  -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()),
                  hit.mFraction * distance, hit.mFraction);
}

} // namespace demi::runtime
