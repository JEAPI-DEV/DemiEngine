#include "demi/runtime/terrain/TerrainSurfaceSediment.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace demi::runtime {
namespace {
void validate(const HeightField &field) {
  if (field.surfaceSedimentBalance &&
      field.surfaceSedimentBalance->size() != field.heights.size()) {
    throw std::invalid_argument(
        "Surface sediment balance does not match its field");
  }
}
float finiteBalance(double value) {
  if (!std::isfinite(value) ||
      std::abs(value) > std::numeric_limits<float>::max()) {
    throw std::invalid_argument(
        "Surface sediment balance must fit finite samples");
  }
  return float(value);
}
double balanceAt(const HeightField &field, std::size_t sample) {
  return field.surfaceSedimentBalance ? (*field.surfaceSedimentBalance)[sample]
                                      : 0;
}
} // namespace

std::optional<std::shared_ptr<const TerrainSamples<float>>>
blendTerrainSurfaceSediment(const HeightField &a, const HeightField &b,
                            const TerrainSamples<float> *mask, double amount,
                            std::stop_token stop) {
  validate(a);
  validate(b);
  if (a.heights.size() != b.heights.size() ||
      (mask && mask->size() != a.heights.size()) || !std::isfinite(amount) ||
      amount < 0 || amount > 1) {
    throw std::invalid_argument("Surface sediment blend inputs do not match");
  }
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  if (!a.surfaceSedimentBalance && !b.surfaceSedimentBalance) {
    return std::shared_ptr<const TerrainSamples<float>>{};
  }
  auto result = std::make_shared<TerrainSamples<float>>();
  result->resize(a.heights.size());
  for (std::size_t sample = 0; sample < result->size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested()) {
      return std::nullopt;
    }
    const double weight = amount * (mask ? (*mask)[sample] : 1);
    if (!std::isfinite(weight) || weight < 0 || weight > 1) {
      throw std::invalid_argument(
          "Surface sediment blend mask must be normalized");
    }
    result->set(sample, finiteBalance((1 - weight) * balanceAt(a, sample) +
                                      weight * balanceAt(b, sample)));
  }
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  return std::shared_ptr<const TerrainSamples<float>>(std::move(result));
}

std::optional<std::shared_ptr<const TerrainSamples<float>>>
accumulateTerrainSurfaceSediment(const HeightField &source,
                                 const TerrainSamples<float> &delta,
                                 std::stop_token stop) {
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  validate(source);
  if (delta.size() != source.heights.size()) {
    throw std::invalid_argument(
        "Surface sediment transport does not match its field");
  }
  auto result = std::make_shared<TerrainSamples<float>>();
  result->resize(delta.size());
  for (std::size_t sample = 0; sample < result->size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested()) {
      return std::nullopt;
    }
    result->set(sample,
                finiteBalance(balanceAt(source, sample) + delta[sample]));
  }
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  return std::shared_ptr<const TerrainSamples<float>>(std::move(result));
}
} // namespace demi::runtime
