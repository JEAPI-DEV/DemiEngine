#include "demi/runtime/render/bgfx3d/MeshVertexPreparation3D.h"

#include <cmath>

namespace demi::runtime::render {

bool prepareMeshVertices3D(
    const std::span<const Vec3> positions, const std::span<const Vec2> uvs,
    const std::span<const std::uint32_t> indices, const std::uint32_t rgba,
    std::span<const Vec3> normals, const std::span<const std::uint32_t> colors,
    std::vector<GpuMeshVertex3D> &vertices, std::string &error) {
  if (positions.empty()) {
    error = "GPU mesh requires at least one vertex.";
    return false;
  }
  if ((!uvs.empty() && uvs.size() != positions.size()) ||
      (!normals.empty() && normals.size() != positions.size()) ||
      (!colors.empty() && colors.size() != positions.size())) {
    error = "GPU mesh attribute count must match its vertices.";
    return false;
  }
  if (indices.empty() || indices.size() % 3 != 0) {
    error = "GPU mesh indices must describe complete triangles.";
    return false;
  }
  for (const auto index : indices) {
    if (index >= positions.size()) {
      error = "GPU mesh index lies outside the vertex array.";
      return false;
    }
  }
  std::vector<Vec3> generatedNormals;
  if (normals.empty()) {
    generatedNormals.resize(positions.size());
    for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3) {
      const Vec3 a = positions[indices[triangle]];
      const Vec3 b = positions[indices[triangle + 1]];
      const Vec3 c = positions[indices[triangle + 2]];
      const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
      const Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
      const Vec3 face{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z,
                      ab.x * ac.y - ab.y * ac.x};
      for (std::size_t corner = 0; corner < 3; ++corner) {
        auto &normal = generatedNormals[indices[triangle + corner]];
        normal = {normal.x + face.x, normal.y + face.y, normal.z + face.z};
      }
    }
    for (auto &normal : generatedNormals) {
      const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y +
                                     normal.z * normal.z);
      normal = length > 0.000001F ? Vec3{normal.x / length, normal.y / length,
                                         normal.z / length}
                                  : Vec3{0, 1, 0};
    }
    normals = generatedNormals;
  }
  vertices.resize(positions.size());
  for (std::size_t index = 0; index < positions.size(); ++index) {
    const Vec2 uv = uvs.empty() ? Vec2{} : uvs[index];
    const Vec3 p = positions[index], n = normals[index];
    vertices[index] = {.x = p.x,
                       .y = p.y,
                       .z = p.z,
                       .nx = n.x,
                       .ny = n.y,
                       .nz = n.z,
                       .rgba = colors.empty() ? rgba : colors[index],
                       .u = uv.x,
                       .v = uv.y};
  }
  error.clear();
  return true;
}

} // namespace demi::runtime::render
