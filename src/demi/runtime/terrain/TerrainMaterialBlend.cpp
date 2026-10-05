#include "demi/runtime/terrain/TerrainMaterialBlend.h"
#include "demi/runtime/terrain/TerrainMaterialLayers.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace demi::runtime {
namespace {

using assets::TerrainMaterialRole;
using assets::terrainMaterialRoleFromName;
using assets::terrainMaterialRoleName;

// The whole vocabulary, in the discrete stage's own precedence order. The blend
// scores every role independently, so the order is only a stable index; it is
// kept identical to TerrainMaterialLayers' table so a role's identity never
// depends on which of the two stages is reading it.
constexpr TerrainMaterialRole kRoles[]{
    TerrainMaterialRole::Rock,      TerrainMaterialRole::Cliff,
    TerrainMaterialRole::Sediment,  TerrainMaterialRole::Ground,
    TerrainMaterialRole::Grass,     TerrainMaterialRole::Sand,
    TerrainMaterialRole::Snow,      TerrainMaterialRole::WetGround,
    TerrainMaterialRole::Underwater};
constexpr std::size_t kRoleCount = std::size(kRoles);

// Slope bands, in degrees, and in world units. Every threshold is the same kind
// of quantity the discrete stage compares against, and most of them are its own
// numbers: the blend has to agree with the decision about what a face is, or it
// would paint a role the decision stage never allows anywhere near here.
//
// The bands OVERLAP on purpose, and that is the single most important property
// of this table. Two adjacent roles whose ramps cross between the same two
// slopes produce a product with the presence factor that is nearly a step, so
// the lattice puts one role at 1 and the other at 0 and the cell-sized seam this
// stage exists to remove comes straight back. Overlapping bands leave both roles
// with some say across the whole junction, and presence then decides how much of
// each survives.
constexpr float kRockSlopeStart = 18.F;
constexpr float kRockSlopeFull = 54.F;
// Rock never falls to zero. Bare stone shows through thin soil on gentle ground
// too, which is both what a real surface does and what gives the rock/soil
// junction something to cross-fade through. It stays small enough that a soil
// role with presence on both sides still wins a flat vegetated sample.
constexpr float kRockFloor = 0.12F;
constexpr float kCliffSlopeStart = 48.F;
constexpr float kCliffSlopeFull = 70.F;
// Snow lies on ground, not on a face, so the slope term is a falloff rather than
// a gate: a little snow caught on a ledge is a blend, not a cell-sized step.
constexpr float kSnowSlopeStart = 30.F;
constexpr float kSnowSlopeFull = 45.F;
constexpr float kGroundFlatStart = 8.F;
constexpr float kGroundFlatFull = 34.F;
constexpr float kSandFlatStart = 6.F;
constexpr float kSandFlatFull = 28.F;
constexpr float kSedimentFlatStart = 4.F;
constexpr float kSedimentFlatFull = 34.F;
constexpr float kWetFlatStart = 6.F;
constexpr float kWetFlatFull = 26.F;
constexpr float kShoreDistance = 2.F;
constexpr float kBeachScale = 3.F;
constexpr float kDepositionReliefFraction = 0.10F;

// Where the cliff override begins, relative to TerrainBlendSettings::cliffSlope.
// A ramp rather than a step, so the face fades into rock over the last 12
// degrees instead of snapping to it.
constexpr float kCliffOverrideSpan = 12.F;

// The snow line's place in the field's own dry height distribution. These
// mirror TerrainMaterialLayers' own quantiles and fraction, so a field that has
// not told this stage where its snow line is gets the same answer the decision
// stage would have given. Both stages defaulting to 0 is not agreement; passing
// the settings through TerrainBlendSettings::withMaterialLayers is.
constexpr float kSnowBandFraction = 0.15F;
// One millimetre of world units: below this there is no relief worth placing an
// altitude threshold or a normalisation inside.
constexpr float kReliefEpsilon = 1e-3F;
// Curvature is measured as a dimensionless second derivative: the Laplacian of
// the heights over the cell area, divided by the field's own relief. A gully or
// ridge whose curvature is half the relief per cell maps to the end of the
// curvature ramp, which puts the ramp's useful range on features a player can
// actually see.
constexpr float kCurvatureRamp = 0.5F;
// Presence blur truncation, in cells. Bounds the cost of the separable blur for
// a wide transition without letting a wide setting change the answer: past
// three sigma a Gaussian is under a percent of its peak.
constexpr int kPresenceRadiusMax = 8;
constexpr float kPresenceRadiusMin = 1.F;

float ramp(const float value, const float low, const float high) noexcept {
  if (!(high > low))
    return value >= high ? 1.F : 0.F;
  return std::clamp((value - low) / (high - low), 0.F, 1.F);
}

float smooth(const float value, const float low, const float high) noexcept {
  const auto t = ramp(value, low, high);
  return t * t * (3.F - 2.F * t);
}

float falloff(const float value, const float low, const float high) noexcept {
  return 1.F - smooth(value, low, high);
}

// The sample at this quantile, found by partitioning rather than sorting. The
// index rounds up, so a high quantile lands on a sample that is at least that
// high.
float quantileCeil(std::vector<float> &values, const float quantile) {
  if (values.empty())
    return 0.F;
  const auto last = static_cast<float>(values.size() - 1);
  const auto rounded = std::min(last, std::ceil(quantile * last));
  const auto position = static_cast<std::ptrdiff_t>(rounded);
  std::nth_element(values.begin(), values.begin() + position, values.end());
  return values[static_cast<std::size_t>(position)];
}

// Whole-field facts every lattice sample reads, derived once by scanning the
// field rather than accumulated in visit order.
struct BlendProfile {
  float waterLevel = 0.F;
  // World units. Infinite when the dry relief is flat, which is the honest
  // "this terrain has no snow line" rather than a fabricated one at height zero.
  float snowLine = std::numeric_limits<float>::infinity();
  float snowBand = kReliefEpsilon;
  float relief = 0.F;
  float depthScale = 0.F;
  // World-unit cell spacing, used by the curvature derivation and to normalise
  // it.
  float cellX = 1.F, cellZ = 1.F;
};

BlendProfile readProfile(const HeightField &field,
                         const TerrainBlendSettings &settings) {
  BlendProfile profile;
  profile.waterLevel = settings.waterLevel;
  profile.cellX = field.cellsX > 0 ? field.size.x / std::max(1, field.cellsX) : 1.F;
  profile.cellZ = field.cellsZ > 0 ? field.size.y / std::max(1, field.cellsZ) : 1.F;
  std::vector<float> dry;
  std::vector<float> depths;
  dry.reserve(field.heights.size());
  for (std::size_t cell = 0; cell < field.heights.size(); ++cell) {
    const float height = field.heights[cell];
    if (height > profile.waterLevel)
      dry.push_back(height);
    else
      depths.push_back(profile.waterLevel - height);
  }
  if (!dry.empty()) {
    // The snow line, its band and the relief come from the discrete stage's
    // profile. Deriving them again here is how a blended surface and a discrete
    // one end up disagreeing about where snow begins, and the discrete decision
    // is the one the author sees explained in the editor.
    const auto shared =
        deriveTerrainMaterialProfile(
            field, TerrainMaterialLayerSettings{.snowLine = settings.snowLine,
                                                .waterLevel = settings.waterLevel,
                                                .allowSnowLine = true,
                                                .quality = settings.quality});
    profile.snowLine = shared.snowLine;
    profile.snowBand = shared.snowBand;
    profile.relief = shared.relief;
  }
  if (!depths.empty())
    profile.depthScale = quantileCeil(depths, 0.9F);
  return profile;
}

// One lattice sample's raw mask inputs, interpolated off the cell grid.
struct BlendFacts {
  float height = 0.F;
  float slope = 0.F;
  float moisture = 0.F;
  float waterDistance = 0.F;
  float flow = 0.F;
  float sediment = 0.F;
  // Dimensionless second derivative of the heights: positive in a gully,
  // negative on a ridge.
  float curvature = 0.F;
  // The substrate of the nearest cell. Read as a mild demotion rather than a
  // gate: the discrete stage uses it to decide, but a gate here would stamp a
  // cell-sized edge back into the blend it exists to remove.
  TerrainSubstrate substrate = TerrainSubstrate::Soil;
  bool hasSubstrate = false;
  bool submerged = false;
};

// The discrete Laplacian of the heights at one field sample, in 1/world-units^2.
// Derived from the finished heights rather than from the normals: sculpting and
// erosion write heights, so a normal array that had not been refreshed would
// quietly give this stage a second, disagreeing surface.
float laplacianAt(const HeightField &field, const BlendProfile &profile,
                  const int x, const int z) noexcept {
  const auto sample = [&](const int sx, const int sz) {
    const auto cx = std::clamp(sx, 0, field.cellsX);
    const auto cz = std::clamp(sz, 0, field.cellsZ);
    return field.heights[field.index(cx, cz)];
  };
  const float centre = sample(x, z);
  const float alongX = (sample(x - 1, z) - 2.F * centre + sample(x + 1, z)) /
                        (profile.cellX * profile.cellX);
  const float alongZ = (sample(x, z - 1) - 2.F * centre + sample(x, z + 1)) /
                        (profile.cellZ * profile.cellZ);
  return alongX + alongZ;
}

// Bilinear sample of a field sample at a position in cell units.
float sampleField(const TerrainSamples<float> &samples, const HeightField &field,
                  const float cellX, const float cellZ) noexcept {
  const auto x0 = std::clamp(static_cast<int>(std::floor(cellX)), 0,
                             std::max(0, field.cellsX - 1));
  const auto z0 = std::clamp(static_cast<int>(std::floor(cellZ)), 0,
                             std::max(0, field.cellsZ - 1));
  const float tx = std::clamp(cellX - static_cast<float>(x0), 0.F, 1.F);
  const float tz = std::clamp(cellZ - static_cast<float>(z0), 0.F, 1.F);
  const auto at = [&](const int x, const int z) {
    return samples[field.index(std::clamp(x, 0, field.cellsX),
                               std::clamp(z, 0, field.cellsZ))];
  };
  const float bottom = at(x0, z0) * (1.F - tx) + at(x0 + 1, z0) * tx;
  const float top = at(x0, z0 + 1) * (1.F - tx) + at(x0 + 1, z0 + 1) * tx;
  return bottom * (1.F - tz) + top * tz;
}

BlendFacts readFacts(const HeightField &field, const TerrainMasks &masks,
                     const BlendProfile &profile, const float cellX,
                     const float cellZ) noexcept {
  BlendFacts facts;
  facts.height = sampleField(field.heights, field, cellX, cellZ);
  facts.slope = std::clamp(sampleField(masks.slope, field, cellX, cellZ), 0.F,
                           90.F);
  facts.moisture = std::clamp(sampleField(masks.moisture, field, cellX, cellZ),
                              0.F, 1.F);
  facts.waterDistance =
      std::max(0.F, sampleField(masks.waterDistance, field, cellX, cellZ));
  facts.flow = std::clamp(sampleField(masks.flow, field, cellX, cellZ), 0.F, 1.F);
  facts.sediment = sampleField(masks.sediment, field, cellX, cellZ);

  // Curvature cannot be interpolated from the interpolated heights: a bilinear
  // patch is linear in each axis separately, so its own second derivative is
  // identically zero. It is measured at the four samples the position falls
  // between and interpolated instead, which keeps the term smooth across a cell
  // rather than stepping with it.
  const auto x0 = std::clamp(static_cast<int>(std::floor(cellX)), 0,
                             std::max(0, field.cellsX - 1));
  const auto z0 = std::clamp(static_cast<int>(std::floor(cellZ)), 0,
                             std::max(0, field.cellsZ - 1));
  const float tx = std::clamp(cellX - static_cast<float>(x0), 0.F, 1.F);
  const float tz = std::clamp(cellZ - static_cast<float>(z0), 0.F, 1.F);
  const float area = profile.cellX * profile.cellZ;
  const auto at = [&](const int x, const int z) {
    return laplacianAt(field, profile, x, z) * area;
  };
  const float bottom = at(x0, z0) * (1.F - tx) + at(x0 + 1, z0) * tx;
  const float top = at(x0, z0 + 1) * (1.F - tx) + at(x0 + 1, z0 + 1) * tx;
  facts.curvature =
      (bottom * (1.F - tz) + top * tz) / std::max(kReliefEpsilon, profile.relief);

  const auto substrate = masks.substrate[field.index(
      std::clamp(static_cast<int>(std::lround(cellX)), 0, field.cellsX),
      std::clamp(static_cast<int>(std::lround(cellZ)), 0, field.cellsZ))];
  if (substrate <= static_cast<std::size_t>(TerrainSubstrate::Wet)) {
    facts.substrate = static_cast<TerrainSubstrate>(substrate);
    facts.hasSubstrate = true;
  }
  facts.submerged = facts.height <= profile.waterLevel;
  return facts;
}

// How exposed a point is: 0 in a gully, 1 on a ridge.
float convexity(const BlendFacts &facts) noexcept {
  return smooth(-facts.curvature, 0.F, kCurvatureRamp);
}

float concavity(const BlendFacts &facts) noexcept {
  return smooth(facts.curvature, 0.F, kCurvatureRamp);
}

float substrateDemotion(const BlendFacts &facts, const TerrainSubstrate wet,
                        const TerrainSubstrate sand,
                        const float demote) noexcept {
  if (!facts.hasSubstrate)
    return 1.F;
  if (facts.substrate == wet)
    return 1.F - 0.5F * demote;
  if (facts.substrate == sand)
    return 1.F - demote;
  return 1.F;
}

// Per-role affinity at one point, over the masks and the field's own profile.
// Each is quoted as the expression it implements; together they are what makes
// this a blend rather than a smoothed step function. Every one of them is
// multiplied by a presence factor the caller supplies, so a role no cell chose
// anywhere near here weighs nothing here however strongly its masks like the
// ground.
//
//   rock     = (0.12 + 0.88 * smooth(slope, 18, 54)) * (0.45 + 0.55 * convexity)
//   cliff    = smooth(slope, 48, 70)
//   ground   = falloff(slope, 8, 34) * (0.45 + 0.35 * (1 - moisture))
//              * (0.55 + 0.45 * concavity) * substrate
//   grass    = falloff(slope, 8, 34) * smooth(moisture, 0.45, 0.75) * substrate
//   sand     = falloff(waterDistance, 4, 6) * falloff(slope, 6, 28)
//              * (0.8 - 0.3 * moisture)
//   wet      = falloff(waterDistance, 1, 2) * (0.5 + 0.3 * moisture)
//              * falloff(slope, 6, 26)
//   sediment = falloff(slope, 4, 34) * max(sediment / (0.1 * relief), 0.7 * flow)
//              * (0.45 + 0.55 * concavity)
//   snow     = smooth(height, snowLine, snowLine + snowBand)
//              * falloff(slope, 30, 45)
//   bed      = submerged ? 0.55 + 0.45 * smooth(depth, 0, depthScale) : 0
//
// Curvature is the only term the discrete stage has no counterpart for, and it
// exists because slope alone cannot tell a cliff from a rounded shoulder: both
// are 50 degrees, and only one of them should be bare rock all the way to the
// valley floor. Convexity lifts rock on a ridge and lifts soil and sediment in
// a gully, so a valley floor keeps its fine material while the ridge above it
// strips.
float affinity(const TerrainMaterialRole role, const BlendFacts &facts,
               const BlendProfile &profile) noexcept {
  switch (role) {
  case TerrainMaterialRole::Rock:
    if (facts.submerged)
      return 0.F;
    return (kRockFloor + (1.F - kRockFloor) *
                           smooth(facts.slope, kRockSlopeStart, kRockSlopeFull)) *
           (0.45F + 0.55F * convexity(facts));
  case TerrainMaterialRole::Cliff:
    if (facts.submerged)
      return 0.F;
    return smooth(facts.slope, kCliffSlopeStart, kCliffSlopeFull);
  case TerrainMaterialRole::Ground:
    if (facts.submerged)
      return 0.F;
    return falloff(facts.slope, kGroundFlatStart, kGroundFlatFull) *
           (0.45F + 0.35F * (1.F - facts.moisture)) *
           (0.55F + 0.45F * concavity(facts)) *
           substrateDemotion(facts, TerrainSubstrate::Wet,
                             TerrainSubstrate::Sand, 0.15F);
  case TerrainMaterialRole::Grass:
    if (facts.submerged)
      return 0.F;
    return falloff(facts.slope, kGroundFlatStart, kGroundFlatFull) *
           smooth(facts.moisture, 0.45F, 0.75F) *
           substrateDemotion(facts, TerrainSubstrate::Wet,
                             TerrainSubstrate::Sand, 0.30F);
  case TerrainMaterialRole::Sand: {
    if (facts.submerged)
      return 0.F;
    const float reach = kShoreDistance * kBeachScale;
    const float band = falloff(facts.waterDistance, 0.5F * (kShoreDistance + reach),
                               reach);
    if (band <= 0.F)
      return 0.F;
    return band * falloff(facts.slope, kSandFlatStart, kSandFlatFull) *
           (0.8F - 0.3F * facts.moisture);
  }
  case TerrainMaterialRole::WetGround: {
    if (facts.submerged)
      return 0.F;
    const float band =
        falloff(facts.waterDistance, 0.5F * kShoreDistance, kShoreDistance);
    if (band <= 0.F)
      return 0.F;
    return band * (0.5F + 0.3F * facts.moisture) *
           falloff(facts.slope, kWetFlatStart, kWetFlatFull);
  }
  case TerrainMaterialRole::Sediment: {
    if (facts.submerged)
      return 0.F;
    const float flat =
        falloff(facts.slope, kSedimentFlatStart, kSedimentFlatFull);
    if (flat <= 0.F)
      return 0.F;
    const float scale =
        std::max(kReliefEpsilon, kDepositionReliefFraction * profile.relief);
    const float deposition = std::clamp(facts.sediment / scale, 0.F, 1.F);
    return flat * std::max(deposition, 0.7F * facts.flow) *
           (0.45F + 0.55F * concavity(facts));
  }
  case TerrainMaterialRole::Snow: {
    if (facts.submerged || !std::isfinite(profile.snowLine))
      return 0.F;
    return smooth(facts.height, profile.snowLine,
                  profile.snowLine + profile.snowBand) *
           falloff(facts.slope, kSnowSlopeStart, kSnowSlopeFull);
  }
  case TerrainMaterialRole::Underwater: {
    if (!facts.submerged)
      return 0.F;
    const float depth = std::max(0.F, profile.waterLevel - facts.height);
    return 0.55F + 0.45F * smooth(depth, 0.F,
                                  std::max(kReliefEpsilon, profile.depthScale));
  }
  }
  return 0.F;
}

// How much of each role's decision field reaches a point, as a truncated
// separable Gaussian over cell centres, normalised per point.
//
// Per-point normalisation is what makes presence a partition: in the interior
// of the field the roles' presences sum to exactly 1 wherever any nearby cell
// named a role, so a point on a boundary splits its influence between the two
// sides in proportion to their distance and nothing has to be renormalised
// downstream for that to hold. On the last few cells of a field the clamp folds
// the far taps back onto the edge, which biases every role the same way, so a
// role there can read above 1; the vertex weights are normalised regardless.
struct PresenceFields {
  int cellsX = 0, cellsZ = 0;
  std::vector<std::vector<float>> byRole;  // kRoleCount, each cellsX*cellsZ
};

std::vector<float> blurPass(const std::vector<float> &source, const int width,
                            const int height, const std::vector<float> &taps,
                            const bool horizontal) {
  std::vector<float> out(source.size(), 0.F);
  const auto radius = static_cast<int>(taps.size() / 2);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      float total = 0.F;
      float value = 0.F;
      for (int k = -radius; k <= radius; ++k) {
        const auto tap = taps[static_cast<std::size_t>(k + radius)];
        const int sx = horizontal ? std::clamp(x + k, 0, width - 1) : x;
        const int sy = horizontal ? y : std::clamp(y + k, 0, height - 1);
        value += tap * source[static_cast<std::size_t>(sy) * width + sx];
        total += tap;
      }
      out[static_cast<std::size_t>(y) * width + x] =
          total > 0.F ? value / total : 0.F;
    }
  }
  return out;
}

