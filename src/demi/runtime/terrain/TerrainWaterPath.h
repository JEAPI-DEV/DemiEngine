#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <nlohmann/json_fwd.hpp>
#include <span>
#include <vector>

namespace demi::runtime {
// Authoring-only settings. Prepared bodies retain just the sampled polyline.
void validateTerrainWaterPathSettings(const nlohmann::json &parameters);
[[nodiscard]] std::vector<Vec3>
sampleTerrainWaterPath(std::span<const Vec3> controls,
                       const nlohmann::json &parameters);
} // namespace demi::runtime
