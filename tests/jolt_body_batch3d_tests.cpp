#include "demi/runtime/physics/JoltBodyBatch3D.h"
#include "demi/runtime/physics/JoltLifetime.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <iostream>
#include <stdexcept>

namespace {
struct Layers : JPH::BroadPhaseLayerInterface {
  JPH::uint GetNumBroadPhaseLayers() const override { return 1; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override {
    return JPH::BroadPhaseLayer(0);
  }
};
struct BroadFilter : JPH::ObjectVsBroadPhaseLayerFilter {
  bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override {
    return true;
  }
};
struct PairFilter : JPH::ObjectLayerPairFilter {
  bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override {
    return true;
  }
};
void expect(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void run() {
  demi::runtime::JoltLifetime lifetime;
  Layers layers;
  BroadFilter broad;
  PairFilter pairs;
  JPH::PhysicsSystem physics;
  physics.Init(4, 0, 4, 4, layers, broad, pairs);
  auto &bodies = physics.GetBodyInterface();
  JPH::BodyCreationSettings settings(
      new JPH::BoxShape(JPH::Vec3::sReplicate(0.5F)), JPH::RVec3::sZero(),
      JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, 0);
  auto original = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
  expect(!original.IsInvalid(), "Source body creation failed");
  {
    demi::runtime::JoltBodyBatch3D batch(bodies, 4);
    for (int i = 0; i < 3; ++i)
      expect(batch.create(settings) != nullptr,
             "Body preparation failed unexpectedly");
    expect(batch.create(settings) == nullptr,
           "Native capacity test did not exhaust pool");
  }
  expect(physics.GetNumBodies() == 1 && bodies.IsAdded(original),
         "Capacity failure damaged source or leaked staged bodies");
  {
    demi::runtime::JoltBodyBatch3D batch(bodies, 2);
    expect(batch.create(settings) != nullptr,
           "Preparation after rollback failed");
    batch.prepare();
    // Scope exit explicitly exercises AddBodiesAbort after broadphase
    // preparation.
  }
  expect(physics.GetNumBodies() == 1 && bodies.IsAdded(original),
         "Prepared cancellation leaked or retired source");
  bodies.RemoveBody(original);
  bodies.DestroyBody(original);
  expect(physics.GetNumBodies() == 0, "Native batch cleanup failed");
}
} // namespace
int main() {
  try {
    for (int i = 0; i < 20; ++i)
      run();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Native capacity rollback and prepared cancellation passed\n";
}
