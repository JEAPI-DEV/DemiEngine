#include "demi/runtime/terrain/TerrainWaterMesh.h"

#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace demi::runtime {
namespace {
std::span<const TerrainWaterSurface> waterSurfaces(const HeightField *field) {
  if (!field || !field->graphArtifacts || !field->graphArtifacts->waterResult)
    return {};
  return field->graphArtifacts->waterResult->surfaces;
}

const TerrainWaterAppearance &bodyAppearance(const HeightField &field,
                                             std::string_view id) {
  if (field.graphArtifacts)
    for (const auto &body : field.graphArtifacts->water.bodies)
      if (body.id == id)
        return body.appearance;
  static const TerrainWaterAppearance defaults;
  return defaults;
}

bool sameWaterSurface(const TerrainWaterSurface &before,
                      const TerrainWaterSurface &after) {
  const auto sameVector = [](Vec3 left, Vec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
  };
  return before.id == after.id && before.kind == after.kind &&
         before.level == after.level && before.indices == after.indices &&
         before.depth == after.depth &&
         std::ranges::equal(before.vertices, after.vertices, sameVector) &&
         std::ranges::equal(before.normals, after.normals, sameVector);
}

std::string encodedBodyId(std::string_view id) {
  constexpr char hex[] = "0123456789abcdef";
  std::string encoded;
  for (unsigned char value : id) {
    if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') || value == '_' || value == '-') {
      encoded += static_cast<char>(value);
    } else {
      encoded += '%';
      encoded += hex[value >> 4];
      encoded += hex[value & 15];
    }
  }
  return encoded;
}

[[noreturn]] void invalidSurface(const Entity &owner,
                                 const TerrainWaterSurface &surface,
                                 std::string_view reason) {
  throw std::invalid_argument("Terrain water body '" + surface.id + "' on '" +
                              owner.id + "': " + std::string(reason));
}

bool finite(Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

bool validateSurface(const Entity &owner, const TerrainWaterSurface &surface,
                     std::stop_token stop) {
  if (!std::isfinite(surface.level))
    invalidSurface(owner, surface, "level must be finite");
  if (surface.normals.size() != surface.vertices.size() ||
      surface.depth.size() != surface.vertices.size())
    invalidSurface(owner, surface, "requires one normal and depth per vertex");
  if (surface.indices.size() % 3 != 0)
    invalidSurface(owner, surface, "indices must describe complete triangles");

  for (std::size_t index = 0; index < surface.vertices.size(); ++index) {
    if (stop.stop_requested())
      return false;
    if (!finite(surface.vertices[index]) || !finite(surface.normals[index]))
      invalidSurface(owner, surface, "positions and normals must be finite");
    if (!std::isfinite(surface.depth[index]) || surface.depth[index] < 0)
      invalidSurface(owner, surface, "depth must be finite and nonnegative");
  }
  for (const auto index : surface.indices) {
    if (stop.stop_requested())
      return false;
    if (index >= surface.vertices.size())
      invalidSurface(owner, surface, "index is outside the vertex array");
  }
  return true;
}

Entity waterEntity(const Entity &owner, const TerrainWaterSurface &surface,
                   MeshRendererComponent mesh) {
  Entity entity;
  entity.id = owner.id + "/__water/" + encodedBodyId(surface.id);
  entity.name = "Terrain water " + surface.id;
  entity.enabled = owner.enabled;
  entity.tags = owner.tags;
  entity.layer = owner.layer;
  entity.sceneOwner = owner.sceneOwner;
  entity.prefabInstance = owner.prefabInstance;
  entity.persistent = owner.persistent;
  entity.setComponent(Transform3DComponent{.parent = owner.id});
  entity.setComponent(std::move(mesh));
  entity.setComponent(
      terrain_detail::TerrainGeneratedWaterSurface{.owner = owner.id});
  return entity;
}
} // namespace

std::vector<Entity> buildTerrainWaterMeshes(const Entity &owner,
                                            const HeightField &field,
                                            std::stop_token stop) {
  if (stop.stop_requested() || !field.graphArtifacts ||
      !field.graphArtifacts->waterResult)
    return {};
  if (owner.id.empty())
    throw std::invalid_argument("Terrain water meshes require an owner ID");

  const auto &surfaces = field.graphArtifacts->waterResult->surfaces;
  std::vector<Entity> entities;
  std::unordered_set<std::string_view> bodyIds;
  for (const auto &surface : surfaces) {
    if (stop.stop_requested())
      return {};
    if (surface.id.empty() || !bodyIds.insert(surface.id).second)
      invalidSurface(owner, surface, "body IDs must be nonempty and unique");
    if (!validateSurface(owner, surface, stop))
      return {};
    if (surface.indices.empty())
      continue;

    MeshRendererComponent mesh;
    const auto &appearance = bodyAppearance(field, surface.id);
    validateTerrainWaterAppearance(appearance);
    mesh.vertices.reserve(surface.indices.size());
    mesh.normals.reserve(surface.indices.size());
    mesh.uvs.reserve(surface.indices.size());
    mesh.vertexColors.reserve(surface.indices.size());
    for (const auto index : surface.indices) {
      if (stop.stop_requested())
        return {};
      const auto position = surface.vertices[index];
      mesh.vertices.push_back(position);
      mesh.normals.push_back(surface.normals[index]);
      mesh.uvs.push_back({position.x, position.z});
      mesh.vertexColors.push_back(
          terrainWaterDepthColor(appearance, surface.depth[index]));
    }
    // Alpha comes from opacity alone, avoiding a second tint-alpha multiplier.
    mesh.color = {1.F, 1.F, 1.F, 1.F};
    mesh.surfaceMode = "transparent";
    mesh.roughness = appearance.roughness;
    mesh.metallic = 0.F;
    mesh.opacity = 1.F;
    mesh.markGeometryChanged();
    entities.push_back(waterEntity(owner, surface, std::move(mesh)));
  }
  if (stop.stop_requested())
    return {};
  return entities;
}

bool terrainWaterMeshesChanged(const HeightField *before,
                               const HeightField &after) {
  if (before && before->graphArtifacts == after.graphArtifacts)
    return false;

  const auto previous = waterSurfaces(before);
  const auto current = waterSurfaces(&after);
  auto previousBody = previous.begin();
  auto currentBody = current.begin();
  const auto drawable = [](const TerrainWaterSurface &surface) {
    return !surface.indices.empty();
  };
  for (;;) {
    previousBody = std::find_if(previousBody, previous.end(), drawable);
    currentBody = std::find_if(currentBody, current.end(), drawable);
    if (previousBody == previous.end() || currentBody == current.end())
      return previousBody != previous.end() || currentBody != current.end();
    if (!sameWaterSurface(*previousBody, *currentBody))
      return true;
    if (before &&
        !sameTerrainWaterAppearance(bodyAppearance(*before, previousBody->id),
                                    bodyAppearance(after, currentBody->id)))
      return true;
    ++previousBody;
    ++currentBody;
  }
}
} // namespace demi::runtime
