#include "demi/runtime/physics/JoltBodyShape3D.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/physics/JoltCompoundShape3D.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <Jolt/Geometry/Triangle.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <cmath>

namespace demi::runtime {

JPH::ShapeRefC createJoltCapsuleShape3D(float radius, float totalHeight) {
  radius = std::max(radius, 0.001F);
  const float halfCylinder = totalHeight * 0.5F - radius;
  // A capsule with no cylindrical section is exactly a sphere. Jolt's
  // capsule constructor requires a strictly positive cylinder height.
  if (halfCylinder <= 0.0F) {
    return new JPH::SphereShape(radius);
  }
  return new JPH::CapsuleShape(halfCylinder, radius);
}

JPH::ShapeRefC createJoltBodyShape3D(const World &world,
                                     const PhysicsBodyInputs3D &inputs,
                                     const WorldTransform3D &resolved) {
  const Entity &entity = inputs.entity;
  const Vec3 scale{std::abs(resolved.scale.x), std::abs(resolved.scale.y),
                   std::abs(resolved.scale.z)};
  JPH::ShapeRefC shape;
  Vec3 offset;
  if (const auto *box = inputs.box) {
    const JPH::Vec3 half(std::max(box->size.x * scale.x * 0.5F, 0.001F),
                         std::max(box->size.y * scale.y * 0.5F, 0.001F),
                         std::max(box->size.z * scale.z * 0.5F, 0.001F));
    shape = new JPH::BoxShape(half);
    offset = {box->offset.x * scale.x, box->offset.y * scale.y,
              box->offset.z * scale.z};
  } else if (const auto *sphere = inputs.sphere) {
    const float radius = sphere->radius * std::max({scale.x, scale.y, scale.z});
    shape = new JPH::SphereShape(std::max(radius, 0.001F));
    offset = {sphere->offset.x * scale.x, sphere->offset.y * scale.y,
              sphere->offset.z * scale.z};
  } else if (const auto *capsule = inputs.capsule) {
    const float radius = capsule->radius * std::max(scale.x, scale.z);
    const float totalHeight = capsule->height * scale.y;
    shape = createJoltCapsuleShape3D(radius, totalHeight);
    offset = {capsule->offset.x * scale.x, capsule->offset.y * scale.y,
              capsule->offset.z * scale.z};
  } else if (const auto *convex = inputs.convex;
             (convex != nullptr && convex->points.size() >= 4) ||
             resolvedConvexCollider3D(world, entity) != nullptr) {
    const auto &sourcePoints =
        convex ? convex->points : *resolvedConvexCollider3D(world, entity);
    JPH::Array<JPH::Vec3> points;
    points.reserve(sourcePoints.size());
    for (const Vec3 point : sourcePoints) {
      points.emplace_back(point.x * scale.x, point.y * scale.y,
                          point.z * scale.z);
    }
    JPH::ConvexHullShapeSettings settings(points);
    const auto result = settings.Create();
    if (result.HasError()) {
      return {};
    }
    shape = result.Get();
    if (convex) {
      offset = {convex->offset.x * scale.x, convex->offset.y * scale.y,
                convex->offset.z * scale.z};
    }
  } else if (const auto *model = inputs.model) {
    if (const auto *parts = resolvedCompoundCollider3D(world, entity)) {
      return createJoltCompoundShape3D(*parts, resolved.scale);
    }
    const auto triangles = resolvedTriangleCollider3D(world, entity);
    if (triangles != nullptr && !triangles->empty()) {
      JPH::TriangleList list;
      list.reserve(triangles->size());
      for (const TriangleCollider3D &triangle : *triangles) {
        list.emplace_back(
            JPH::Vec3(triangle.a.x * scale.x, triangle.a.y * scale.y,
                      triangle.a.z * scale.z),
            JPH::Vec3(triangle.b.x * scale.x, triangle.b.y * scale.y,
                      triangle.b.z * scale.z),
            JPH::Vec3(triangle.c.x * scale.x, triangle.c.y * scale.y,
                      triangle.c.z * scale.z));
      }
      JPH::MeshShapeSettings settings(list);
      const auto result = settings.Create();
      if (result.HasError()) {
        return {};
      }
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
  if (shape == nullptr) {
    return {};
  }
  if (std::abs(offset.x) > 0.000001F || std::abs(offset.y) > 0.000001F ||
      std::abs(offset.z) > 0.000001F) {
    JPH::RotatedTranslatedShapeSettings settings(
        JPH::Vec3(offset.x, offset.y, offset.z), JPH::Quat::sIdentity(), shape);
    const auto result = settings.Create();
    if (result.HasError()) {
      return {};
    }
    shape = result.Get();
  }
  return shape;
}

} // namespace demi::runtime
