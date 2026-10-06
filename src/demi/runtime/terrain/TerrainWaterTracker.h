#pragma once

#include "demi/runtime/terrain/TerrainWaterSample.h"
#include <optional>
#include <vector>

namespace demi::runtime {
struct TerrainWaterEvent {
  std::string phase;
  TerrainWaterSample sample;
};

// Pure transition state. Callers choose sampling cadence and own mechanics.
class TerrainWaterTracker {
public:
  std::vector<TerrainWaterEvent>
  update(std::optional<TerrainWaterSample> sample);
  const std::optional<TerrainWaterSample> &current() const { return current_; }
  void reset() { current_.reset(); }

private:
  std::optional<TerrainWaterSample> current_;
};
} // namespace demi::runtime