float presenceAt(const PresenceFields &fields, const std::size_t role,
                 const float cellX, const float cellZ) noexcept {
  // A role this field never chose was never blurred, so it has no presence
  // anywhere rather than an uninitialised one.
  if (fields.byRole[role].empty())
    return 0.F;
  // The lattice sits at cell centres, so cell centre i is at coordinate i + 0.5.
  const auto gx = std::clamp(cellX - 0.5F, 0.F,
                             static_cast<float>(std::max(0, fields.cellsX - 1)));
  const auto gz = std::clamp(cellZ - 0.5F, 0.F,
                             static_cast<float>(std::max(0, fields.cellsZ - 1)));
  const auto x0 = std::clamp(static_cast<int>(std::floor(gx)), 0,
                             std::max(0, fields.cellsX - 1));
  const auto z0 = std::clamp(static_cast<int>(std::floor(gz)), 0,
                             std::max(0, fields.cellsZ - 1));
  const float tx = std::clamp(gx - static_cast<float>(x0), 0.F, 1.F);
  const float tz = std::clamp(gz - static_cast<float>(z0), 0.F, 1.F);
  const auto &grid = fields.byRole[role];
  const auto at = [&](const int x, const int z) {
    const auto cx = std::clamp(x, 0, fields.cellsX - 1);
    const auto cz = std::clamp(z, 0, fields.cellsZ - 1);
    return grid[static_cast<std::size_t>(cz) * fields.cellsX + cx];
  };
  const float bottom = at(x0, z0) * (1.F - tx) + at(x0 + 1, z0) * tx;
  const float top = at(x0, z0 + 1) * (1.F - tx) + at(x0 + 1, z0 + 1) * tx;
  return bottom * (1.F - tz) + top * tz;
}

