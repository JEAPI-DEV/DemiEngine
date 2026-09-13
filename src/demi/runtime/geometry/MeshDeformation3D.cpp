#include "demi/runtime/geometry/MeshDeformation3D.h"

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"

#include <bit>
#include <cmath>

namespace demi::runtime {
namespace {
bool finite(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

std::span<const MeshDent3D> entityMeshDents3D(const Entity &entity) {
  const auto *dentable = entity.component<Dentable3DComponent>();
  return dentable ? std::span<const MeshDent3D>(dentable->dents) : std::span<const MeshDent3D>{};
}

bool dentMesh3D(World &world, const std::string &entityId, Vec3 worldPoint,
                Vec3 worldDirection, float radius, float depth,
                std::string &error) {
  error.clear();
  auto *entity = findEntity(world, entityId);
  auto *dentable = entity ? entity->component<Dentable3DComponent>() : nullptr;
  if (!dentable) {
    error = "Mesh denting requires the Dentable3D component.";
    return false;
  }
  auto *mesh = entity ? entity->component<MeshRendererComponent>() : nullptr;
  if (!mesh || (mesh->model.empty() && mesh->vertices.empty())) {
    error = "Mesh denting requires a model or procedural triangle mesh.";
    return false;
  }
  if (entity->hasComponent<AnimationPlayer3DComponent>()) {
    error = "Mesh denting does not support skinned/animated entities yet.";
    return false;
  }
  const float length =
      std::hypot(worldDirection.x, worldDirection.y, worldDirection.z);
  if (!finite(worldPoint) || !finite(worldDirection) ||
      !std::isfinite(length) || length < 1e-6F || !std::isfinite(radius) ||
      !std::isfinite(depth) || radius <= 0.0F || depth <= 0.0F ||
      depth > radius * 0.5F) {
    error = "Dent requires finite coordinates, a nonzero direction, positive "
            "radius/depth, and depth <= radius/2.";
    return false;
  }
  if (dentable->dents.size() >= MaximumMeshDents3D) {
    error = "Mesh dent limit reached (32); reset the mesh before adding more.";
    return false;
  }
  auto transform = resolveWorldTransform3D(world, *entity);
  if (!transform) {
    error = "Mesh denting requires a valid Transform3D hierarchy.";
    return false;
  }
  transform->scale = {transform->scale.x * mesh->size.x,
                      transform->scale.y * mesh->size.y,
                      transform->scale.z * mesh->size.z};
  if (!finite(transform->scale) || std::abs(transform->scale.x) < 1e-6F ||
      std::abs(transform->scale.y) < 1e-6F ||
      std::abs(transform->scale.z) < 1e-6F) {
    error = "Mesh denting requires finite nonzero scale.";
    return false;
  }
  const Vec3 displacement{worldDirection.x / length * depth,
                          worldDirection.y / length * depth,
                          worldDirection.z / length * depth};
  MeshDent3D dent{.center = inverseTransformPoint3D(*transform, worldPoint),
                  .displacement =
                      inverseTransformVector3D(*transform, displacement),
                  .metricScale = transform->scale,
                  .radius = radius};
  if (!finite(dent.center) || !finite(dent.displacement)) {
    error = "Mesh dent could not be transformed into finite mesh coordinates.";
    return false;
  }
  dentable->dents.push_back(dent);
  return true;
}

bool resetMeshDents3D(World &world, const std::string &entityId) {
  auto *entity = findEntity(world, entityId);
  auto *dentable = entity ? entity->component<Dentable3DComponent>() : nullptr;
  if (!dentable)
    return false;
  dentable->dents.clear();
  return true;
}

void deformMeshPositions3D(std::span<Vec3> positions,
                           std::span<const MeshDent3D> dents) {
  for (const auto &dent : dents)
    for (Vec3 &position : positions) {
      const float x =
          (position.x - dent.center.x) * dent.metricScale.x / dent.radius;
      const float y =
          (position.y - dent.center.y) * dent.metricScale.y / dent.radius;
      const float z =
          (position.z - dent.center.z) * dent.metricScale.z / dent.radius;
      const float distanceSquared = x * x + y * y + z * z;
      if (distanceSquared >= 1.0F)
        continue;
      const float t = 1.0F - distanceSquared;
      const float weight = t * t * (3.0F - 2.0F * t);
      position.x += dent.displacement.x * weight;
      position.y += dent.displacement.y * weight;
      position.z += dent.displacement.z * weight;
    }
}

std::uint64_t meshDentsSignature3D(std::span<const MeshDent3D> dents) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto &dent : dents)
    for (const float value :
         {dent.center.x, dent.center.y, dent.center.z, dent.displacement.x,
          dent.displacement.y, dent.displacement.z, dent.metricScale.x,
          dent.metricScale.y, dent.metricScale.z, dent.radius}) {
      hash ^= std::bit_cast<std::uint32_t>(value);
      hash *= 1099511628211ULL;
    }
  return hash;
}

Vec3 meshDentBoundsExpansion3D(std::span<const MeshDent3D> dents) {
  Vec3 extent{};
  for (const auto &dent : dents) {
    extent.x += std::abs(dent.displacement.x);
    extent.y += std::abs(dent.displacement.y);
    extent.z += std::abs(dent.displacement.z);
  }
  return extent;
}
} // namespace demi::runtime
