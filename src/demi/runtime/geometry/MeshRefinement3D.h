#pragma once
#include "demi/runtime/geometry/MeshDeformation3D.h"
#include <vector>

namespace demi::runtime {
struct MeshGeometry3D {
  std::vector<Vec3> positions;
  std::vector<Vec2> uvs;
  std::vector<std::uint32_t> indices;
  std::vector<std::uint32_t> colors;
};

// Bounded, uniform triangle refinement preserves matching edges, UV seams,
// material vertex colors, and source geometry. Runs only for dented instances.
[[nodiscard]] bool refineMeshForDents3D(std::span<const Vec3> positions,
                                        std::span<const Vec2> uvs,
                                        std::span<const std::uint32_t> indices,
                                        std::span<const std::uint32_t> colors,
                                        std::span<const MeshDent3D> dents,
                                        MeshGeometry3D &result,
                                        std::string &error);
inline constexpr std::size_t MaximumRefinedMeshTriangles3D = 200000;
inline constexpr unsigned MaximumMeshRefinementLevels3D = 3;
} // namespace demi::runtime
