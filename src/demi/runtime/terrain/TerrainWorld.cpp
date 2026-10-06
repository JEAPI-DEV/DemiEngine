#include "demi/runtime/terrain/TerrainWorld.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include "demi/runtime/terrain/TerrainWorldBatch.h"

#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainMeshBuilder.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWaterMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {

namespace {
using terrain_detail::buildChunkTriangles;
using terrain_detail::buildMeshEntity;
using terrain_detail::surfaceId;
using terrain_detail::TerrainGeneratedSurface;
using terrain_detail::TerrainGeneratedWaterSurface;

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

bool samePoint(Vec3 a, Vec3 b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool sameUv(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

bool sameColor(Color a, Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool sameTriangles(const ColliderAsset3D &collider,
                   std::span<const Vec3> vertices) {
  if (collider.triangles.size() != vertices.size() / 3)
    return false;
  for (std::size_t triangle = 0; triangle < collider.triangles.size();
       ++triangle) {
    const auto &points = collider.triangles[triangle];
    if (!samePoint(points.a, vertices[triangle * 3]) ||
        !samePoint(points.b, vertices[triangle * 3 + 1]) ||
        !samePoint(points.c, vertices[triangle * 3 + 2]))
      return false;
  }
  return true;
}

} // namespace

struct PreparedTerrainWorldUpdate::State {
  struct SurfaceChange {
    std::size_t index;
    std::string id;
    std::uint64_t revision;
    std::shared_ptr<const ColliderAsset3D> collider;
    Color beforeColor;
    std::string beforeMaterial;
    std::optional<Entity> replacement;
    std::optional<Color> tint;
    std::optional<std::string> material;
    std::optional<std::vector<Vec2>> uvs;
    bool remove = false;
  };

  std::string ownerId;
  std::size_t ownerIndex = 0;
  std::size_t entityCount = 0;
  std::shared_ptr<const HeightField> beforeField;
  std::shared_ptr<const HeightField> field;
  nlohmann::json beforeRecipe;
  nlohmann::json recipe;
  std::string serializedTerrainKey = "Terrain3D";
  std::optional<std::string> beforeSerializedTerrain;
  std::string serializedTerrain;
  std::unordered_map<std::string, std::string>::node_type
      serializedTerrainInsertion;
  std::string layer, sceneOwner, prefabInstance;
  bool persistent = false;
  std::vector<SurfaceChange> changes;
  std::vector<Entity> additions;
  std::unordered_set<std::string> additionIds;
  std::vector<std::size_t> removals;
  std::optional<std::size_t> ownershipIndex;
  std::vector<std::string> beforeSurfaces;
  TerrainRuntimeOwner ownership;
  bool ownershipChanged = false;
};

PreparedTerrainWorldUpdate::PreparedTerrainWorldUpdate(
    std::unique_ptr<State> state)
    : state_(std::move(state)) {}
PreparedTerrainWorldUpdate::PreparedTerrainWorldUpdate(
    PreparedTerrainWorldUpdate &&) noexcept = default;
PreparedTerrainWorldUpdate &PreparedTerrainWorldUpdate::operator=(
    PreparedTerrainWorldUpdate &&) noexcept = default;
PreparedTerrainWorldUpdate::~PreparedTerrainWorldUpdate() = default;

namespace {
TerrainRect chunkSamples(const TerrainChunk &chunk) {
  return {chunk.firstCellX, chunk.firstCellZ, chunk.firstCellX + chunk.cellsX,
          chunk.firstCellZ + chunk.cellsZ};
}

bool sameSample(const HeightField &field, std::size_t index,
                const TerrainSampleValue &value) {
  return field.baseHeights.at(index) == value.base &&
         field.heights.at(index) == value.height &&
         field.exclusions.at(index) == value.exclusion &&
         samePoint(field.normals.at(index), value.normal) &&
         field.biomeIndices.at(index) == value.biome;
}

bool sameBiomePalette(const HeightField &field,
                      const TerrainBiomePalette &palette) {
  return field.biomeIds == palette.ids &&
         std::ranges::equal(field.biomeColors, palette.colors, sameColor) &&
         field.biomeMaterials == palette.materials &&
         field.biomeTextureScales == palette.textureScales;
}

void validateUpdatePatch(const HeightField *before, const TerrainUpdate &update,
                         std::stop_token stop) {
  if (!update.patch)
    return;
  const auto &patch = *update.patch;
  if (patch.fullBefore || patch.fullAfter) {
    if (!update.invalidation.fullGeneration &&
        !update.invalidation.layoutChanged)
      throw std::invalid_argument(
          "Global terrain patch requires full/layout invalidation");
    const auto matches =
        [](const HeightField *current,
           const std::shared_ptr<const HeightField> &snapshot) {
          return current == snapshot.get() ||
                 (current && snapshot &&
                  terrainFieldsEqual(*current, *snapshot));
        };
    // Local history reconstructs an equivalent COW field rather than the
    // original snapshot pointer. Global history must accept that exact value.
    if (!((matches(before, patch.fullBefore) &&
           matches(update.field.get(), patch.fullAfter)) ||
          (matches(before, patch.fullAfter) &&
           matches(update.field.get(), patch.fullBefore))))
      throw std::invalid_argument(
          "Terrain patch is stale: global source/result changed");
    return;
  }
  const auto &field = *update.field;
  if (!before || !sameUv(patch.size, field.size) ||
      patch.cellsX != field.cellsX || patch.cellsZ != field.cellsZ ||
      !sameUv(before->size, field.size) || before->cellsX != field.cellsX ||
      before->cellsZ != field.cellsZ)
    throw std::invalid_argument(
        "Terrain patch does not match the retained grid");
  if (patch.beforePalette.has_value() != patch.afterPalette.has_value())
    throw std::invalid_argument("Terrain patch requires both palette states");
  bool forward = true, backward = true;
  if (patch.beforePalette) {
    forward = sameBiomePalette(*before, *patch.beforePalette) &&
              sameBiomePalette(field, *patch.afterPalette);
    backward = sameBiomePalette(*before, *patch.afterPalette) &&
               sameBiomePalette(field, *patch.beforePalette);
  }
  for (const auto &change : patch.samples) {
    if (stop.stop_requested())
      throw std::runtime_error("Terrain publication cancelled");
    forward = forward && sameSample(*before, change.index, change.before) &&
              sameSample(field, change.index, change.after);
    backward = backward && sameSample(*before, change.index, change.after) &&
               sameSample(field, change.index, change.before);
    if (!forward && !backward)
      throw std::invalid_argument(
          "Terrain patch is stale: source/result samples changed");
    const auto index = change.index;
    const auto normal = field.normals.at(index);
    if (!std::isfinite(field.baseHeights.at(index)) ||
        !std::isfinite(field.heights.at(index)) ||
        !std::isfinite(field.exclusions.at(index)) ||
        field.exclusions.at(index) < 0 || field.exclusions.at(index) > 1 ||
        !std::isfinite(normal.x) || !std::isfinite(normal.y) ||
        !std::isfinite(normal.z) || normal.y < 0 ||
        std::abs(
            std::hypot(double(normal.x), double(normal.y), double(normal.z)) -
            1) > 1e-4 ||
        field.biomeIndices.at(index) >= field.biomeIds.size())
      throw std::invalid_argument(
          "Terrain patch contains invalid sample values");
  }
  if (!forward && !backward)
    throw std::invalid_argument(
        "Terrain patch is stale: source/result palette changed");
}

void validateUpdateLayout(const TerrainRecipe &recipe, const HeightField &field,
                          const HeightField *before,
                          const TerrainInvalidation &invalidation) {
  if (field.cellsX != recipe.cellsX || field.cellsZ != recipe.cellsZ ||
      !sameUv(field.size, recipe.size))
    throw std::invalid_argument(
        "Terrain result dimensions do not match its recipe");
  const auto count = recipe.sampleCount();
  if (field.baseHeights.size() != count || field.heights.size() != count ||
      field.normals.size() != count || field.biomeIndices.size() != count ||
      field.exclusions.size() != count)
    throw std::invalid_argument(
        "Terrain result arrays do not match its sample count");
  if (field.biomeIds.size() != recipe.biomes.size() ||
      field.biomeColors.size() != recipe.biomes.size() ||
      (!field.biomeMaterials.empty() &&
       field.biomeMaterials.size() != recipe.biomes.size()) ||
      (!field.biomeTextureScales.empty() &&
       field.biomeTextureScales.size() != recipe.biomes.size()))
    throw std::invalid_argument(
        "Terrain result palette does not match its recipe");
  std::size_t biome = 0;
  for (const auto &[id, settings] : recipe.biomes) {
    if (field.biomeIds[biome] != id ||
        !sameColor(field.biomeColors[biome], settings.color) ||
        field.biomeMaterial(biome) != settings.material ||
        field.biomeTextureScale(biome) != settings.textureScale)
      throw std::invalid_argument(
          "Terrain result biome appearance does not match its recipe");
    ++biome;
  }

  const auto chunksX = (std::size_t(recipe.cellsX) - 1) / recipe.chunkCells + 1;
  const auto chunksZ = (std::size_t(recipe.cellsZ) - 1) / recipe.chunkCells + 1;
  if (field.chunks.size() != chunksX * chunksZ)
    throw std::invalid_argument(
        "Terrain result chunk count does not match its recipe");
  std::size_t index = 0;
  bool sameLayout = before && before->cellsX == field.cellsX &&
                    before->cellsZ == field.cellsZ &&
                    sameUv(before->size, field.size) &&
                    before->chunks.size() == field.chunks.size();
  for (int z = 0; z < recipe.cellsZ;) {
    const int depth = std::min(recipe.chunkCells, recipe.cellsZ - z);
    for (int x = 0; x < recipe.cellsX;) {
      const int width = std::min(recipe.chunkCells, recipe.cellsX - x);
      const auto &chunk = field.chunks[index];
      if (chunk.firstCellX != x || chunk.firstCellZ != z ||
          chunk.cellsX != width || chunk.cellsZ != depth)
        throw std::invalid_argument(
            "Terrain result chunk layout does not match its recipe");
      if (sameLayout) {
        const auto &old = before->chunks[index];
        sameLayout = old.firstCellX == x && old.firstCellZ == z &&
                     old.cellsX == width && old.cellsZ == depth;
      }
      ++index;
      x += width;
    }
    z += depth;
  }
  if (!sameLayout && !invalidation.fullGeneration &&
      !invalidation.layoutChanged)
    throw std::invalid_argument(
        "Terrain layout changes require explicit full/layout invalidation");
  // Global history rectangles may describe the opposite-sized grid on Undo.
  // Global/layout publication replaces the entire owner, so no rect is used.
  if (invalidation.fullGeneration || invalidation.layoutChanged)
    return;
  const TerrainRect bounds{0, 0, field.cellsX, field.cellsZ};
  for (const auto &rect :
       {invalidation.baseSamples, invalidation.heightSamples,
        invalidation.normalSamples, invalidation.biomeSamples,
        invalidation.exclusionSamples})
    if (!rect.empty() && (!bounds.contains(rect.minX, rect.minZ) ||
                          !bounds.contains(rect.maxX, rect.maxZ)))
      throw std::invalid_argument(
          "Terrain dirty samples extend outside its grid");
}
} // namespace

std::optional<PreparedTerrainWorldUpdate> prepareTerrainWorldUpdate(
    const World &world, std::string_view ownerId, const nlohmann::json &recipe,
    const TerrainUpdate &update, std::string &error, std::stop_token stop) {
  error.clear();
  try {
    if (stop.stop_requested()) {
      error = "Terrain publication cancelled";
      return std::nullopt;
    }
    const auto ownerIt =
        std::ranges::find(world.entities, ownerId, &Entity::id);
    if (ownerIt == world.entities.end())
      throw std::invalid_argument("Terrain owner does not exist");
    const Entity &owner = *ownerIt;
    const auto *terrain = owner.component<Terrain3DComponent>();
    if (!terrain || !owner.hasComponent<Transform3DComponent>())
      throw std::invalid_argument(
          "Terrain owner requires Terrain3D and Transform3D");
    if (!update.field)
      throw std::invalid_argument("Terrain update has no heightfield");
    const auto parsed = TerrainRecipe::parse(recipe);
    const auto &field = *update.field;
    validateUpdateLayout(parsed, field, terrain->generated.get(),
                         update.invalidation);
    validateUpdatePatch(terrain->generated.get(), update, stop);

    auto state = std::make_unique<PreparedTerrainWorldUpdate::State>();
    state->ownerId = owner.id;
    state->ownerIndex = std::size_t(ownerIt - world.entities.begin());
    state->entityCount = world.entities.size();
    state->beforeField = terrain->generated;
    state->field = update.field;
    state->beforeRecipe = terrain->recipe;
    state->recipe = recipe;
    auto serialized = nlohmann::json::object();
    const auto source =
        owner.serializedComponents.find(state->serializedTerrainKey);
    if (source != owner.serializedComponents.end()) {
      state->beforeSerializedTerrain = source->second;
      serialized = nlohmann::json::parse(source->second);
      if (!serialized.is_object())
        throw std::invalid_argument("Serialized Terrain3D must be an object");
    }
    if (terrain->asset.empty()) {
      serialized["recipe"] = recipe;
    } else {
      serialized.erase("recipe");
      serialized["asset"] = terrain->asset;
    }
    state->serializedTerrain = serialized.dump();
    if (!state->beforeSerializedTerrain) {
      // A prepared node permits first publication on native-created owners
      // without allocating an entry after the commit point.
      std::unordered_map<std::string, std::string> insertion;
      insertion.emplace(state->serializedTerrainKey,
                        std::move(state->serializedTerrain));
      state->serializedTerrainInsertion = insertion.extract(insertion.begin());
    }
    state->layer = owner.layer;
    state->sceneOwner = owner.sceneOwner;
    state->prefabInstance = owner.prefabInstance;
    state->persistent = owner.persistent;
    const bool full =
        update.invalidation.fullGeneration || update.invalidation.layoutChanged;
    const auto dirty = update.invalidation.geometrySamples();
    const bool waterChanged =
        terrainWaterMeshesChanged(terrain->generated.get(), field);

    // Exclusions and recipe-only changes do not even inspect native surfaces.
    if (!full && dirty.empty() && !update.invalidation.materialsChanged &&
        !waterChanged) {
      if (stop.stop_requested()) {
        error = "Terrain publication cancelled";
        return std::nullopt;
      }
      return PreparedTerrainWorldUpdate(std::move(state));
    }

    // One read-only index pass. Only this owner's affected native surfaces are
    // staged; authored entities and other owners are never copied/rebuilt.
    std::unordered_map<std::string, std::size_t> ids;
    std::map<std::pair<int, int>, std::vector<std::size_t>> previousChunks;
    std::vector<std::size_t> ownerSurfaces;
    for (std::size_t index = 0; index < world.entities.size(); ++index) {
      const auto &entity = world.entities[index];
      if (!ids.emplace(entity.id, index).second)
        throw std::invalid_argument("Duplicate entity ID: " + entity.id);
      const auto *surface = entity.component<TerrainGeneratedSurface>();
      if (surface && surface->owner == owner.id) {
        ownerSurfaces.push_back(index);
        previousChunks[{surface->firstCellX, surface->firstCellZ}].push_back(
            index);
      } else if (const auto *water =
                     entity.component<TerrainGeneratedWaterSurface>();
                 water && water->owner == owner.id) {
        ownerSurfaces.push_back(index);
      }
    }
    for (std::size_t index = 0; index < world.terrainOwners.size(); ++index)
      if (world.terrainOwners[index].id == owner.id) {
        state->ownershipIndex = index;
        break;
      }

    std::unordered_map<std::size_t, std::size_t> staged;
    const auto stage = [&](std::size_t index) -> auto & {
      if (const auto found = staged.find(index); found != staged.end())
        return state->changes[found->second];
      const auto &entity = world.entities[index];
      const auto *mesh = entity.component<MeshRendererComponent>();
      const auto *collider = entity.component<ModelCollider3DComponent>();
      const bool water = entity.hasComponent<TerrainGeneratedWaterSurface>();
      if (!mesh || (!water && (!collider || !collider->inlineGeometry)))
        throw std::invalid_argument(
            "Terrain surface is missing mesh/collision: " + entity.id);
      staged.emplace(index, state->changes.size());
      state->changes.push_back(
          {.index = index,
           .id = entity.id,
           .revision = mesh->revision,
           .collider = collider ? collider->inlineGeometry : nullptr,
           .beforeColor = mesh->color,
           .beforeMaterial = mesh->material});
      return state->changes.back();
    };
    if (full)
      for (const auto index : ownerSurfaces)
        stage(index).remove = true;

    const bool enabled = terrainHierarchyEnabled(world, owner);
    for (const auto &chunk : field.chunks) {
      if (!full && !dirty.intersects(chunkSamples(chunk)))
        continue;
      if (stop.stop_requested()) {
        error = "Terrain publication cancelled";
        return std::nullopt;
      }
      const auto previous =
          previousChunks.find({chunk.firstCellX, chunk.firstCellZ});
      if (previous != previousChunks.end())
        for (const auto index : previous->second)
          stage(index).remove = true;

      const auto surfaces = buildChunkTriangles(field, chunk, stop);
      if (stop.stop_requested()) {
        error = "Terrain publication cancelled";
        return std::nullopt;
      }
      for (const auto &[biome, triangles] : surfaces) {
        const auto &biomeId = field.biomeIds.at(biome);
        const auto color = field.biomeColors.at(biome);
        const auto &material = field.biomeMaterial(biome);
        auto id = surfaceId(owner.id, chunk, biomeId);
        const auto existing = ids.find(id);
        const Entity *old =
            existing == ids.end() ? nullptr : &world.entities[existing->second];
        if (old && terrainSurfaceOwner(*old) != owner.id)
          throw std::invalid_argument(
              "Generated terrain entity ID conflicts with existing entity: " +
              id);

        std::shared_ptr<const ColliderAsset3D> retainedCollider;
        if (old) {
          auto &change = stage(existing->second);
          change.remove = false;
          const auto *mesh = old->component<MeshRendererComponent>();
          const bool sameGeometry =
              std::ranges::equal(mesh->vertices, triangles.vertices,
                                 samePoint) &&
              std::ranges::equal(mesh->normals, triangles.normals, samePoint) &&
              std::ranges::equal(mesh->uvs, triangles.uvs, sameUv);
          if (sameGeometry) {
            if (!sameColor(mesh->color, color))
              change.tint = color;
            if (mesh->material != material)
              change.material = material;
            continue;
          }
          if (sameTriangles(*change.collider, triangles.vertices))
            retainedCollider = change.collider;
        }
        // Renaming/reindexing a biome can retain its physical surface even
        // when the stable generated group ID changes.
        if (!retainedCollider && previous != previousChunks.end())
          for (const auto index : previous->second) {
            const auto *collider =
                world.entities[index].component<ModelCollider3DComponent>();
            if (sameTriangles(*collider->inlineGeometry, triangles.vertices)) {
              retainedCollider = collider->inlineGeometry;
              break;
            }
          }
        auto entity = buildMeshEntity(
            owner, std::move(id), triangles.vertices, triangles.normals,
            triangles.uvs, color, material, error, std::move(retainedCollider));
        if (!entity)
          return std::nullopt;
        entity->enabled = enabled;
        entity->setComponent(TerrainGeneratedSurface{
            owner.id, chunk.firstCellX, chunk.firstCellZ, biomeId});
        if (old)
          stage(existing->second).replacement = std::move(*entity);
        else {
          if (!state->additionIds.insert(entity->id).second)
            throw std::invalid_argument(
                "Duplicate generated terrain entity ID: " + entity->id);
          state->additions.push_back(std::move(*entity));
        }
      }
    }

    if (update.invalidation.materialsChanged) {
      for (const auto index : ownerSurfaces) {
        if (stop.stop_requested()) {
          error = "Terrain publication cancelled";
          return std::nullopt;
        }
        const auto found = staged.find(index);
        if (found != staged.end() &&
            (state->changes[found->second].remove ||
             state->changes[found->second].replacement))
          continue;
        const auto *surface =
            world.entities[index].component<TerrainGeneratedSurface>();
        if (!surface)
          continue;
        const auto biome = std::ranges::find(field.biomeIds, surface->biome);
        if (biome == field.biomeIds.end())
          throw std::invalid_argument(
              "Removed biome still owns unchanged terrain triangles: " +
              surface->biome);
        const auto biomeIndex = std::size_t(biome - field.biomeIds.begin());
        const auto color = field.biomeColors.at(biomeIndex);
        const auto *mesh =
            world.entities[index].component<MeshRendererComponent>();
        if (!mesh)
          throw std::invalid_argument("Terrain surface is missing mesh: " +
                                      world.entities[index].id);
        if (!sameColor(mesh->color, color))
          stage(index).tint = color;
        if (mesh->material != field.biomeMaterial(biomeIndex))
          stage(index).material = field.biomeMaterial(biomeIndex);
        const auto oldBiome =
            std::ranges::find(state->beforeField->biomeIds, surface->biome);
        if (oldBiome == state->beforeField->biomeIds.end())
          throw std::invalid_argument(
              "Terrain surface biome is missing from retained field");
        const auto oldIndex =
            std::size_t(oldBiome - state->beforeField->biomeIds.begin());
        if (state->beforeField->biomeTextureScale(oldIndex) !=
            field.biomeTextureScale(biomeIndex))
          stage(index).uvs = terrain_detail::terrainUvs(
              mesh->vertices, field.biomeTextureScale(biomeIndex));
      }
    }
    if (full || waterChanged) {
      for (const auto index : ownerSurfaces)
        if (world.entities[index].hasComponent<TerrainGeneratedWaterSurface>())
          stage(index).remove = true;
      for (auto &entity : buildTerrainWaterMeshes(owner, field, stop)) {
        entity.enabled = enabled;
        const auto existing = ids.find(entity.id);
        if (existing != ids.end()) {
          const auto &previous = world.entities[existing->second];
          if (!previous.hasComponent<TerrainGeneratedWaterSurface>() ||
              terrainSurfaceOwner(previous) != owner.id)
            throw std::invalid_argument(
                "Generated water entity ID conflicts with existing entity: " +
                entity.id);
          auto &change = stage(existing->second);
          change.remove = false;
          change.replacement = std::move(entity);
        } else {
          if (!state->additionIds.insert(entity.id).second)
            throw std::invalid_argument(
                "Duplicate generated water entity ID: " + entity.id);
          state->additions.push_back(std::move(entity));
        }
      }
    }
    for (const auto &change : state->changes)
      if (change.remove)
        state->removals.push_back(change.index);
    std::ranges::sort(state->removals);
    state->ownershipChanged =
        !state->removals.empty() || !state->additions.empty();
    if (state->ownershipChanged) {
      if (state->ownershipIndex)
        state->beforeSurfaces =
            world.terrainOwners[*state->ownershipIndex].surfaces;
      state->ownership.id = owner.id;
      state->ownership.surfaces.reserve(ownerSurfaces.size() +
                                        state->additions.size());
      for (const auto index : ownerSurfaces)
        if (!std::ranges::binary_search(state->removals, index))
          state->ownership.surfaces.push_back(world.entities[index].id);
      for (const auto &entity : state->additions)
        state->ownership.surfaces.push_back(entity.id);
    }
    if (stop.stop_requested()) {
      error = "Terrain publication cancelled";
      return std::nullopt;
    }
    return PreparedTerrainWorldUpdate(std::move(state));
  } catch (const std::exception &exception) {
    error = "Terrain3D on " + std::string(ownerId) + ": " + exception.what();
    return std::nullopt;
  }
}

bool publishTerrainWorldUpdate(World &world,
                               PreparedTerrainWorldUpdate &&prepared,
                               std::string &error, std::stop_token stop) {
  error.clear();
  try {
    if (!prepared.state_)
      throw std::invalid_argument(
          "Terrain preparation has already been consumed");
    auto &state = *prepared.state_;
    if (stop.stop_requested()) {
      error = "Terrain publication cancelled";
      return false;
    }
    if (world.entities.size() != state.entityCount ||
        state.ownerIndex >= world.entities.size() ||
        world.entities[state.ownerIndex].id != state.ownerId)
      throw std::invalid_argument(
          "Terrain preparation is stale: entity collection changed");
    const auto &owner = world.entities[state.ownerIndex];
    const auto *terrain = owner.component<Terrain3DComponent>();
    if (!terrain || !owner.hasComponent<Transform3DComponent>() ||
        terrain->generated != state.beforeField ||
        terrain->recipe != state.beforeRecipe || owner.layer != state.layer ||
        owner.sceneOwner != state.sceneOwner ||
        owner.prefabInstance != state.prefabInstance ||
        owner.persistent != state.persistent)
      throw std::invalid_argument(
          "Terrain preparation is stale: owner changed");
    const auto serialized =
        owner.serializedComponents.find(state.serializedTerrainKey);
    if (state.beforeSerializedTerrain
            ? serialized == owner.serializedComponents.end() ||
                  serialized->second != *state.beforeSerializedTerrain
            : serialized != owner.serializedComponents.end())
      throw std::invalid_argument(
          "Terrain preparation is stale: serialized Terrain3D changed");
    for (const auto &change : state.changes) {
      const auto &entity = world.entities[change.index];
      const auto *mesh = entity.component<MeshRendererComponent>();
      const auto *collider = entity.component<ModelCollider3DComponent>();
      const auto currentCollider =
          collider ? collider->inlineGeometry : nullptr;
      if (entity.id != change.id ||
          terrainSurfaceOwner(entity) != state.ownerId || !mesh ||
          mesh->revision != change.revision ||
          !sameColor(mesh->color, change.beforeColor) ||
          mesh->material != change.beforeMaterial ||
          currentCollider != change.collider)
        throw std::invalid_argument(
            "Terrain preparation is stale: surface changed");
    }
    if (state.ownershipChanged && state.ownershipIndex &&
        (*state.ownershipIndex >= world.terrainOwners.size() ||
         world.terrainOwners[*state.ownershipIndex].id != state.ownerId ||
         world.terrainOwners[*state.ownershipIndex].surfaces !=
             state.beforeSurfaces))
      throw std::invalid_argument(
          "Terrain preparation is stale: ownership changed");
    // Collection size alone cannot detect a same-size authored rename into a
    // newly generated ID while background preparation was in flight.
    if (!state.additionIds.empty())
      for (const auto &entity : world.entities)
        if (state.additionIds.contains(entity.id))
          throw std::invalid_argument(
              "Generated terrain entity ID conflicts with existing entity: " +
              entity.id);
    if (state.ownershipChanged && !state.ownershipIndex &&
        std::ranges::find(world.terrainOwners, state.ownerId,
                          &TerrainRuntimeOwner::id) !=
            world.terrainOwners.end())
      throw std::invalid_argument(
          "Terrain preparation is stale: ownership changed");
    if (state.additions.size() >
        world.entities.max_size() - world.entities.size())
      throw std::length_error(
          "Generated terrain exceeds entity storage capacity");
    // Capacity is the last fallible work. Nothing below allocates, so render,
    // collision, recipe and the retained field become visible in one commit.
    if (state.ownershipChanged && !state.ownershipIndex)
      world.terrainOwners.reserve(world.terrainOwners.size() + 1);
    const bool enabled = terrainHierarchyEnabled(world, owner);
    if (!state.beforeSerializedTerrain) {
      auto &components = world.entities[state.ownerIndex].serializedComponents;
      if (components.size() == components.max_size())
        throw std::length_error(
            "Serialized components exceed storage capacity");
      components.reserve(components.size() + 1);
    }
    if (!state.additions.empty())
      world.entities.reserve(world.entities.size() + state.additions.size());
    if (stop.stop_requested()) {
      error = "Terrain publication cancelled";
      return false;
    }
    static_assert(std::is_nothrow_move_assignable_v<Entity>);
    static_assert(std::is_nothrow_move_constructible_v<Entity>);
    for (auto &change : state.changes) {
      if (change.replacement) {
        change.replacement->enabled = enabled;
        world.entities[change.index] = std::move(*change.replacement);
      } else {
        auto *mesh =
            world.entities[change.index].component<MeshRendererComponent>();
        if (change.tint)
          mesh->color = *change.tint;
        if (change.material)
          mesh->material.swap(*change.material);
        if (change.uvs) {
          mesh->uvs.swap(*change.uvs);
          mesh->markGeometryChanged();
        }
      }
    }
    auto &installedOwner = world.entities[state.ownerIndex];
    auto *installed = installedOwner.component<Terrain3DComponent>();
    installed->recipe.swap(state.recipe);
    installed->generated.swap(state.field);
    if (state.beforeSerializedTerrain)
      installedOwner.serializedComponents.find(state.serializedTerrainKey)
          ->second.swap(state.serializedTerrain);
    else
      installedOwner.serializedComponents.insert(
          std::move(state.serializedTerrainInsertion));
    if (!state.removals.empty()) {
      std::size_t index = 0;
      std::erase_if(world.entities, [&](const Entity &) {
        return std::ranges::binary_search(state.removals, index++);
      });
    }
    for (auto &entity : state.additions) {
      entity.enabled = enabled;
      world.entities.push_back(std::move(entity));
    }
    if (state.ownershipChanged) {
      if (state.ownershipIndex)
        world.terrainOwners[*state.ownershipIndex] = std::move(state.ownership);
      else
        world.terrainOwners.push_back(std::move(state.ownership));
      world.terrainEntityLookup.clear();
    }
    prepared.state_.reset();
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool updateTerrainWorld(World &world, std::string_view ownerId,
                        const nlohmann::json &recipe,
                        const TerrainUpdate &update, std::string &error,
                        RuntimePrefabService *prefabs) {
  const std::array<std::string, 1> owners{std::string(ownerId)};
  return updateTerrainWorldBatch(world, owners, recipe, update, error, prefabs);
}

std::optional<std::string_view> terrainSurfaceOwner(const Entity &entity) {
  if (const auto *water = entity.component<TerrainGeneratedWaterSurface>())
    return water->owner;
  const auto *surface = entity.component<TerrainGeneratedSurface>();
  return surface ? std::optional<std::string_view>{surface->owner}
                 : std::nullopt;
}

bool materializeTerrains(World &world, std::string &error,
                         RuntimePrefabService *prefabs,
                         const TerrainInputResolver &resolveInputs,
                         const TerrainFieldResolver &resolveField) {
  error.clear();
  std::vector<Entity> generated;
  std::vector<TerrainRuntimeOwner> owners;
  std::unordered_set<std::string> ids;
  for (const auto &entity : world.entities)
    if (!terrainSurfaceOwner(entity))
      ids.insert(entity.id);

  // Retention is committed with the meshes only after every owner succeeds.
  std::vector<std::tuple<std::size_t, std::shared_ptr<const HeightField>,
                         nlohmann::json>>
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
      nlohmann::json sourceRecipe = terrain->recipe;
      std::shared_ptr<const HeightField> field;
      if (!terrain->asset.empty()) {
        if (!resolveField)
          throw std::invalid_argument(
              "Terrain asset requires an asset resolver: " + terrain->asset);
        field = resolveField(terrain->asset, sourceRecipe);
      } else if (!sourceRecipe.is_null()) {
        // An inline recipe explicitly opts into procedural generation.
        const auto parsed = TerrainRecipe::parse(sourceRecipe);
        const auto inputs =
            resolveInputs ? resolveInputs(parsed) : TerrainGenerationInputs{};
        field = acquireTerrain(sourceRecipe, inputs);
      } else {
        // Empty components are authoring placeholders, not implicit generation.
        continue;
      }
      if (!field)
        throw std::runtime_error("Terrain source returned no heightfield");
      if (field->biomeIds.size() != field->biomeColors.size() ||
          (!field->biomeMaterials.empty() &&
           field->biomeIds.size() != field->biomeMaterials.size()) ||
          (!field->biomeTextureScales.empty() &&
           field->biomeIds.size() != field->biomeTextureScales.size()))
        throw std::invalid_argument(
            "Terrain field has incomplete biome appearance metadata");
      for (const float scale : field->biomeTextureScales)
        if (!std::isfinite(scale) || scale <= 0)
          throw std::invalid_argument(
              "Terrain field has invalid biome texture scale");
      fields.emplace_back(ownerIndex, field, std::move(sourceRecipe));
      owners.push_back({.id = owner.id});
      const bool enabled = terrainHierarchyEnabled(world, owner);
      for (const TerrainChunk &chunk : field->chunks) {
        const auto surfaces = buildChunkTriangles(*field, chunk);
        for (const auto &[biome, surface] : surfaces) {
          std::string id =
              surfaceId(owner.id, chunk, field->biomeIds.at(biome));
          if (!ids.insert(id).second) {
            error =
                "Generated terrain entity ID conflicts with existing entity: " +
                id;
            return false;
          }
          auto entity = buildTerrainMeshEntity(
              owner, std::move(id), surface.vertices, surface.normals,
              surface.uvs, field->biomeColors.at(biome),
              field->biomeMaterial(biome), error);
          if (!entity)
            return false;
          entity->setComponent(TerrainGeneratedSurface{
              owner.id, chunk.firstCellX, chunk.firstCellZ,
              field->biomeIds.at(biome)});
          entity->enabled = enabled;
          owners.back().surfaces.push_back(entity->id);
          generated.push_back(std::move(*entity));
        }
      }
      for (auto &entity : buildTerrainWaterMeshes(owner, *field)) {
        if (!ids.insert(entity.id).second)
          throw std::invalid_argument(
              "Generated water entity ID conflicts with existing entity: " +
              entity.id);
        entity.enabled = enabled;
        owners.back().surfaces.push_back(entity.id);
        generated.push_back(std::move(entity));
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
  for (auto &[index, field, sourceRecipe] : fields) {
    auto *terrain = world.entities[index].component<Terrain3DComponent>();
    terrain->generated = field;
    if (!terrain->asset.empty())
      terrain->recipe.swap(sourceRecipe);
  }
  std::erase_if(world.entities, [](const Entity &entity) {
    return terrainSurfaceOwner(entity).has_value();
  });
  for (auto &entity : generated)
    world.entities.push_back(std::move(entity));
  world.terrainOwners = std::move(owners);
  world.terrainEntityLookup.clear();
  // Scattering runs after the commit, outside the all-or-nothing block, because
  // publication is allocation-free by contract and instantiating a prefab
  // allocates. A scatter failure must not discard terrain that is already
  // correct, so the placements stay recorded on the field for a later attempt.
  {
    WorldCommandBuffer commands;
    for (const auto &[index, field, sourceRecipe] : fields) {
      if (field->scatterPlacements.empty())
        continue;
      std::string scatterError;
      (void)syncTerrainScatter(world, commands, world.entities[index].id,
                               *field, prefabs, scatterError);
    }
    (void)commands.flush(world);
  }
  return true;
}

TerrainScatterResolution
materializeTerrainScatter(World &world, RuntimePrefabService *prefabs,
                          std::string &error) {
  error.clear();
  TerrainScatterResolution resolution;
  WorldCommandBuffer commands;
  // Snapshot the owners first: the command buffer mutates world.entities, and
  // iterating it while instances are appended would invalidate the walk.
  std::vector<std::string> owners;
  for (const auto &entity : world.entities) {
    const auto *terrain = entity.component<Terrain3DComponent>();
    if (terrain != nullptr && terrain->generated &&
        !terrain->generated->scatterPlacements.empty())
      owners.push_back(entity.id);
  }
  for (const auto &id : owners) {
    const auto *owner = findEntity(world, id);
    const auto *terrain =
        owner == nullptr ? nullptr : owner->component<Terrain3DComponent>();
    if (terrain == nullptr || !terrain->generated)
      continue;
    std::string scatterError;
    const auto stats = syncTerrainScatter(
        world, commands, id, *terrain->generated, prefabs, scatterError);
    if (!scatterError.empty() && error.empty())
      error = scatterError;
    resolution.placed += stats.total();
    resolution.unresolved += stats.unresolved;
  }
  if (error.empty() && resolution.unresolved == 0)
    (void)commands.flush(world);
  if (prefabs != nullptr)
    prefabs->prune(world);
  return resolution;
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