int roleIndexOf(const std::string_view roleName) {
  const auto role = terrainMaterialRoleFromName(roleName);
  if (!role.has_value())
    throw std::invalid_argument("Terrain material blend received the role '" +
                                std::string(roleName) +
                                "', which is not in the terrain material role "
                                "vocabulary.");
  for (std::size_t index = 0; index < kRoleCount; ++index)
    if (kRoles[index] == *role)
      return static_cast<int>(index);
  return -1;
}

// Samples per cell. Rounded DOWN, so the lattice is never finer than the
// setting asks for: a resolution of 0.3 gives three samples per cell, not four.
int subdivisionsFor(const float resolution) {
  return std::clamp(static_cast<int>(std::floor(1.F / std::max(resolution, 1e-6F))),
                    1, 256);
}

// Lattice samples across `cells` cells. Preview halves this in each axis; an
// integer subdivision could not, because one sample per cell is already the
// coarsest lattice this stage can express, so the halving divides the lattice
// itself rather than the rate it was built at.
std::size_t latticeSamples(const int cells, const int subdivisions,
                           const bool preview) {
  const auto count = static_cast<std::size_t>(cells) *
                     static_cast<std::size_t>(subdivisions);
  const auto halved = preview ? (count / 2 + count % 2) : count;
  return std::max<std::size_t>(1, halved);
}

