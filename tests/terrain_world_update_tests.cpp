#include "demi/runtime/physics/ColliderAsset3D.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include "demi/runtime/terrain/TerrainWorldBatch.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

using namespace demi::runtime;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool equal(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool equal(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
bool equal(Color a, Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

Entity &entity(World &world, const std::string &id) {
  const auto found = std::ranges::find(world.entities, id, &Entity::id);
  check(found != world.entities.end(), "Missing expected entity");
  return *found;
}

const Entity &entity(const World &world, const std::string &id) {
  const auto found = std::ranges::find(world.entities, id, &Entity::id);
  check(found != world.entities.end(), "Missing expected entity");
  return *found;
}

std::shared_ptr<const HeightField> field(const World &world,
                                         const std::string &id = "land") {
  return entity(world, id).component<Terrain3DComponent>()->generated;
}

TerrainRecipe flatRecipe() {
  TerrainRecipe recipe;
  recipe.size = {12, 12};
  recipe.cellsX = recipe.cellsZ = 12;
  recipe.chunkCells = 4;
  recipe.landforms.at("default").baseHeight = 1;
  recipe.landforms.at("default").heightVariation = 0;
  return recipe;
}

void addOwner(World &world, const std::string &id,
              const TerrainRecipe &recipe) {
  Entity owner;
  owner.id = id;
  owner.layer = "terrain";
  owner.sceneOwner = "scene://terrain";
  owner.setComponent(
      Transform3DComponent{.position = {10, 3, 20}, .scale = {2, 3, 2}});
  Terrain3DComponent terrain;
  terrain.recipe = recipe.toJson();
  owner.setComponent(std::move(terrain));
  world.entities.push_back(std::move(owner));
}

World makeWorld(const TerrainRecipe &recipe) {
  World world;
  addOwner(world, "land", recipe);
  addOwner(world, "other", recipe);
  Entity building;
  building.id = "building";
  building.serializedComponents["authored"] = "keep source formatting";
  building.setComponent(Transform3DComponent{.position = {2, 7, 3}});
  MeshRendererComponent mesh;
  mesh.vertices = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}};
  mesh.normals.assign(3, {0, 1, 0});
  mesh.uvs = {{0, 0}, {0, 1}, {1, 0}};
  mesh.markGeometryChanged();
  auto collider = std::make_shared<ColliderAsset3D>();
  collider->revision = mesh.revision;
  collider->triangles.push_back(
      {mesh.vertices[0], mesh.vertices[1], mesh.vertices[2]});
  building.setComponent(std::move(mesh));
  building.setComponent(ModelCollider3DComponent{.inlineGeometry = collider});
  world.entities.push_back(std::move(building));
  std::string error;
  check(materializeTerrains(world, error), error.c_str());
  return world;
}

struct Resource {
  std::uint64_t revision;
  std::uint64_t collisionRevision;
  const Vec3 *vertices;
  const Vec3 *normals;
  const Vec2 *uvs;
  std::shared_ptr<const ColliderAsset3D> collider;
  Color color;
};
using Resources = std::map<std::string, Resource>;

Resources resources(const World &world) {
  Resources result;
  for (const auto &entry : world.entities) {
    const auto *mesh = entry.component<MeshRendererComponent>();
    const auto *collider = entry.component<ModelCollider3DComponent>();
    if (mesh && collider)
      result.emplace(entry.id, Resource{mesh->revision,
                                        collider->inlineGeometry->revision,
                                        mesh->vertices.data(),
                                        mesh->normals.data(), mesh->uvs.data(),
                                        collider->inlineGeometry, mesh->color});
  }
  return result;
}

void checkResource(const World &world, const std::string &id,
                   const Resource &before, bool checkTint = true) {
  const auto &entry = entity(world, id);
  const auto *mesh = entry.component<MeshRendererComponent>();
  const auto *collider = entry.component<ModelCollider3DComponent>();
  check(mesh->revision == before.revision &&
            mesh->vertices.data() == before.vertices &&
            mesh->normals.data() == before.normals &&
            mesh->uvs.data() == before.uvs,
        "Unchanged geometry lost revision or buffer identity");
  check(collider->inlineGeometry == before.collider &&
            collider->inlineGeometry->revision == before.collisionRevision,
        "Unchanged collision lost pointer or revision identity");
  if (checkTint)
    check(equal(mesh->color, before.color), "Unchanged surface tint changed");
}

void checkAllResources(const World &world, const Resources &before,
                       bool checkTint = true) {
  check(resources(world).size() == before.size(),
        "Publication changed surface count");
  for (const auto &[id, resource] : before)
    checkResource(world, id, resource, checkTint);
}

void checkUnrelated(const World &world, const Resources &before,
                    const std::shared_ptr<const HeightField> &otherField) {
  check(field(world, "other") == otherField,
        "Publication changed another owner's field");
  for (const auto &[id, resource] : before)
    if (!id.starts_with("land/__terrain/"))
      checkResource(world, id, resource);
  const auto &building = entity(world, "building");
  check(building.serializedComponents.at("authored") ==
                "keep source formatting" &&
            building.component<Transform3DComponent>()->position.y == 7,
        "Publication changed authored scene content");
}

void checkReference(const World &world, const TerrainRecipe &recipe) {
  check(scene_loading::serializeComponent<Terrain3DComponent>(
            entity(world, "land"))
                .at("recipe") == recipe.toJson(),
        "Runtime serialization lost the published terrain recipe");
  World reference;
  addOwner(reference, "land", recipe);
  std::string error;
  check(materializeTerrains(reference, error), error.c_str());
  std::size_t actualCount = 0;
  for (const auto &expected : reference.entities) {
    if (!terrainSurfaceOwner(expected))
      continue;
    ++actualCount;
    const auto &actual = entity(world, expected.id);
    const auto *a = actual.component<MeshRendererComponent>();
    const auto *b = expected.component<MeshRendererComponent>();
    check(std::ranges::equal(a->vertices, b->vertices,
                             [](Vec3 x, Vec3 y) { return equal(x, y); }) &&
              std::ranges::equal(a->normals, b->normals,
                                 [](Vec3 x, Vec3 y) { return equal(x, y); }) &&
              std::ranges::equal(a->uvs, b->uvs,
                                 [](Vec2 x, Vec2 y) { return equal(x, y); }) &&
              equal(a->color, b->color),
          "Incremental geometry differs from full reference generation");
    const auto &triangles =
        actual.component<ModelCollider3DComponent>()->inlineGeometry->triangles;
    check(triangles.size() * 3 == a->vertices.size(),
          "Collision triangle count differs from rendering");
    for (std::size_t index = 0; index < triangles.size(); ++index)
      check(equal(triangles[index].a, a->vertices[index * 3]) &&
                equal(triangles[index].b, a->vertices[index * 3 + 1]) &&
                equal(triangles[index].c, a->vertices[index * 3 + 2]),
            "Publication installed stale collision");
    check(actual.component<Transform3DComponent>()->parent == "land" &&
              actual.layer == "terrain" &&
              actual.sceneOwner == "scene://terrain",
          "Updated surface lost native ownership metadata");
  }
  check(std::ranges::count_if(world.entities,
                              [](const Entity &entry) {
                                return terrainSurfaceOwner(entry) == "land";
                              }) == static_cast<std::ptrdiff_t>(actualCount),
        "Incremental publication left orphan/missing biome groups");
  const auto owner = std::ranges::find(world.terrainOwners, std::string("land"),
                                       &TerrainRuntimeOwner::id);
  check(owner != world.terrainOwners.end() &&
            owner->surfaces.size() == actualCount,
        "Incremental publication left a stale ownership index");
  for (const auto &id : owner->surfaces)
    check(terrainSurfaceOwner(entity(world, id)) == "land",
          "Ownership references a missing/nonterrain entity");
}

TerrainRecipe raised(TerrainRecipe recipe, Vec2 center) {
  TerrainEdit edit;
  edit.center = center;
  edit.radius = .4F;
  edit.falloff = 0;
  edit.amount = 2;
  recipe.edits.push_back(edit);
  return recipe;
}

void testLocalHeightAndNormalHalo() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  const auto original = resources(world);
  const auto other = field(world, "other");
  const auto after = raised(before, {3, 2});
  const auto update = updateTerrain(before, after, field(world));
  check(update.has_value() && !update->invalidation.fullGeneration,
        "Local brush unexpectedly requested global generation");
  check(update->invalidation.normalSamples.contains(4, 2),
        "Normal invalidation omitted the border halo");
  std::string error;
  auto prepared =
      prepareTerrainWorldUpdate(world, "land", after.toJson(), *update, error);
  check(prepared.has_value(), error.c_str());
  checkAllResources(world, original);
  check(entity(world, "land").component<Terrain3DComponent>()->recipe ==
            before.toJson(),
        "Preparation changed the recipe");
  check(publishTerrainWorldUpdate(world, std::move(*prepared), error),
        error.c_str());
  const auto changed = resources(world);
  check(changed.at("land/__terrain/0_0/default").revision !=
                original.at("land/__terrain/0_0/default").revision &&
            changed.at("land/__terrain/0_0/default").collider !=
                original.at("land/__terrain/0_0/default").collider,
        "Height edit did not update visual/collision geometry together");
  check(changed.at("land/__terrain/4_0/default").revision !=
                original.at("land/__terrain/4_0/default").revision &&
            changed.at("land/__terrain/4_0/default").collider ==
                original.at("land/__terrain/4_0/default").collider,
        "Normal halo failed to refresh border lighting or rebuilt unchanged "
        "collision");
  for (const auto &[id, resource] : original)
    if (id != "land/__terrain/0_0/default" &&
        id != "land/__terrain/4_0/default")
      checkResource(world, id, resource);
  check(field(world) == update->field,
        "Publication did not retain the supplied field");
  checkUnrelated(world, original, other);
  checkReference(world, after);
}

