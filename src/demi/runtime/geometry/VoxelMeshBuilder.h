#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime::geometry {

inline std::size_t checkedVoxelCellCount(std::int64_t width,
                                         std::int64_t height,
                                         std::int64_t depth) {
  std::int64_t count = 1;
  for (const auto dimension : {width, height, depth}) {
    if (dimension <= 0 || dimension > std::numeric_limits<int>::max() / count) {
      throw std::invalid_argument(
          "Voxel dimensions must fit positive cell indices");
    }
    count *= dimension;
  }
  return static_cast<std::size_t>(count);
}

struct VoxelBlock {
  int x = 0;
  int y = 0;
  int z = 0;
  int id = 0;
};

struct VoxelTiles {
  int side = 0;
  int top = 0;
  int bottom = 0;
};
using VoxelTileMap = std::unordered_map<int, VoxelTiles>;

// Row-major heightfield including a one-column border on every side.
struct VoxelColumn {
  int height = -1;
  int top = 0;
  int fill = 0;
  int base = 0;
  bool rocky = false;
};

struct ProceduralMeshBuilder {
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;

  void clear() {
    vertices.clear();
    normals.clear();
    uvs.clear();
  }

  void reserve(const int vertexCount) {
    const auto count = static_cast<std::size_t>(std::max(vertexCount, 0));
    vertices.reserve(count);
    normals.reserve(count);
    uvs.reserve(count);
  }

  [[nodiscard]] int vertexCount() const {
    return static_cast<int>(vertices.size());
  }

  void addVertex(const float x, const float y, const float z, const float nx,
                 const float ny, const float nz, const float u, const float v) {
    vertices.push_back(Vec3{x, y, z});
    normals.push_back(Vec3{nx, ny, nz});
    uvs.push_back(Vec2{u, v});
  }

  void addQuad(const float nx, const float ny, const float nz, const float x1,
               const float y1, const float z1, const float u1, const float v1,
               const float x2, const float y2, const float z2, const float u2,
               const float v2, const float x3, const float y3, const float z3,
               const float u3, const float v3, const float x4, const float y4,
               const float z4, const float u4, const float v4) {
    addVertex(x1, y1, z1, nx, ny, nz, u1, v1);
    addVertex(x2, y2, z2, nx, ny, nz, u2, v2);
    addVertex(x3, y3, z3, nx, ny, nz, u3, v3);
    addVertex(x1, y1, z1, nx, ny, nz, u1, v1);
    addVertex(x3, y3, z3, nx, ny, nz, u3, v3);
    addVertex(x4, y4, z4, nx, ny, nz, u4, v4);
  }

  void addVoxelBlocks(const std::vector<VoxelBlock> &blocks,
                      const std::unordered_set<int> &occupiedCells,
                      const VoxelTileMap &tilesByBlock, int atlasColumns,
                      int occupancyStride) {
    if (atlasColumns <= 0 || occupancyStride <= 0) {
      return;
    }
    const float tileWidth = 1.0F / static_cast<float>(atlasColumns);
    constexpr std::array<std::array<int, 3>, 6> directions{{
        {1, 0, 0},
        {-1, 0, 0},
        {0, 1, 0},
        {0, -1, 0},
        {0, 0, 1},
        {0, 0, -1},
    }};
    for (const auto &block : blocks) {
      const int x = block.x;
      const int y = block.y;
      const int z = block.z;
      const int blockId = block.id;
      const auto tiles = tilesByBlock.find(blockId);
      if (tiles == tilesByBlock.end())
        continue;
      for (std::size_t face = 0; face < directions.size(); ++face) {
        const auto &[nx, ny, nz] = directions[face];
        const int neighborKey =
            voxelOccupancyKey(x + nx, y + ny, z + nz, occupancyStride);
        if (occupiedCells.contains(neighborKey))
          continue;
        const int tile = face == 2   ? tiles->second.top
                         : face == 3 ? tiles->second.bottom
                                     : tiles->second.side;
        const float u0 = static_cast<float>(tile) * tileWidth;
        addVoxelFace(x, y, z, nx, ny, nz, u0, u0 + tileWidth);
      }
    }
  }