[[noreturn]] void fail(std::string message) {
  throw std::invalid_argument(std::move(message));
}

} // namespace

std::size_t TerrainBlendSlots::find(const std::string_view role) const noexcept {
  for (std::size_t index = 0; index < roles.size(); ++index)
    if (roles[index] == role)
      return index;
  return roles.size();
}

TerrainBlendSettings TerrainBlendSettings::withMaterialLayers(
    TerrainBlendSettings settings, const TerrainMaterialLayerSettings &layers) {
  settings.snowLine = layers.snowLine;
  settings.waterLevel = layers.waterLevel;
  return settings;
}

TerrainBlendSlots
resolveTerrainBlendSlots(const std::vector<TerrainMaterialDecision> &decisions,
                         const std::size_t samples) {
  std::array<std::size_t, kRoleCount> influence{};
  for (const auto &decision : decisions) {
    if (decision.roleName.empty())
      continue;
    influence[static_cast<std::size_t>(roleIndexOf(decision.roleName))] += 1;
  }
  struct Ranked {
    std::size_t index;
    std::size_t count;
  };
  std::vector<Ranked> ranked;
  ranked.reserve(kRoleCount);
  const std::size_t total = decisions.size();
  for (std::size_t index = 0; index < kRoleCount; ++index) {
    if (influence[index] == 0)
      continue;
    // A role chosen for fewer cells than one sample's share of the field cannot
    // be resolved by a lattice of that size, so it is not a candidate for a
    // slot. Skipped rather than dropped: blendTerrainMaterials folds it and
    // reports it.
    if (samples != 0 && total != 0 &&
        influence[index] * samples < total)
      continue;
    ranked.push_back({index, influence[index]});
  }
  // Most significant first, then by role name, so the order of the decisions
  // vector and the storage order of the material set cannot reach the answer.
  std::sort(ranked.begin(), ranked.end(),
            [](const Ranked &a, const Ranked &b) {
              if (a.count != b.count)
                return a.count > b.count;
              return terrainMaterialRoleName(kRoles[a.index]) <
                     terrainMaterialRoleName(kRoles[b.index]);
            });
  TerrainBlendSlots slots;
  const std::size_t retained = std::min(ranked.size(), terrainBlendLayerCount);
  slots.roles.reserve(retained);
  slots.materialAssetIds.reserve(retained);
  for (std::size_t order = 0; order < retained; ++order) {
    const auto &role = ranked[order];
    const std::string name(terrainMaterialRoleName(kRoles[role.index]));
    slots.roles.push_back(name);
    // The first cell that chose this role carries the material behind it. Key
    // sorted and scanned in decision order, so the answer does not depend on
    // which cell happened to be visited first.
    std::string material;
    for (const auto &decision : decisions)
      if (decision.roleName == name && !decision.materialAssetId.empty()) {
        material = decision.materialAssetId;
        break;
      }
    slots.materialAssetIds.push_back(std::move(material));
  }
  return slots;
}

