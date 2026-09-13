#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace demi::runtime {
struct World;
struct Entity;
[[nodiscard]] std::span<const struct MeshDent3D> entityMeshDents3D(const Entity &entity);

// Runtime-only impact in mesh coordinates. The metric preserves a spherical
// world-space brush under nonuniform scale at the time of impact.
struct MeshDent3D {
  Vec3 center;
  Vec3 displacement;
  Vec3 metricScale{1, 1, 1};
  float radius = 0.0F;
};

inline constexpr std::size_t MaximumMeshDents3D = 32;

[[nodiscard]] bool dentMesh3D(World &world, const std::string &entityId,
                              Vec3 worldPoint, Vec3 worldDirection,
                              float radius, float depth, std::string &error);
[[nodiscard]] bool resetMeshDents3D(World &world, const std::string &entityId);
void deformMeshPositions3D(std::span<Vec3> positions,
                           std::span<const MeshDent3D> dents);
[[nodiscard]] std::uint64_t
meshDentsSignature3D(std::span<const MeshDent3D> dents);
[[nodiscard]] Vec3 meshDentBoundsExpansion3D(std::span<const MeshDent3D> dents);
} // namespace demi::runtime
