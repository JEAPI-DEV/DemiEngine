#pragma once

#include "demi/assets/MaterialSet.h"
#include "demi/runtime/terrain/TerrainGeneration.h"
#include "demi/runtime/terrain/TerrainGenerator.h"

#include <cstddef>
#include <string>
#include <vector>

namespace demi::runtime {

// Knobs the masks and the set cannot express, because they are per-terrain
// rather than per-asset.
//
// Every threshold a decision compares against is a slope in degrees, a distance
// in world units, or normalised moisture. Nothing here is a texel count, a UV
// span or a fraction of the sample grid, so a field twice as large in world
// units with the same masks reads the same way; how often a material repeats
// across that surface is TerrainMaterialAsset::tiling's business and is
// deliberately not consulted here.
struct TerrainMaterialLayerSettings {
  // Multiplies the slope mask before any role reads it, so one number brings
  // rock and cliff surfaces forward (above 1) or pushes them back (below 1)
  // without any role's own degree thresholds moving. 0 makes the field read as
  // flat for this stage alone; the height field itself is untouched.
  float slopeScale = 1.F;
  // Multiplies normalised moisture before any role reads it: a dry climate
  // (below 1) loses grass and dries the shore, a wet one (above 1) gains both.
  // It never touches waterDistance, which is already a world-unit measurement.
  float wetnessScale = 1.F;
  // Below this water distance a cell is treated as shoreline rather than open
  // water, which is what separates "wet ground" from "underwater".
  float shoreDistance = 2.F;
  // The dry beach band reaches this multiple of the wet band, so the beach is
  // authored in the same world units as the shoreline rather than as a second
  // independent distance that could disagree with it. 1 or less leaves no dry
  // beach at all, which is a legitimate choice for a shoreline that is wet or
  // rock the whole way round.
  float beachScale = 3.F;
  // Height above which snow wins, in world units, when the recipe has no
  // explicit snow rule. Keep it derived from the field, not hardcoded.
  //
  // Non-positive means "derive it from the field's own dry height
  // distribution", which is the default and the only scale-independent choice:
  // a fixed altitude would put the snow line in the wrong place on a field
  // authored twice as large. A field with no dry relief has no snow line at
  // all rather than one at height zero.
  float snowLine = 0.F;
  // Water level in world units. A sample at or below it is submerged, and its
  // bed is the Underwater role; a sample above it is dry ground however close
  // to the water it is. The caller resolves this the way
  // TerrainScatterConstraints resolves it, via withWaterLevel below, so an
  // authored level and a recipe-derived level cannot mean two different things
  // to two stages.
  float waterLevel = 0.F;
  // When false, snow is not merely outscored, it is not considered at all: no
  // altitude reaches it. A hot world and a project that paints its own snow
  // both need that, and neither should have to out-score the other roles to get
  // it.
  bool allowSnowLine = true;
  // Carried, not consulted by the decision. Which role a cell wears is a shape
  // decision, and a preview that picked different roles from the final field
  // would make the preview lie about the terrain it is previewing. The tier
  // belongs to the residency and detail pass that reads this same settings
  // value, so it is resolved once here rather than at two call sites.
  TerrainQuality quality = TerrainQuality::Standard;

