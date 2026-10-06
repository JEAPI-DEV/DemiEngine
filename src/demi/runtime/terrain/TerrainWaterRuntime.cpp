#include "demi/runtime/terrain/TerrainWaterRuntime.h"

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainWaterQueries.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace demi::runtime {
namespace {
bool finite(Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}
bool enabledHierarchy(World &world, const Entity &owner) {
  const auto *entity = &owner;
  for (std::size_t depth = 0; depth < world.entities.size(); ++depth) {
    if (!entity->enabled)
      return false;
    const auto *transform = entity->component<Transform3DComponent>();
    if (!transform || transform->parent.empty())
      return true;
    entity = world.terrainEntityLookup.find(world.entities, transform->parent);
    if (!entity)
      return false;
  }
  return false;
}
std::string waterKind(TerrainWaterBody kind) {
  switch (kind) {
  case TerrainWaterBody::Lake:
    return "lake";
  case TerrainWaterBody::Ocean:
    return "ocean";
  case TerrainWaterBody::River:
    return "river";
  }
  throw std::invalid_argument("Unknown terrain water kind");
}
float distance(Vec3 a, Vec3 b) {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
} // namespace

struct TerrainWaterRuntime::State {
  struct CachedField {
    std::weak_ptr<const HeightField> field;
    TerrainWaterQueryContext context;
    bool active = false;
  };
  std::unordered_map<const HeightField *, CachedField> fields;
};

TerrainWaterRuntime::TerrainWaterRuntime()
    : state_(std::make_unique<State>()) {}
TerrainWaterRuntime::~TerrainWaterRuntime() = default;
std::size_t TerrainWaterRuntime::cachedFieldCount() const {
  return state_->fields.size();
}

std::optional<TerrainWaterSample>
TerrainWaterRuntime::sample(World &world, Vec3 position,
                            std::string_view terrainId) {
  if (!finite(position))
    throw std::invalid_argument(
        "Water position must contain finite XYZ values");
  std::erase_if(state_->fields,
                [](const auto &entry) { return entry.second.field.expired(); });
  for (auto &[field, cached] : state_->fields)
    cached.active = false;
  std::optional<TerrainWaterSample> selected;
  float selectedDistance = 0;
  for (const auto &ownership : world.terrainOwners) {
    const auto *owner =
        world.terrainEntityLookup.find(world.entities, ownership.id);
    if (!owner)
      continue;
    const auto *terrain = owner->component<Terrain3DComponent>();
    if (!terrain || !terrain->generated || !terrain->generated->graphArtifacts)
      continue;
    const auto &field = terrain->generated;
    if (const auto retained = state_->fields.find(field.get());
        retained != state_->fields.end())
      retained->second.active = true;
    if ((!terrainId.empty() && ownership.id != terrainId) ||
        !enabledHierarchy(world, *owner))
      continue;
    const auto &artifacts = *field->graphArtifacts;
    if (!artifacts.waterResult)
      continue;
    const auto transform = resolveWorldTransform3D(world, *owner);
    if (!transform || !finite(transform->position) ||
        !finite(transform->rotation) || !finite(transform->scale) ||
        transform->scale.x == 0 || transform->scale.y == 0 ||
        transform->scale.z == 0)
      continue;
    const auto local = inverseTransformPoint3D(*transform, position);
    if (!finite(local) || local.x < 0 || local.z < 0 ||
        local.x > field->size.x || local.z > field->size.y)
      continue;
    auto cached = state_->fields.find(field.get());
    if (cached == state_->fields.end()) {
      auto context = TerrainWaterQueryContext::build(
          artifacts.water, *artifacts.waterResult, nullptr, field->cellsX,
          field->cellsZ, field->size);
      if (!context)
        throw std::invalid_argument(
            "Prepared water queries are invalid on terrain " + owner->id);
      cached =
          state_->fields
              .emplace(field.get(),
                       State::CachedField{field, std::move(*context), true})
              .first;
    }
    const auto sample = cached->second.context.sample(local);
    if (!sample || !sample->submerged)
      continue;
    const auto body = std::ranges::find(artifacts.water.bodies, sample->bodyId,
                                        &TerrainWaterBodySpec::id);
    if (body == artifacts.water.bodies.end())
      throw std::invalid_argument(
          "Prepared water body is missing from its authored table");
    TerrainWaterSample result;
    result.terrainId = owner->id;
    result.bodyId = sample->bodyId;
    result.sceneId =
        owner->sceneOwner.empty() ? world.activeSceneId : owner->sceneOwner;
    result.kind = waterKind(body->kind);
    result.surface =
        transformPoint3D(*transform, {local.x, sample->surfaceHeight, local.z});
    result.normal = transformDirection3D(
        *transform, {0, transform->scale.y < 0 ? -1.F : 1.F, 0});
    result.depth = sample->depth * std::abs(transform->scale.y);
    result.underwater = sample->underwater;
    if (!finite(result.surface) || !finite(result.normal) ||
        !std::isfinite(result.depth))
      continue;
    const float candidateDistance = distance(position, result.surface);
    if (!selected || (result.underwater && !selected->underwater) ||
        (result.underwater == selected->underwater &&
         candidateDistance < selectedDistance)) {
      selectedDistance = candidateDistance;
      selected = std::move(result);
    }
  }
  std::erase_if(state_->fields,
                [](const auto &entry) { return !entry.second.active; });
  return selected;
}
} // namespace demi::runtime
