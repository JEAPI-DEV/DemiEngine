#include "demi/runtime/terrain/TerrainGraphSedimentMasks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace demi::runtime::terrain_graph_detail {

std::optional<TerrainGraphSedimentMasks> prepareTerrainGraphSedimentMasks(
    GraphField source, TerrainSamples<float> balance, std::stop_token stop) {
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  if (!source || balance.size() != source->heights.size()) {
    throw std::invalid_argument(
        "Sediment balance does not match its source field");
  }
  float maximumDeposition = 0;
  float maximumCut = 0;
  for (std::size_t sample = 0; sample < balance.size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested()) {
      return std::nullopt;
    }
    const float transport = balance[sample];
    if (!std::isfinite(transport)) {
      throw std::invalid_argument(
          "Sediment balance must contain finite values");
    }
    maximumDeposition = std::max(maximumDeposition, transport);
    maximumCut = std::max(maximumCut, -transport);
  }
  auto deposition = std::make_shared<GraphMask>();
  auto cut = std::make_shared<GraphMask>();
  deposition->source = source;
  cut->source = std::move(source);
  deposition->samples.resize(balance.size());
  cut->samples.resize(balance.size());
  for (std::size_t sample = 0; sample < balance.size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested()) {
      return std::nullopt;
    }
    const float transport = balance[sample];
    const float deposited = maximumDeposition > 0
                                ? std::max(0.0F, transport) / maximumDeposition
                                : 0.0F;
    const float removed =
        maximumCut > 0 ? std::max(0.0F, -transport) / maximumCut : 0.0F;
    deposition->samples.set(sample, deposited);
    cut->samples.set(sample, removed);
  }
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  return TerrainGraphSedimentMasks{
      std::make_shared<const TerrainSamples<float>>(std::move(balance)),
      std::move(deposition), std::move(cut)};
}

} // namespace demi::runtime::terrain_graph_detail
