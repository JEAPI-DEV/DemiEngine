#pragma once

#include "demi/runtime/terrain/TerrainWaterSample.h"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {
struct World;

// Scene-facing sampling owns prepared-context caching, never terrain
// generation. Shared fields use one context across placements; transforms
// remain live.
class TerrainWaterRuntime {
public:
  TerrainWaterRuntime();
  ~TerrainWaterRuntime();
  TerrainWaterRuntime(const TerrainWaterRuntime &) = delete;
  TerrainWaterRuntime &operator=(const TerrainWaterRuntime &) = delete;
  std::optional<TerrainWaterSample> sample(World &world, Vec3 position,
                                           std::string_view terrainId = {});
  std::size_t cachedFieldCount() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace demi::runtime
