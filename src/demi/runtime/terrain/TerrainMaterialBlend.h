#pragma once

#include "demi/runtime/terrain/TerrainGeneration.h"
#include "demi/runtime/terrain/TerrainMaterialLayers.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

// How many material layers a vertex can blend. Four is enough to cover a
// rock/soil/grass/snow junction without a sorting cost, and it is a CAP and not
// a promise: a neighbourhood that wants a fifth role has it folded into the
// fourth, and TerrainBlendResult::foldedRoles says so rather than the role
// disappearing quietly.
inline constexpr std::size_t terrainBlendLayerCount = 4;

// One role's share of a vertex, in the descriptive form a renderer, an editor
// inspector or a diagnostics dump wants. TerrainBlendVertex stores the compact
// per-slot form instead, because a vertex cannot afford to carry its own
// strings; this is the readable projection of that.
struct TerrainBlendWeight {
  // The material role this weight belongs to.
  std::string roleName;
  // 0..1, and the weights of one vertex sum to 1.
  float weight = 0;
  std::string materialAssetId;
};

// One blend sample on the lattice, addressed in the field's own sample order:
// vertex index z * result.latticeX + x.
struct TerrainBlendVertex {
  // The field sample this lattice sample is nearest to, so a consumer can look
  // up the discrete decision that informed it. The lattice sits at the centre
  // of the span it covers rather than on a field sample, so a blend is
  // interpolated across a cell instead of being pinned to one corner of it.
  std::size_t cell = 0;
  // One weight per TerrainBlendSlots::roles entry, in slot order. A slot the
  // vertex does not use is 0. Unused trailing slots are 0 too, so the four
  // weights always sum to 1.
  std::array<float, terrainBlendLayerCount> weights{};
  // Recorded from TerrainBlendSettings::triplanarCliffs: above the cliff slope
  // the renderer should sample this material across all three axes by world
  // position instead of projecting it down a near-vertical face. Computed here
  // so the renderer never re-derives the slope threshold to answer that.
  bool triplanar = false;
  // True where blending does not apply and exactly one weight is 1: below the
  // water level, or where the neighbourhood named no role at all. The renderer
  // reads this as "do not cross-fade here" instead of inferring it from a weight
  // vector that happens to have one entry set.
  bool frozen = false;
};

// The roles every vertex of one mesh blends, resolved once for the whole field or
// chunk. The same set everywhere is what makes two vertices' weights comparable:
// a rock weight of 0.3 means 0.3 of THIS field's most significant rock, at every
// vertex, rather than 0.3 of whichever rock happened to be significant near it.
struct TerrainBlendSlots {
  // The role each slot refers to, in slot order. Stable for a whole mesh, so a
  // vertex carries four floats and never its own strings.
  std::vector<std::string> roles;
  // The asset:// bound to each role, parallel to roles. Empty when the field's
  // material set does not bind that role, which is the same gap
  // terrainMaterialCoverage reports rather than something to paper over here.
  std::vector<std::string> materialAssetIds;

  [[nodiscard]] std::size_t find(std::string_view role) const noexcept;
};

// Resolves the slot set for a whole field or chunk from the discrete decisions
// alone, ranked by how much of the field each role was chosen for (so a chunk
// whose surface is mostly rock keeps rock first even at a small absolute count)
// and then by role name, so two runs and two orderings agree exactly.
//
// `samples` is how many blend lattice samples the caller is about to produce.
// A role chosen for fewer cells than one sample's share of the field is not a
// candidate: on a 256x256 field a single cell is below the lattice's own
// resolution, so admitting it would let one cell steal a slot from the four
// surfaces that actually shape the terrain. Pass 0 when the lattice has not been
// sized yet and every present role is a candidate.
//
// Roles outside the four slots are not dropped: blendTerrainMaterials folds their
// weight into the last slot and reports them in TerrainBlendResult::foldedRoles.
[[nodiscard]] TerrainBlendSlots
resolveTerrainBlendSlots(const std::vector<TerrainMaterialDecision> &decisions,
                         std::size_t samples);

struct TerrainBlendSettings {
  // The sample lattice is this many cells per blend vertex, in each axis. 1
  // means one vertex per cell; 0.5 means four per cell. Samples per cell is
  // floor(1 / resolution), rounded DOWN so the lattice is never finer than the
  // setting asks for.
  float resolution = 1.F;
  // Gaussian sigma in CELLS over which two roles cross-fade. Below the sample
  // spacing the blend is effectively discontinuous, which is the point at which
  // the cell-sized steps reappear.
  float transitionWidth = 1.5F;
  // Slope in degrees above which rock takes over regardless of the discrete
  // decision, so a cliff face is never half grass. The override begins
  // 12 degrees below this, so it is a ramp and not a step.
  float cliffSlope = 55.F;
  // Records TerrainBlendVertex::triplanar where the cliff override takes hold.
  // Selecting the material is this stage's business; sampling it across three
  // axes is the renderer's, and it reads the flag rather than re-deriving the
  // threshold.
  bool triplanarCliffs = true;
  // Preview halves the lattice in each axis, so an author iterating on shape
  // pays a quarter of the blend cost. It is carried rather than decided here:
  // the residency and detail passes read the same tier from their own settings,
  // and a preview that blended differently from the field it previews would lie
  // about it.
  TerrainQuality quality = TerrainQuality::Standard;

  // World units. Below the line a sample is the bed, not a surface; 0 for
  // snowLine derives it from the field's own dry height distribution, and
  // infinity when the field has no dry relief, which is the honest "this terrain
  // has no snow line" rather than one at height zero.
  float snowLine = 0.F;
  // Water level in world units. A sample at or below it is submerged and its bed
  // is the Underwater role, alone: a submerged face is not half rock and half
  // grass, it is water.
  float waterLevel = 0.F;