TerrainBlendResult
blendTerrainMaterials(const HeightField &field, const TerrainMasks &masks,
                      const std::vector<TerrainMaterialDecision> &decisions,
                      const TerrainBlendSettings &settings) {
  if (field.heights.empty())
    fail("Terrain height field has no samples, so no material can be blended.");
  if (field.cellsX <= 0 || field.cellsZ <= 0)
    fail("Terrain height field has no cells, so no blend lattice can be laid "
         "out.");
  if (!std::isfinite(settings.resolution) || settings.resolution <= 0.F)
    fail("Terrain blend resolution must be a positive number of cells per "
         "vertex.");
  if (!std::isfinite(settings.transitionWidth) ||
      settings.transitionWidth < 0.F)
    fail("Terrain blend transition width must be a non-negative number of "
         "cells.");
  const std::pair<const char *, std::size_t> grids[]{
      {"slope", masks.slope.size()},
      {"moisture", masks.moisture.size()},
      {"waterDistance", masks.waterDistance.size()},
      {"flow", masks.flow.size()},
      {"sediment", masks.sediment.size()},
      {"substrate", masks.substrate.size()}};
  for (const auto &[name, size] : grids)
    if (size != field.heights.size())
      fail("Terrain mask '" + std::string(name) + "' has " +
           std::to_string(size) + " samples but the field has " +
           std::to_string(field.heights.size()) +
           ": the masks are not on the field's grid, and blending against the "
           "wrong grid is refused rather than resampled.");
  if (decisions.size() != field.heights.size())
    fail("Terrain material blending needs one decision per field sample: got " +
         std::to_string(decisions.size()) + " decisions for " +
         std::to_string(field.heights.size()) + " samples.");

  TerrainBlendResult result;
  const auto profile = readProfile(field, settings);
  const int subdivisions = subdivisionsFor(settings.resolution);
  const bool preview = settings.quality == TerrainQuality::Preview;
  result.cellsX = static_cast<std::size_t>(field.cellsX);
  result.cellsZ = static_cast<std::size_t>(field.cellsZ);
  result.latticeX = latticeSamples(field.cellsX, subdivisions, preview);
  result.latticeZ = latticeSamples(field.cellsZ, subdivisions, preview);
  result.slots = resolveTerrainBlendSlots(decisions,
                                           result.latticeX * result.latticeZ);
  if (result.slots.roles.empty())
    fail("No terrain material role was chosen anywhere on this field, so there "
         "is nothing to blend. Assign materials first: a blend with no role is "
         "a surface the renderer cannot draw.");

  // Each decision's role resolved to its vocabulary index once. The strings are
  // a stable handle, but parsing them per sample per role is a cost with no
  // answer in it.
  std::vector<int> decisionRole(decisions.size(), -1);
  for (std::size_t cell = 0; cell < decisions.size(); ++cell)
    if (!decisions[cell].roleName.empty())
      decisionRole[cell] = roleIndexOf(decisions[cell].roleName);

  // Forced roles. Underwater is not a decision the neighbourhood gets to
  // overrule, because a sample below the water level is the bed whatever the
  // masks preferred above it; rock is forced where a cliff is, because that is
  // what the override below is for. Both displace the least significant retained
  // slot rather than being appended, so the cap is still four.
  const float cliffSlope =
      std::clamp(settings.cliffSlope, 0.F, 90.F);
  std::set<std::string> forced;
  std::vector<std::string> forcedOrder;
  std::size_t submergedCells = 0;
  std::size_t cliffCells = 0;
  for (std::size_t cell = 0; cell < field.heights.size(); ++cell) {
    if (field.heights[cell] <= profile.waterLevel)
      ++submergedCells;
    if (masks.slope[cell] >= cliffSlope)
      ++cliffCells;
  }
  for (const auto &candidate : {std::string("underwater"), std::string("rock")})
    if ((candidate == "underwater" && submergedCells > 0) ||
        (candidate == "rock" && cliffCells > 0))
      forcedOrder.push_back(candidate);
  for (const auto &role : forcedOrder) {
    forced.insert(role);
    if (result.slots.find(role) != result.slots.roles.size())
      continue;
    std::string material;
    for (const auto &decision : decisions)
      if (decision.roleName == role && !decision.materialAssetId.empty()) {
        material = decision.materialAssetId;
        break;
      }
    if (result.slots.roles.size() < terrainBlendLayerCount) {
      result.slots.roles.push_back(role);
      result.slots.materialAssetIds.push_back(std::move(material));
      continue;
    }
    // The last retained slot is the least significant of the four by
    // construction, so it is the one the cap gives up. The displaced role is
    // still present in the decisions, so foldedRoles below reports it.
    result.slots.roles.back() = role;
    result.slots.materialAssetIds.back() = std::move(material);
  }

  // Role index -> slot, and the roles the cap did not keep.
  std::array<int, kRoleCount> slotOfRole{};
  slotOfRole.fill(-1);
  for (std::size_t slot = 0; slot < result.slots.roles.size(); ++slot)
    slotOfRole[static_cast<std::size_t>(roleIndexOf(result.slots.roles[slot]))] =
        static_cast<int>(slot);

  PresenceFields presence;
  presence.cellsX = field.cellsX;
  presence.cellsZ = field.cellsZ;
  {
    const float sigma =
        std::max(kPresenceRadiusMin, std::max(0.F, settings.transitionWidth));
    const auto radius = std::clamp(
        static_cast<int>(std::ceil(3.F * sigma)), 1, kPresenceRadiusMax);
    std::vector<float> taps;
    taps.reserve(static_cast<std::size_t>(2 * radius + 1));
    for (int k = -radius; k <= radius; ++k) {
      const float x = static_cast<float>(k) / sigma;
      taps.push_back(std::exp(-0.5F * x * x));
    }
    presence.byRole.assign(kRoleCount, {});
    for (std::size_t role = 0; role < kRoleCount; ++role) {
      // Only the roles this field actually chose are blurred: a field with four
      // surfaces pays for four fields, not nine.
      if (std::none_of(decisionRole.begin(), decisionRole.end(),
                       [role](const int chosen) {
                         return chosen == static_cast<int>(role);
                       }))
        continue;
      std::vector<float> indicator(
          static_cast<std::size_t>(field.cellsX) *
          static_cast<std::size_t>(field.cellsZ));
      for (int z = 0; z < field.cellsZ; ++z)
        for (int x = 0; x < field.cellsX; ++x) {
          if (decisionRole[field.index(x, z)] != static_cast<int>(role))
            continue;
          indicator[static_cast<std::size_t>(z) * field.cellsX + x] = 1.F;
        }
      const auto horizontal = blurPass(indicator, field.cellsX, field.cellsZ,
                                       taps, true);
      presence.byRole[role] =
          blurPass(horizontal, field.cellsX, field.cellsZ, taps, false);
    }
  }

  result.vertices.resize(result.latticeX * result.latticeZ);
  std::size_t singleRoleVertices = 0;
  const auto bedSlot = static_cast<std::size_t>(
      slotOfRole[static_cast<std::size_t>(
          roleIndexOf(assets::terrainMaterialRoleName(
              TerrainMaterialRole::Underwater)))]);
  // Lattice sample i sits at the centre of the (cellsX / latticeX)-cell-wide
  // span it covers, so the lattice spans exactly the field and a consumer can
  // invert the mapping from a vertex position alone.
  const float stepX =
      static_cast<float>(field.cellsX) / static_cast<float>(result.latticeX);
  const float stepZ =
      static_cast<float>(field.cellsZ) / static_cast<float>(result.latticeZ);
  for (std::size_t z = 0; z < result.latticeZ; ++z) {
    for (std::size_t x = 0; x < result.latticeX; ++x) {
      const float cellX = (static_cast<float>(x) + 0.5F) * stepX;
      const float cellZ = (static_cast<float>(z) + 0.5F) * stepZ;
      const auto facts = readFacts(field, masks, profile, cellX, cellZ);
      auto &vertex = result.vertices[z * result.latticeX + x];
      vertex.cell = field.index(
          std::clamp(static_cast<int>(std::lround(cellX)), 0, field.cellsX),
          std::clamp(static_cast<int>(std::lround(cellZ)), 0, field.cellsZ));
      vertex.triplanar = settings.triplanarCliffs &&
                         facts.slope > cliffSlope - kCliffOverrideSpan;

      //   override = smooth(slope, cliffSlope - 12, cliffSlope)
      //
      // Rock's weight rises to the override and every other role's weight falls
      // by it, including rock's own presence, because a cliff face is not
      // something the discrete decision gets a say in. Past the midpoint of the
      // ramp rock holds more than half the vertex even if no neighbouring cell
      // ever chose rock, which is the whole point: a face must not be half
      // grass because the cell below it was.
      //
      // Zero below the water line. A submerged face is the bed of a body of
      // water, not a cliff face, and letting the override through would put
      // rock back on a vertex the water is already covering.
      const float override =
          facts.submerged
              ? 0.F
              : smooth(facts.slope, cliffSlope - kCliffOverrideSpan, cliffSlope);

      std::array<float, kRoleCount> raw{};
      float total = 0.F;
      float presenceTotal = 0.F;
      for (std::size_t role = 0; role < kRoleCount; ++role) {
        // Below the water line the neighbourhood has no vote: the shore above it
        // cannot cross-fade into the bed, so presence is not consulted at all and
        // only the Underwater role survives. That is what makes a submerged
        // vertex report one weight rather than blending rock with grass.
        float amount =
            facts.submerged
                ? (kRoles[role] == TerrainMaterialRole::Underwater ? 1.F : 0.F)
                : presenceAt(presence, role, cellX, cellZ);
        float weight = affinity(kRoles[role], facts, profile);
        if (override > 0.F) {
          // The override is rock's alone. Cliff is a normal role here: it is a
          // material the field's set may bind for a face, and letting the
          // override manufacture a cliff weight on a field that chose rock
          // would put material on the surface the author did not ask for.
          if (kRoles[role] == TerrainMaterialRole::Rock) {
            weight = std::max(weight, override);
            amount = std::max(amount, override);
          } else {
            weight *= 1.F - override;
            amount *= 1.F - override;
          }
        }
        raw[role] = weight * amount;
        total += raw[role];
        presenceTotal += amount;
      }
      // No affinity survived here: fall back to presence alone, so the vertex
      // still names the surface the neighbourhood agreed on rather than
      // normalising nothing into a divide by zero.
      if (!(total > 0.F) && presenceTotal > 0.F) {
        raw = {};
        total = 0.F;
        for (std::size_t role = 0; role < kRoleCount; ++role) {
          raw[role] = presenceAt(presence, role, cellX, cellZ);
          total += raw[role];
        }
      }
      if (!(total > 0.F)) {
        // Nothing nearby named a role at all. Frozen on the nearest cell's own
        // decision, which is the only thing that can answer for this point.
        vertex.frozen = true;
        const int slot = decisionRole[vertex.cell] < 0
                             ? -1
                             : slotOfRole[static_cast<std::size_t>(
                                   decisionRole[vertex.cell])];
        if (slot >= 0) {
          vertex.weights[static_cast<std::size_t>(slot)] = 1.F;
          ++result.frozenVertices;
          ++singleRoleVertices;
          continue;
        }
        // The nearest cell's role did not survive the cap either. Putting its
        // mass on the least significant retained slot keeps the vertex
        // drawable and reports the approximation through foldedRoles.
        const std::size_t last = result.slots.roles.size() - 1;
        vertex.weights[last] = 1.F;
        ++result.frozenVertices;
        ++singleRoleVertices;
        continue;
      }

      // Distribute into the slots, folding whatever the cap did not keep.
      std::array<float, kRoleCount> share{};
      for (std::size_t role = 0; role < kRoleCount; ++role)
        share[role] = raw[role] / total;
      float folded = 0.F;
      for (std::size_t role = 0; role < kRoleCount; ++role) {
        if (slotOfRole[role] >= 0) {
          vertex.weights[static_cast<std::size_t>(slotOfRole[role])] +=
              share[role];
          continue;
        }
        if (!(share[role] > 0.F))
          continue;
        folded += share[role];
        // The folded mass goes to the weakest retained slot, which is the one
        // whose material the error distorts least, and is reported in foldedRoles
        // so the approximation is visible rather than silent.
        std::size_t target = 0;
        for (std::size_t slot = 1; slot < result.slots.roles.size(); ++slot)
          if (vertex.weights[slot] < vertex.weights[target])
            target = slot;
        vertex.weights[target] += share[role];
      }
      result.maxFoldedWeight = std::max(result.maxFoldedWeight, folded);
      if (folded > terrainBlendFoldedReportThreshold)
        ++result.foldedVertices;

      float sum = 0.F;
      std::size_t live = 0;
      std::size_t liveSlot = 0;
      for (std::size_t slot = 0; slot < terrainBlendLayerCount; ++slot) {
        sum += vertex.weights[slot];
        if (vertex.weights[slot] > 0.F) {
          ++live;
          liveSlot = slot;
        }
      }
      assert(sum > 0.F);
      // Renormalising after the fold keeps the invariant exact rather than
      // approximately true: four rounded additions are not obliged to sum to one.
      for (float &weight : vertex.weights)
        weight /= sum;
      if (live <= 1)
        ++singleRoleVertices;
      // Below the water line the bed is the bed: one role, no cross-fade, and
      // the vertex says so rather than leaving the renderer to infer it from a
      // weight vector that happens to have one entry set.
      if (facts.submerged && live == 1 && liveSlot == bedSlot) {
        vertex.frozen = true;
        ++result.frozenVertices;
      }
    }
  }
  result.degenerate =
      !result.vertices.empty() && singleRoleVertices == result.vertices.size();

  std::set<std::string> foldedRoles;
  for (const auto &decision : decisions)
    if (!decision.roleName.empty() &&
        result.slots.find(decision.roleName) == result.slots.roles.size())
      foldedRoles.insert(decision.roleName);
  for (const auto &role : forced)
    if (result.slots.find(role) == result.slots.roles.size())
      foldedRoles.insert(role);
  result.foldedRoles.assign(foldedRoles.begin(), foldedRoles.end());
  return result;
}