void testSharedCornerAndUndoRedo() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  const auto original = resources(world);
  const auto after = raised(before, {4, 4});
  const auto update = updateTerrain(before, after, field(world));
  check(update && update->patch && !update->patch->fullBefore &&
            !update->patch->fullAfter,
        "Local edit retained full-world history snapshots");
  std::string error;
  check(updateTerrainWorld(world, "land", after.toJson(), *update, error),
        error.c_str());
  for (const auto &[id, resource] : original) {
    if (id == "land/__terrain/0_0/default" ||
        id == "land/__terrain/4_0/default" ||
        id == "land/__terrain/0_4/default" ||
        id == "land/__terrain/4_4/default")
      check(entity(world, id)
                    .component<ModelCollider3DComponent>()
                    ->inlineGeometry != resource.collider,
            "Shared corner height edit left one border collider stale");
    else
      checkResource(world, id, resource);
  }
  checkReference(world, after);
  const auto undone = applyTerrainPatch(field(world), *update->patch, false);
  check(updateTerrainWorld(world, "land", before.toJson(), undone, error),
        error.c_str());
  checkReference(world, before);
  const auto redone = applyTerrainPatch(field(world), *update->patch, true);
  check(updateTerrainWorld(world, "land", after.toJson(), redone, error),
        error.c_str());
  checkReference(world, after);
}

