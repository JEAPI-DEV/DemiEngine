#include "demi/runtime/geometry/MeshRefinement3D.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace demi::runtime {
bool refineMeshForDents3D(std::span<const Vec3> positions,
                          std::span<const Vec2> uvs,
                          std::span<const std::uint32_t> indices,
                          std::span<const std::uint32_t> colors,
                          std::span<const MeshDent3D> dents,
                          MeshGeometry3D &result, std::string &error) {
  if (positions.empty() || (!uvs.empty() && uvs.size() != positions.size()) ||
      (!colors.empty() && colors.size() != positions.size()) ||
      (indices.empty() ? positions.size() : indices.size()) % 3 != 0 ||
      std::ranges::any_of(indices,
                          [&](auto i) { return i >= positions.size(); })) {
    error = "Invalid triangle mesh for dent refinement.";
    return false;
  }
  MeshGeometry3D mesh{.positions = {positions.begin(), positions.end()},
                      .uvs = {uvs.begin(), uvs.end()},
                      .indices = {indices.begin(), indices.end()},
                      .colors = {colors.begin(), colors.end()}};
  if (mesh.indices.empty()) {
    mesh.indices.resize(positions.size());
    std::iota(mesh.indices.begin(), mesh.indices.end(), 0U);
  }
  float targetEdge = std::numeric_limits<float>::max();
  for (const auto &dent : dents) {
    const float scale =
        std::max({std::abs(dent.metricScale.x), std::abs(dent.metricScale.y),
                  std::abs(dent.metricScale.z)});
    targetEdge = std::min(targetEdge, dent.radius / scale * 0.5F);
  }
  float longestEdge = 0;
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
    for (unsigned edge = 0; edge < 3; ++edge) {
      const auto a = mesh.positions[mesh.indices[i + edge]];
      const auto b = mesh.positions[mesh.indices[i + (edge + 1) % 3]];
      longestEdge =
          std::max(longestEdge, std::hypot(a.x - b.x, a.y - b.y, a.z - b.z));
    }
  for (unsigned level = 0;
       level < MaximumMeshRefinementLevels3D && longestEdge > targetEdge &&
       mesh.indices.size() / 3 <= MaximumRefinedMeshTriangles3D / 4;
       ++level) {
    std::unordered_map<std::uint64_t, std::uint32_t> midpoints;
    const auto midpoint = [&](std::uint32_t a, std::uint32_t b) {
      const std::uint64_t key =
          (std::uint64_t(std::min(a, b)) << 32) | std::max(a, b);
      if (const auto found = midpoints.find(key); found != midpoints.end())
        return found->second;
      const auto index = static_cast<std::uint32_t>(mesh.positions.size());
      const Vec3 p = mesh.positions[a], q = mesh.positions[b];
      mesh.positions.push_back({p.x * 0.5F + q.x * 0.5F,
                                p.y * 0.5F + q.y * 0.5F,
                                p.z * 0.5F + q.z * 0.5F});
      if (!mesh.uvs.empty()) {
        const Vec2 u = mesh.uvs[a], v = mesh.uvs[b];
        mesh.uvs.push_back({(u.x + v.x) * 0.5F, (u.y + v.y) * 0.5F});
      }
      if (!mesh.colors.empty()) {
        std::uint32_t color = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
          color |= ((((mesh.colors[a] >> shift) & 255) +
                     ((mesh.colors[b] >> shift) & 255)) /
                    2)
                   << shift;
        mesh.colors.push_back(color);
      }
      midpoints.emplace(key, index);
      return index;
    };
    std::vector<std::uint32_t> refined;
    refined.reserve(mesh.indices.size() * 4);
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
      const auto a = mesh.indices[i], b = mesh.indices[i + 1],
                 c = mesh.indices[i + 2];
      const auto ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
      refined.insert(refined.end(),
                     {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
    }
    mesh.indices = std::move(refined);
    longestEdge *= 0.5F;
  }
  result = std::move(mesh);
  return true;
}
} // namespace demi::runtime
