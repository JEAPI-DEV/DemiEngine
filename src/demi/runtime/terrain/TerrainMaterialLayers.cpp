#include "demi/runtime/terrain/TerrainMaterialLayers.h"

#include "demi/runtime/terrain/TerrainBiomeRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace demi::runtime {
namespace {

using assets::TerrainMaterialRole;

// Every role in the vocabulary, in the fixed order a tie resolves with.
//
// This is the precedence contract, not an implementation detail: two roles that
// score equally must resolve the same way on every machine, in every rebuild,
// and in an editor's explanation. The order is the vocabulary's own declaration
// order and nothing else, so it cannot drift with authored array order or with
// the key-sorted role map.
//
// A role added to TerrainMaterialRole has to be added here with a score below;
// terrainMaterialCandidateRoles() refuses a set that names a role missing from
// this table rather than letting it sit in the vocabulary unreachable.
constexpr TerrainMaterialRole RolePrecedence[]{
    TerrainMaterialRole::Rock,     TerrainMaterialRole::Cliff,
    TerrainMaterialRole::Sediment, TerrainMaterialRole::Ground,
    TerrainMaterialRole::Grass,    TerrainMaterialRole::Sand,
    TerrainMaterialRole::Snow,     TerrainMaterialRole::WetGround,
    TerrainMaterialRole::Underwater};

// Slope thresholds, in degrees, because a surface's steepness is an angle and
// not a property of how finely the terrain was sampled. Every role below reads
// these, so the whole vocabulary moves together when settings.slopeScale moves.
constexpr float RockSlopeStart = 32.F;
constexpr float RockSlopeFull = 58.F;
// Rock tops out below 1 so a near-vertical face resolves as Cliff, whose score
// reaches 1: the material that samples all three axes wins the cliff, and the
// material that does not stops competing for it before the crossover.
constexpr float RockScoreCeiling = 0.9F;
constexpr float CliffSlopeStart = 55.F;
constexpr float CliffSlopeFull = 70.F;
// Snow lies on ground, not on a face. Above this the rock shows through.
constexpr float SnowSlopeLimit = 45.F;
// Flatness ramps: where a score starts ignoring slope and where it stops
// caring about it entirely.
constexpr float SedimentFlatStart = 6.F;
constexpr float SedimentFlatFull = 30.F;
constexpr float GroundFlatStart = 10.F;
constexpr float GroundFlatFull = 30.F;
constexpr float SandFlatStart = 8.F;
constexpr float SandFlatFull = 24.F;

// The snow line sits inside the dry height distribution, so the same recipe
// reads the same way on a field authored twice as large in world units. The low
// quantile ignores the basin floor and the high one ignores a single summit
// pixel; the line is placed 62% of the way up what is left between them.
constexpr float SnowLineLowQuantile = 0.05F;
constexpr float SnowLineHighQuantile = 0.95F;
constexpr float SnowLineFraction = 0.62F;
constexpr float SnowBandFraction = 0.15F;
// One millimetre of world units: below this a heightfield has no relief worth
// placing an altitude threshold inside, and the snow line becomes unreachable
// instead of landing at height zero where every sample would clear it.
constexpr float ReliefEpsilon = 1e-3F;
// Deposition is a world-unit transport balance, so it is measured against the
// field's own relief rather than against an absolute depth of fill.
constexpr float DepositionReliefFraction = 0.10F;
// The submerged bed is scored against the depth at which this field's water is
// genuinely deep, which is a quantile of its own submerged samples.
constexpr float DepthQuantile = 0.9F;

// Clamped linear ramp.
float ramp(const float value, const float low, const float high) noexcept {
  if (!(high > low))
    return value >= high ? 1.F : 0.F;
  return std::clamp((value - low) / (high - low), 0.F, 1.F);
}

// Clamped smoothstep, so a score does not jump at a threshold. A threshold
// describes where a surface becomes visible, and an abrupt winner would put a
// one-cell seam along it that no amount of layer blending can hide.
float smooth(const float value, const float low, const float high) noexcept {
  const float t = ramp(value, low, high);
  return t * t * (3.F - 2.F * t);
}

// Clamped falloff from 1 to 0, for the terms that are strongest at the low end
// and have to be gone by the far end of their band.
float falloff(const float value, const float low, const float high) noexcept {
  return 1.F - smooth(value, low, high);
}

// The sample at this quantile, found by partitioning rather than sorting so a
// single-cell query stays linear in the field. The index rounds up, so a high
// quantile lands on a sample that is actually at least that high.
float quantileCeil(std::vector<float> &values, const float quantile) {
  if (values.empty())
    return 0.F;
  const auto last = static_cast<float>(values.size() - 1);
  const auto rounded = std::min(last, std::ceil(quantile * last));
  const auto position = static_cast<std::ptrdiff_t>(rounded);
  std::nth_element(values.begin(), values.begin() + position, values.end());
  return values[static_cast<std::size_t>(position)];
}

// Whole-field facts every cell's decision reads.
//
// Derived by scanning the field once, never accumulated in the order cells are
// visited. That is what makes a single-cell query agree with the bulk resolve
// and makes two runs agree with each other.
struct MaterialProfile {
  float waterLevel = 0.F;
  // World units. Infinite when the dry relief is flat, which is the honest
  // "this terrain has no snow line" rather than a fabricated one at height
  // zero.
  float snowLine = std::numeric_limits<float>::infinity();
  // World units over which snow deepens above the line.
  float snowBand = ReliefEpsilon;
  // The dry height range the snow line is placed inside. World units.
  float relief = 0.F;
  // The depth at which a submerged bed reads as fully deep. World units.
  float depthScale = 0.F;
};

MaterialProfile
deriveProfile(const HeightField &field,
              const TerrainMaterialLayerSettings &settings) {
  MaterialProfile profile;
  profile.waterLevel = settings.waterLevel;
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
    const float low = quantileCeil(dry, SnowLineLowQuantile);
    const float high = quantileCeil(dry, SnowLineHighQuantile);
    profile.relief = std::max(0.F, high - low);
    const float derived =
        profile.relief > ReliefEpsilon
            ? low + SnowLineFraction * profile.relief
            : std::numeric_limits<float>::infinity();
    // An authored line wins over the derived one, exactly as an authored water
    // level wins over the derived one: both are decisions the project made.
    profile.snowLine = settings.snowLine > 0.F ? settings.snowLine : derived;
    profile.snowBand =
        std::max(ReliefEpsilon, SnowBandFraction * profile.relief);
  }
  if (!depths.empty())
    profile.depthScale = quantileCeil(depths, DepthQuantile);
  return profile;
}