void testTintAndExclusion() {
  auto before = flatRecipe();
  auto world = makeWorld(before);
  const auto original = resources(world);
  const auto other = field(world, "other");
  const auto *ownership = world.terrainOwners[0].surfaces.data();
  auto after = before;
  after.biomes.at("default").color = {.9F, .2F, .1F, 1};
  const auto tint = updateTerrain(before, after, field(world));
  check(tint && tint->invalidation.materialsChanged &&
            tint->invalidation.geometrySamples().empty(),
        "Tint invalidated geometry");
  std::string error;
  check(updateTerrainWorld(world, "land", after.toJson(), *tint, error),
        error.c_str());
  checkAllResources(world, original, false);
  check(world.terrainOwners[0].surfaces.data() == ownership,
        "Tint rebuilt native ownership");
  checkUnrelated(world, original, other);
  for (const auto &[id, resource] : resources(world))
    if (id.starts_with("land/__terrain/"))
      check(equal(resource.color, after.biomes.at("default").color),
            "Tint did not reach existing meshes");
  checkReference(world, after);

  before = after;
  after.exclusions.push_back({.center = {2, 2}, .radius = 1, .falloff = 0});
  const auto exclusions = updateTerrain(before, after, field(world));
  check(exclusions && !exclusions->invalidation.exclusionSamples.empty() &&
            exclusions->invalidation.geometrySamples().empty(),
        "Exclusion invalidated visual/collision geometry");
  const auto tinted = resources(world);
  check(updateTerrainWorld(world, "land", after.toJson(), *exclusions, error),
        error.c_str());
  checkAllResources(world, tinted);
  check(world.terrainOwners[0].surfaces.data() == ownership &&
            field(world)->exclusions.at(field(world)->index(2, 2)) == 1,
        "Exclusion publication missed native samples or rebuilt ownership");
  const auto undo = applyTerrainPatch(field(world), *exclusions->patch, false);
  check(updateTerrainWorld(world, "land", before.toJson(), undo, error),
        error.c_str());
  checkAllResources(world, tinted);
  check(field(world)->exclusions.at(field(world)->index(2, 2)) == 0,
        "Exclusion undo did not restore samples");
}

void testBiomeGrouping() {
  auto before = flatRecipe();
  auto red = before.biomes.at("default");
  red.color = {1, 0, 0, 1};
  before.biomes.emplace("red/rock", red);
  auto world = makeWorld(before);
  const auto original = resources(world);
  auto after = before;
  after.regions.push_back(
      {.biome = "red/rock", .center = {2, 2}, .radius = .4F, .falloff = 0});
  const auto isolated = updateTerrain(before, after, field(world));
  check(isolated && !isolated->invalidation.biomeSamples.empty(),
        "Biome sample did not change");
  std::string error;
  check(updateTerrainWorld(world, "land", after.toJson(), *isolated, error),
        error.c_str());
  checkAllResources(world, original);
  checkReference(world, after);

  before = after;
  after.regions.back().radius = 1.6F;
  const auto groups = updateTerrain(before, after, field(world));
  check(groups.has_value(), "Biome regroup update failed");
  check(updateTerrainWorld(world, "land", after.toJson(), *groups, error),
        error.c_str());
  check(entity(world, "land/__terrain/0_0/red%2frock")
                .component<MeshRendererComponent>()
                ->color.r == 1,
        "Biome regroup failed to add stable encoded group ID");
  for (const auto &[id, resource] : original)
    if (id != "land/__terrain/0_0/default")
      checkResource(world, id, resource);
  checkReference(world, after);

  const auto undo = applyTerrainPatch(field(world), *groups->patch, false);
  check(updateTerrainWorld(world, "land", before.toJson(), undo, error),
        error.c_str());
  checkReference(world, before);
}

