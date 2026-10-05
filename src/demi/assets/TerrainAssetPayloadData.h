#pragma once

#include "demi/runtime/terrain/TerrainGraphExecutor.h"

#include <nlohmann/json.hpp>

namespace demi::assets::terrain_payload {

// Structured derived data accompanying the packed heightfield. Reading it is
// deliberately strict: a damaged cache must never silently lose placements or
// water while continuing to render a plausible ground surface.
nlohmann::json encodeDerived(const runtime::HeightField &field);
void decodeDerived(const nlohmann::json &document, runtime::HeightField &field);

} // namespace demi::assets::terrain_payload
