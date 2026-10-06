#include "demi/runtime/terrain/TerrainWaterTracker.h"

namespace demi::runtime {
std::vector<TerrainWaterEvent>
TerrainWaterTracker::update(std::optional<TerrainWaterSample> sample) {
  if (sample && !sample->underwater)
    sample.reset();
  const bool sameBody = current_ && sample &&
                        current_->terrainId == sample->terrainId &&
                        current_->bodyId == sample->bodyId &&
                        current_->sceneId == sample->sceneId;
  std::vector<TerrainWaterEvent> events;
  if (current_ && !sameBody)
    events.push_back({"exit", *current_});
  if (sample)
    events.push_back({sameBody ? "stay" : "enter", *sample});
  current_ = std::move(sample);
  return events;
}
} // namespace demi::runtime
