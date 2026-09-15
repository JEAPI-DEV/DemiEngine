#include "demi/runtime/physics/JoltCompoundShape3D.h"
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

namespace demi::runtime {
JPH::ShapeRefC
createJoltCompoundShape3D(const std::vector<ColliderPart3D> &parts,
                          Vec3 scale) {
  if (parts.empty())
    return {};
  JPH::StaticCompoundShapeSettings compound;
  for (std::size_t index = 0; index < parts.size(); ++index) {
    JPH::Array<JPH::Vec3> points;
    points.reserve(parts[index].points.size());
    for (Vec3 point : parts[index].points)
      points.emplace_back(point.x * scale.x, point.y * scale.y,
                          point.z * scale.z);
    JPH::ConvexHullShapeSettings settings(points);
    settings.mUserData = index + 1;
    auto leaf = settings.Create();
    if (leaf.HasError())
      return {};
    compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), leaf.Get());
  }
  auto result = compound.Create();
  return result.HasError() ? JPH::ShapeRefC{} : result.Get();
}
} // namespace demi::runtime
