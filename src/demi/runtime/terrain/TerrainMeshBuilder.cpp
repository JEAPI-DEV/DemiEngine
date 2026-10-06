#include "demi/runtime/terrain/TerrainMeshBuilder.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace demi::runtime {
namespace terrain_detail {
namespace {
std::string biomeIdSegment(const std::string &id) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (unsigned char value : id) {
    if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') || value == '_' || value == '-') {
      result += static_cast<char>(value);
    } else {
      result += '%';
      result += hex[value >> 4];
      result += hex[value & 15];
    }
  }
  return result;
}
} // namespace

std::string surfaceId(const std::string &owner, const TerrainChunk &chunk,
                      const std::string &biome) {
  // Reversible encoding keeps group IDs stable when biome indices shift.
  return owner + "/__terrain/" + std::to_string(chunk.firstCellX) + "_" +
         std::to_string(chunk.firstCellZ) + "/" + biomeIdSegment(biome);
}

Vec2 terrainUv(Vec2 position, float textureScale) {
  if (!std::isfinite(textureScale) || textureScale <= 0)
    throw std::invalid_argument(
        "Terrain texture_scale must be finite and positive");
  const auto u = float(double(position.x) * textureScale);
  const auto v = float(double(position.y) * textureScale);
  if (!std::isfinite(u) || !std::isfinite(v))
    throw std::invalid_argument(
        "Terrain texture_scale produces non-finite UVs");
  return {u, v};
}

std::vector<Vec2> terrainUvs(std::span<const Vec3> vertices,
                             float textureScale) {
  std::vector<Vec2> uvs;
  uvs.reserve(vertices.size());
  for (const auto vertex : vertices)
    uvs.push_back(terrainUv({vertex.x, vertex.z}, textureScale));
  return uvs;
}

std::map<std::size_t, SurfaceTriangles>
buildChunkTriangles(const HeightField &field, const TerrainChunk &chunk,
                    std::stop_token stop) {
  std::map<std::size_t, SurfaceTriangles> surfaces;
  const auto append = [&](std::array<std::size_t, 3> samples,
                          std::array<Vec2, 3> positions) {
    // Majority label keeps grouping local to triangles. Heights and normals
    // already include the generator's smooth biome blends.
    const auto a = field.biomeIndices.at(samples[0]);
    const auto b = field.biomeIndices.at(samples[1]);
    const auto c = field.biomeIndices.at(samples[2]);
    const auto biome = b == c ? b : a;
    auto &surface = surfaces[biome];
    const auto textureScale = field.biomeTextureScale(biome);
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
      const auto sample = samples[vertex];
      const Vec2 position = positions[vertex];
      surface.vertices.push_back(
          {position.x, field.heights.at(sample), position.y});
      surface.normals.push_back(field.normals.at(sample));
      surface.uvs.push_back(terrainUv(position, textureScale));
    }
  };
  for (int z = chunk.firstCellZ; z < chunk.firstCellZ + chunk.cellsZ; ++z) {
    if (stop.stop_requested())
      return {};
    for (int x = chunk.firstCellX; x < chunk.firstCellX + chunk.cellsX; ++x) {
      if ((x & 255) == 0 && stop.stop_requested())
        return {};
      const auto a = field.index(x, z);
      const auto b = field.index(x + 1, z);
      const auto c = field.index(x, z + 1);
      const auto d = field.index(x + 1, z + 1);
      const auto pa = field.position(x, z);
      const auto pb = field.position(x + 1, z);
      const auto pc = field.position(x, z + 1);
      const auto pd = field.position(x + 1, z + 1);
      append({a, c, b}, {pa, pc, pb});
      append({b, c, d}, {pb, pc, pd});
    }
  }
  return surfaces;
}

std::optional<Entity>
buildMeshEntity(const Entity &owner, std::string id,
                std::span<const Vec3> vertices, std::span<const Vec3> normals,
                std::span<const Vec2> uvs, Color color, std::string material,
                std::string &error,
                std::shared_ptr<const ColliderAsset3D> retainedCollider) {
  if (vertices.empty() || vertices.size() % 3 != 0 ||
      normals.size() != vertices.size() || uvs.size() != vertices.size()) {
    error = "Terrain geometry on " + owner.id +
            " requires complete triangles and one normal/UV per vertex";
    return std::nullopt;
  }
  const auto finite = [](Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
  };
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    if (!finite(vertices[index]) || !finite(normals[index]) ||
        !std::isfinite(uvs[index].x) || !std::isfinite(uvs[index].y)) {
      error = "Terrain geometry on " + owner.id + " contains non-finite values";
      return std::nullopt;
    }
  }

  Entity entity;
  entity.id = std::move(id);
  entity.name = "Terrain surface";
  entity.enabled = owner.enabled;
  entity.layer = owner.layer;
  entity.sceneOwner = owner.sceneOwner;
  entity.prefabInstance = owner.prefabInstance;
  entity.persistent = owner.persistent;
  entity.setComponent(Transform3DComponent{.parent = owner.id});

  MeshRendererComponent mesh;
  mesh.vertices.assign(vertices.begin(), vertices.end());
  mesh.normals.assign(normals.begin(), normals.end());
  mesh.uvs.assign(uvs.begin(), uvs.end());
  mesh.color = color;
  mesh.material = std::move(material);
  mesh.markGeometryChanged();

  if (!retainedCollider) {
    auto geometry = std::make_shared<ColliderAsset3D>();
    geometry->revision = mesh.revision;
    geometry->size = {mesh.boundsMax.x - mesh.boundsMin.x,
                      mesh.boundsMax.y - mesh.boundsMin.y,
                      mesh.boundsMax.z - mesh.boundsMin.z};
    geometry->offset = {(mesh.boundsMax.x + mesh.boundsMin.x) * .5F,
                        (mesh.boundsMax.y + mesh.boundsMin.y) * .5F,
                        (mesh.boundsMax.z + mesh.boundsMin.z) * .5F};
    geometry->triangles.reserve(vertices.size() / 3);
    for (std::size_t index = 0; index < vertices.size(); index += 3)
      geometry->triangles.push_back(
          {vertices[index], vertices[index + 1], vertices[index + 2]});
    retainedCollider = std::move(geometry);
  }
  entity.setComponent(std::move(mesh));
  entity.setComponent(ModelCollider3DComponent{
      .layer = owner.layer, .inlineGeometry = std::move(retainedCollider)});
  entity.setComponent(TerrainGeneratedSurface{.owner = owner.id});
  // No rigidbody means static collision, irrespective of the owner's body.
  return entity;
}
} // namespace terrain_detail

std::optional<Entity>
buildTerrainMeshEntity(const Entity &owner, std::string id,
                       std::span<const Vec3> vertices,
                       std::span<const Vec3> normals, std::span<const Vec2> uvs,
                       Color color, std::string material, std::string &error) {
  return terrain_detail::buildMeshEntity(owner, std::move(id), vertices,
                                         normals, uvs, color,
                                         std::move(material), error);
}
} // namespace demi::runtime