  // Copies the two world-unit thresholds out of the discrete layer's settings,
  // so the blend and the decision cannot mean two different water levels or two
  // different snow lines. Both stages defaulting to 0 is not agreement; this is.
  [[nodiscard]] static TerrainBlendSettings
  withMaterialLayers(TerrainBlendSettings settings,
                     const TerrainMaterialLayerSettings &layers);
};

struct TerrainBlendResult {
  // One entry per lattice sample, in z * latticeX + x order.
  std::vector<TerrainBlendVertex> vertices;
  // The slots every vertex above refers to. Read together, not per vertex.
  TerrainBlendSlots slots;
  // The field's cell counts. The lattice is laid out over them, so a consumer
  // mapping a vertex position back onto the lattice needs them and cannot infer
  // them from the lattice alone.
  std::size_t cellsX = 0, cellsZ = 0;
  // Lattice size in SAMPLES, not in cells:
  //
  //   subdivisions = max(1, floor(1 / resolution))     // samples per cell
  //   latticeX     = max(1, ceil(cellsX * subdivisions / previewDivisor))
  //
  // with previewDivisor 2 in Preview and 1 otherwise. The floor is what keeps
  // the lattice from being finer than the setting asks for, and the divisor is
  // what Preview halves the lattice with: an integer subdivision could not,
  // because one sample per cell is already the coarsest lattice this stage can
  // express. Measured on a 256x256-cell field: 65,536 vertices at the default
  // resolution, 262,144 at resolution 0.5 (exactly four times as many, because
  // both axes double), 16,384 in Preview and 65,536 in Preview at resolution
  // 0.5.
  std::size_t latticeX = 0, latticeZ = 0;
  // True when every vertex's weight landed on a single role, which is what a
  // field whose decisions never disagree looks like: the blend is a no-op and
  // says so rather than pretending to have produced a smooth surface.
  bool degenerate = false;

  // Roles the field wanted that the four-slot cap did not keep, sorted by name.
  // Their weight was folded into the weakest retained slot at every vertex that
  // had any, so nothing disappears; this list is how a caller learns which role
  // is being approximated.
  std::vector<std::string> foldedRoles;
  // Lattice samples where a folded role carried at least
  // TerrainBlendFoldedReportThreshold of the vertex's weight, and the largest
  // such share seen. Zero means the cap cost nothing at any vertex.
  std::size_t foldedVertices = 0;
  float maxFoldedWeight = 0.F;
  // Lattice samples where blending does not apply (see
  // TerrainBlendVertex::frozen).
  std::size_t frozenVertices = 0;
};

// Below this share of a vertex, a folded role is reported as folded but is not
// counted as a vertex the cap actually changed. It is small enough to be float
// noise at the far end of a Gaussian tail and large enough to be visible.
inline constexpr float terrainBlendFoldedReportThreshold = 0.01F;

// Blends the discrete decisions into per-sample weights driven by the masks.
//
// The discrete decision says WHICH role wins a cell; this says how much of each
// neighbouring role survives at a point finer than a cell. It never reads the
// discrete decision's score: the score is a comparison between roles at one cell,
// and reusing it would reproduce the cell-sized step this stage exists to remove.
// What survives comes from a per-role affinity over slope, curvature, altitude
// against the snow line, distance to water, moisture and flow, multiplied by the
// role's presence in a Gaussian neighbourhood of the discrete decisions. The
// decision therefore gates which roles may appear here, and the masks decide how
// much of each.
//
// Every returned vertex's four weights sum to 1 and at least one is non-zero: a
// point where no affinity survives falls back to the neighbourhood's presence,
// and a point with no presence at all is frozen on the nearest cell's own
// decision. A submerged sample is frozen on Underwater and can never blend a
// surface role with the bed.
//
// Cost: one separable Gaussian blur of the per-role decision indicators, O(cells
// * radius) with radius = ceil(3 * transitionWidth) clamped to [1, 8], then
// O(lattice samples * roles) with roles bounded by the vocabulary. No RNG, no
// running state and no dependence on the order samples are visited in, so two
// runs over equal inputs agree byte for byte.
//
// Throws std::invalid_argument when the decisions are not one per field sample,
// when no role was chosen anywhere (a weight lattice with no role cannot be
// rendered), and for a non-finite or non-positive resolution or transition
// width; std::out_of_range for a field with no samples.
[[nodiscard]] TerrainBlendResult
blendTerrainMaterials(const HeightField &field, const TerrainMasks &masks,
                      const std::vector<TerrainMaterialDecision> &decisions,
                      const TerrainBlendSettings &settings = {});

// The readable form of one vertex: its non-zero weights as role/material pairs,
// in slot order. Diagnostics, an inspector and a cook report read this; the mesh
// path reads TerrainBlendVertex::weights directly.
[[nodiscard]] std::vector<TerrainBlendWeight>
terrainBlendVertexWeights(const TerrainBlendResult &result,
                          std::size_t vertex);

// Bilinearly samples the lattice at a position in cell units over
// [0, cellsX] x [0, cellsZ], and renormalises so the four weights still sum to
// 1 off-lattice. This is what a mesh vertex or a refined LOD ring reads when the
// lattice is coarser than the geometry: the blend is a continuous field sampled
// on a grid, not a per-vertex attribute that only exists where the grid landed.
//
// Throws std::out_of_range when the result has no vertices.
[[nodiscard]] std::array<float, terrainBlendLayerCount>
sampleTerrainBlendWeights(const TerrainBlendResult &result, float cellX,
                          float cellZ);

} // namespace demi::runtime