// One sample's masks, read once so no role re-derives another role's input.
struct CellFacts {
  float height = 0.F;
  // Degrees, after settings.slopeScale.
  float slope = 0.F;
  // 0 dry to 1 saturated, after settings.wetnessScale.
  float moisture = 0.F;
  // World units to the nearest sample at or below the water level.
  float waterDistance = 0.F;
  // Normalised flow accumulation.
  float flow = 0.F;
  // Signed transport balance: positive deposition, negative cut.
  float sediment = 0.F;
  bool submerged = false;
  // Empty when the mask carries a value outside the substrate vocabulary, which
  // is then ignored rather than read as some arbitrary family.
  std::optional<TerrainSubstrate> substrate;
};

CellFacts readCell(const HeightField &field, const TerrainMasks &masks,
                   const MaterialProfile &profile,
                   const TerrainMaterialLayerSettings &settings,
                   const std::size_t cell) noexcept {
  CellFacts facts;
  facts.height = field.heights[cell];
  facts.slope = std::clamp(masks.slope[cell] * settings.slopeScale, 0.F, 90.F);
  facts.moisture =
      std::clamp(masks.moisture[cell] * settings.wetnessScale, 0.F, 1.F);
  facts.waterDistance = std::max(0.F, masks.waterDistance[cell]);
  facts.flow = std::clamp(masks.flow[cell], 0.F, 1.F);
  facts.sediment = masks.sediment[cell];
  facts.submerged = facts.height <= profile.waterLevel;
  const auto substrate = masks.substrate[cell];
  if (substrate <= static_cast<std::size_t>(TerrainSubstrate::Wet))
    facts.substrate = static_cast<TerrainSubstrate>(substrate);
  return facts;
}

// Score for the Underwater role: the submerged bed.
//
//   score = 0.60 + 0.35 * smooth(depth, 0, depthScale)
//        + 0.05 * (substrate == Wet)
//
// Declines on every sample above the water level, which keeps a submerged
// cliff from being resolved as a dry one: the bed of a lake is underwater
// material whether the shore above it is rock or sand. Every surface role
// declines outright down there, so the floor only has to beat zero, and 0.60
// leaves room above it for a second shallow-water role to be added later
// without the bed silently changing. The depth term separates a shallow shelf
// from a deep basin in world units against a depth derived from this field's
// own water, so it needs no absolute depth.
float scoreUnderwater(const CellFacts &facts,
                      const MaterialProfile &profile) noexcept {
  if (!facts.submerged)
    return 0.F;
  const float depth = std::max(0.F, profile.waterLevel - facts.height);
  return 0.60F + 0.35F * smooth(depth, 0.F, profile.depthScale) +
         (facts.substrate == TerrainSubstrate::Wet ? 0.05F : 0.F);
}

