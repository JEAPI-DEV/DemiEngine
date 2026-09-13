#include "CompoundTransitionProbe.h"
#include "BlastFixture.h"

// Jolt's platform and assertion definitions must precede every other SDK
// header.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

class JoltLifetime {
public:
  JoltLifetime() {
    require(JPH::Factory::sInstance == nullptr,
            "probe must own its Jolt lifetime");
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory;
    JPH::RegisterTypes();
  }
  ~JoltLifetime() {
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
  }
};

class PhysicsFixture {
public:
  explicit PhysicsFixture(unsigned capacity)
      : layers(2, 2), pairs(2), temporary(16 * 1024 * 1024), jobs(2048) {
    layers.MapObjectToBroadPhaseLayer(0, JPH::BroadPhaseLayer(0));
    layers.MapObjectToBroadPhaseLayer(1, JPH::BroadPhaseLayer(1));
    pairs.EnableCollision(0, 1);
    pairs.EnableCollision(1, 1);
    broadFilter = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
        layers, 2, pairs, 2);
    physics.Init(capacity, 0, 32768, 32768, layers, *broadFilter, pairs);
  }
  ~PhysicsFixture() { clear(); }
  void clear() {
    JPH::BodyIDVector bodies;
    physics.GetBodies(bodies);
    auto &interface = physics.GetBodyInterface();
    for (const auto &id : bodies) {
      if (interface.IsAdded(id))
        interface.RemoveBody(id);
      interface.DestroyBody(id);
    }
  }
  void step() {
    require(physics.Update(1.0F / 60.0F, 1, &temporary, &jobs) ==
                JPH::EPhysicsUpdateError::None,
            "physics update capacity error");
  }
  JPH::BroadPhaseLayerInterfaceTable layers;
  JPH::ObjectLayerPairFilterTable pairs;
  std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> broadFilter;
  JPH::TempAllocatorImpl temporary;
  JPH::JobSystemSingleThreaded jobs;
  JPH::PhysicsSystem physics;
};

struct Chunk {
  JPH::Vec3 position;
  JPH::RefConst<JPH::Shape> shape;
};
using Group = std::vector<std::uint32_t>;

JPH::RefConst<JPH::Shape> compound(const std::vector<Chunk> &chunks,
                                   const Group &ids) {
  JPH::StaticCompoundShapeSettings settings;
  for (auto id : ids)
    settings.AddShape(chunks.at(id).position, JPH::Quat::sIdentity(),
                      chunks.at(id).shape, id);
  const auto created = settings.Create();
  require(!created.HasError(), "compound shape creation failed");
  return created.Get();
}

struct Assembly {
  JPH::BodyID body;
  Group chunks;
};

JPH::Vec3 angularMomentum(const JPH::Body &body) {
  // Shape mass properties are about its own center of mass; transform the
  // tensor through the current body orientation before applying world omega.
  const auto rotation = JPH::Mat44::sRotation(body.GetRotation());
  return rotation.Multiply3x3(
      body.GetShape()->GetMassProperties().mInertia.Multiply3x3(
          rotation.Multiply3x3Transposed(body.GetAngularVelocity())));
}

void checkChunkQueries(PhysicsFixture &world, const std::vector<Chunk> &chunks,
                       const std::vector<Assembly> &assemblies) {
  for (const auto &assembly : assemblies) {
    JPH::RVec3 origin;
    JPH::Quat rotation;
    JPH::RefConst<JPH::Shape> shape;
    {
      JPH::BodyLockRead lock(world.physics.GetBodyLockInterface(),
                             assembly.body);
      require(lock.Succeeded(), "chunk owner is no longer a live body");
      origin = lock.GetBody().GetPosition();
      rotation = lock.GetBody().GetRotation();
      shape = lock.GetBody().GetShape();
    }
    for (const auto id : assembly.chunks) {
      const auto center = origin + rotation * chunks[id].position;
      JPH::RayCastResult hit;
      require(world.physics.GetNarrowPhaseQuery().CastRay(
                  JPH::RRayCast(center + rotation * JPH::Vec3(0, 0, 2),
                                rotation * JPH::Vec3(0, 0, -4)),
                  hit),
              "chunk collider disappeared");
      require(hit.mBodyID == assembly.body,
              "ray hit the wrong replacement body");
      require(shape->GetSubShapeUserData(hit.mSubShapeID2) == id + 1,
              "replacement subshape no longer maps to the stable chunk ID");
    }
  }
}
} // namespace

