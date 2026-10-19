#pragma once

#include "demi/runtime/physics/PhysicsBodyInputs3D.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <map>
#include <string>

namespace demi::runtime {

// Immutable local-space ModelCollider geometry is shared across differently
// scaled body instances. Revisions replace entries without mutating live
// shapes.
class JoltModelShapeCache3D {
public:
  [[nodiscard]] JPH::ShapeRefC resolve(const World &world,
                                       const PhysicsBodyInputs3D &inputs,
                                       const WorldTransform3D &transform);
  void adopt(JoltModelShapeCache3D &source);
  void eraseInline(const std::string &entityId);
  void retireUnused();
  [[nodiscard]] std::size_t geometryBuilds() const { return geometryBuilds_; }
  [[nodiscard]] double geometryBuildMilliseconds() const {
    return geometryBuildMilliseconds_;
  }

private:
  struct Entry {
    std::uint64_t revision = 0;
    JPH::ShapeRefC shape;
  };
  std::map<std::string, Entry> shapes_;
  std::size_t geometryBuilds_ = 0;
  double geometryBuildMilliseconds_ = 0.0;
};

// Backend-only shape construction from a borrowed native input snapshot.
[[nodiscard]] JPH::ShapeRefC
createJoltBodyShape3D(const World &world, const PhysicsBodyInputs3D &inputs,
                      const WorldTransform3D &transform,
                      JoltModelShapeCache3D *models = nullptr);
[[nodiscard]] JPH::ShapeRefC createJoltCapsuleShape3D(float radius,
                                                      float totalHeight);

} // namespace demi::runtime