// The dry reach of the beach, in world units, derived from the authored wet
// band so the two can never disagree about where the shore is.
float beachReach(const TerrainMaterialLayerSettings &settings) noexcept {
  const float shore = std::max(ReliefEpsilon, settings.shoreDistance);
  return std::max(shore + ReliefEpsilon,
                  shore * std::max(1.F, settings.beachScale));
}

// Score for the WetGround role: shoreline inside the wet band.
//
//   band  = falloff(waterDistance, shoreDistance / 2, shoreDistance)
//   score = band * (0.55 + 0.30 * moisture)
//        + 0.05 * band * (substrate == Wet)
//
// The band holds full score across its inner half and falls to nothing at the
// authored width, so the shoreline is a shore rather than a one-cell edge, and
// the transition out of it is a blend the layer pass can hide. It is a
// distance in world units, never a number of samples: the same masks on a
// field ten times larger describe the same shore. It declines when the sample
// is submerged, because that cell belongs to the bed, and it reaches 0.90 at
// the water's edge so it wins the whole band it owns while staying under
// Snow's 1.00, so a high shoreline above the snow line still reads as snow.
float scoreWetGround(const CellFacts &facts,
                     const TerrainMaterialLayerSettings &settings) noexcept {
  if (facts.submerged)
    return 0.F;
  const float shore = std::max(ReliefEpsilon, settings.shoreDistance);
  const float band = falloff(facts.waterDistance, 0.5F * shore, shore);
  if (band <= 0.F)
    return 0.F;
  return band * (0.55F + 0.30F * facts.moisture) +
         (facts.substrate == TerrainSubstrate::Wet ? 0.05F * band : 0.F);
}

// Score for the Snow role: high ground that is not a face.
//
//   score = 0.55 + 0.45 * smooth(height, snowLine, snowLine + snowBand)
//
// Declines below the line, above the snow slope limit, on a submerged sample
// and whenever the recipe turned the snow line off. The altitude is compared
// against a line derived from the field, never against an absolute height.
// Reaching 1.00 above the line is deliberate: snow covers what is under it
// rather than competing with it, so a snowfield does not end in a fringe of
// whatever scored 0.98.
float scoreSnow(const CellFacts &facts, const MaterialProfile &profile,
                const TerrainMaterialLayerSettings &settings) noexcept {
  if (!settings.allowSnowLine || facts.submerged)
    return 0.F;
  // Snow does not stick to a cliff. The limit is in degrees, so it is the face
  // that disqualifies a sample, not its height.
  if (facts.slope > SnowSlopeLimit)
    return 0.F;
  if (!(facts.height >= profile.snowLine))
    return 0.F;
  // An authored line near the top of the float range would overflow the band to
  // infinity, so the band is measured from whichever of the two the sum
  // survives.
  const float line =
      std::min(profile.snowLine, profile.snowLine + profile.snowBand);
  return 0.55F + 0.45F * smooth(facts.height, line, line + profile.snowBand);
}

// Score for the Rock role: steep exposed ground.
//
//   score = 0.90 * smooth(slope, 32, 58) degrees
//
// One ramp, no second term: exposure here is the face itself, which is why the
// erosion mask does not enter. The ceiling keeps the hand-off to Cliff clean,
// and a flat cell scores exactly 0 so it declines rather than competing with
// the ground roles at zero.
float scoreRock(const CellFacts &facts) noexcept {
  if (facts.submerged)
    return 0.F;
  return RockScoreCeiling * smooth(facts.slope, RockSlopeStart, RockSlopeFull);
}

// Score for the Cliff role: near-vertical faces.
//
//   score = smooth(slope, 55, 70) degrees
//
// Reaches 1.00 where Rock's ramp has already saturated, so the face material
// wins every sample past the crossover and the rock material wins the slopes
// below it. It needs no substrate or height input: a vertical face is a face.
float scoreCliff(const CellFacts &facts) noexcept {
  if (facts.submerged)
    return 0.F;
  return smooth(facts.slope, CliffSlopeStart, CliffSlopeFull);
}

