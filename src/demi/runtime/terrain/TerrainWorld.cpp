#include "demi/runtime/terrain/TerrainWorld.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_set>

namespace demi::runtime {

namespace {
// Native ownership marker prevents regeneration from replacing authored
// entities even when their IDs happen to occupy the generated namespace.
struct TerrainGeneratedSurface {
  std::string owner;
};

struct SurfaceTriangles {
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;
};

bool terrainHierarchyEnabled(const World &world, const Entity &owner) {
  const Entity *ancestor = &owner;
  // A valid parent chain cannot contain more entities than the world. This
  // bounds cycle detection without imposing a content or hierarchy-depth cap.
  for (std::size_t depth = 0; depth < world.entities.size(); ++depth) {
    if (!ancestor->enabled)
      return false;
    const auto *transform = ancestor->component<Transform3DComponent>();
    if (!transform || transform->parent.empty())
      return true;
    const auto parent =
        std::ranges::find(world.entities, transform->parent, &Entity::id);
    if (parent == world.entities.end())
      return false;
    ancestor = &*parent;
  }
  return false;
}

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

std::optional<std::string_view> terrainSurfaceOwner(const Entity &entity) {
  const auto *surface = entity.component<TerrainGeneratedSurface>();
  return surface ? std::optional<std::string_view>{surface->owner}
                 : std::nullopt;
}

std::optional<Entity>
buildTerrainMeshEntity(const Entity &owner, std::string id,
                       std::span<const Vec3> vertices,
                       std::span<const Vec3> normals, std::span<const Vec2> uvs,
                       Color color, std::string material, std::string &error) {
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
  entity.setComponent(std::move(mesh));
  entity.setComponent(ModelCollider3DComponent{
      .layer = owner.layer, .inlineGeometry = std::move(geometry)});
  entity.setComponent(TerrainGeneratedSurface{owner.id});
  // No rigidbody means static collision, irrespective of the owner's body.
  return entity;
}

bool materializeTerrains(World &world, std::string &error) {
  error.clear();
  std::vector<Entity> generated;
  std::vector<TerrainRuntimeOwner> owners;
  std::unordered_set<std::string> ids;
  for (const auto &entity : world.entities)
    if (!entity.hasComponent<TerrainGeneratedSurface>())
      ids.insert(entity.id);

  // Retention is committed with the meshes only after every owner succeeds.
  std::vector<std::pair<std::size_t, std::shared_ptr<const HeightField>>>
      fields;
  std::string ownerId;
  try {
    for (std::size_t ownerIndex = 0; ownerIndex < world.entities.size();
         ++ownerIndex) {
      const Entity &owner = world.entities[ownerIndex];
      const auto *terrain = owner.component<Terrain3DComponent>();
      if (!terrain)
        continue;
      ownerId = owner.id;
      if (!owner.hasComponent<Transform3DComponent>()) {
        error = "Terrain3D on " + owner.id + " requires Transform3D";
        return false;
      }
      const auto field = acquireTerrain(terrain->recipe);
      if (!field)
        throw std::runtime_error("Terrain generation returned no heightfield");
      fields.emplace_back(ownerIndex, field);
      owners.push_back({.id = owner.id});
      const bool enabled = terrainHierarchyEnabled(world, owner);
      for (const TerrainChunk &chunk : field->chunks) {
        std::map<std::size_t, SurfaceTriangles> surfaces;
        const auto append = [&](std::array<std::size_t, 3> samples,
                                std::array<Vec2, 3> positions) {
          // Majority label keeps grouping local to triangles. Heights and
          // normals already include the generator's smooth biome blends.
          const auto a = field->biomeIndices.at(samples[0]);
          const auto b = field->biomeIndices.at(samples[1]);
          const auto c = field->biomeIndices.at(samples[2]);
          const auto biome = b == c ? b : a;
          auto &surface = surfaces[biome];
          for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            const auto sample = samples[vertex];
            const Vec2 position = positions[vertex];
            surface.vertices.push_back(
                {position.x, field->heights.at(sample), position.y});
            surface.normals.push_back(field->normals.at(sample));
            surface.uvs.push_back(
                {position.x / field->size.x, position.y / field->size.y});
          }
        };
        for (int z = chunk.firstCellZ; z < chunk.firstCellZ + chunk.cellsZ;
             ++z) {
          for (int x = chunk.firstCellX; x < chunk.firstCellX + chunk.cellsX;
               ++x) {
            const auto a = field->index(x, z);
            const auto b = field->index(x + 1, z);
            const auto c = field->index(x, z + 1);
            const auto d = field->index(x + 1, z + 1);
            const auto pa = field->position(x, z);
            const auto pb = field->position(x + 1, z);
            const auto pc = field->position(x, z + 1);
            const auto pd = field->position(x + 1, z + 1);
            append({a, c, b}, {pa, pc, pb});
            append({b, c, d}, {pb, pc, pd});
          }
        }
        for (const auto &[biome, surface] : surfaces) {
          // Encode IDs reversibly so biome names containing '/' cannot create
          // aliases, and insertion of another biome never renames this group.
          std::string id = owner.id + "/__terrain/" +
                           std::to_string(chunk.firstCellX) + "_" +
                           std::to_string(chunk.firstCellZ) + "/" +
                           biomeIdSegment(field->biomeIds.at(biome));
          if (!ids.insert(id).second) {
            error =
                "Generated terrain entity ID conflicts with existing entity: " +
                id;
            return false;
          }
          auto entity = buildTerrainMeshEntity(
              owner, std::move(id), surface.vertices, surface.normals,
              surface.uvs, field->biomeColors.at(biome), {}, error);
          if (!entity)
            return false;
          entity->enabled = enabled;
          owners.back().surfaces.push_back(entity->id);
          generated.push_back(std::move(*entity));
        }
      }
    }
    // Reserve before mutating the world so allocation failure leaves it intact.
    if (generated.size() > world.entities.max_size() - world.entities.size())
      throw std::length_error(
          "Generated terrain exceeds entity storage capacity");
    world.entities.reserve(world.entities.size() + generated.size());
  } catch (const std::exception &exception) {
    error = "Terrain3D on " + ownerId + ": " + exception.what();
    return false;
  }
  for (const auto &[index, field] : fields)
    world.entities[index].component<Terrain3DComponent>()->generated = field;
  std::erase_if(world.entities, [](const Entity &entity) {
    return entity.hasComponent<TerrainGeneratedSurface>();
  });
  for (auto &entity : generated)
    world.entities.push_back(std::move(entity));
  world.terrainOwners = std::move(owners);
  world.terrainEntityLookup.clear();
  return true;
}

void synchronizeTerrainVisibility(World &world) {
  if (world.terrainOwners.empty())
    return;
  // EntityLookup validates cached offsets and rebuilds only after collection
  // changes. Stable frames visit terrain surfaces and their owner chains only.
  auto &lookup = world.terrainEntityLookup;
  for (const auto &owner : world.terrainOwners) {
    const Entity *ancestor = lookup.find(world.entities, owner.id);
    bool enabled = false;
    if (ancestor && ancestor->hasComponent<Terrain3DComponent>()) {
      for (std::size_t depth = 0; depth < world.entities.size(); ++depth) {
        if (!ancestor->enabled)
          break;
        const auto *transform = ancestor->component<Transform3DComponent>();
        if (!transform || transform->parent.empty()) {
          enabled = true;
          break;
        }
        ancestor = lookup.find(world.entities, transform->parent);
        if (!ancestor)
          break;
      }
    }
    for (const auto &id : owner.surfaces) {
      Entity *surface = lookup.find(world.entities, id);
      if (surface && terrainSurfaceOwner(*surface) == owner.id)
        surface->enabled = enabled;
    }
  }
}

void rebuildTerrainOwnership(World &world) {
  std::map<std::string, std::vector<std::string>> surfaces;
  for (const auto &entity : world.entities)
    if (const auto owner = terrainSurfaceOwner(entity))
      surfaces[std::string(*owner)].push_back(entity.id);
  std::vector<TerrainRuntimeOwner> owners;
  owners.reserve(surfaces.size());
  for (auto &[owner, ids] : surfaces)
    owners.push_back({.id = owner, .surfaces = std::move(ids)});
  world.terrainOwners = std::move(owners);
  world.terrainEntityLookup.clear();
}

} // namespace demi::runtime
