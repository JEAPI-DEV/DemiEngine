#include "demi/runtime/isometric/IsoGridApi.h"

#include "demi/runtime/isometric/IsoGridMath.h"
#include "demi/runtime/isometric/IsoWorldQueries.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

namespace demi::runtime::isometric {

void IsoGridApi::attach(World *world) { world_ = world; }

bool IsoGridApi::available() const {
  return world_ != nullptr && gridDefinition(*world_).has_value();
}

std::optional<Vec2> IsoGridApi::tileToWorld(const Vec2 tile,
                                            const float elevation) const {
  const auto grid = world_ != nullptr ? gridDefinition(*world_) : std::nullopt;
  return grid ? std::optional<Vec2>{isometric::tileToWorld(*grid, tile,
                                                           elevation)}
              : std::nullopt;
}

std::optional<Vec2> IsoGridApi::worldToTile(const Vec2 world) const {
  const auto grid = world_ != nullptr ? gridDefinition(*world_) : std::nullopt;
  return grid ? std::optional<Vec2>{worldToTileFractional(*grid, world)}
              : std::nullopt;
}

std::optional<Vec2> IsoGridApi::entityTile(const std::string &entityId) const {
  const Entity *entity =
      world_ != nullptr ? findEntity(*world_, entityId) : nullptr;
  const auto *transform =
      entity != nullptr ? entity->component<IsoTransformComponent>() : nullptr;
  return transform != nullptr ? std::optional<Vec2>{transform->tile}
                              : std::nullopt;
}

bool IsoGridApi::setEntityTile(const std::string &entityId, const Vec2 tile,
                               const float elevation) {
  Entity *entity = world_ != nullptr ? findEntity(*world_, entityId) : nullptr;
  auto *transform =
      entity != nullptr ? entity->component<IsoTransformComponent>() : nullptr;
  if (transform == nullptr)
    return false;
  transform->tile = tile;
  transform->height = elevation;
  return true;
}

std::optional<std::string> IsoGridApi::entityAt(const GridCell cell) const {
  return world_ != nullptr ? isometric::entityAt(*world_, cell) : std::nullopt;
}

PlacementResult IsoGridApi::canPlace(const GridCell origin,
                                     const GridFootprint footprint) const {
  return world_ != nullptr
             ? queryPlacement(*world_, origin, footprint)
             : PlacementResult{.code = "GRID_UNAVAILABLE",
                               .message = "The world has no isometric grid."};
}

PathResult
IsoGridApi::path(const GridCell start, const GridCell goal,
                 const std::optional<PlacementRequest> preview) const {
  if (world_ == nullptr)
    return {.diagnostic = "GRID_UNAVAILABLE", .cells = {}};
  GridOccupancy occupancy = buildOccupancy(*world_);
  if (preview &&
      !occupancy.occupy(preview->origin, preview->footprint, "__query_preview"))
    return {.diagnostic = "PATH_PREVIEW_OCCUPIED", .cells = {}};
  return findPath(occupancy, start, goal);
}

void IsoGridApi::setPreview(const GridCell origin,
                            const GridFootprint footprint, const bool valid) {
  if (world_ == nullptr)
    return;
  world_->placementPreview = {
      .visible = true,
      .valid = valid,
      .tile = {.x = static_cast<float>(origin.x),
               .y = static_cast<float>(origin.y)},
      .footprint = {.x = static_cast<float>(footprint.width),
                    .y = static_cast<float>(footprint.height)}};
}

void IsoGridApi::clearPreview() {
  if (world_ != nullptr)
    world_->placementPreview = {};
}

} // namespace demi::runtime::isometric