std::vector<TerrainBlendWeight>
terrainBlendVertexWeights(const TerrainBlendResult &result,
                          const std::size_t vertex) {
  if (vertex >= result.vertices.size())
    throw std::out_of_range("Terrain blend vertex " + std::to_string(vertex) +
                            " is outside the lattice's " +
                            std::to_string(result.vertices.size()) +
                            " samples.");
  std::vector<TerrainBlendWeight> weights;
  const auto &source = result.vertices[vertex];
  for (std::size_t slot = 0; slot < result.slots.roles.size(); ++slot) {
    const float weight = source.weights[slot];
    if (!(weight > 0.F))
      continue;
    weights.push_back(TerrainBlendWeight{
        .roleName = result.slots.roles[slot],
        .weight = weight,
        .materialAssetId = result.slots.materialAssetIds[slot]});
  }
  return weights;
}

std::array<float, terrainBlendLayerCount>
sampleTerrainBlendWeights(const TerrainBlendResult &result, const float cellX,
                          const float cellZ) {
  if (result.vertices.empty() || result.latticeX == 0 || result.latticeZ == 0)
    throw std::out_of_range("Terrain blend lattice is empty.");
  // The lattice sample i sits at cell coordinate (i + 0.5) * cells / samples, so
  // the position is mapped back through the same expression it was produced by
  // rather than through a second convention that could drift from it.
  const float stepX = static_cast<float>(result.cellsX) /
                      static_cast<float>(result.latticeX);
  const float stepZ = static_cast<float>(result.cellsZ) /
                      static_cast<float>(result.latticeZ);
  const auto gx = std::clamp(cellX / stepX - 0.5F, 0.F,
                             static_cast<float>(result.latticeX - 1));
  const auto gz = std::clamp(cellZ / stepZ - 0.5F, 0.F,
                             static_cast<float>(result.latticeZ - 1));
  const auto x0 = static_cast<std::size_t>(std::floor(gx));
  const auto z0 = static_cast<std::size_t>(std::floor(gz));
  const auto x1 = std::min(x0 + 1, result.latticeX - 1);
  const auto z1 = std::min(z0 + 1, result.latticeZ - 1);
  const float tx = gx - static_cast<float>(x0);
  const float tz = gz - static_cast<float>(z0);
  std::array<float, terrainBlendLayerCount> weights{};
  for (std::size_t slot = 0; slot < terrainBlendLayerCount; ++slot) {
    const float bottom =
        result.vertices[z0 * result.latticeX + x0].weights[slot] * (1.F - tx) +
        result.vertices[z0 * result.latticeX + x1].weights[slot] * tx;
    const float top =
        result.vertices[z1 * result.latticeX + x0].weights[slot] * (1.F - tx) +
        result.vertices[z1 * result.latticeX + x1].weights[slot] * tx;
    weights[slot] = bottom * (1.F - tz) + top * tz;
  }
  float sum = 0.F;
  for (const float weight : weights)
    sum += weight;
  if (!(sum > 0.F))
    return weights;
  for (float &weight : weights)
    weight /= sum;
  return weights;
}

} // namespace demi::runtime