void testRenamedGroupRetainsCollision() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  const auto original = resources(world);
  const auto other = field(world, "other");
  auto after = before;
  const auto settings = after.biomes.at("default");
  after.biomes.clear();
  after.biomes.emplace("new/biome", settings);
  after.defaultBiome = "new/biome";
  const auto update = updateTerrain(before, after, field(world));
  check(update.has_value(), "Biome rename update failed");
  std::string error;
  check(updateTerrainWorld(world, "land", after.toJson(), *update, error),
        error.c_str());
  for (const auto &[id, resource] : original) {
    if (!id.starts_with("land/__terrain/"))
      continue;
    const auto newId =
        id.substr(0, id.size() - std::string("default").size()) + "new%2fbiome";
    const auto *collider =
        entity(world, newId).component<ModelCollider3DComponent>();
    check(collider->inlineGeometry == resource.collider &&
              collider->inlineGeometry->revision == resource.collisionRevision,
          "Same triangles under a new biome group rebuilt collision");
  }
  checkUnrelated(world, original, other);
  checkReference(world, after);
}

void testCoalescedPatchAndStalePreparation() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  const auto firstRecipe = raised(before, {2, 2});
  const auto first = updateTerrain(before, firstRecipe, field(world));
  const auto secondRecipe = raised(firstRecipe, {9, 9});
  const auto second = updateTerrain(firstRecipe, secondRecipe, first->field);
  const auto merged = mergeTerrainPatches(*first->patch, *second->patch);
  const auto coalesced = applyTerrainPatch(field(world), *merged, true);
  std::string error;
  auto stale = prepareTerrainWorldUpdate(world, "land", firstRecipe.toJson(),
                                         *first, error);
  check(stale.has_value(), error.c_str());
  check(updateTerrainWorld(world, "land", secondRecipe.toJson(), coalesced,
                           error),
        error.c_str());
  checkReference(world, secondRecipe);
  const auto committed = resources(world);
  const auto committedField = field(world);
  check(!publishTerrainWorldUpdate(world, std::move(*stale), error) &&
            error.find("stale") != std::string::npos,
        "Older prepared result overwrote a coalesced update");
  checkAllResources(world, committed);
  check(field(world) == committedField,
        "Rejected stale preparation changed the field");
  check(
      !updateTerrainWorld(world, "land", firstRecipe.toJson(), *first, error) &&
          error.find("stale") != std::string::npos,
      "Overlapping stale patch was accepted before preparation");
  checkAllResources(world, committed);
  check(field(world) == committedField,
        "Rejected overlapping patch changed the field");
}

void testFailureAndCancellationAreAtomic() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  const auto original = resources(world);
  const auto originalField = field(world);
  const auto after = raised(before, {2, 2});
  const auto update = updateTerrain(before, after, originalField);
  std::string error;
  auto invalid = *update;
  auto broken = std::make_shared<HeightField>(*update->field);
  broken->heights.set(broken->index(10, 10),
                      std::numeric_limits<float>::quiet_NaN());
  invalid.field = broken;
  invalid.invalidation.heightSamples.include(10, 10);
  check(!updateTerrainWorld(world, "land", after.toJson(), invalid, error),
        "Nonfinite late chunk was published");
  checkAllResources(world, original);
  check(field(world) == originalField &&
            entity(world, "land").component<Terrain3DComponent>()->recipe ==
                before.toJson(),
        "Failed preparation mutated recipe/field");

  invalid = *update;
  invalid.field.reset();
  check(!updateTerrainWorld(world, "land", after.toJson(), invalid, error),
        "Empty terrain result was published");
  check(!updateTerrainWorld(world, "missing", after.toJson(), *update, error),
        "Missing owner was accepted");
  auto wrongRecipe = after.toJson();
  wrongRecipe["resolution"] = {0, 12};
  check(!updateTerrainWorld(world, "land", wrongRecipe, *update, error),
        "Invalid recipe was published");
  checkAllResources(world, original);

  std::stop_source stopped;
  stopped.request_stop();
  check(!prepareTerrainWorldUpdate(world, "land", after.toJson(), *update,
                                   error, stopped.get_token()),
        "Cancelled preparation completed");
  auto prepared =
      prepareTerrainWorldUpdate(world, "land", after.toJson(), *update, error);
  check(prepared.has_value(), error.c_str());
  check(!publishTerrainWorldUpdate(world, std::move(*prepared), error,
                                   stopped.get_token()),
        "Cancelled prepared result was published");
  checkAllResources(world, original);
  check(field(world) == originalField, "Cancellation replaced the field");
  check(publishTerrainWorldUpdate(world, std::move(*prepared), error),
        error.c_str());
  const auto committed = resources(world);
  check(!publishTerrainWorldUpdate(world, std::move(*prepared), error),
        "Consumed preparation was published twice");
  checkAllResources(world, committed);
}