  // Bulk heightfield meshing avoids a Lua/C++ call for every emitted face.
  // The input includes a one-column border, allowing side visibility to be
  // decided without callbacks into mutable script state.
  void addVoxelHeightfield(const std::vector<VoxelColumn> &columns,
                           const VoxelTileMap &tilesByBlock, int atlasColumns,
                           int chunkSize, int sectionMinimumY,
                           int sectionHeight, int fillDepth) {
    if (atlasColumns <= 0 || chunkSize <= 0 || sectionHeight <= 0) {
      return;
    }
    const auto expectedColumns =
        checkedVoxelCellCount(static_cast<std::int64_t>(chunkSize) + 2, 1,
                              static_cast<std::int64_t>(chunkSize) + 2);
    if (columns.size() != expectedColumns) {
      throw std::invalid_argument(
          "Voxel heightfield requires a complete border");
    }
    const int stride = chunkSize + 2;
    const float tileWidth = 1.0F / static_cast<float>(atlasColumns);
    const auto index = [stride](const int x, const int z) {
      return ((z + 1) * stride) + x + 1;
    };
    const auto heightAt = [&](const int x, const int z) {
      return columns.at(index(x, z)).height;
    };
    const auto blockAtHeight = [&](const int fieldIndex, const int y,
                                   const int height) {
      const auto &column = columns.at(fieldIndex);
      const int top = column.top;
      const int base = column.base;
      if (y == height)
        return top;
      if (y == height - 1 && column.rocky)
        return base;
      if (y >= height - fillDepth)
        return column.fill;
      return base;
    };
    const int sectionMaximumY = sectionMinimumY + sectionHeight - 1;
    constexpr std::array<std::array<int, 2>, 4> SideDirections{{
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1},
    }};
    constexpr std::array<std::array<int, 3>, 4> SideNormals{{
        {1, 0, 0},
        {-1, 0, 0},
        {0, 0, 1},
        {0, 0, -1},
    }};
    for (int z = 0; z < chunkSize; ++z) {
      for (int x = 0; x < chunkSize; ++x) {
        const int fieldIndex = index(x, z);
        const int height = heightAt(x, z);
        if (height >= sectionMinimumY && height <= sectionMaximumY) {
          const int block = blockAtHeight(fieldIndex, height, height);
          const auto tiles = tilesByBlock.find(block);
          if (tiles != tilesByBlock.end()) {
            const float u0 = static_cast<float>(tiles->second.top) * tileWidth;
            addVoxelFace(x, height - sectionMinimumY, z, 0, 1, 0, u0,
                         u0 + tileWidth);
          }
        }
        for (std::size_t side = 0; side < SideDirections.size(); ++side) {
          const int neighborHeight = heightAt(x + SideDirections[side][0],
                                              z + SideDirections[side][1]);
          const int minimumY = std::max(neighborHeight + 1, sectionMinimumY);
          const int maximumY = std::min(height, sectionMaximumY);
          for (int y = minimumY; y <= maximumY; ++y) {
            const int block = blockAtHeight(fieldIndex, y, height);
            const auto tiles = tilesByBlock.find(block);
            if (tiles == tilesByBlock.end())
              continue;
            const float u0 = static_cast<float>(tiles->second.side) * tileWidth;
            addVoxelFace(x, y - sectionMinimumY, z, SideNormals[side][0],
                         SideNormals[side][1], SideNormals[side][2], u0,
                         u0 + tileWidth);
          }
        }
      }
    }
  }

  static int voxelOccupancyKey(const int x, const int y, const int z,
                               const int occupancyStride) {
    return (y * occupancyStride * occupancyStride) +
           ((z + 1) * occupancyStride) + (x + 1);
  }

  void addVoxelFace(const int x, const int y, const int z, const int nx,
                    const int ny, const int nz, const float u0,
                    const float u1) {
    if (nx == 1) {
      addQuad(1.0F, 0.0F, 0.0F, x + 1.0F, y + 0.0F, z + 1.0F, u0, 1.0F,
              x + 1.0F, y + 0.0F, z + 0.0F, u1, 1.0F, x + 1.0F, y + 1.0F,
              z + 0.0F, u1, 0.0F, x + 1.0F, y + 1.0F, z + 1.0F, u0, 0.0F);
    } else if (nx == -1) {
      addQuad(-1.0F, 0.0F, 0.0F, x + 0.0F, y + 0.0F, z + 0.0F, u0, 1.0F,
              x + 0.0F, y + 0.0F, z + 1.0F, u1, 1.0F, x + 0.0F, y + 1.0F,
              z + 1.0F, u1, 0.0F, x + 0.0F, y + 1.0F, z + 0.0F, u0, 0.0F);
    } else if (ny == 1) {
      addQuad(0.0F, 1.0F, 0.0F, x + 0.0F, y + 1.0F, z + 1.0F, u0, 1.0F,
              x + 1.0F, y + 1.0F, z + 1.0F, u1, 1.0F, x + 1.0F, y + 1.0F,
              z + 0.0F, u1, 0.0F, x + 0.0F, y + 1.0F, z + 0.0F, u0, 0.0F);
    } else if (ny == -1) {
      addQuad(0.0F, -1.0F, 0.0F, x + 0.0F, y + 0.0F, z + 0.0F, u0, 1.0F,
              x + 1.0F, y + 0.0F, z + 0.0F, u1, 1.0F, x + 1.0F, y + 0.0F,
              z + 1.0F, u1, 0.0F, x + 0.0F, y + 0.0F, z + 1.0F, u0, 0.0F);
    } else if (nz == 1) {
      addQuad(0.0F, 0.0F, 1.0F, x + 0.0F, y + 0.0F, z + 1.0F, u0, 1.0F,
              x + 1.0F, y + 0.0F, z + 1.0F, u1, 1.0F, x + 1.0F, y + 1.0F,
              z + 1.0F, u1, 0.0F, x + 0.0F, y + 1.0F, z + 1.0F, u0, 0.0F);
    } else if (nz == -1) {
      addQuad(0.0F, 0.0F, -1.0F, x + 1.0F, y + 0.0F, z + 0.0F, u0, 1.0F,
              x + 0.0F, y + 0.0F, z + 0.0F, u1, 1.0F, x + 0.0F, y + 1.0F,
              z + 0.0F, u1, 0.0F, x + 1.0F, y + 1.0F, z + 0.0F, u0, 0.0F);
    }
  }
};

