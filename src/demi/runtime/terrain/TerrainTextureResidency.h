#pragma once

#include "demi/assets/MaterialAsset.h"
#include "demi/runtime/terrain/TerrainGeneration.h"
#include "demi/runtime/terrain/TerrainLod.h"

#include <cstddef>
#include <string>
#include <vector>

namespace demi {
struct AssetRegistry;
}

namespace demi::runtime {

// ---------------------------------------------------------------------------
// BYTE ESTIMATION, AND ITS ERROR
//
// The real size of a texture is a property of the imported image: its
// resolution, its channel count, and the storage format the runtime picks. None
// of that exists until the image is loaded, and loading every candidate texture
// in order to learn how much room is left would defeat the point of the budget.
// So this stage ESTIMATES from the map slot and from the material's own
// declared settings, and states the nominal format it assumed:
//
//   stored = nominal_edge^2 * channels * (4/3), rounded up to 256 KiB
//
// The 4/3 is a full mip chain (1 + 1/4 + 1/16 + ... = 4/3), and the 256 KiB
// step is the granularity a real driver rounds mip chains and block-aligned
// rows up to, so two textures differing by a few texels do not pretend to be
// two different allocations. The nominal table lives in
// terrainResidencyNominalFormat so a caller, a test and this comment cannot
// disagree about it.
//
// ERROR, STATED PLAAINLY. The estimate is exact arithmetic over an ASSUMED
// format, and the assumption is wrong whenever the authored texture does not
// match it: a 512-square detail map is overestimated, a 4096-square base colour
// underestimated. Authored terrain maps commonly sit within about a factor of
// two of the nominal table, and the bound cannot be tightened below that,
// because the source resolution is not part of the material document.
//
// What does NOT depend on the estimate is the eviction ORDER, which is derived
// from slot importance and dependent count alone. A wrong estimate can change
// how MANY textures fit; it can never change WHICH one is dropped first. That
// is why the byte figure appears in exactly two places -- the budget arithmetic
// and the reported totals -- and never in the priority. A caller that wants
// exact numbers re-plans from measured sizes after upload.
//
// Anisotropy is a SAMPLING cost, not a storage cost, so it never enters the
// estimate either. Neither does sRGB: it is a sampling convention over the same
// stored channels. A runtime that keeps colour maps in a higher-precision
// linear format would exceed this estimate, and the header says so rather than
// pretending the number covers it.
// ---------------------------------------------------------------------------

// A budget in BYTES, with a separate cap on distinct textures, because a
// hundred 4 KB textures cost less resident state than four 4 MB ones even
// though the byte total looks fine: every texture also costs a binding, a
// sampler state and a slot in the renderer's own tables, which bytes do not
// see.
struct TerrainResidencyBudget {
  std::size_t textureBytes = 64u * 1024 * 1024;
  std::size_t distinctTextures = 128;
  // Texture detail is dropped before the base colour, which is dropped before
  // the normal map. Say so in a comment and enforce it -- see
  // terrainResidencyEvictionRank, which is the one place that order is written.
  std::size_t anisotropy = 4;
  bool srgbTextures = true;
  // The tier decides how many LOD levels the terrain HOLDS, and a level set is
  // what decides which maps are sampled at all. Carried here rather than passed
  // as a second argument so there is exactly one place a tier is read from,
  // which is the same rule TerrainMaterialLayerSettings and
  // TerrainBlendSettings follow with their own copies.
  TerrainQuality quality = TerrainQuality::Standard;
};

// One texture's residency.
struct TerrainTextureResidency {
  // The texture asset:// id.
  std::string assetId;
  assets::TerrainMaterialMapSlot slot;
  // Which material role asked for it. The same texture may appear more than
  // once with different roles; those entries are ONE upload, which is why bytes
  // are accounted per distinct asset id and not per entry.
  std::string roleName;
  // The COARSEST LOD level that still samples this texture. A chunk at that
  // level or nearer needs it resident; a chunk beyond it does not and the
  // renderer may release it. It is the level at which the texture becomes
  // droppable, not the finest level that needs it, and it is why the same
  // material contributes different maps for the two quality tiers.
  TerrainLod minimumLod = TerrainLod::Full;
  std::size_t estimatedBytes = 0;
  bool srgb = false;
  int anisotropy = 1;
  // How many planned requests name this asset id. A texture two roles sample
  // outlives one a single role samples: dropping it would degrade the surface
  // everywhere, where a single-cell texture degrades one spot. In the
  // field-driven stage the same reasoning runs per cell; here the unit of
  // demand is a (role, slot) request, because a residency plan is built from a
  // material set and not from a resolved heightfield.
  std::size_t dependents = 1;
  // Eviction order. Lower is evicted first. Derived from slot importance,
  // then from how many requests depend on it (a shared texture outlives a
  // single-role one), then from the asset id for determinism. The value is the
  // texture's rank in that order, so all entries naming one texture share it.
  std::size_t evictionPriority = 0;
  bool resident = true;
  // Empty while resident. Never empty once evicted: a drop the author cannot
  // see is a drop they cannot fix.
  std::string evictionReason;
};

struct TerrainResidencyPlan {
  // Sorted by asset id, then by canonical slot order, so two runs over equal
  // inputs produce byte-identical plans.
  std::vector<TerrainTextureResidency> textures;
  // The estimate for every candidate, before the budget was applied.
  std::size_t requestedBytes = 0;
  // The estimate for what survived. Counted once per DISTINCT texture: one
  // upload serves every entry that samples it.
  std::size_t plannedBytes = 0;
  // Entries dropped under pressure, not distinct textures: a texture two roles
  // sample is reported once per request, because a renderer frees it once and
  // an author reads about it once per surface that changed.
  std::size_t evicted = 0;
  // Requests that are absent WITHOUT being evicted: a map no held level samples
  // (detail dropped for a coarser LOD) or a map whose own strength is zero.
  std::size_t trimmedCount = 0;
  bool withinBudget = true;
  // Both caps hold for what the plan kept. It reports the BUDGET, not the
  // content: a plan that had to drop a base colour is within budget and is
  // still a visible failure, which is exactly why lostRequired exists beside
  // it. A texture whose BASE colour was evicted, which is a visible failure
  // rather than a quality reduction, and must be reported rather than absorbed.
  std::vector<std::string> lostRequired;
  // One entry per request that is absent WITHOUT being evicted: a map no held
  // level samples, or a map whose own authored strength is zero. Neither costs
  // budget and neither is a failure, so they are counted and named instead of
  // being carried as non-resident textures a renderer might try to release.
  std::vector<std::string> trimmed;
};

// One material the terrain actually uses, with the role that asked for it.
// A caller holding a TerrainMaterialSet pairs role names with material ids
// here; a caller holding only ids gets the identity-flavoured overload below.
struct TerrainResidencyMaterial {
  std::string roleName;
  assets::TerrainMaterialAsset material;
};

// Builds the plan for the roles the terrain actually uses at the levels it
// actually holds. Pure: no GPU, no renderer, no clock, no RNG, and no ordering
// that depends on a hash. A renderer executes the plan; this stage never does.
//
// Throws std::invalid_argument rather than returning an empty plan, because an
// empty plan is indistinguishable from a terrain that legitimately shades flat:
// an empty material list means nobody decided what the surface is made of, and
// a zero-byte or zero-texture budget means every texture would be evicted,
// which is a configuration mistake wearing the costume of a decision. Materials
// whose maps are all inactive are NOT an error and contribute no texture,
// because a flat tinted surface is a legitimate authored material.
[[nodiscard]] TerrainResidencyPlan
planTerrainTextureResidency(const std::vector<TerrainResidencyMaterial> &,
                            const TerrainResidencyBudget &);

// The identity-flavoured overload: loads each material through the registry and
// labels its requests with the material id itself, because a caller that has
// not resolved a TerrainMaterialSet has no role vocabulary to name them with.
// Duplicate (id, role) pairs are collapsed, and an id that fails to load
// propagates the loader's own actionable message.
[[nodiscard]] TerrainResidencyPlan
planTerrainTextureResidency(const std::vector<std::string> &materialAssetIds,
                            const demi::AssetRegistry &,
                            const TerrainResidencyBudget &);

// The levels a quality tier holds, nearest first. THIS is the whole mechanism
// by which a tier is not a bool that switches everything off: a tier holds
// fewer LOD levels, a level samples only the maps terrainSlotsForLod keeps, and
// a map no held level samples is not resident at all rather than "cheapened".
[[nodiscard]] std::vector<TerrainLod>
terrainResidencyHeldLevels(TerrainQuality quality);

// Which slots a material still samples at a level: full detail near, and
// progressively fewer map slots further away. `slot` appears in the result only
// if it survives that level; the result is in canonical slot order, and shrinks
// monotonically as the level coarsens. TerrainLod::Off returns nothing, because
// no surface is drawn there to sample anything.
//
// Every level that draws a surface keeps the base colour. A coarse chunk that
// lost its base colour would wear the palette tint with no texture at all,
// which is the failure this module reports rather than accepts.
[[nodiscard]] std::vector<assets::TerrainMaterialMapSlot>
    terrainSlotsForLod(assets::TerrainMaterialMapSlot, TerrainLod);

// THE EVICTION ORDER, as a rank: 0 is dropped first. Exposed so the documented
// order is testable rather than a comment nobody can check.
//
//   0 detail             highest frequency, invisible past Half, and purely a
//                        break-up layer over an already correct surface
//   1 height             shading-only relief; it is already forbidden from
//                        moving vertices, so dropping it loses no geometry
//   2 emissive           a distant glow is below what a coarse mesh resolves,
//                        and terrain rarely authors one
//   3 base_color         the largest VISIBLE loss, but TerrainMaterialAsset
//                        carries a linear base_color tint and the palette
//                        carries the role colour, so an untextured surface is
//                        still the right hue. It ranks below detail/height/
//                        emissive because those three have no authored fallback
//                        at all. Dropping it is the first loss an author must
//                        be shown, which is why it lands in lostRequired
//   4 normal             no scalar fallback, and a cliff without one is
//                        unrecognisable, so it outlives the base colour map
//   5 ambient_occlusion  missing occlusion reads as slightly flatter contact
//                        shading, which the role shading already imitates; it
//                        outlives the normal map because it changes no cue that
//                        identifies the surface
//   6 roughness          TerrainMaterialAsset::roughness substitutes exactly
//   7 metallic           substitutes exactly too, is 0 (dielectric) for nearly
//                        all terrain, and is the least visible PBR slot at any
//                        distance, so it outlives everything
[[nodiscard]] std::size_t
    terrainResidencyEvictionRank(assets::TerrainMaterialMapSlot);

// The nominal format the byte estimate assumes for a slot. Exported so the
// estimate, its documentation and its tests quote one table.
struct TerrainResidencyNominalFormat {
  // Square edge in texels.
  std::size_t edge = 0;
  // Always 4: every map is stored as RGBA8 whatever the map itself reads, which
  // is what a renderer's texture upload actually asks for.
  std::size_t channels = 0;
  bool colourBearing = false;
};
[[nodiscard]] TerrainResidencyNominalFormat
    terrainResidencyNominalFormat(assets::TerrainMaterialMapSlot);

// nominal_edge^2 * channels * (4/3 mip chain), rounded up to 256 KiB. The
// single place the estimate is computed.
[[nodiscard]] std::size_t
    terrainResidencyEstimateBytes(assets::TerrainMaterialMapSlot);

} // namespace demi::runtime