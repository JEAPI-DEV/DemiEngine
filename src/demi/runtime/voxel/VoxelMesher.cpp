#include "demi/runtime/voxel/VoxelMesher.h"

#include <algorithm>
#include <array>

namespace demi::runtime {

namespace {

struct FaceGeometry {
  VoxelFace face;
  int dx;
  int dy;
  int dz;
  Vec3 normal;
  std::array<Vec3, 4> corners;
};

constexpr std::array FaceGeometries{
  FaceGeometry{VoxelFace::Right, 1, 0, 0, {1.0F, 0.0F, 0.0F}, {{{1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 1.0F}}}},
  FaceGeometry{VoxelFace::Left, -1, 0, 0, {-1.0F, 0.0F, 0.0F}, {{{0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F}}}},
  FaceGeometry{VoxelFace::Top, 0, 1, 0, {0.0F, 1.0F, 0.0F}, {{{0.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}}},
  FaceGeometry{VoxelFace::Bottom, 0, -1, 0, {0.0F, -1.0F, 0.0F}, {{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}}},
  FaceGeometry{VoxelFace::Front, 0, 0, 1, {0.0F, 0.0F, 1.0F}, {{{1.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}, {0.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}}},
  FaceGeometry{VoxelFace::Back, 0, 0, -1, {0.0F, 0.0F, -1.0F}, {{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}}},
};

std::array<Vec2, 4> tileUvs(const int tile, const VoxelMeshBuildOptions options) {
  const int columns = std::max(options.atlasColumns, 1);
  const int rows = std::max(options.atlasRows, 1);
  const int clampedTile = std::max(tile, 0);
  const int tileX = clampedTile % columns;
  const int tileY = clampedTile / columns;
  const float u0 = static_cast<float>(tileX) / static_cast<float>(columns);
  const float v0 = static_cast<float>(tileY) / static_cast<float>(rows);
  const float u1 = static_cast<float>(tileX + 1) / static_cast<float>(columns);
  const float v1 = static_cast<float>(tileY + 1) / static_cast<float>(rows);
  return {{{u0, v1}, {u0, v0}, {u1, v0}, {u1, v1}}};
}

void appendFace(VoxelMeshData& mesh, const int x, const int y, const int z, const FaceGeometry& face, const std::array<Vec2, 4>& uvs) {
  constexpr std::array<int, 6> order{0, 1, 2, 0, 2, 3};
  for (const int cornerIndex : order) {
    const Vec3 corner = face.corners[static_cast<std::size_t>(cornerIndex)];
    mesh.positions.push_back(Vec3{.x = static_cast<float>(x) + corner.x, .y = static_cast<float>(y) + corner.y, .z = static_cast<float>(z) + corner.z});
    mesh.normals.push_back(face.normal);
    mesh.texcoords.push_back(uvs[static_cast<std::size_t>(cornerIndex)]);
  }
  ++mesh.faceCount;
}

} // namespace

VoxelMeshData buildVoxelMesh(const VoxelChunk& chunk, const VoxelBlockSet& blockSet, const VoxelMeshBuildOptions options) {
  VoxelMeshData mesh;
  for (int y = 0; y < chunk.height(); ++y) {
    for (int z = 0; z < chunk.depth(); ++z) {
      for (int x = 0; x < chunk.width(); ++x) {
        const std::uint16_t block = chunk.block(x, y, z);
        if (!blockSet.isSolid(block)) {
          continue;
        }
        for (const FaceGeometry& face : FaceGeometries) {
          const std::uint16_t neighbor = chunk.block(x + face.dx, y + face.dy, z + face.dz);
          if (blockSet.isSolid(neighbor)) {
            continue;
          }
          appendFace(mesh, x, y, z, face, tileUvs(blockSet.tileForFace(block, face.face), options));
        }
      }
    }
  }
  return mesh;
}

} // namespace demi::runtime