// Score for the Sediment role: low-slope deposition.
//
//   flat       = falloff(slope, 6, 30) degrees
//   deposition = clamp01(sediment / max(0.001, 0.10 * relief))
//   score      = flat * max(deposition, 0.70 * flow)
//
// Deposition is a world-unit transport balance, so it is normalised against
// the field's own relief: a tenth of this terrain's vertical range is a tenth
// of the erosion that could have happened here, at any world scale. Flow stands
// in when the erosion stage did not run, so a riverbed still reads as a
// riverbed on a field generated without erosion.
float scoreSediment(const CellFacts &facts,
                    const MaterialProfile &profile) noexcept {
  if (facts.submerged)
    return 0.F;
  const float flat = falloff(facts.slope, SedimentFlatStart, SedimentFlatFull);
  if (flat <= 0.F)
    return 0.F;
  const float scale =
      std::max(ReliefEpsilon, DepositionReliefFraction * profile.relief);
  const float deposition = std::clamp(facts.sediment / scale, 0.F, 1.F);
  return flat * std::max(deposition, 0.70F * facts.flow);
}

// Score for the Sand role: the dry beach beyond the wet band.
//
//   reach = shoreDistance * beachScale                // world units
//   band  = falloff(waterDistance, (shore + reach)/2, reach)
//   flat  = falloff(slope, 8, 24) degrees
//   score = flat * band * (0.70 - 0.25 * moisture)
//
// The band starts at the outer edge of the wet band and holds full score across
// the beach before falling to nothing at the reach, so the two roles meet at
// one authored distance instead of two that can disagree, and the reach is a
// multiple of that distance rather than an absolute number of units. Moisture
// dries the sand down because saturated sand is the wet role's cell to answer
// for. The ceiling of 0.70 is above the Ground role's 0.65, so the beach wins
// the region it owns instead of reading as bare soil, and below WetGround's
// 0.90, so the wet part of the beach always outranks the dry part of it.
float scoreSand(const CellFacts &facts,
                const TerrainMaterialLayerSettings &settings) noexcept {
  if (facts.submerged)
    return 0.F;
  const float shore = std::max(ReliefEpsilon, settings.shoreDistance);
  const float reach = beachReach(settings);
  const float band =
      falloff(facts.waterDistance, 0.5F * (shore + reach), reach);
  if (band <= 0.F)
    return 0.F;
  const float flat = falloff(facts.slope, SandFlatStart, SandFlatFull);
  return flat * band * (0.70F - 0.25F * facts.moisture);
}

// Score for the Ground role: bare vegetated soil, the default dry surface.
//
//   flat  = falloff(slope, 10, 30) degrees
//   score = flat * (0.25 + 0.40 * (1 - moisture))
//
// This is the baseline every other ground role has to beat, so it never
// reaches 1: it reaches 0 at its slope limit, where the exposed roles have
// already taken over, because a cell too steep for soil is rock rather than a
// hole in the terrain. Drier ground scores higher because a dry soil surface is
// more bare soil and less cover.
float scoreGround(const CellFacts &facts) noexcept {
  if (facts.submerged)
    return 0.F;
  const float flat = falloff(facts.slope, GroundFlatStart, GroundFlatFull);
  return flat * (0.25F + 0.40F * (1.F - facts.moisture));
}

// Score for the Grass role: dense cover where it can grow.
//
//   cover = smooth(moisture, 0.50, 0.78)          // normalised, 0..1
//   score = 0.95 * falloff(slope, 10, 30) * cover
//
// It declines across the whole beach reach, where the ground is sand or
// saturated rather than soil, and on the derived Sand and Wet substrates, which
// are the masks saying the surface is not soil. The moisture ramp crosses the
// Ground role at about 0.63 normalised moisture, which is where bare soil stops
// being the better description of a flat cell.
float scoreGrass(const CellFacts &facts,
                 const TerrainMaterialLayerSettings &settings) noexcept {
  if (facts.submerged)
    return 0.F;
  if (facts.waterDistance < beachReach(settings))
    return 0.F;
  if (facts.substrate == TerrainSubstrate::Sand ||
      facts.substrate == TerrainSubstrate::Wet)
    return 0.F;
  const float flat = falloff(facts.slope, GroundFlatStart, GroundFlatFull);
  return 0.95F * flat * smooth(facts.moisture, 0.50F, 0.78F);
}

