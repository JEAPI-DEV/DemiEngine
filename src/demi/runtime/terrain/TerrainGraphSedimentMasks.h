#pragma once

#include "demi/runtime/terrain/TerrainGraphNodeEvaluation.h"

namespace demi::runtime::terrain_graph_detail {

struct TerrainGraphSedimentMasks {
  std::shared_ptr<const TerrainSamples<float>> balance;
  std::shared_ptr<const GraphMask> deposition;
  std::shared_ptr<const GraphMask> cut;
};

// Retains raw signed transport and independently normalizes deposited/cut
// magnitudes. A side without transport has an all-zero mask. Source is the
// erosion output field, so downstream graph consumers share its grid identity.
[[nodiscard]] std::optional<TerrainGraphSedimentMasks>
prepareTerrainGraphSedimentMasks(GraphField source,
                                 TerrainSamples<float> balance,
                                 std::stop_token stop = {});

} // namespace demi::runtime::terrain_graph_detail