struct VoxelMeshWorld {
  int chunkSize = 16;
  int sectionHeight = 16;
  std::unordered_map<std::string, std::vector<int>> sections;

  VoxelMeshWorld(const int chunkSizeValue, const int sectionHeightValue)
      : chunkSize(chunkSizeValue), sectionHeight(sectionHeightValue) {
    (void)checkedVoxelCellCount(chunkSize, sectionHeight, chunkSize);
  }

  [[nodiscard]] std::string sectionKey(const int cx, const int sy,
                                       const int cz) const {
    return std::to_string(cx) + ":" + std::to_string(sy) + ":" +
           std::to_string(cz);
  }

  [[nodiscard]] int sectionIndex(const int x, const int y, const int z) const {
    return (y * chunkSize * chunkSize) + (z * chunkSize) + x;
  }

  void clear() { sections.clear(); }

  void setSection(const int cx, const int sy, const int cz,
                  const std::vector<VoxelBlock> &blocks) {
    std::vector<int> section(
        static_cast<std::size_t>(chunkSize * sectionHeight * chunkSize), 0);
    for (const auto &block : blocks) {
      const int x = block.x;
      const int y = block.y;
      const int z = block.z;
      if (x < 0 || x >= chunkSize || y < 0 || y >= sectionHeight || z < 0 ||
          z >= chunkSize) {
        continue;
      }
      section[static_cast<std::size_t>(sectionIndex(x, y, z))] = block.id;
    }
    sections[sectionKey(cx, sy, cz)] = std::move(section);
  }

  void eraseSection(const int cx, const int sy, const int cz) {
    sections.erase(sectionKey(cx, sy, cz));
  }

  [[nodiscard]] int blockAt(const int cx, const int sy, const int cz, int x,
                            int y, int z) const {
    int sectionCx = cx;
    int sectionSy = sy;
    int sectionCz = cz;
    while (x < 0) {
      x += chunkSize;
      --sectionCx;
    }
    while (x >= chunkSize) {
      x -= chunkSize;
      ++sectionCx;
    }
    while (z < 0) {
      z += chunkSize;
      --sectionCz;
    }
    while (z >= chunkSize) {
      z -= chunkSize;
      ++sectionCz;
    }
    while (y < 0) {
      y += sectionHeight;
      --sectionSy;
    }
    while (y >= sectionHeight) {
      y -= sectionHeight;
      ++sectionSy;
    }
    const auto it = sections.find(sectionKey(sectionCx, sectionSy, sectionCz));
    if (it == sections.end()) {
      return 0;
    }
    return it->second[static_cast<std::size_t>(sectionIndex(x, y, z))];
  }

  [[nodiscard]] ProceduralMeshBuilder
  buildSectionMesh(const int cx, const int sy, const int cz,
                   const VoxelTileMap &blockTiles,
                   const int atlasColumns) const {
    ProceduralMeshBuilder builder;
    const auto it = sections.find(sectionKey(cx, sy, cz));
    if (it == sections.end() || atlasColumns <= 0) {
      return builder;
    }
    const float tileWidth = 1.0F / static_cast<float>(atlasColumns);
    builder.reserve(4096);
    const std::vector<int> &section = it->second;
    constexpr int directions[6][3] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                      {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
    for (int z = 0; z < chunkSize; ++z) {
      for (int y = 0; y < sectionHeight; ++y) {
        for (int x = 0; x < chunkSize; ++x) {
          const int block =
              section[static_cast<std::size_t>(sectionIndex(x, y, z))];
          if (block == 0) {
            continue;
          }
          const auto tileEntry = blockTiles.find(block);
          if (tileEntry == blockTiles.end()) {
            continue;
          }
          const auto &tiles = tileEntry->second;
          for (int face = 0; face < 6; ++face) {
            const int nx = directions[face][0];
            const int ny = directions[face][1];
            const int nz = directions[face][2];
            if (blockAt(cx, sy, cz, x + nx, y + ny, z + nz) != 0) {
              continue;
            }
            const int tile = face == 2   ? tiles.top
                             : face == 3 ? tiles.bottom
                                         : tiles.side;
            const float u0 = static_cast<float>(tile) * tileWidth;
            builder.addVoxelFace(x, y, z, nx, ny, nz, u0, u0 + tileWidth);
          }
        }
      }
    }
    return builder;
  }
};

} // namespace demi::runtime::geometry