void testPreviewBatchesAndMergedHistory() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  const auto original = resources(world);
  const auto firstRecipe = raised(before, {2, 2});
  const auto first = updateTerrain(before, firstRecipe, field(world));
  std::string error;
  check(updateTerrainWorld(world, "land", firstRecipe.toJson(), *first, error),
        error.c_str());
  const auto secondRecipe = raised(firstRecipe, {9, 9});
  const auto second = updateTerrain(firstRecipe, secondRecipe, field(world));
  const auto history = mergeTerrainPatches(*first->patch, *second->patch);

  // The editor publishes the next batch's delta while retaining a separate
  // cumulative patch for the single stroke's Undo/Redo command.
  check(
      updateTerrainWorld(world, "land", secondRecipe.toJson(), *second, error),
      error.c_str());
  checkReference(world, secondRecipe);
  const auto undo = applyTerrainPatch(field(world), *history, false);
  check(updateTerrainWorld(world, "land", before.toJson(), undo, error),
        error.c_str());
  checkReference(world, before);
  const auto redo = applyTerrainPatch(field(world), *history, true);
  check(updateTerrainWorld(world, "land", secondRecipe.toJson(), redo, error),
        error.c_str());
  checkReference(world, secondRecipe);
  for (const auto &[id, resource] : original)
    if (!id.starts_with("land/__terrain/"))
      checkResource(world, id, resource);
}

void testAuthoredIdConflictAndLayoutChange() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  auto after = before;
  after.biomes.emplace("red", before.biomes.at("default"));
  after.defaultBiome = "red";
  const auto update = updateTerrain(before, after, field(world));
  Entity authored;
  authored.id = "land/__terrain/0_0/red";
  authored.serializedComponents["authored"] = "do not replace";
  world.entities.push_back(std::move(authored));
  const auto original = resources(world);
  const auto originalField = field(world);
  std::string error;
  check(!updateTerrainWorld(world, "land", after.toJson(), *update, error) &&
            error.find("conflicts") != std::string::npos,
        "Biome regroup replaced an authored entity occupying the generated "
        "namespace");
  checkAllResources(world, original);
  check(entity(world, "land/__terrain/0_0/red")
                    .serializedComponents.at("authored") == "do not replace" &&
            field(world) == originalField,
        "ID conflict changed authored/native data");
  world.entities.pop_back();

  after = before;
  after.cellsX = after.cellsZ = 8;
  after.size = {8, 8};
  const auto resized = updateTerrain(before, after, field(world));
  check(resized && resized->invalidation.layoutChanged,
        "Grid resize was not classified explicitly");
  const auto other = field(world, "other");
  check(updateTerrainWorld(world, "land", after.toJson(), *resized, error),
        error.c_str());
  checkUnrelated(world, original, other);
  checkReference(world, after);
  const auto restored = applyTerrainPatch(field(world), *resized->patch, false);
  check(updateTerrainWorld(world, "land", before.toJson(), restored, error),
        error.c_str());
  checkUnrelated(world, original, other);
  checkReference(world, before);
  const auto replayed = applyTerrainPatch(field(world), *resized->patch, true);
  check(updateTerrainWorld(world, "land", after.toJson(), replayed, error),
        error.c_str());
  checkReference(world, after);
}

void testConflictAppearingAfterPreparation() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  auto after = before;
  after.biomes.emplace("red", before.biomes.at("default"));
  after.defaultBiome = "red";
  const auto update = updateTerrain(before, after, field(world));
  std::string error;
  auto prepared =
      prepareTerrainWorldUpdate(world, "land", after.toJson(), *update, error);
  check(prepared.has_value(), error.c_str());
  // A same-size collection edit must not evade the commit-time ID audit.
  entity(world, "building").id = "land/__terrain/0_0/red";
  const auto original = resources(world);
  const auto originalField = field(world);
  check(
      !publishTerrainWorldUpdate(world, std::move(*prepared), error) &&
          error.find("conflicts") != std::string::npos,
      "Publication ignored an authored ID conflict created during preparation");
  checkAllResources(world, original);
  check(field(world) == originalField, "Late ID conflict replaced the field");
}

void testChunkRepartitionAndVisibility() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  const auto original = resources(world);
  const auto other = field(world, "other");
  const auto *otherOwnership = world.terrainOwners[1].surfaces.data();
  auto after = before;
  after.chunkCells = 3;
  const auto update = updateTerrain(before, after, field(world));
  check(update && update->invalidation.layoutChanged,
        "Chunk repartition omitted layout invalidation");
  std::string error;
  auto prepared =
      prepareTerrainWorldUpdate(world, "land", after.toJson(), *update, error);
  check(prepared.has_value(), error.c_str());
  entity(world, "land").enabled = false;
  check(publishTerrainWorldUpdate(world, std::move(*prepared), error),
        error.c_str());
  checkUnrelated(world, original, other);
  check(world.terrainOwners[1].surfaces.data() == otherOwnership,
        "Repartition replaced another owner's native surface index");
  checkReference(world, after);
  for (const auto &surface : world.entities)
    if (terrainSurfaceOwner(surface) == "land")
      check(!surface.enabled,
            "Publication ignored the owner's current visibility");
  entity(world, "land").enabled = true;
  synchronizeTerrainVisibility(world);
  for (const auto &surface : world.entities)
    if (terrainSurfaceOwner(surface) == "land")
      check(surface.enabled,
            "Ownership index did not restore repartitioned surfaces");
}

