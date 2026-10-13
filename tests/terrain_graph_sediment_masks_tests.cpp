#include "demi/runtime/terrain/TerrainGraphSedimentMasks.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace demi::runtime;
using namespace demi::runtime::terrain_graph_detail;

namespace {
GraphField sourceField() {
  auto field = std::make_shared<HeightField>();
  field->size = {2, 1};
  field->cellsX = 2;
  field->cellsZ = 1;
  field->heights.resize(6);
  return field;
}

TerrainSamples<float> balanceSamples(std::initializer_list<float> values) {
  TerrainSamples<float> samples;
  samples.resize(values.size());
  std::size_t index = 0;
  for (const float value : values) {
    samples.set(index++, value);
  }
  return samples;
}

void signsNormalizeIndependentlyAndRetainRawBalance() {
  const auto source = sourceField();
  const auto balance = balanceSamples({-8, -2, 0, 1, 2, 4});
  const auto result = prepareTerrainGraphSedimentMasks(source, balance);
  assert(result);
  assert(*result->balance == balance);
  assert(result->deposition->source == source);
  assert(result->cut->source == source);
  const float deposition[] = {0, 0, 0, .25F, .5F, 1};
  const float cut[] = {1, .25F, 0, 0, 0, 0};
  for (std::size_t sample = 0; sample < balance.size(); ++sample) {
    assert(result->deposition->samples[sample] == deposition[sample]);
    assert(result->cut->samples[sample] == cut[sample]);
  }
  auto changed = balance;
  changed.set(0, 100);
  assert((*result->balance)[0] == -8);
}

void absentTransportProducesZeroMask() {
  for (const float sign : {-1.0F, 0.0F, 1.0F}) {
    const auto result = prepareTerrainGraphSedimentMasks(
        sourceField(), balanceSamples({sign, sign, sign, sign, sign, sign}));
    assert(result);
    for (std::size_t sample = 0; sample < 6; ++sample) {
      assert(result->deposition->samples[sample] == (sign > 0 ? 1 : 0));
      assert(result->cut->samples[sample] == (sign < 0 ? 1 : 0));
    }
  }
}

void invalidBalanceAndCancellationAreRejected() {
  const auto source = sourceField();
  for (const auto &balance :
       {balanceSamples({0}),
        balanceSamples({0, 0, 0, 0, 0, std::numeric_limits<float>::infinity()}),
        balanceSamples(
            {0, 0, 0, 0, 0, std::numeric_limits<float>::quiet_NaN()})}) {
    bool rejected = false;
    try {
      (void)prepareTerrainGraphSedimentMasks(source, balance);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  }
  std::stop_source cancelled;
  cancelled.request_stop();
  assert(!prepareTerrainGraphSedimentMasks(
      source, balanceSamples({0, 0, 0, 0, 0, 0}), cancelled.get_token()));
}
} // namespace

int main() {
  signsNormalizeIndependentlyAndRetainRawBalance();
  absentTransportProducesZeroMask();
  invalidBalanceAndCancellationAreRejected();
  std::cout << "Terrain graph sediment masks passed\n";
}