  // Resolves the water level the way TerrainScatterConstraints::build does: an
  // authored TerrainWaterLevel wins, otherwise the level derived from the
  // recipe's landforms. This stage never derives a level of its own, because
  // two derivations of "what counts as water" is exactly the disagreement the
  // masks stage exists to prevent.
  [[nodiscard]] static TerrainMaterialLayerSettings
  withWaterLevel(TerrainMaterialLayerSettings settings,
                 const TerrainRecipe &recipe, const TerrainWaterLevel &water);
};

// Which surface a cell wears, and the score that produced it. The score is kept
// so the editor can explain a material choice the way it explains a biome
// choice: "wet ground, 0.69, 0.8 units from water" is an answer an author can
// act on, and a flat terrain with no explanation is not.
struct TerrainMaterialDecision {
  // Index into terrainMaterialCandidateRoles(). For a role the set binds this
  // is its index in TerrainMaterialSet::roles; for a role the set does not bind
  // it is the index of the trailing vocabulary entries, and materialAssetId is
  // empty. Read roleName for the identity and role for the cheap handle.
  std::size_t role = 0;
  std::string roleName;
  // Empty when the winning role is not bound by the set. A cell with no
  // material is counted as unassigned by terrainMaterialCoverage and named in
  // its `unbound` list, rather than quietly borrowing some other role's
  // material.
  std::string materialAssetId;
  float score = 0;
};

// The roles a decision considers for this set: the roles it binds, in
// TerrainMaterialSet::roles order, followed by every vocabulary role it does
// not bind, in vocabulary order. The unbound roles are not decoration. A field
// that wants rock where the set has no rock must still say so; if only the
// bound roles competed, that cliff would quietly wear grass and the author
// would never learn the palette is incomplete. Throws std::logic_error when the
// set names a role this stage's precedence table does not know, which is what
// turns a role added to the vocabulary without a score into a loud failure
// instead of an unreachable one.
[[nodiscard]] std::vector<assets::TerrainMaterialRole>
terrainMaterialCandidateRoles(const assets::TerrainMaterialSet &set);

// The field-derived quantities every consumer needs, in world units. Exported
// because the blend stage needs the same numbers: a second derivation of a snow
// line is how two stages end up disagreeing about where snow begins.
struct TerrainMaterialProfile {
  float waterLevel = 0.F;
  // Infinite when the dry relief is flat, which is the honest "this terrain has
  // no snow line" rather than a fabricated one at height zero.
  float snowLine = 0.F;
  float snowBand = 0.F;
  float relief = 0.F;
  float depthScale = 0.F;
};
[[nodiscard]] TerrainMaterialProfile
deriveTerrainMaterialProfile(const HeightField &,
                             const TerrainMaterialLayerSettings &);

// Decides one cell from the masks.
//
// Pure function of the set, the field, the masks, the settings and the cell:
// there is no RNG in the path and no running state, so asking about one cell
// before or after another cannot change either answer. Whole-field facts the
// decision needs (the snow line, the submerged depth scale) are derived by
// scanning the field, which makes a single-cell query cost about as much as a
// full resolve; a consumer answering per-cell queries over a large field should
// call decideTerrainMaterials once and index the result.
//
// Throws std::invalid_argument when the set binds no role, when the masks are
// not on the field's grid, or when a mask is only partly populated, and
// std::out_of_range for a cell the field cannot produce. Silently deciding
// against the wrong grid is how a terrain ends up uniformly material-less with
// nothing in any log.
[[nodiscard]] TerrainMaterialDecision
decideTerrainMaterial(const assets::TerrainMaterialSet &set,
                      const HeightField &field, const TerrainMasks &masks,
                      std::size_t cell,
                      const TerrainMaterialLayerSettings &settings = {});

// Decides every cell, in the field's own sample order.
//
// Every cell is resolved from the same whole-field facts, so the result is
// identical whichever cell a caller asks about first, and identical between two
// runs over equal inputs. Returns one decision per field sample; a cell no role
// wanted carries score 0 and an empty roleName.
[[nodiscard]] std::vector<TerrainMaterialDecision>
decideTerrainMaterials(const assets::TerrainMaterialSet &set,
                       const HeightField &field, const TerrainMasks &masks,
                       const TerrainMaterialLayerSettings &settings = {});

// What the set and the field did not meet. Both lists are gaps an author should
// see rather than a silently flat terrain.
struct TerrainMaterialCoverage {
  // Roles a cell wanted and the set does not bind. Those cells carry no
  // material at all, which is why they also count as unassigned.
  std::vector<std::string> unbound;
  // Roles the set binds that no cell selected: either the field never produces
  // that surface, or the thresholds never let it win here.
  std::vector<std::string> unused;
  std::size_t assigned = 0;
  // Cells with no material: the winning role is not bound by the set, or no
  // role wanted the cell at all. The second case only happens on a non-finite
  // mask sample, and it appears in neither list because there is no role name
  // to report; it is counted here so it cannot pass unnoticed.
  std::size_t unassigned = 0;
};

// Reconciles a resolved field against the set that was meant to fill it.
//
// Both lists are sorted by role name, so two runs over the same inputs report
// the same gaps in the same order. Throws std::invalid_argument when the set
// binds no role, and when the decisions do not cover exactly the field's
// samples: a partial resolve would otherwise be reported as full coverage.
[[nodiscard]] TerrainMaterialCoverage
terrainMaterialCoverage(const assets::TerrainMaterialSet &set,
                        const HeightField &field,
                        const std::vector<TerrainMaterialDecision> &decisions);

} // namespace demi::runtime