float scoreRole(const TerrainMaterialRole role, const CellFacts &facts,
                const MaterialProfile &profile,
                const TerrainMaterialLayerSettings &settings) noexcept {
  switch (role) {
  case TerrainMaterialRole::Underwater:
    return scoreUnderwater(facts, profile);
  case TerrainMaterialRole::WetGround:
    return scoreWetGround(facts, settings);
  case TerrainMaterialRole::Snow:
    return scoreSnow(facts, profile, settings);
  case TerrainMaterialRole::Rock:
    return scoreRock(facts);
  case TerrainMaterialRole::Cliff:
    return scoreCliff(facts);
  case TerrainMaterialRole::Sediment:
    return scoreSediment(facts, profile);
  case TerrainMaterialRole::Sand:
    return scoreSand(facts, settings);
  case TerrainMaterialRole::Ground:
    return scoreGround(facts);
  case TerrainMaterialRole::Grass:
    return scoreGrass(facts, settings);
  }
  return 0.F;
}

// The best score across the candidates. Ties fall to the earlier role in
// RolePrecedence, so the winner depends on the masks alone and not on the order
// the set happens to store its roles in. The scan walks RolePrecedence rather
// than the candidate list for exactly that reason: the candidate list is in the
// set's key order, and letting it break a tie would make the answer depend on a
// container the author never chose. A best score of zero means no role wanted
// the cell, and the caller reports it as unassigned. That is also what a
// non-finite mask sample produces: a NaN fails every comparison, so nothing
// wins and the gap is visible, instead of one role being handed a terrain it
// never asked for.
TerrainMaterialDecision
decide(const std::vector<TerrainMaterialRole> &candidates,
       const assets::TerrainMaterialSet &set, const CellFacts &facts,
       const MaterialProfile &profile,
       const TerrainMaterialLayerSettings &settings) {
  TerrainMaterialDecision decision;
  float best = 0.F;
  for (const TerrainMaterialRole role : RolePrecedence) {
    const auto score = scoreRole(role, facts, profile, settings);
    if (!(score > best))
      continue;
    const auto candidate =
        std::find(candidates.begin(), candidates.end(), role);
    best = score;
    decision.role = static_cast<std::size_t>(candidate - candidates.begin());
    decision.roleName = std::string(assets::terrainMaterialRoleName(role));
    decision.score = score;
  }
  if (decision.roleName.empty())
    return decision;
  const auto bound = set.roles.find(decision.roleName);
  if (bound != set.roles.end())
    decision.materialAssetId = bound->second;
  return decision;
}

[[noreturn]] void fail(const std::string &message) {
  throw std::invalid_argument(message);
}

// Everything a decision reads has to be on the field's own grid and completely
// populated. A mask of the wrong length is a caller error rather than something
// to resample: quietly measuring slope on a different resolution is how terrain
// ends up uniformly material-less with no explanation anywhere.
void requireUsable(const assets::TerrainMaterialSet &set,
                   const HeightField &field, const TerrainMasks &masks) {
  if (set.roles.empty())
    fail("Terrain material set '" + set.id +
              "' binds no roles, so no cell could be assigned a material.");
  if (field.heights.empty())
    fail("Terrain height field has no samples, so no material can be "
              "assigned.");
  const auto count = field.heights.size();
  const std::pair<const char *, std::size_t> grids[]{
      {"slope", masks.slope.size()},
      {"moisture", masks.moisture.size()},
      {"waterDistance", masks.waterDistance.size()},
      {"flow", masks.flow.size()},
      {"sediment", masks.sediment.size()},
      {"substrate", masks.substrate.size()}};
  for (const auto &[name, size] : grids)
    if (size != count)
      fail("Terrain mask '" + std::string(name) + "' has " +
                std::to_string(size) + " samples but the field has " +
                std::to_string(count) +
                ": the masks are not on the field's grid, and assigning "
                "materials against the wrong grid is refused rather than "
                "resampled.");
}

bool hasRole(const TerrainMaterialRole role) noexcept {
  return std::any_of(std::begin(RolePrecedence), std::end(RolePrecedence),
                     [role](const TerrainMaterialRole known) {
                       return known == role;
                     });
}

} // namespace

TerrainMaterialLayerSettings TerrainMaterialLayerSettings::withWaterLevel(
    TerrainMaterialLayerSettings settings, const TerrainRecipe &recipe,
    const TerrainWaterLevel &water) {
  settings.waterLevel = water.authored
                            ? water.seaLevel
                            : TerrainRuleContextBuilder::seaLevel(recipe);
  return settings;
}

