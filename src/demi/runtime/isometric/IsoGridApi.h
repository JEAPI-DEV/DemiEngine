#pragma once

#include "demi/runtime/isometric/GridPathfinder.h"
#include "demi/runtime/isometric/PlacementRules.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <optional>
#include <string>

namespace demi::runtime {
struct World;
}

namespace demi::runtime::isometric {

class IsoGridApi {
public:
  void attach(World *world);
  [[nodiscard]] bool available() const;
  [[nodiscard]] std::optional<Vec2> tileToWorld(Vec2 tile,
                                                float elevation = 0.0F) const;
  [[nodiscard]] std::optional<Vec2> worldToTile(Vec2 world) const;
  [[nodiscard]] std::optional<Vec2>
  entityTile(const std::string &entityId) const;
  [[nodiscard]] bool setEntityTile(const std::string &entityId, Vec2 tile,
                                   float elevation = 0.0F);
  [[nodiscard]] std::optional<std::string> entityAt(GridCell cell) const;
  [[nodiscard]] PlacementResult canPlace(GridCell origin,
                                         GridFootprint footprint) const;
  [[nodiscard]] PathResult
  path(GridCell start, GridCell goal,
       std::optional<PlacementRequest> preview = std::nullopt) const;
  void setPreview(GridCell origin, GridFootprint footprint, bool valid);
  void clearPreview();

private:
  World *world_ = nullptr;
};

} // namespace demi::runtime::isometric
