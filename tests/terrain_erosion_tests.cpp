#include "demi/runtime/terrain/TerrainErosion.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace demi::runtime;

namespace {

// Fields are built to a named shape so each test asserts on the surface it
// asked for rather than on generated noise.
template <class Shape>
HeightField makeField(int cellsX, int cellsZ, Shape shape) {
  HeightField field;
  field.size = {float(cellsX), float(cellsZ)};
  field.cellsX = cellsX;
  field.cellsZ = cellsZ;
  const auto count = std::size_t(cellsX + 1) * std::size_t(cellsZ + 1);
  field.heights.resize(count);
  field.baseHeights.resize(count);
  for (int z = 0; z <= cellsZ; ++z)
    for (int x = 0; x <= cellsX; ++x) {
      const auto cell = std::size_t(z) * (cellsX + 1) + std::size_t(x);
      field.heights.set(cell, shape(x, z));
      field.baseHeights.set(cell, shape(x, z));
    }
  return field;
}

TerrainRecipe recipeFor(const HeightField &field) {
  TerrainRecipe recipe;
  recipe.size = field.size;
  recipe.cellsX = field.cellsX;
  recipe.cellsZ = field.cellsZ;
  return recipe;
}

TerrainDrainage drain(const HeightField &field) {
  const auto result = computeTerrainDrainage(field, recipeFor(field),
                                             TerrainDrainageSettings{});
  assert(result);
  return *result;
}

// A slope draining outward with a rimmed pit in it: real relief for droplets to
// run down and a genuine depression to walk into.
float slopeWithPit(int x, int z) {
  const auto d = std::hypot(float(x) - 8.F, float(z) - 8.F);
  const float rim = 3.F;
  const auto g =
      d <= rim ? 10.F * ((d / rim) * (d / rim) - 1.F) : -2.F * (d - rim);
  return 0.35F * (float(x) + float(z)) + g;
}

// The same slope with a tall narrow spike on top, which is what erosion is
// supposed to take down and spread out again.
float spikeOnSlope(int x, int z) {
  const auto d = std::hypot(float(x) - 8.F, float(z) - 8.F);
  const auto spike = d < 2.F ? 30.F * (1.F - d / 2.F) : 0.F;
  return slopeWithPit(x, z) + spike;
}

float flat(int, int) { return 5.F; }

// Total distance the surface travelled, used as the observable proxy for how
// much work a settings tier actually did.
double travel(const HeightField &field, const TerrainSamples<float> &result) {
  double total = 0;
  for (std::size_t i = 0; i < result.size(); ++i)
    total += std::fabs(double(result[i]) - double(field.heights[i]));
  return total;
}

// The same inputs must produce the same surface, every time. Erosion is a
// simulation, so a hidden global generator or an order-dependent accumulation
// would show up here first.
void identicalSettingsGiveAnIdenticalField() {
  const auto field = makeField(16, 16, spikeOnSlope);
  const auto drainage = drain(field);
  TerrainErosionSettings settings;
  const auto first = applyTerrainErosion(field, drainage, settings);
  const auto second = applyTerrainErosion(field, drainage, settings);
  assert(first && second);
  assert(*first == *second);
  // And the same again on a fresh copy of the settings, so nothing is carried
  // over from the previous call.
  const auto third =
      applyTerrainErosion(field, drainage, TerrainErosionSettings{});
  assert(third);
  assert(*first == *third);
}

// Every droplet's start comes from the Erosion seed channel folded with its
// index, so a different world seed is a different landscape rather than the
// same one re-run.
void differentSeedsProduceDifferentFields() {
  const auto field = makeField(16, 16, spikeOnSlope);
  const auto drainage = drain(field);
  auto first = TerrainErosionSettings{};
  auto second = TerrainErosionSettings{};
  second.seed = 90210;
  const auto a = applyTerrainErosion(field, drainage, first);
  const auto b = applyTerrainErosion(field, drainage, second);
  assert(a && b);
  assert(*a != *b);
}

// The surface is returned, not edited. A stage that wrote through the field
// would corrupt every downstream reader and the undo history with it.
void theFieldIsNeverMutated() {
  const auto field = makeField(16, 16, spikeOnSlope);
  const auto before = field.heights;
  const auto drainage = drain(field);
  const auto result =
      applyTerrainErosion(field, drainage, TerrainErosionSettings{});
  assert(result);
  assert(field.heights == before);
}

// A perfectly flat field has no fall for a droplet to gain energy on and no
// slope over the talus angle, so there is nothing for either pass to do.
void aFlatFieldIsUnchanged() {
  const auto field = makeField(16, 16, flat);
  const auto drainage = drain(field);
  TerrainSamples<float> sediment;
  const auto result =
      applyTerrainErosion(field, drainage, TerrainErosionSettings{}, &sediment);
  assert(result);
  assert(*result == field.heights);
  for (std::size_t i = 0; i < sediment.size(); ++i)
    assert(sediment[i] == 0.F);
}

// A spike is exactly what erosion exists to remove: its peak comes down, and
// the material it loses is laid down on lower ground rather than deleted.
void aSpikeLowersAndSpreadsItsMaterial() {
  const auto field = makeField(16, 16, spikeOnSlope);
  const auto drainage = drain(field);
  const auto result =
      applyTerrainErosion(field, drainage, TerrainErosionSettings{});
  assert(result);
  const auto peak = field.index(8, 8);
  assert((*result)[peak] < field.heights[peak]);
  // Transport moves material downhill rather than destroying it, so something
  // below the peak's original height has to have gained some. The gain need not
  // land next to the spike: it travels along the drainage the stage was given.
  auto spread = false;
  double gained = 0;
  for (std::size_t i = 0; i < result->size(); ++i) {
    const auto change = (*result)[i] - field.heights[i];
    if (change <= 0.F)
      continue;
    gained += double(change);
    if (field.heights[i] < field.heights[peak])
      spread = true;
  }
  assert(gained > 0);
  assert(spread);
  assert(travel(field, *result) > 0);
}

// How much material the run laid down in total, which is the receiving half of
// what erosion transports.
double deposited(const HeightField &field,
                 const TerrainSamples<float> &result) {
  double total = 0;
  for (std::size_t i = 0; i < result.size(); ++i)
    if (result[i] > field.heights[i])
      total += double(result[i]) - double(field.heights[i]);
  return total;
}

// Thermal relaxation is a separate pass with its own buffer. Comparing it
// against the same run with thermal strength at zero isolates exactly what it
// did, instead of inferring it from a field the hydraulic pass also touched.
void thermalMovesSteepSamplesDownhill() {
  const auto field = makeField(16, 16, spikeOnSlope);
  const auto drainage = drain(field);
  auto withThermal = TerrainErosionSettings{};
  // One hydraulic iteration, so the thermal pass is the only thing left that
  // can explain a difference in the peak.
  withThermal.iterations = 1;
  auto without = withThermal;
  without.thermalStrength = 0.F;
  const auto thermal = applyTerrainErosion(field, drainage, withThermal);
  const auto hydraulicOnly = applyTerrainErosion(field, drainage, without);
  assert(thermal && hydraulicOnly);
  assert(*thermal != *hydraulicOnly);
  // Anything standing steeper than the talus angle sheds onto its neighbours,
  // so the spike comes down further and more material is laid down than the
  // hydraulic pass alone would have managed.
  const auto peak = field.index(8, 8);
  assert((*thermal)[peak] < (*hydraulicOnly)[peak]);
  assert(deposited(field, *thermal) > deposited(field, *hydraulicOnly));
}

TerrainErosionSettings thermalOnlySettings() {
  TerrainErosionSettings settings;
  settings.iterations = 0;
  return settings;
}

void zeroHydraulicIterationsPreserveIndependentThermalSemantics() {
  const auto field = makeField(8, 8, spikeOnSlope);
  const auto drainage = drain(field);
  TerrainErosionSettings settings;
  settings.iterations = 0;
  settings.thermalStrength = 0;
  TerrainSamples<float> balance;
  const auto unchanged =
      applyTerrainErosion(field, drainage, settings, &balance);
  assert(unchanged && *unchanged == field.heights);
  for (const float transported : balance) {
    assert(transported == 0);
  }
  settings.thermalStrength = 0.35F;
  const auto thermal = applyTerrainErosion(field, drainage, settings);
  assert(thermal && *thermal != field.heights);
  double netTransport = 0;
  for (std::size_t sample = 0; sample < thermal->size(); ++sample) {
    netTransport += double((*thermal)[sample]) - field.heights[sample];
  }
  assert(std::abs(netTransport) < 1.0e-4);
  settings.quality = TerrainQuality::Preview;
  const auto preview = applyTerrainErosion(field, drainage, settings);
  assert(preview && *preview == field.heights);
}

void thermalProgressCancellationPreservesInputsAndUnpublishedBalance() {
  const auto field = makeField(
      8, 300, [](int x, int z) { return x == 4 && z == 150 ? 20.0F : 0.0F; });
  const auto original = field.heights;
  TerrainSamples<float> balance;
  balance.resize(1);
  balance.set(0, 123);
  const auto settings = thermalOnlySettings();
  std::stop_source cancellation;
  float previousProgress = 0;
  bool completedThermalPass = false;
  const auto cancelled = applyTerrainErosion(
      field, drain(field), settings, &balance, cancellation.get_token(),
      [&](float progress) {
        assert(progress >= previousProgress && progress <= 1);
        previousProgress = progress;
        if (progress > 0.8F && progress < 1) {
          completedThermalPass = true;
          cancellation.request_stop();
        }
      });
  assert(completedThermalPass && !cancelled);
  assert(field.heights == original);
  assert(balance.size() == 1 && balance[0] == 123);
}

void diagonalRunPreservesSubTalusPlane() {
  const auto field =
      makeField(8, 8, [](int x, int z) { return 0.4F * float(x + z); });
  // Diagonal rise .8 over sqrt(2) is below the default .6 talus slope.
  const auto result =
      applyTerrainErosion(field, drain(field), thermalOnlySettings());
  assert(result);
  assert(*result == field.heights);
}

void anisotropicRunPreservesSubTalusPlane() {
  auto field = makeField(8, 8, [](int x, int) { return 2.0F * float(x); });
  field.size = {32, 8};
  // X spacing4: rise2 is slope.5, not slope2 as min-spacing would imply.
  const auto result =
      applyTerrainErosion(field, drain(field), thermalOnlySettings());
  assert(result);
  assert(*result == field.heights);
}

void anisotropicThermalTransportConservesMassAndStaysBounded() {
  auto field = makeField(
      8, 8, [](int x, int z) { return x == 4 && z == 4 ? 20.0F : 0.0F; });
  field.size = {32, 8};
  auto settings = thermalOnlySettings();
  settings.thermalStrength = 1;
  const auto result = applyTerrainErosion(field, drain(field), settings);
  const auto repeated = applyTerrainErosion(field, drain(field), settings);
  assert(result && repeated);
  assert(*result == *repeated);
  double total = 0;
  for (std::size_t sample = 0; sample < result->size(); ++sample) {
    const float height = (*result)[sample];
    assert(std::isfinite(height));
    assert(height >= 0 && height <= 20);
    total += height;
  }
  assert(std::abs(total - 20.0) < 1.0e-4);
  assert((*result)[field.index(4, 4)] < 20);
  assert(field.height(4, 4) == 20);
}

// Preview is a real tier, not the same work labelled differently. It takes a
// smaller droplet population, walks each droplet less far and skips the thermal
// pass entirely, and the surface it returns shows it.
void previewDoesStrictlyLessWork() {
  const auto field = makeField(16, 16, spikeOnSlope);
  const auto drainage = drain(field);
  auto standard = TerrainErosionSettings{};
  auto preview = standard;
  preview.quality = TerrainQuality::Preview;
  const auto full = applyTerrainErosion(field, drainage, standard);
  const auto quick = applyTerrainErosion(field, drainage, preview);
  assert(full && quick);
  assert(*full != *quick);
  const auto fullTravel = travel(field, *full);
  const auto quickTravel = travel(field, *quick);
  assert(quickTravel > 0);
  assert(quickTravel < fullTravel);
}

// Sediment is a signed transport balance, not a magnitude: material was both
// cut and laid down, and it is recovered by differencing the two surfaces
// rather than being a second opinion about which cells moved.
void sedimentIsSignedAndRoughlyBalanced() {
  const auto field = makeField(16, 16, spikeOnSlope);
  const auto drainage = drain(field);
  // Gentle settings, so neither pass dominates and the balance is visible.
  auto settings = TerrainErosionSettings{};
  settings.iterations = 12;
  settings.thermalStrength = 0.05F;
  TerrainSamples<float> sediment;
  const auto result = applyTerrainErosion(field, drainage, settings, &sediment);
  assert(result);
  assert(sediment.size() == result->size());
  double deposited = 0, cut = 0, net = 0;
  for (std::size_t i = 0; i < sediment.size(); ++i) {
    // It is exactly the change in the surface, so a consumer never has to diff
    // two sample sets to recover it.
    assert(sediment[i] == (*result)[i] - field.heights[i]);
    const auto value = double(sediment[i]);
    if (value > 0)
      deposited += value;
    else
      cut -= value;
    net += value;
  }
  assert(deposited > 0);
  assert(cut > 0);
  // Erosion moves material rather than creating or destroying it, so the two
  // sides are of the same order. Remaining droplet loads are deposited at termination;
  // floating-point updates make the balance approximate rather than exact.
  assert(std::fabs(net) < 0.5 * (deposited + cut));
}

// The stage walks the drainage it was handed. Given directions computed from a
// different surface it must behave differently, or the whole point of computing
// drainage once and passing it down is lost.
void drainageFromAnotherSurfaceChangesTheResult() {
  const auto field = makeField(16, 16, spikeOnSlope);
  // Same resolution, genuinely different shape, so nothing is rejected as a
  // mismatch and only the direction itself differs.
  const auto other = makeField(16, 16, slopeWithPit);
  const auto own = drain(field);
  const auto foreign = drain(other);
  assert(foreign.downstream.size() == own.downstream.size());
  assert(own.downstream != foreign.downstream);
  const auto correct =
      applyTerrainErosion(field, own, TerrainErosionSettings{});
  const auto mismatched =
      applyTerrainErosion(field, foreign, TerrainErosionSettings{});
  assert(correct && mismatched);
  assert(*correct != *mismatched);
}

// A drainage that does not describe this field would send droplets along
// directions belonging to some other surface, so it is refused rather than
// quietly used.
void gridMismatchIsRejected() {
  const auto field = makeField(16, 16, spikeOnSlope);
  const auto drainage = drain(field);
  auto wrongSize = drainage;
  wrongSize.downstream.resize(drainage.downstream.size() / 2);
  bool failed = false;
  try {
    (void)applyTerrainErosion(field, wrongSize, TerrainErosionSettings{});
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);

  HeightField broken = field;
  broken.heights.resize(field.heights.size() / 2);
  bool fieldFailed = false;
  try {
    (void)applyTerrainErosion(broken, drainage, TerrainErosionSettings{});
  } catch (const std::invalid_argument &) {
    fieldFailed = true;
  }
  assert(fieldFailed);
}
} // namespace

int main() {
  identicalSettingsGiveAnIdenticalField();
  differentSeedsProduceDifferentFields();
  theFieldIsNeverMutated();
  aFlatFieldIsUnchanged();
  aSpikeLowersAndSpreadsItsMaterial();
  thermalMovesSteepSamplesDownhill();
  zeroHydraulicIterationsPreserveIndependentThermalSemantics();
  thermalProgressCancellationPreservesInputsAndUnpublishedBalance();
  diagonalRunPreservesSubTalusPlane();
  anisotropicRunPreservesSubTalusPlane();
  anisotropicThermalTransportConservesMassAndStaysBounded();
  previewDoesStrictlyLessWork();
  sedimentIsSignedAndRoughlyBalanced();
  drainageFromAnotherSurfaceChangesTheResult();
  gridMismatchIsRejected();
  std::cout << "Terrain erosion checks passed\n";
}