TerrainMaterialProfile
deriveTerrainMaterialProfile(const HeightField &field,
                             const TerrainMaterialLayerSettings &settings) {
  const auto profile = deriveProfile(field, settings);
  return TerrainMaterialProfile{.waterLevel = profile.waterLevel,
                                .snowLine = profile.snowLine,
                                .snowBand = profile.snowBand,
                                .relief = profile.relief,
                                .depthScale = profile.depthScale};
}

std::vector<TerrainMaterialRole>
terrainMaterialCandidateRoles(const assets::TerrainMaterialSet &set) {
  std::vector<TerrainMaterialRole> candidates;
  candidates.reserve(set.roles.size() + std::size(RolePrecedence));
  for (const auto &[name, material] : set.roles) {
    (void)material;
    const auto role = assets::terrainMaterialRoleFromName(name);
    if (!role.has_value())
      throw std::logic_error("Terrain material set '" + set.id +
                             "' binds role '" + name +
                             "', which is not in the terrain material role "
                             "vocabulary.");
    if (!hasRole(*role))
      throw std::logic_error(
          "Terrain material set '" + set.id + "' binds role '" + name +
          "', which has no score in the terrain material layer precedence "
          "table. Adding a role to the vocabulary without scoring it would "
          "leave it unreachable.");
    candidates.push_back(*role);
  }
  // The roles this set does not bind, so a field that wants one says so instead
  // of wearing the nearest material the set happens to own.
  for (const TerrainMaterialRole role : RolePrecedence) {
    const auto name = assets::terrainMaterialRoleName(role);
    if (set.roles.find(std::string(name)) == set.roles.end())
      candidates.push_back(role);
  }
  return candidates;
}

TerrainMaterialDecision
decideTerrainMaterial(const assets::TerrainMaterialSet &set,
                      const HeightField &field, const TerrainMasks &masks,
                      const std::size_t cell,
                      const TerrainMaterialLayerSettings &settings) {
  requireUsable(set, field, masks);
  if (cell >= field.heights.size())
    throw std::out_of_range("Terrain material cell " + std::to_string(cell) +
                            " is outside the field's " +
                            std::to_string(field.heights.size()) + " samples.");
  const auto candidates = terrainMaterialCandidateRoles(set);
  const auto profile = deriveProfile(field, settings);
  const auto facts = readCell(field, masks, profile, settings, cell);
  return decide(candidates, set, facts, profile, settings);
}

std::vector<TerrainMaterialDecision>
decideTerrainMaterials(const assets::TerrainMaterialSet &set,
                       const HeightField &field, const TerrainMasks &masks,
                       const TerrainMaterialLayerSettings &settings) {
  requireUsable(set, field, masks);
  const auto candidates = terrainMaterialCandidateRoles(set);
  // Derived once for the whole field: every cell resolves against the same
  // facts, so the result cannot depend on which cell was visited first.
  const auto profile = deriveProfile(field, settings);
  std::vector<TerrainMaterialDecision> decisions;
  decisions.reserve(field.heights.size());
  for (std::size_t cell = 0; cell < field.heights.size(); ++cell) {
    const auto facts = readCell(field, masks, profile, settings, cell);
    decisions.push_back(decide(candidates, set, facts, profile, settings));
  }
  return decisions;
}

TerrainMaterialCoverage
terrainMaterialCoverage(const assets::TerrainMaterialSet &set,
                        const HeightField &field,
                        const std::vector<TerrainMaterialDecision> &decisions) {
  if (set.roles.empty())
    fail("Terrain material set '" + set.id +
         "' binds no roles, so no coverage can be reported.");
  if (decisions.size() != field.heights.size())
    fail("Terrain material coverage needs one decision per field sample: got " +
         std::to_string(decisions.size()) + " decisions for " +
         std::to_string(field.heights.size()) + " samples.");
  // Key-sorted, so the report is identical between runs and between the editor
  // and a CLI check.
  std::set<std::string> selected;
  TerrainMaterialCoverage coverage;
  for (const auto &decision : decisions) {
    if (decision.materialAssetId.empty())
      ++coverage.unassigned;
    else
      ++coverage.assigned;
    if (!decision.roleName.empty())
      selected.insert(decision.roleName);
  }
  for (const auto &name : selected)
    if (set.roles.find(name) == set.roles.end())
      coverage.unbound.push_back(name);
  for (const auto &[name, material] : set.roles) {
    (void)material;
    if (!selected.contains(name))
      coverage.unused.push_back(name);
  }
  return coverage;
}

} // namespace demi::runtime