CompoundTransitionResult
runCompoundTransitionProbe(const CompoundTransitionOptions &options) {
  require(options.chunks >= 2 && options.chunks <= 256 &&
              options.groupSize > 0 && options.physicsSteps <= 600,
          "invalid compound probe options");
  JoltLifetime lifetime;
  PhysicsFixture world(options.maximumBodies ? options.maximumBodies
                                             : options.chunks + 4);
  CompoundTransitionResult result;
  auto &interface = world.physics.GetBodyInterface();
  const JPH::BodyID floor = interface.CreateAndAddBody(
      JPH::BodyCreationSettings(
          new JPH::BoxShape(
              JPH::Vec3(options.chunks + 10, 0.5F, options.chunks + 10)),
          JPH::RVec3(0, -0.5F, 0), JPH::Quat::sIdentity(),
          JPH::EMotionType::Static, 0),
      JPH::EActivation::DontActivate);
  require(!floor.IsInvalid(), "floor allocation failed");
  std::vector<Chunk> chunks;
  Group all(options.chunks);
  std::iota(all.begin(), all.end(), 0U);
  auto start = Clock::now();
  for (auto id : all) {
    JPH::BoxShapeSettings box(JPH::Vec3::sReplicate(0.45F), 0.01F);
    box.SetDensity(float(1 + id % 3) / (0.9F * 0.9F * 0.9F));
    box.mUserData = id + 1;
    const auto shape = box.Create();
    require(!shape.HasError(), "chunk shape creation failed");
    chunks.push_back(
        {JPH::Vec3(float(id) - float(options.chunks - 1) * 0.5F, 0, 0),
         shape.Get()});
  }
  auto intactShape = compound(chunks, all);
  result.initialShapeMs = milliseconds(start);
  JPH::BodyCreationSettings settings(
      intactShape, JPH::RVec3(0, 5, 0),
      JPH::Quat::sRotation(JPH::Vec3::sAxisY(), 0.45F),
      JPH::EMotionType::Dynamic, 1);
  settings.mLinearVelocity = JPH::Vec3(0.5F, 0, 0.2F);
  settings.mAngularVelocity = JPH::Vec3(0.05F, 0.3F, 0.07F);
  settings.mLinearDamping = settings.mAngularDamping = 0;
  auto *source = interface.CreateBody(settings);
  require(source != nullptr, "intact body allocation failed");
  const auto sourceId = source->GetID();
  interface.AddBody(sourceId, JPH::EActivation::Activate);
  world.physics.OptimizeBroadPhase();
  for (unsigned i = 0; i < 3; ++i)
    world.step();
  require(world.physics.GetNumBodies() == 2,
          "intact chunks must form one body, plus the floor");
  checkChunkQueries(world, chunks, {{sourceId, all}});

  start = Clock::now();
  const auto proposal = runBlastFixture(
      {.chunks = options.chunks, .groupSize = options.groupSize});
  result.blastTotalMs = milliseconds(start);
  result.groups = static_cast<std::uint32_t>(proposal.groups.size());
  result.damageMs = proposal.applyTotalMs;
  result.splitMs = proposal.splitMs;
  Group coverage;
  for (const auto &group : proposal.groups)
    coverage.insert(coverage.end(), group.begin(), group.end());
  std::ranges::sort(coverage);
  require(coverage == all,
          "Blast result must partition all chunks exactly once");

  const auto origin = source->GetPosition();
  const auto rotation = source->GetRotation();
  const auto parentCom = source->GetCenterOfMassPosition();
  const auto parentVelocity = source->GetLinearVelocity();
  const auto omega = source->GetAngularVelocity();
  const float parentMass =
      1.0F / source->GetMotionProperties()->GetInverseMass();
  const auto parentLinear = parentMass * parentVelocity;
  const auto parentAngular = angularMomentum(*source);
  std::vector<Assembly> active{{sourceId, all}};
  result.peakBodies = 2;

  if (proposal.groups.size() > 1) {
    std::vector<JPH::RefConst<JPH::Shape>> shapes;
    start = Clock::now();
    for (const auto &group : proposal.groups)
      shapes.push_back(compound(chunks, group));
    result.replacementShapesMs = milliseconds(start);
    std::vector<Assembly> staged;
    std::vector<JPH::BodyID> ids;
    staged.reserve(shapes.size());
    ids.reserve(shapes.size());
    start = Clock::now();
    for (std::size_t i = 0; i < shapes.size(); ++i) {
      JPH::BodyCreationSettings child(shapes[i], origin, rotation,
                                      JPH::EMotionType::Dynamic, 1);
      const auto childCom = origin + rotation * shapes[i]->GetCenterOfMass();
      child.mLinearVelocity =
          parentVelocity + omega.Cross(JPH::Vec3(childCom - parentCom));
      child.mAngularVelocity = omega;
      child.mLinearDamping = child.mAngularDamping = 0;
      auto *body = interface.CreateBody(child);
      if (!body)
        break;
      ids.push_back(body->GetID());
      staged.push_back({body->GetID(), proposal.groups[i]});
    }
    result.prepareBodiesMs = milliseconds(start);
    result.peakBodies = world.physics.GetNumBodies();
    if (staged.size() != shapes.size()) {
      start = Clock::now();
      for (auto id : ids)
        interface.DestroyBody(id);
      result.rollbackMs = milliseconds(start);
    } else {
      start = Clock::now();
      auto prepared =
          interface.AddBodiesPrepare(ids.data(), static_cast<int>(ids.size()));
      result.prepareBodiesMs += milliseconds(start);
      if (options.cancelBeforeCommit) {
        start = Clock::now();
        interface.AddBodiesAbort(ids.data(), static_cast<int>(ids.size()),
                                 prepared);
        for (auto id : ids)
          interface.DestroyBody(id);
        result.rollbackMs = milliseconds(start);
      } else {
        // No simulation/query callback runs inside this fixed-step boundary.
        // All body slots and broadphase additions are prepared before
        // retirement.
        start = Clock::now();
        interface.RemoveBody(sourceId);
        interface.AddBodiesFinalize(ids.data(), static_cast<int>(ids.size()),
                                    prepared, JPH::EActivation::Activate);
        interface.DestroyBody(sourceId);
        active = std::move(staged);
        result.commitMs = milliseconds(start);
        result.committed = true;
      }
    }
  }

  require(world.physics.GetNumBodies() == active.size() + 1,
          "staging leaked body slots");
  float mass = 0;
  JPH::Vec3 linear = JPH::Vec3::sZero(), angular = JPH::Vec3::sZero();
  for (const auto &assembly : active) {
    JPH::BodyLockRead lock(world.physics.GetBodyLockInterface(), assembly.body);
    require(lock.Succeeded(), "missing committed body");
    const auto &body = lock.GetBody();
    const float childMass = 1.0F / body.GetMotionProperties()->GetInverseMass();
    float expectedMass = 0;
    for (auto chunk : assembly.chunks)
      expectedMass += float(1 + chunk % 3);
    require(std::abs(childMass - expectedMass) < 0.001F * expectedMass,
            "body mass does not match its chunk densities");
    const auto momentum = childMass * body.GetLinearVelocity();
    mass += childMass;
    linear += momentum;
    angular +=
        angularMomentum(body) +
        JPH::Vec3(body.GetCenterOfMassPosition() - parentCom).Cross(momentum);
    require((body.GetPosition() - origin).Length() < 0.001F,
            "shape origin jumped at split");
    require(std::abs(std::abs(body.GetRotation().Dot(rotation)) - 1.0F) <
                0.0001F,
            "shape orientation jumped at split");
    require(
        (body.GetInverseInertia().Multiply3x3(angularMomentum(body)) - omega)
                .Length() < 0.002F,
        "native inertia disagrees with conserved shape mass properties");
    require((body.GetAngularVelocity() - omega).Length() < 0.001F,
            "angular velocity was lost");
    const auto expected =
        parentVelocity +
        omega.Cross(JPH::Vec3(body.GetCenterOfMassPosition() - parentCom));
    require((body.GetLinearVelocity() - expected).Length() < 0.001F,
            "rigid velocity field was not inherited");
  }
  result.massError = std::abs(mass - parentMass);
  result.linearMomentumError = (linear - parentLinear).Length();
  result.angularMomentumError = (angular - parentAngular).Length();
  require(result.massError < 0.001F * std::max(parentMass, 1.0F),
          "mass not conserved");
  require(result.linearMomentumError <
              0.002F * std::max(parentLinear.Length(), 1.0F),
          "linear momentum not conserved");
  require(result.angularMomentumError <
              0.002F * std::max(parentAngular.Length(), 1.0F),
          "angular momentum/inertia not conserved");
  checkChunkQueries(world, chunks, active);

  for (unsigned step = 0; step < options.physicsSteps; ++step) {
    start = Clock::now();
    world.step();
    const auto duration = milliseconds(start);
    result.physicsTotalMs += duration;
    result.physicsMaxMs = std::max(result.physicsMaxMs, duration);
  }
  if (options.physicsSteps >= 180) {
    for (const auto &assembly : active) {
      JPH::BodyLockRead lock(world.physics.GetBodyLockInterface(),
                             assembly.body);
      require(lock.Succeeded(), "settling body disappeared");
      require(lock.GetBody().GetWorldSpaceBounds().mMin.GetY() > -0.08F &&
                  lock.GetBody().GetCenterOfMassPosition().GetY() < 4.0F,
              "gravity/floor collision failed after transition");
    }
  }
  start = Clock::now();
  world.clear();
  result.cleanupMs = milliseconds(start);
  require(world.physics.GetNumBodies() == 0,
          "world cleanup leaked native bodies");
  return result;
}
