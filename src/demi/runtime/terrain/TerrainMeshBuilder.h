#pragma once

#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace demi::runtime {
struct ColliderAsset3D;
struct HeightField;
struct TerrainChunk;

// Internal geometry/ownership contract shared by full loading and incremental
// publication. Authored entities never acquire this native-only marker.
namespace terrain_detail {
struct TerrainGeneratedSurface {
  std::string owner;
  int firstCellX = 0;
  int firstCellZ = 0;
  std::string biome;
};

struct SurfaceTriangles {
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;
};

std::string surfaceId(const std::string &owner, const TerrainChunk &chunk,
                      const std::string &biome);

Vec2 terrainUv(Vec2 position, float textureScale);
std::vector<Vec2> terrainUvs(std::span<const Vec3> vertices,
                             float textureScale);

std::map<std::size_t, SurfaceTriangles>
buildChunkTriangles(const HeightField &field, const TerrainChunk &chunk,
                    std::stop_token stop = {});

std::optional<Entity>
buildMeshEntity(const Entity &owner, std::string id,
                std::span<const Vec3> vertices, std::span<const Vec3> normals,
                std::span<const Vec2> uvs, Color color, std::string material,
                std::string &error,
                std::shared_ptr<const ColliderAsset3D> retainedCollider = {});
} // namespace terrain_detail
} // namespace demi::runtime