void testSerializedTerrainPublication() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  auto &owner = entity(world, "land");
  const nlohmann::json source{{"recipe", before.toJson()},
                              {"preview_note", "preserve terrain metadata"}};
  owner.serializedComponents["Terrain3D"] = source.dump(2);
  owner.serializedComponents["Transform3D"] =
      "{\n  \"position\": [10, 3, 20]\n}";
  const auto *descriptor = scene_loading::findComponentDescriptor("Terrain3D");
  check(descriptor != nullptr, "Terrain descriptor is missing");
  const auto authored =
      scene_loading::makeAuthoredComponent(*descriptor, source.dump());
  owner.authoredComponents.push_back(authored);
  const auto original = resources(world);
  const auto after = raised(before, {3, 2});
  const auto update = updateTerrain(before, after, field(world));
  std::string error;
  auto prepared =
      prepareTerrainWorldUpdate(world, "land", after.toJson(), *update, error);
  check(prepared.has_value(), error.c_str());
  check(scene_loading::serializeComponent<Terrain3DComponent>(owner) == source,
        "Preparation mutated serialized terrain metadata");
  checkAllResources(world, original);

  // Staging only the Terrain3D key preserves independent metadata edits and
  // insertions that occur while its background preparation is in flight.
  const std::string transform = "{\n  \"position\": [31, 7, 2]\n}";
  owner.serializedComponents["Transform3D"] = transform;
  owner.serializedComponents["GameplayData"] = "{ \"active\": true }";
  const auto *transformData =
      owner.serializedComponents.at("Transform3D").data();
  check(publishTerrainWorldUpdate(world, std::move(*prepared), error),
        error.c_str());
  const auto &installed = entity(world, "land");
  const auto serialized =
      scene_loading::serializeComponent<Terrain3DComponent>(installed);
  check(serialized.at("recipe") == after.toJson() &&
            serialized.at("preview_note") == source.at("preview_note"),
        "Publication left stale serialized terrain or removed its other "
        "metadata");
  check(installed.serializedComponents.at("Transform3D") == transform &&
            installed.serializedComponents.at("Transform3D").data() ==
                transformData &&
            installed.serializedComponents.at("GameplayData") ==
                "{ \"active\": true }",
        "Publication overwrote another component's source");
  check(installed.authoredComponents.front() == authored &&
            authored->json() == source.dump(),
        "Publication replaced immutable authored-component source snapshots");
  checkReference(world, after);

  const auto undo = applyTerrainPatch(field(world), *update->patch, false);
  check(updateTerrainWorld(world, "land", before.toJson(), undo, error),
        error.c_str());
  checkReference(world, before);
  check(scene_loading::serializeComponent<Terrain3DComponent>(
            entity(world, "land"))
                .at("preview_note") == source.at("preview_note"),
        "Undo removed serialized terrain metadata");
}

void testSerializedTerrainStalenessAndCancellation() {
  const auto before = flatRecipe();
  for (const bool hadSerializedTerrain : {false, true}) {
    auto world = makeWorld(before);
    auto &owner = entity(world, "land");
    if (hadSerializedTerrain)
      owner.serializedComponents["Terrain3D"] =
          nlohmann::json{{"recipe", before.toJson()}}.dump();
    const auto original = resources(world);
    const auto originalField = field(world);
    const auto after = raised(before, {2, 2});
    const auto update = updateTerrain(before, after, originalField);
    std::string error;
    auto prepared = prepareTerrainWorldUpdate(world, "land", after.toJson(),
                                              *update, error);
    check(prepared.has_value(), error.c_str());
    const auto source = owner.serializedComponents;
    std::stop_source stopped;
    stopped.request_stop();
    check(!publishTerrainWorldUpdate(world, std::move(*prepared), error,
                                     stopped.get_token()),
          "Cancellation published serialized metadata");
    check(owner.serializedComponents == source && field(world) == originalField,
          "Cancelled publication changed serialized/native terrain");
    checkAllResources(world, original);

    const std::string concurrent =
        nlohmann::json{{"recipe", before.toJson()},
                       {"preview_note", "changed during preparation"}}
            .dump();
    owner.serializedComponents["Terrain3D"] = concurrent;
    check(!publishTerrainWorldUpdate(world, std::move(*prepared), error) &&
              error.find("serialized Terrain3D") != std::string::npos,
          "Publication accepted stale serialized terrain metadata");
    check(owner.serializedComponents.at("Terrain3D") == concurrent &&
              field(world) == originalField &&
              owner.component<Terrain3DComponent>()->recipe == before.toJson(),
          "Rejected metadata race changed serialized/native terrain");
    checkAllResources(world, original);
  }
}

