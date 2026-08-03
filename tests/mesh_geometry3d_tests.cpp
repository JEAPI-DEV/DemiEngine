#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveMeshFactory3D.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string_view>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

bool near(const float left, const float right) {
  return std::abs(left - right) < 0.0001F;
}

float basisLength(const std::array<float, 16> &matrix,
                  const std::size_t offset) {
  return std::sqrt(matrix[offset] * matrix[offset] +
                   matrix[offset + 1U] * matrix[offset + 1U] +
                   matrix[offset + 2U] * matrix[offset + 2U]);
}

void assertValid(const PrimitiveMeshData3D &mesh) {
  assert(!mesh.positions.empty());
  assert(mesh.textureCoordinates.size() == mesh.positions.size());
  assert(!mesh.indices.empty());
  assert(mesh.indices.size() % 3U == 0U);
  for (const std::uint32_t index : mesh.indices)
    assert(index < mesh.positions.size());
  for (const Vec3 position : mesh.positions) {
    assert(std::isfinite(position.x));
    assert(std::isfinite(position.y));
    assert(std::isfinite(position.z));
    assert(position.x >= -0.5001F && position.x <= 0.5001F);
    assert(position.y >= -0.5001F && position.y <= 0.5001F);
    assert(position.z >= -0.5001F && position.z <= 0.5001F);
  }
}

void assertOutwardWinding(const PrimitiveMeshData3D &mesh) {
  std::size_t outwardFaces = 0;
  for (std::size_t triangle = 0; triangle < mesh.indices.size();
       triangle += 3U) {
    const Vec3 a = mesh.positions[mesh.indices[triangle]];
    const Vec3 b = mesh.positions[mesh.indices[triangle + 1U]];
    const Vec3 c = mesh.positions[mesh.indices[triangle + 2U]];
    const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3 normal{ab.y * ac.z - ab.z * ac.y,
                      ab.z * ac.x - ab.x * ac.z,
                      ab.x * ac.y - ab.y * ac.x};
    const Vec3 center{(a.x + b.x + c.x) / 3.0F,
                      (a.y + b.y + c.y) / 3.0F,
                      (a.z + b.z + c.z) / 3.0F};
    const float direction = normal.x * center.x + normal.y * center.y +
                            normal.z * center.z;
    // Pole triangles may be degenerate, but no non-degenerate face may point
    // toward the primitive center or generated lighting normals invert.
    assert(direction >= -0.000001F);
    if (direction > 0.000001F)
      ++outwardFaces;
  }
  assert(outwardFaces > 0U);
}

} // namespace

int main() {
  const auto scaled = composeMeshTransform3D(
      {.position = {7.0F, 8.0F, 9.0F}, .scale = {2.0F, 3.0F, 4.0F}},
      {5.0F, 0.5F, 2.0F});
  assert(near(scaled[0], 10.0F));
  assert(near(scaled[5], 1.5F));
  assert(near(scaled[10], 8.0F));
  assert(near(scaled[12], 7.0F));
  assert(near(scaled[13], 8.0F));
  assert(near(scaled[14], 9.0F));

  const auto rotated = composeMeshTransform3D(
      {.rotation = {0.0F, 0.0F, 1.57079632679F},
       .scale = {2.0F, 3.0F, 4.0F}},
      {5.0F, 0.5F, 2.0F});
  assert(near(basisLength(rotated, 0), 10.0F));
  assert(near(basisLength(rotated, 4), 1.5F));
  assert(near(basisLength(rotated, 8), 8.0F));
  assert(near(rotated[0], 0.0F));
  assert(near(rotated[1], 10.0F));
  assert(near(rotated[4], -1.5F));
  assert(near(rotated[5], 0.0F));

  const auto degenerate = composeMeshTransform3D(
      {.scale = {-2.0F, 0.0F, 1.0F}}, {1.0F, 4.0F, 3.0F});
  assert(near(degenerate[0], -2.0F));
  assert(near(basisLength(degenerate, 4), 0.0F));
  assert(near(degenerate[10], 3.0F));

  PrimitiveMeshData3D mesh;
  assert(createPrimitiveMesh3D("cube", mesh));
  assertValid(mesh);
  assertOutwardWinding(mesh);
  assert(mesh.positions.size() == 24U);
  assert(mesh.indices.size() == 36U);
  assert(createPrimitiveMesh3D("plane", mesh));
  assertValid(mesh);
  assert(mesh.positions.size() == 4U);
  assert(mesh.indices.size() == 6U);
  assert(createPrimitiveMesh3D("sphere", mesh));
  assertValid(mesh);
  assertOutwardWinding(mesh);
  assert(mesh.positions.size() > 100U);
  assert(createPrimitiveMesh3D("cylinder", mesh));
  assertValid(mesh);
  assertOutwardWinding(mesh);
  assert(mesh.indices.size() == 16U * 12U);

  // Reuse replaces output, and rejected shapes never leave stale geometry.
  assert(!createPrimitiveMesh3D("capsule", mesh));
  assert(mesh.positions.empty());
  assert(mesh.textureCoordinates.empty());
  assert(mesh.indices.empty());
  return 0;
}
