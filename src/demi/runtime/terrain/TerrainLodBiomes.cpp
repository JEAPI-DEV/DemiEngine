#include "demi/runtime/terrain/TerrainLodBiomes.h"

#include "demi/runtime/terrain/TerrainGenerator.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

namespace demi::runtime {
namespace {

// A chunk dimension reduced to one level's sample positions, mirroring the
// lattice buildTerrainLodMesh builds internally: regular entries every `step`
// cells, plus the chunk's exact far edge when that edge is not a whole number of
// strides away. The two have to agree entry for entry, because this partition
// reads the flat mesh's vertices and indices and attaches a biome to each one; a
// different lattice would attribute vertices to cells they are not in.
struct Lattice {
  int count = 0;
  int step = 1;
  int uniform = 1; // entries at 0, step, 2 * step, ... below `count`

  std::size_t size() const {
    if (count <= 0)
      return 0;
    return std::size_t(uniform) +
           (std::size_t(uniform - 1) * std::size_t(step) == std::size_t(count)
                ? 0
                : 1);
  }
  int sample(std::size_t entry) const {
    return entry < std::size_t(uniform) ? int(entry) * step : count;
  }
};

Lattice makeLattice(int count, int step) {
  Lattice lattice;
  lattice.count = std::max(0, count);
  lattice.step = std::max(1, step);
  lattice.uniform = lattice.count / lattice.step + 1;
  return lattice;
}

bool chunkFits(const HeightField &field, const TerrainChunk &chunk) {
  return chunk.cellsX > 0 && chunk.cellsZ > 0 && chunk.firstCellX >= 0 &&
         chunk.firstCellZ >= 0 &&
         chunk.firstCellX + chunk.cellsX <= field.cellsX &&
         chunk.firstCellZ + chunk.cellsZ <= field.cellsZ;
}

// The biome that owns a rectangle of samples: the one that holds the most of
// them, ties going to the LOWER index.
//
// The tie-break is what makes a boundary cell land somewhere on every run. The
// full-resolution chunk builder labels a triangle by the majority of its three
// corners (`b == c ? b : a`), which is the same rule at a smaller scale: here the
// majority is taken over a whole reduced cell, because a reduced cell is the
// unit a vertex belongs to and it has to belong to exactly one biome. Labels are
// visited in index order and a running best is only replaced on a strictly
// higher count, so an exact tie keeps the lower label rather than depending on
// which one the sample walk happened to reach first.
//
// Returns nothing when the rectangle holds no label the field actually names,
// which is a corrupt field rather than a boundary.
std::optional<std::size_t> majorityLabel(const HeightField &field, int x0, int x1,
                                         int z0, int z1,
                                         std::vector<std::size_t> &counts) {
  counts.assign(field.biomeIds.size(), 0);
  for (int z = z0; z <= z1; ++z)
    for (int x = x0; x <= x1; ++x) {
      const std::size_t label = field.biomeIndices.at(field.index(x, z));
      if (label < counts.size())
        ++counts[label];
    }
  std::optional<std::size_t> winner;
  std::size_t best = 0;
  for (std::size_t label = 0; label < counts.size(); ++label) {
    if (counts[label] == 0)
      continue;
    if (!winner || counts[label] > counts[best]) {
      best = label;
      winner = label;
    }
  }
  return winner;
}

constexpr std::size_t kUnmapped = ~std::size_t(0);

} // namespace

std::size_t terrainLodBiomeForCell(const HeightField &field, int cellX, int cellZ,
                                   int step) {
  if (step <= 0)
    throw std::invalid_argument("A reduced cell needs a positive cell stride, not " +
                                std::to_string(step));
  if (cellX < 0 || cellZ < 0 || cellX > field.cellsX || cellZ > field.cellsZ)
    throw std::invalid_argument("Terrain cell " + std::to_string(cellX) + "," +
                                std::to_string(cellZ) +
                                " is outside a field of " +
                                std::to_string(field.cellsX) + "x" +
                                std::to_string(field.cellsZ) + " cells");
  if (field.biomeIds.empty())
    throw std::invalid_argument(
        "Terrain field names no biome, so it has no per-biome surface to read");
  // Reduced cells are anchored to the field's own grid at multiples of the
  // stride, so the same cell resolves the same way whatever chunk asks about it.
  const int firstX = (cellX / step) * step;
  const int firstZ = (cellZ / step) * step;
  std::vector<std::size_t> counts;
  const auto label = majorityLabel(field, firstX, std::min(firstX + step, field.cellsX),
                                   firstZ, std::min(firstZ + step, field.cellsZ),
                                   counts);
  if (!label)
    throw std::invalid_argument(
        "Every sample in the reduced cell at " + std::to_string(firstX) + "," +
        std::to_string(firstZ) + " carries a biome label the field does not name");
  return *label;
}

std::optional<TerrainLodBiomeMesh>
buildTerrainLodBiomeMesh(const HeightField &field, const TerrainChunk &chunk,
                         TerrainLod level, float skirtDepth) {
  const int ordinal = terrainLodIndex(level);
  if (ordinal < 0 || ordinal >= terrainLodLevelCount)
    throw std::invalid_argument("Terrain LOD level " + std::to_string(ordinal) +
                                " is not one of the " +
                                std::to_string(terrainLodLevelCount) +
                                " terrain detail levels");
  // Off is a level with a meaning of its own rather than a bad value: it draws
  // nothing, which is the same answer the flat mesh gives it.
  if (level == TerrainLod::Off)
    return std::nullopt;
  const int step = terrainLodStep(level);
  if (step <= 0)
    throw std::invalid_argument("Terrain LOD level " +
                                std::string(terrainLodName(level)) +
                                " has no cell stride");
  if (!chunkFits(field, chunk))
    throw std::invalid_argument(
        "Terrain chunk at " + std::to_string(chunk.firstCellX) + "," +
        std::to_string(chunk.firstCellZ) + " sized " +
        std::to_string(chunk.cellsX) + "x" + std::to_string(chunk.cellsZ) +
        " does not fit inside a field of " + std::to_string(field.cellsX) + "x" +
        std::to_string(field.cellsZ) + " cells");
  if (field.biomeIds.empty())
    throw std::invalid_argument(
        "Terrain field names no biome, so it has no per-biome surface to partition");
  if (field.size.x <= 0.F || field.size.y <= 0.F)
    throw std::invalid_argument(
        "Terrain field has no extent, so it has no world-scale UVs");
  // The samplers read through HeightField::index, which throws rather than wraps;
  // a field whose buffers do not cover its own grid is refused here instead of
  // inside the walk.
  const std::size_t samples = field.index(field.cellsX, field.cellsZ) + 1;
  if (field.heights.size() < samples || field.biomeIndices.size() < samples)
    throw std::invalid_argument(
        "Terrain field buffers do not cover its own grid, so its biomes "
        "cannot be read");

  const auto flat = buildTerrainLodMesh(field, chunk, level, skirtDepth);
  if (!flat)
    return std::nullopt;

  const Lattice latticeX = makeLattice(chunk.cellsX, step);
  const Lattice latticeZ = makeLattice(chunk.cellsZ, step);
  const std::size_t columns = latticeX.size();
  const std::size_t rows = latticeZ.size();
  const std::size_t surfaceVertices = columns * rows;
  const std::size_t cellColumns = columns > 0 ? columns - 1 : 0;
  const std::size_t cellRows = rows > 0 ? rows - 1 : 0;

  TerrainLodBiomeMesh mesh;
  mesh.sourceCells = flat->sourceCells;
  // A lattice with no interior has no cell and therefore no triangle at all, and
  // the flat mesh has no skirt either. There is nothing to partition, and an
  // empty mesh is the honest answer rather than a group with nothing in it.
  if (cellColumns == 0 || cellRows == 0)
    return mesh;

  // One label per reduced cell, resolved at most once. Both the triangle walk and
  // the skirt ownership ask for the same rectangle, so this is also what keeps
  // the whole function O(cells in the chunk).
  std::vector<std::optional<std::size_t>> cellBiome(cellColumns * cellRows);
  std::vector<char> resolved(cellColumns * cellRows, 0);
  std::vector<std::size_t> counts;
  const auto cellsInCell = [&](std::size_t cellColumn, std::size_t cellRow) {
    return std::size_t(latticeX.sample(cellColumn + 1) - latticeX.sample(cellColumn)) *
           std::size_t(latticeZ.sample(cellRow + 1) - latticeZ.sample(cellRow));
  };
  const auto biomeOfCell = [&](std::size_t cellColumn,
                               std::size_t cellRow) -> std::optional<std::size_t> {
    const std::size_t slot = cellRow * cellColumns + cellColumn;
    if (!resolved[slot]) {
      resolved[slot] = 1;
      // The sample rectangle reaches one stride past its corner, which is where
      // the neighbouring reduced cell's samples begin, and it is clipped to the
      // chunk so a ragged trailing hop cannot read past the chunk's own edge.
      cellBiome[slot] = majorityLabel(
          field, chunk.firstCellX + latticeX.sample(cellColumn),
          chunk.firstCellX + latticeX.sample(cellColumn + 1),
          chunk.firstCellZ + latticeZ.sample(cellRow),
          chunk.firstCellZ + latticeZ.sample(cellRow + 1), counts);
    }
    return cellBiome[slot];
  };

  // Groups are created for the biomes that actually received cells, walked in
  // biomeIds order, so the draw order follows the recipe rather than the cells.
  // Each cell's vote is counted here, once per cell, which is what makes the
  // groups' source cells add up to the chunk's whole area.
  std::vector<char> used(field.biomeIds.size(), 0);
  std::vector<std::size_t> cellsByLabel(field.biomeIds.size(), 0);
  for (std::size_t cellRow = 0; cellRow < cellRows; ++cellRow)
    for (std::size_t cellColumn = 0; cellColumn < cellColumns; ++cellColumn) {
      const auto label = biomeOfCell(cellColumn, cellRow);
      // A cell whose biome has no id and tint cannot be drawn by any group, so it
      // is counted rather than quietly dropped.
      if (!label) {
        mesh.skippedBiomes += cellsInCell(cellColumn, cellRow);
        continue;
      }
      used[*label] = 1;
      cellsByLabel[*label] += cellsInCell(cellColumn, cellRow);
    }

  std::vector<std::size_t> groupOf(field.biomeIds.size(), kUnmapped);
  std::vector<std::size_t> groupCells;
  groupCells.reserve(field.biomeIds.size());
  for (std::size_t label = 0; label < field.biomeIds.size(); ++label) {
    if (!used[label])
      continue;
    groupOf[label] = mesh.groups.size();
    groupCells.push_back(cellsByLabel[label]);
    TerrainLodBiomeGroup group;
    group.biomeId = field.biomeIds[label];
    // The tint travels with the group so the shader needs no biome lookup, and
    // it is only a valid color when the field's labels and colors agree.
    if (label < field.biomeColors.size())
      group.tint = field.biomeColors[label];
    mesh.groups.push_back(std::move(group));
  }
  // Per-group vertex maps, so a boundary vertex shared by two biomes is emitted
  // once per group and each group stays self-contained and indexable on its own.
  std::vector<std::vector<std::size_t>> localOf;
  localOf.reserve(mesh.groups.size());
  for (std::size_t group = 0; group < mesh.groups.size(); ++group)
    localOf.emplace_back(flat->vertices.size(), kUnmapped);

  const auto appendVertex = [&](std::size_t group, std::size_t flatIndex) {
    std::size_t &local = localOf[group][flatIndex];
    if (local == kUnmapped) {
      local = mesh.groups[group].vertices.size();
      const Vec3 vertex = flat->vertices.at(flatIndex);
      mesh.groups[group].vertices.push_back(vertex);
      mesh.groups[group].normals.push_back(flat->normals.at(flatIndex));
      // World position over the field's own extent, which is the convention the
      // full-resolution chunk builder uses: the divisor is the field, not the
      // group and not the level, so a reduced chunk's texel density matches the
      // full-resolution one exactly instead of stretching with the stride.
      mesh.groups[group].uvs.push_back(
          {vertex.x / field.size.x, vertex.z / field.size.y});
    }
    return local;
  };

  // The flat mesh emits its surface triangles first and its skirt triangles
  // after, which is what lets the skirt be identified without a second pass.
  const std::size_t surfaceTriangles = cellColumns * cellRows * 2;
  const std::size_t triangles = flat->indices.size() / 3;
  for (std::size_t triangle = 0; triangle < triangles; ++triangle) {
    const std::size_t base = triangle * 3;
    // Which reduced cell a triangle belongs to. A surface triangle's three
    // corners sit in one cell's four corners, so the lowest row and column of its
    // corners ARE that cell. A skirt triangle's third corner is a skirt bottom
    // rather than a surface vertex, and its second triangle names a different
    // ring entry, so a skirt triangle takes its FIRST surface corner alone: that
    // is the segment's own top corner, and both of a segment's triangles then
    // resolve to the same cell instead of splitting the segment's wall in two.
    const bool skirt = triangle >= surfaceTriangles;
    std::size_t cellColumn = 0;
    std::size_t cellRow = 0;
    bool named = false;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const std::size_t flatIndex = flat->indices.at(base + corner);
      if (flatIndex >= surfaceVertices)
        continue;
      const std::size_t row = flatIndex / columns;
      const std::size_t column = flatIndex % columns;
      if (!named) {
        cellRow = row;
        cellColumn = column;
        named = true;
      } else if (!skirt) {
        cellRow = std::min(cellRow, row);
        cellColumn = std::min(cellColumn, column);
      }
    }
    if (!named)
      continue;
    // The far edge entry belongs to the last cell, not to a cell past it.
    cellColumn = std::min(cellColumn, cellColumns - 1);
    cellRow = std::min(cellRow, cellRows - 1);

    const auto label = biomeOfCell(cellColumn, cellRow);
    if (!label)
      continue;
    const std::size_t group = groupOf[*label];
    if (group == kUnmapped)
      continue;
    for (std::size_t corner = 0; corner < 3; ++corner)
      mesh.groups[group].indices.push_back(
          appendVertex(group, flat->indices.at(base + corner)));
    // A skirt segment hangs one bottom vertex, reached by both of its triangles,
    // so it is owned exactly once. Its bottom is also a corner of the previous
    // segment's triangles and therefore appears in that group's vertices too,
    // which is why ownership cannot be read back off the vertex arrays.
    if (skirt && (triangle - surfaceTriangles) % 2 == 0)
      ++mesh.groups[group].skirtDepths;
  }

  for (std::size_t group = 0; group < mesh.groups.size(); ++group)
    mesh.groups[group].sourceCells = groupCells[group];
  return mesh;
}
} // namespace demi::runtime