void testGlobalHistoryAfterLocalUndo() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  const auto original = resources(world);
  const auto other = field(world, "other");
  auto regeneratedRecipe = before;
  regeneratedRecipe.landforms.at("default").baseHeight = 5;
  const auto regenerated =
      updateTerrain(before, regeneratedRecipe, field(world));
  check(regenerated && regenerated->invalidation.fullGeneration,
        "Base change did not produce global history");
  std::string error;
  check(updateTerrainWorld(world, "land", regeneratedRecipe.toJson(),
                           *regenerated, error),
        error.c_str());
  const auto brushRecipe = raised(regeneratedRecipe, {2, 2});
  const auto brush =
      updateTerrain(regeneratedRecipe, brushRecipe, field(world));
  check(brush && !brush->invalidation.fullGeneration,
        "Brush did not produce local history");
  check(updateTerrainWorld(world, "land", brushRecipe.toJson(), *brush, error),
        error.c_str());
  const auto undoBrush = applyTerrainPatch(field(world), *brush->patch, false);
  check(undoBrush.field != regenerated->field &&
            terrainFieldsEqual(*undoBrush.field, *regenerated->field),
        "Local undo did not exercise a reconstructed global snapshot");
  check(updateTerrainWorld(world, "land", regeneratedRecipe.toJson(), undoBrush,
                           error),
        error.c_str());
  checkReference(world, regeneratedRecipe);

  const auto undoGlobal =
      applyTerrainPatch(field(world), *regenerated->patch, false);
  auto reconstructedUndo = undoGlobal;
  reconstructedUndo.field =
      std::make_shared<const HeightField>(*undoGlobal.field);
  check(updateTerrainWorld(world, "land", before.toJson(), reconstructedUndo,
                           error),
        error.c_str());
  checkReference(world, before);
  checkUnrelated(world, original, other);
  const auto redoGlobal =
      applyTerrainPatch(field(world), *regenerated->patch, true);
  check(updateTerrainWorld(world, "land", regeneratedRecipe.toJson(),
                           redoGlobal, error),
        error.c_str());
  checkReference(world, regeneratedRecipe);

  const auto committed = resources(world);
  const auto committedField = field(world);
  auto wrongDestination = undoGlobal;
  auto corrupted = std::make_shared<HeightField>(*undoGlobal.field);
  const auto sample = corrupted->index(1, 1);
  corrupted->heights.set(sample, corrupted->heights.at(sample) + 1);
  wrongDestination.field = std::move(corrupted);
  check(!updateTerrainWorld(world, "land", before.toJson(), wrongDestination,
                            error) &&
            error.find("stale") != std::string::npos,
        "Global history accepted a snapshot with different sample values");
  checkAllResources(world, committed);
  check(field(world) == committedField,
        "Rejected global result changed the retained field");
  check(updateTerrainWorld(world, "land", brushRecipe.toJson(), *brush, error),
        error.c_str());
  const auto brushed = resources(world);
  const auto brushedField = field(world);
  check(
      !updateTerrainWorld(world, "land", before.toJson(), undoGlobal, error) &&
          error.find("stale") != std::string::npos,
      "Global history accepted a source with an un-undone brush");
  checkAllResources(world, brushed);
  check(field(world) == brushedField &&
            scene_loading::serializeComponent<Terrain3DComponent>(
                entity(world, "land"))
                    .at("recipe") == brushRecipe.toJson(),
        "Rejected global source changed native/serialized terrain");
}
void testSharedAssetPublicationAndRollback() {
  const auto before = flatRecipe();
  auto world = makeWorld(before);
  for (const auto &id : {"land", "other"})
    entity(world, id).component<Terrain3DComponent>()->asset = "asset://shared";
  const auto original = resources(world);
  const auto originalField = field(world);
  // Cooked assets retain their field and chunk layout without a source recipe.
  for (const auto &id : {"land", "other"})
    entity(world, id).component<Terrain3DComponent>()->recipe = nullptr;
  auto after = before;
  after.edits.push_back({.kind = TerrainEditKind::Raise,
                         .center = {1, 1},
                         .radius = 1,
                         .amount = 2});
  auto update = updateTerrain(before, after, originalField);
  check(update.has_value(), "Shared terrain stroke failed to generate");
  std::string error;
  check(!updateTerrainAssetWorld(world, "", after.toJson(), *update, error),
        "Empty asset ID was accepted by shared publication");
  checkAllResources(world, original);
  error.clear();
  check(updateTerrainAssetWorld(world, "asset://shared", after.toJson(),
                                *update, error),
        error.c_str());
  check(field(world) == update->field && field(world, "other") == update->field,
        "Shared placements did not receive one immutable updated field");
  for (const auto &id : {"land", "other"}) {
    const auto source = scene_loading::serializeComponent<Terrain3DComponent>(
        entity(world, id));
    check(source.at("asset") == "asset://shared" && !source.contains("recipe"),
          "Shared terrain publication embedded a recipe in scene data");
    checkResource(world, std::string(id) + "/__terrain/8_8/default",
                  original.at(std::string(id) + "/__terrain/8_8/default"));
    check(entity(world, id).component<Transform3DComponent>()->position.x == 10,
          "Shared terrain publication changed a placement transform");
  }
  checkResource(world, "building", original.at("building"));

  const auto undo = applyTerrainPatch(field(world), *update->patch, false);
  check(updateTerrainAssetWorld(world, "asset://shared", before.toJson(), undo,
                                error),
        error.c_str());
  const auto redo = applyTerrainPatch(field(world), *update->patch, true);
  check(updateTerrainAssetWorld(world, "asset://shared", after.toJson(), redo,
                                error),
        error.c_str());

  // A stale second placement must reject the batch before the first changes.
  auto stale = std::make_shared<HeightField>(*field(world, "other"));
  const auto sample = stale->index(1, 1);
  stale->heights.set(sample, stale->heights.at(sample) + 10);
  entity(world, "other").component<Terrain3DComponent>()->generated = stale;
  const auto committed = resources(world);
  const auto firstField = field(world);
  error.clear();
  check(!updateTerrainAssetWorld(world, "asset://shared", before.toJson(), undo,
                                 error) &&
            !error.empty(),
        "Shared batch accepted a stale second placement");
  check(field(world) == firstField && field(world, "other") == stale,
        "Failed shared publication partially changed terrain owners");
  checkAllResources(world, committed);
}

