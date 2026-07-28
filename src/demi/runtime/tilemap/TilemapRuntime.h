#pragma once

#include "demi/runtime/tilemap/TilemapAsset.h"

#include <optional>
#include <string>

namespace demi {
struct AssetRegistry;
}

namespace demi::runtime {
struct World;
namespace navigation {
class NavigationGrid2D;
}

class TilemapRuntime {
public:
  void attach(World *world, const AssetRegistry *assets,
              navigation::NavigationGrid2D *navigation = nullptr);
  [[nodiscard]] std::optional<int> tile(const std::string &entityId,
                                        const std::string &layer, int column,
                                        int row) const;
  [[nodiscard]] bool setTile(const std::string &entityId,
                             const std::string &layer, int column, int row,
                             int tile);
  [[nodiscard]] bool clearOverrides(const std::string &entityId);
  [[nodiscard]] bool bakeNavigation(const std::string &entityId);
  [[nodiscard]] std::vector<TilemapObject2D>
  objects(const std::string &entityId, const std::string &layer) const;

private:
  [[nodiscard]] bool refreshNavigation();

  World *world_ = nullptr;
  const AssetRegistry *assets_ = nullptr;
  navigation::NavigationGrid2D *navigation_ = nullptr;
  std::string navigationEntityId_;
};

} // namespace demi::runtime
