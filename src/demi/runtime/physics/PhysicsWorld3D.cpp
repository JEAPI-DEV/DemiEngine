#include "demi/runtime/physics/PhysicsWorld3D.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/World.h"

// Jolt.h defines platform/compiler macros required by every other Jolt header.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
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
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {
namespace {

constexpr JPH::ObjectLayer StaticLayer = 0;
constexpr JPH::ObjectLayer MovingLayer = 1;

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

struct JoltLifetime {
  JoltLifetime() {
    std::scoped_lock lock(mutex);
    if (references++ != 0)
      return;
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  }
  ~JoltLifetime() {
    std::scoped_lock lock(mutex);
    if (--references != 0)
      return;
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
  }
  static inline std::mutex mutex;
  static inline int references = 0;
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
    mix(hash, value->isTrigger);
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
    mix(hash, body->continuous);
    mix(hash, body->allowSleep);
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
             convex != nullptr && convex->points.size() >= 4) {
    JPH::Array<JPH::Vec3> points;
    points.reserve(convex->points.size());
    for (const Vec3 point : convex->points)
      points.emplace_back(point.x * scale.x, point.y * scale.y,
                          point.z * scale.z);
    JPH::ConvexHullShapeSettings settings(points);
    const auto result = settings.Create();
    if (result.HasError())
      return {};
    shape = result.Get();
    offset = {convex->offset.x * scale.x, convex->offset.y * scale.y,
              convex->offset.z * scale.z};
  } else if (const auto *model = entity.component<ModelCollider3DComponent>()) {
    const auto triangles = resolvedTriangleCollider3D(world, entity);
    if (triangles == nullptr || triangles->empty())
      return {};
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

[[nodiscard]] std::string pairKey(std::string first, std::string second) {
  if (second < first)
    std::swap(first, second);
  return first + '\0' + second;
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
    bool added = true;
  };
  struct RawContact {
    std::string first;
    std::string second;
    Vec3 point;
    Vec3 normal;
    float penetration = 0.0F;
    bool trigger = false;
  };
  struct CharacterRecord {
    JPH::Ref<JPH::CharacterVirtual> character;
    std::uint64_t shapeSignature = 0;
    float padding = 0.0F;
    float slopeLimit = 0.0F;
  };

  JoltLifetime lifetime;
  BroadPhaseLayers broadPhaseLayers;
  ObjectVsBroadPhaseFilter objectVsBroadPhase;
  ObjectLayerPairFilter layerPairs;
  JPH::TempAllocatorMalloc allocator;
  JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
  JPH::PhysicsSystem physics;
  World *world = nullptr;
  std::unordered_map<std::string, BodyRecord> bodies;
  std::unordered_map<std::string, CharacterRecord> characters;
  std::unordered_map<std::uint32_t, std::string> ids;
  std::unordered_map<std::string, RawContact> contacts;

  Impl() {
    physics.Init(65536, 0, 65536, 10240, broadPhaseLayers, objectVsBroadPhase,
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

  JPH::ValidateResult
  OnContactValidate(const JPH::Body &first, const JPH::Body &second,
                    JPH::RVec3Arg, const JPH::CollideShapeResult &) override {
    if (world == nullptr)
      return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
    const Entity *firstEntity = findEntity(*world, idFor(first.GetID()));
    const Entity *secondEntity = findEntity(*world, idFor(second.GetID()));
    return firstEntity != nullptr && secondEntity != nullptr &&
                   layersCollide(*world, *firstEntity, *secondEntity)
               ? JPH::ValidateResult::AcceptAllContactsForThisBodyPair
               : JPH::ValidateResult::RejectAllContactsForThisBodyPair;
  }

  void record(const JPH::Body &first, const JPH::Body &second,
              const JPH::ContactManifold &manifold) {
    const std::string firstId = idFor(first.GetID());
    const std::string secondId = idFor(second.GetID());
    if (firstId.empty() || secondId.empty())
      return;
    const JPH::RVec3 point = manifold.mRelativeContactPointsOn1.empty()
                                 ? manifold.mBaseOffset
                                 : manifold.GetWorldSpaceContactPointOn1(0);
    contacts[pairKey(firstId, secondId)] = {
        .first = firstId,
        .second = secondId,
        .point = demi(point),
        .normal = demi(manifold.mWorldSpaceNormal),
        .penetration = std::max(manifold.mPenetrationDepth, 0.0F),
        .trigger = first.IsSensor() || second.IsSensor(),
    };
  }

  void OnContactAdded(const JPH::Body &first, const JPH::Body &second,
                      const JPH::ContactManifold &manifold,
                      JPH::ContactSettings &) override {
    record(first, second, manifold);
  }
  void OnContactPersisted(const JPH::Body &first, const JPH::Body &second,
                          const JPH::ContactManifold &manifold,
                          JPH::ContactSettings &) override {
    record(first, second, manifold);
  }
  void OnContactRemoved(const JPH::SubShapeIDPair &pair) override {
    const std::string first = idFor(pair.GetBody1ID());
    const std::string second = idFor(pair.GetBody2ID());
    if (!first.empty() && !second.empty())
      contacts.erase(pairKey(first, second));
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

void PhysicsWorld3D::step(World &world, const float fixedDt,
                          const Vec3 gravity) {
  if (fixedDt <= 0.0F)
    return;
  impl_->world = &world;
  impl_->physics.SetGravity(jolt(gravity));
  auto &interface = impl_->physics.GetBodyInterface();
  std::unordered_set<std::string> live;

  for (Entity &entity : world.entities) {
    if (!entity.enabled || !entity.hasComponent<Transform3DComponent>() ||
        !hasCollider(entity) ||
        entity.hasComponent<CharacterController3DComponent>())
      continue;
    live.insert(entity.id);
    auto *body = entity.component<Rigidbody3DComponent>();
    const std::uint64_t signature = shapeSignature(world, entity);
    auto found = impl_->bodies.find(entity.id);
    if (found != impl_->bodies.end() && found->second.signature != signature) {
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
        settings.mGravityFactor = body->useGravity ? body->gravityScale : 0.0F;
        settings.mFriction = body->friction;
        settings.mRestitution = body->restitution;
        settings.mAllowSleeping = body->allowSleep;
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
      interface.AddBody(bodyId, body != nullptr && body->awake
                                    ? JPH::EActivation::Activate
                                    : JPH::EActivation::DontActivate);
      if (body != nullptr && !body->bodyEnabled)
        interface.RemoveBody(bodyId);
      impl_->ids[bodyId.GetIndexAndSequenceNumber()] = entity.id;
      found = impl_->bodies
                  .emplace(entity.id,
                           Impl::BodyRecord{
                               .body = bodyId,
                               .signature = signature,
                               .previousPosition = resolved->position,
                               .currentPosition = resolved->position,
                               .lastAuthoredPosition = local.position,
                               .lastAuthoredRotation = local.rotation,
                               .added = body == nullptr || body->bodyEnabled})
                  .first;
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
          motionType(body) != JPH::EMotionType::Dynamic) {
        interface.SetPositionAndRotationWhenChanged(
            record.body,
            JPH::RVec3(resolved->position.x, resolved->position.y,
                       resolved->position.z),
            joltRotation(resolved->rotation), JPH::EActivation::Activate);
      }
      if (body != nullptr) {
        if (body->bodyEnabled != record.added) {
          if (body->bodyEnabled)
            interface.AddBody(record.body, JPH::EActivation::Activate);
          else
            interface.RemoveBody(record.body);
          record.added = body->bodyEnabled;
        }
        interface.SetLinearAndAngularVelocity(record.body, jolt(body->velocity),
                                              jolt(body->angularVelocity));
        interface.SetGravityFactor(
            record.body, body->useGravity ? body->gravityScale : 0.0F);
        if (body->awake)
          interface.ActivateBody(record.body);
        else
          interface.DeactivateBody(record.body);
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
          body->accumulatedForce.y != 0.0F || body->accumulatedForce.z != 0.0F)
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
  }

  for (auto iterator = impl_->bodies.begin();
       iterator != impl_->bodies.end();) {
    if (live.contains(iterator->first)) {
      ++iterator;
      continue;
    }
    interface.RemoveBody(iterator->second.body);
    interface.DestroyBody(iterator->second.body);
    impl_->ids.erase(iterator->second.body.GetIndexAndSequenceNumber());
    iterator = impl_->bodies.erase(iterator);
  }

  {
    ProfileScope scope("Physics3D.simulate");
    impl_->physics.Update(fixedDt, 1, &impl_->allocator, &impl_->jobs);
  }

  std::unordered_set<std::string> liveCharacters;
  std::vector<PhysicsContact3D> characterContacts;
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
      settings->mMaxSlopeAngle = JPH::DegreesToRadians(controller->slopeLimit);
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

  for (auto &[id, record] : impl_->bodies) {
    Entity *entity = findEntity(world, id);
    auto *transform =
        entity != nullptr ? entity->component<Transform3DComponent>() : nullptr;
    auto *body =
        entity != nullptr ? entity->component<Rigidbody3DComponent>() : nullptr;
    if (transform == nullptr)
      continue;
    record.previousPosition = record.currentPosition;
    const JPH::RVec3 position = interface.GetPosition(record.body);
    const JPH::Quat rotation = interface.GetRotation(record.body);
    record.currentPosition = demi(position);
    if (transform->parent.empty())
      transform->position = record.currentPosition;
    if (transform->parent.empty())
      transform->rotation = demi(rotation.GetEulerAngles());
    record.lastAuthoredPosition = transform->position;
    record.lastAuthoredRotation = transform->rotation;
    if (body != nullptr) {
      body->velocity = demi(interface.GetLinearVelocity(record.body));
      body->angularVelocity = demi(interface.GetAngularVelocity(record.body));
      body->awake = interface.IsActive(record.body);
    }
  }

  world.previousPhysicsContacts3D = world.physicsContacts3D;
  world.physicsContacts3D.clear();
  const auto previousContains = [&](const std::string &entity,
                                    const std::string &other) {
    return std::ranges::any_of(
        world.previousPhysicsContacts3D, [&](const PhysicsContact3D &contact) {
          return contact.entityId == entity && contact.otherEntityId == other &&
                 contact.phase != "exit";
        });
  };
  for (const auto &[key, raw] : impl_->contacts) {
    (void)key;
    const Entity *first = findEntity(world, raw.first);
    const Entity *second = findEntity(world, raw.second);
    if (first == nullptr || second == nullptr)
      continue;
    const auto append = [&](const std::string &entity, const std::string &other,
                            const std::string &otherLayer, const Vec3 normal) {
      world.physicsContacts3D.push_back(
          {.entityId = entity,
           .otherEntityId = other,
           .otherLayer = otherLayer,
           .phase = previousContains(entity, other) ? "stay" : "enter",
           .point = raw.point,
           .normal = normal,
           .penetration = raw.penetration,
           .isTrigger = raw.trigger});
    };
    append(raw.first, raw.second, colliderLayer(*second),
           {-raw.normal.x, -raw.normal.y, -raw.normal.z});
    append(raw.second, raw.first, colliderLayer(*first), raw.normal);
  }
  for (PhysicsContact3D contact : characterContacts) {
    contact.phase = previousContains(contact.entityId, contact.otherEntityId)
                        ? "stay"
                        : "enter";
    world.physicsContacts3D.push_back(std::move(contact));
  }
  for (const PhysicsContact3D &previous : world.previousPhysicsContacts3D) {
    if (previous.phase == "exit" ||
        std::ranges::any_of(
            world.physicsContacts3D, [&](const PhysicsContact3D &current) {
              return current.entityId == previous.entityId &&
                     current.otherEntityId == previous.otherEntityId;
            }))
      continue;
    PhysicsContact3D exited = previous;
    exited.phase = "exit";
    exited.penetration = 0.0F;
    world.physicsContacts3D.push_back(std::move(exited));
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
  if (enabled)
    interface.AddBody(found->second.body, JPH::EActivation::Activate);
  else
    interface.RemoveBody(found->second.body);
  found->second.added = enabled;
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
                      std::string layer, std::string ignored)
      : world_(world), ids_(ids), layer_(std::move(layer)),
        ignored_(std::move(ignored)) {}

  bool ShouldCollide(const JPH::BodyID &body) const override {
    if (world_ == nullptr)
      return false;
    const auto found = ids_->find(body.GetIndexAndSequenceNumber());
    if (found == ids_->end() || found->second == ignored_)
      return false;
    const Entity *entity = findEntity(*world_, found->second);
    return entity != nullptr &&
           (layer_.empty() || colliderLayer(*entity) == layer_);
  }

private:
  const World *world_;
  const std::unordered_map<std::uint32_t, std::string> *ids_;
  std::string layer_;
  std::string ignored_;
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

std::vector<PhysicsQueryHit3D>
PhysicsWorld3D::overlapSphere(const Vec3 center, const float radius,
                              const std::string &layer,
                              const std::string &ignoredEntityId) const {
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
  return queryHit(impl_->world, impl_->ids, result.mBodyID, point, normal,
                  result.mFraction * distance, result.mFraction);
}

std::optional<PhysicsQueryHit3D>
PhysicsWorld3D::castSphere(const Vec3 origin, const float radius,
                           const Vec3 direction, const float distance,
                           const std::string &layer,
                           const std::string &ignoredEntityId) const {
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