void testSharedAssetScatterReconciliation() {
  const auto recipe = flatRecipe();
  auto world = makeWorld(recipe);
  for (const auto &id : {"land", "other"})
    entity(world, id).component<Terrain3DComponent>()->asset = "asset://shared";

  const auto publish = [&](std::shared_ptr<const HeightField> next) {
    auto patch = std::make_shared<TerrainPatch>();
    patch->fullBefore = field(world);
    patch->fullAfter = next;
    patch->invalidation.fullGeneration = true;
    TerrainUpdate update{.field = std::move(next),
                         .patch = patch,
                         .invalidation = patch->invalidation};
    std::string error;
    check(updateTerrainAssetWorld(world, "asset://shared", recipe.toJson(),
                                  update, error),
          error.c_str());
  };
  auto scattered = std::make_shared<HeightField>(*field(world));
  scattered->paletteId = "asset://palette";
  scattered->scatterPlacements.push_back({.role = TerrainPaletteRole::Tree,
                                          .asset = "asset://tree",
                                          .cell = 1,
                                          .position = {1, 1, 1}});
  publish(scattered);
  const auto scatterId = [](std::string_view owner) {
    return terrainScatterInstanceId(owner, "asset://palette",
                                    TerrainPaletteRole::Tree, 1);
  };
  for (const auto &owner : {"land", "other"}) {
    auto &instance = entity(world, scatterId(owner));
    check(instance.component<Transform3DComponent>()->parent == owner,
          "Scattered instance lost its terrain placement parent");
    instance.name = "Keep live instance state";
  }

  auto moved = std::make_shared<HeightField>(*field(world));
  moved->scatterPlacements.front().position.y = 4;
  publish(moved);
  for (const auto &owner : {"land", "other"}) {
    const auto &instance = entity(world, scatterId(owner));
    check(instance.component<Transform3DComponent>()->position.y == 4 &&
              instance.name == "Keep live instance state",
          "Shared publication did not move scenery while retaining state");
  }

  // A missing prefab driver must not delete the existing direct instances.
  auto unresolved = std::make_shared<HeightField>(*field(world));
  unresolved->paletteId = "asset://different-palette";
  unresolved->scatterPlacements.front().prefab = "prefab://tree";
  auto rejectedPatch = std::make_shared<TerrainPatch>();
  rejectedPatch->fullBefore = field(world);
  rejectedPatch->fullAfter = unresolved;
  rejectedPatch->invalidation.fullGeneration = true;
  TerrainUpdate rejected{.field = unresolved,
                         .patch = rejectedPatch,
                         .invalidation = rejectedPatch->invalidation};
  const auto retained = field(world);
  std::string error;
  check(!updateTerrainAssetWorld(world, "asset://shared", recipe.toJson(),
                                 rejected, error),
        "Unresolved scatter prefab was silently accepted");
  check(field(world) == retained &&
            entity(world, scatterId("land")).name == "Keep live instance state",
        "Failed scatter publication mutated terrain or live instances");

  auto excluded = std::make_shared<HeightField>(*field(world));
  excluded->scatterPlacements.clear();
  publish(excluded);
  for (const auto &instance : world.entities)
    check(instance.id != scatterId("land") && instance.id != scatterId("other"),
          "Excluded scenery survived a shared terrain update");
}
} // namespace

int main() {
  try {
    testLocalHeightAndNormalHalo();
    testSharedCornerAndUndoRedo();
    testTintAndExclusion();
    testBiomeGrouping();
    testRenamedGroupRetainsCollision();
    testCoalescedPatchAndStalePreparation();
    testFailureAndCancellationAreAtomic();
    testPreviewBatchesAndMergedHistory();
    testAuthoredIdConflictAndLayoutChange();
    testConflictAppearingAfterPreparation();
    testChunkRepartitionAndVisibility();
    testSerializedTerrainPublication();
    testSerializedTerrainStalenessAndCancellation();
    testGlobalHistoryAfterLocalUndo();
    testSharedAssetPublicationAndRollback();
    testSharedAssetScatterReconciliation();
  } catch (const std::exception &exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }
  return 0;
}
