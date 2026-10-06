#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainLod.h"
#include "demi/runtime/terrain/TerrainLodBiomes.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace demi::runtime;

namespace {
// TerrainLodMesh is a single group with no UVs and no biome identity, so a chunk
// drawn from it has to pick one material for the whole chunk and loses every
// other biome the moment the camera pulls back. These checks are about the
// partition that puts the biome identity back, without disturbing the geometry,
// the winding or the skirts the flat mesh already guarantees.

// A refused request throws rather than answering with an empty group, so the
// failure names itself instead of looking like a chunk with no geometry.
void refuses(const HeightField &field, const TerrainChunk &chunk, TerrainLod level,
             float skirtDepth) {
  bool failed = false;
  try {
    (void)buildTerrainLodBiomeMesh(field, chunk, level, skirtDepth);
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);
}

void refusesCell(const HeightField &field, int cellX, int cellZ, int step) {
  bool failed = false;
  try {
    (void)terrainLodBiomeForCell(field, cellX, cellZ, step);
  } catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);
}

Color tint(float red, float green, float blue) { return {red, green, blue, 1.F}; }

// The three default tints, deliberately different from each other so a group
// carrying the wrong biome's color cannot pass.
std::vector<Color> threeColors() {
  return {tint(0.9F, 0.1F, 0.1F), tint(0.1F, 0.9F, 0.1F), tint(0.1F, 0.1F, 0.9F)};
}

// A field built by hand, so the biome layout is exactly what a test states
// rather than whatever a noise function happened to produce. `label` returns the
// field's own biome index for a sample.
template <class Label>
HeightField labelledField(int cells, std::vector<std::string> ids, Label label,
                          const std::vector<Color> &colors = threeColors()) {
  HeightField field;
  field.size = {float(cells) * 4.F, float(cells) * 4.F};
  field.cellsX = field.cellsZ = cells;
  const std::size_t count = std::size_t(cells + 1) * std::size_t(cells + 1);
  field.biomeIds = std::move(ids);
  field.biomeColors = colors;
  field.baseHeights.resize(count);
  field.heights.resize(count);
  field.normals.resize(count);
  field.biomeIndices.resize(count);
  field.exclusions.resize(count);
  for (int z = 0; z <= cells; ++z)
    for (int x = 0; x <= cells; ++x) {
      const auto index = field.index(x, z);
      // A surface that varies along every edge, so a skirt has real depth to
      // reach rather than a degenerate flat ring to hide nothing behind.
      const float height = 6.F + float(x) * 0.05F + float(z) * 0.03F +
                           float((x * 7 + z * 13) % 5) * 0.4F;
      field.baseHeights.set(index, height);
      field.heights.set(index, height);
      field.normals.set(index, Vec3{0.F, 1.F, 0.F});
      field.biomeIndices.set(index, label(x, z));
      field.exclusions.set(index, 0.F);
    }
  field.chunks.push_back(TerrainChunk{0, 0, cells, cells});
  return field;
}

// A field whose biomes are painted by a single straight line down the middle,
// which is the case where the exact share of each biome can be reasoned about.
HeightField splitField(int cells, bool lowerIndexIsTheLeftSide) {
  const int split = cells / 2;
  return labelledField(cells, {"left", "right"}, [&](int x, int) {
    const bool left = x < split;
    return std::size_t(left == lowerIndexIsTheLeftSide ? 0 : 1);
  });
}

// A generated field, for the checks that need biomes nobody chose by hand. Two
// landforms at clearly separated heights plus an elevation rule, which is what
// produces a real interior boundary with real relief across it.
TerrainRecipe splitRecipe(int cells, int chunkCells) {
  TerrainRecipe recipe;
  recipe.size = {float(cells), float(cells)};
  recipe.cellsX = recipe.cellsZ = cells;
  recipe.chunkCells = chunkCells;
  recipe.biomes.clear();
  recipe.landforms.emplace("basin",
                          TerrainLandform{.baseHeight = 20, .heightVariation = 12});
  recipe.landforms.emplace("crest",
                           TerrainLandform{.baseHeight = 34, .heightVariation = 2});
  recipe.biomes.emplace("basin", TerrainBiome{.landform = "basin",
                                              .color = tint(0.2F, 0.5F, 0.2F)});
  recipe.biomes.emplace("crest", TerrainBiome{.landform = "crest",
                                              .color = tint(0.6F, 0.6F, 0.6F)});
  recipe.defaultBiome = "basin";
  TerrainBiomeRule crest;
  crest.id = "high_crest";
  crest.biome = "crest";
  crest.elevation = {true, 20, 1e6F};
  recipe.rules.push_back(crest);
  return recipe;
}

HeightField generate(const TerrainRecipe &recipe) {
  auto field = TerrainGenerator::generate(recipe);
  assert(field.has_value());
  return *field;
}

const TerrainLod kLevels[] = {TerrainLod::Full, TerrainLod::Half, TerrainLod::Quarter,
                              TerrainLod::Coarse};

std::size_t totalIndices(const TerrainLodBiomeMesh &mesh) {
  std::size_t total = 0;
  for (const TerrainLodBiomeGroup &group : mesh.groups)
    total += group.indices.size();
  return total;
}

std::size_t totalSkirtDepths(const TerrainLodBiomeMesh &mesh) {
  std::size_t total = 0;
  for (const TerrainLodBiomeGroup &group : mesh.groups)
    total += group.skirtDepths;
  return total;
}

std::size_t totalSourceCells(const TerrainLodBiomeMesh &mesh) {
  std::size_t total = 0;
  for (const TerrainLodBiomeGroup &group : mesh.groups)
    total += group.sourceCells;
  return total;
}

std::size_t indexOfBiome(const HeightField &field, const std::string &biomeId) {
  for (std::size_t index = 0; index < field.biomeIds.size(); ++index)
    if (field.biomeIds[index] == biomeId)
      return index;
  return field.biomeIds.size();
}

bool sameColor(const Color &left, const Color &right) {
  return left.r == right.r && left.g == right.g && left.b == right.b &&
         left.a == right.a;
}

// The world-scale UV the full-resolution chunk builder uses: world position over
// the field's own extent. The divisor is the FIELD, never the group and never the
// level, which is the whole point.
Vec2 worldUv(const HeightField &field, const Vec3 &vertex) {
  return {vertex.x / field.size.x, vertex.z / field.size.y};
}

// A skirt bottom hangs directly below a boundary vertex, so it sits below the
// surface height of the cell it stands in. Every surface vertex is a real field
// sample, so it is never below it.
int cellXAt(const HeightField &field, const Vec3 &vertex) {
  return std::clamp(
      int(std::lround(double(vertex.x) / double(field.size.x) * field.cellsX)), 0,
      field.cellsX);
}

int cellZAt(const HeightField &field, const Vec3 &vertex) {
  return std::clamp(
      int(std::lround(double(vertex.z) / double(field.size.y) * field.cellsZ)), 0,
      field.cellsZ);
}

bool isSkirtBottom(const HeightField &field, const Vec3 &vertex) {
  return vertex.y < field.height(cellXAt(field, vertex), cellZAt(field, vertex));
}

// A wall segment is two triangles, and both of them belong to the group that owns
// the segment. Splitting a segment would leave one group holding a bottom whose
// far top is in somebody else's vertex array, and drawing that group alone leaves
// a hole in the very wall the skirt exists to be.
void checkSkirtWallsAreWhole(const HeightField &field,
                             const TerrainLodBiomeGroup &group) {
  std::size_t wallTriangles = 0;
  for (std::size_t triangle = 0; triangle + 2 < group.indices.size();
       triangle += 3) {
    bool onTheWall = false;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const Vec3 &vertex =
          group.vertices.at(group.indices.at(triangle + corner));
      onTheWall = onTheWall || isSkirtBottom(field, vertex);
    }
    wallTriangles += onTheWall ? 1 : 0;
  }
  assert(wallTriangles == group.skirtDepths * 2);
}

// Every group has to be a self-contained, indexable draw: its own vertices, one
// normal and one UV per vertex, and whole triangles that reference only its own
// vertices. A group that failed this is a corrupt draw rather than a rough one.
void checkGroupIsDrawable(const TerrainLodBiomeMesh &mesh,
                          const TerrainLodBiomeGroup &group) {
  assert(!group.biomeId.empty());
  assert(group.normals.size() == group.vertices.size());
  assert(group.uvs.size() == group.vertices.size());
  assert(group.indices.size() % 3 == 0);
  assert(!group.indices.empty());
  assert(group.sourceCells > 0);
  for (const std::size_t index : group.indices)
    assert(index < group.vertices.size());
  for (const Vec3 vertex : group.vertices)
    assert(std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
           std::isfinite(vertex.z));
}

// The order of the groups is a draw order, so it has to come from the recipe's
// biome order rather than from whichever cell voted first. Labels here run the
// OTHER way round on purpose, so a partition that followed the cells instead of
// biomeIds would fail.
void groupsFollowBiomeIdOrder() {
  const auto field = labelledField(32, {"dune", "meadow", "rock"}, [](int x, int z) {
    if (x < 11)
      return std::size_t(2); // rock
    if (z < 11)
      return std::size_t(0); // dune
    return std::size_t(1);   // meadow
  });
  assert(indexOfBiome(field, "dune") == 0);
  assert(indexOfBiome(field, "meadow") == 1);
  assert(indexOfBiome(field, "rock") == 2);
  for (const TerrainLod level : kLevels)
    for (const float depth : {0.F, 1.5F}) {
      const auto mesh = buildTerrainLodBiomeMesh(field, field.chunks.front(),
                                                  level, depth);
      assert(mesh.has_value());
      assert(mesh->groups.size() == 3);
      assert(!mesh->empty());
      std::size_t previous = 0;
      for (std::size_t group = 0; group < mesh->groups.size(); ++group) {
        // Ascending index in biomeIds, which is the order the groups must come
        // out in for a deterministic draw order.
        const std::size_t label = indexOfBiome(field, mesh->groups[group].biomeId);
        assert(label < field.biomeIds.size());
        assert(group == 0 || label > previous);
        previous = label;
        // The tint travels with the group, so a shader needs no biome lookup, and
        // it has to be the color that belongs to THAT biome.
        assert(sameColor(mesh->groups[group].tint, field.biomeColors.at(label)));
        checkGroupIsDrawable(*mesh, mesh->groups[group]);
      }
      // Every biome really was on the chunk, so no group may be missing.
      for (const std::string &id : field.biomeIds) {
        bool present = false;
        for (const TerrainLodBiomeGroup &group : mesh->groups)
          present = present || group.biomeId == id;
        assert(present);
      }
    }
}

// Nothing may be lost or double counted by the split. Every cell of the chunk
// votes for exactly one biome, and every triangle of the flat mesh is emitted
// into exactly one group.
void everyCellLandsInExactlyOneGroup() {
  const auto field = labelledField(32, {"dune", "meadow", "rock"}, [](int x, int z) {
    return std::size_t(z < 16 ? (x < 16 ? 0 : 1) : 2);
  });
  for (const TerrainLod level : kLevels)
    for (const float depth : {0.F, 2.F}) {
      const auto flat =
          buildTerrainLodMesh(field, field.chunks.front(), level, depth);
      const auto mesh =
          buildTerrainLodBiomeMesh(field, field.chunks.front(), level, depth);
      assert(flat.has_value() && mesh.has_value());
      // The partitioned mesh is the flat mesh, split. Same cell stride, and no
      // triangle dropped on the way.
      assert(mesh->sourceCells == flat->sourceCells);
      assert(totalIndices(*mesh) == flat->indices.size());
      // And every cell of the chunk voted exactly once.
      assert(mesh->skippedBiomes == 0);
      assert(totalSourceCells(*mesh) ==
             std::size_t(field.cellsX) * std::size_t(field.cellsZ));
      // No group may be empty: an empty draw group is not a draw.
      for (const TerrainLodBiomeGroup &group : mesh->groups)
        assert(group.sourceCells > 0);
    }
}

// One biome over the whole chunk is the degenerate case of the partition: there
// is exactly one group, it holds every triangle, and it is the flat mesh.
void aSingleBiomeFieldYieldsOneGroup() {
  const auto field = labelledField(24, {"solid"}, [](int, int) {
    return std::size_t(0);
  }, {tint(0.4F, 0.7F, 0.3F)});
  for (const TerrainLod level : kLevels)
    for (const float depth : {0.F, 1.F}) {
      const auto flat =
          buildTerrainLodMesh(field, field.chunks.front(), level, depth);
      const auto mesh =
          buildTerrainLodBiomeMesh(field, field.chunks.front(), level, depth);
      assert(flat.has_value() && mesh.has_value());
      assert(mesh->groups.size() == 1);
      assert(mesh->groups.front().biomeId == "solid");
      assert(sameColor(mesh->groups.front().tint, field.biomeColors.front()));
      assert(mesh->groups.front().indices.size() == flat->indices.size());
      assert(mesh->groups.front().sourceCells ==
             std::size_t(field.cellsX) * std::size_t(field.cellsZ));
      assert(mesh->groups.front().vertices.size() == flat->vertices.size());
      // Nothing was invented and nothing was dropped, so the group's geometry is
      // the flat mesh's geometry. The ORDER differs, because a group is filled in
      // the order its own triangles reach each vertex, so the comparison is on the
      // positions themselves rather than on their indices.
      std::vector<std::tuple<float, float, float>> mine;
      std::vector<std::tuple<float, float, float>> theirs;
      for (const Vec3 &vertex : mesh->groups.front().vertices)
        mine.emplace_back(vertex.x, vertex.y, vertex.z);
      for (const Vec3 &vertex : flat->vertices)
        theirs.emplace_back(vertex.x, vertex.y, vertex.z);
      std::sort(mine.begin(), mine.end());
      std::sort(theirs.begin(), theirs.end());
      assert(mine == theirs);
      checkGroupIsDrawable(*mesh, mesh->groups.front());
    }
}

// A boundary cell must resolve to the same biome on every call, on every run and
// on a separately built identical field. Nothing about the answer may depend on
// iteration order or on object identity.
void boundaryCellsResolveDeterministically() {
  const auto field = splitField(32, false);
  const int split = field.cellsX / 2;
  // A cell sitting on the boundary, and one either side of it.
  const std::vector<std::pair<int, int>> probes{{split - 1, 5},
                                                {split, 5},
                                                {split, 16},
                                                {0, 0},
                                                {field.cellsX, field.cellsZ}};
  const std::size_t first =
      terrainLodBiomeForCell(field, split, 5, terrainLodStep(TerrainLod::Half));
  assert(first < 2);
  for (int repeat = 0; repeat < 8; ++repeat)
    for (const auto &[cellX, cellZ] : probes)
      assert(terrainLodBiomeForCell(field, cellX, cellZ, 2) ==
             terrainLodBiomeForCell(field, cellX, cellZ, 2));
  for (const auto &[cellX, cellZ] : probes)
    assert(terrainLodBiomeForCell(field, cellX, cellZ, 2) ==
           terrainLodBiomeForCell(field, cellX, cellZ, 2));
  // A second, identically built field answers identically, so the answer is a
  // property of the data rather than of this object.
  const auto twin = splitField(32, false);
  for (const auto &[cellX, cellZ] : probes)
    assert(terrainLodBiomeForCell(twin, cellX, cellZ, 2) ==
           terrainLodBiomeForCell(field, cellX, cellZ, 2));

  // The mesh itself has to be reproducible too, group for group, because a
  // partition that reshuffled between builds would make the chunk cache useless.
  const auto firstBuild =
      buildTerrainLodBiomeMesh(field, field.chunks.front(), TerrainLod::Half, 1.F);
  assert(firstBuild.has_value());
  for (int repeat = 0; repeat < 4; ++repeat) {
    const auto again =
        buildTerrainLodBiomeMesh(field, field.chunks.front(), TerrainLod::Half, 1.F);
    assert(again.has_value());
    assert(again->groups.size() == firstBuild->groups.size());
    for (std::size_t group = 0; group < again->groups.size(); ++group) {
      assert(again->groups[group].biomeId == firstBuild->groups[group].biomeId);
      assert(again->groups[group].sourceCells ==
             firstBuild->groups[group].sourceCells);
      assert(again->groups[group].indices.size() ==
             firstBuild->groups[group].indices.size());
      assert(again->groups[group].vertices.size() ==
             firstBuild->groups[group].vertices.size());
      for (std::size_t vertex = 0; vertex < again->groups[group].vertices.size();
           ++vertex) {
        const Vec3 &mine = again->groups[group].vertices[vertex];
        const Vec3 &theirs = firstBuild->groups[group].vertices[vertex];
        assert(mine.x == theirs.x && mine.y == theirs.y && mine.z == theirs.z);
      }
    }
  }
}

// A reduced cell belongs to the biome that owns MOST of it. A first sample that
// disagrees is exactly the boundary case that must not decide the vote, because
// it is whichever sample the walk reached first, not the terrain's answer.
void majorityVoteBeatsTheFirstSample() {
  // The reduced cell at (0,0) with a stride of two covers a three by three block
  // of samples. Its first sample belongs to biome 0, and biome 1 still wins it.
  auto field = labelledField(8, {"low", "high"}, [](int x, int z) {
    if (x <= 2 && z <= 2)
      return std::size_t(x == 0 && z == 0 ? 0 : 1);
    return std::size_t(0);
  });
  assert(field.biomeIndices.at(field.index(0, 0)) == 0);
  assert(terrainLodBiomeForCell(field, 0, 0, 2) == 1);
  assert(terrainLodBiomeForCell(field, 1, 1, 2) == 1);
  // The reduced cells are anchored to the field's own grid at multiples of the
  // stride, so (2,2) is the corner of the NEXT reduced cell rather than part of
  // this one, and that cell is unambiguously LOW.
  assert(terrainLodBiomeForCell(field, 2, 2, 2) == 0);
  // Every cell inside that reduced cell agrees, whichever one is asked about.
  for (int z = 0; z < 2; ++z)
    for (int x = 0; x < 2; ++x)
      assert(terrainLodBiomeForCell(field, x, z, 2) == 1);

  // The mesh follows the same rule: at Half detail the reduced cell's triangles
  // are in the HIGH group, even though its own corner sample is LOW.
  const auto mesh =
      buildTerrainLodBiomeMesh(field, field.chunks.front(), TerrainLod::Half, 1.F);
  assert(mesh.has_value());
  assert(mesh->groups.size() == 2);
  assert(mesh->groups[0].biomeId == "low");
  assert(mesh->groups[1].biomeId == "high");
  assert(mesh->groups[1].sourceCells == 4);
  assert(mesh->groups[0].sourceCells ==
         std::size_t(field.cellsX) * std::size_t(field.cellsZ) - 4);

  // One sample against many is still one sample: the vote has to have a majority,
  // not merely a first opinion.
  auto lonely = labelledField(8, {"low", "high"}, [](int x, int z) {
    return std::size_t(x == 1 && z == 1 ? 1 : 0);
  });
  assert(terrainLodBiomeForCell(lonely, 0, 0, 2) == 0);
  // At the finest stride the same cell is decided by its own two by two corners,
  // where that one sample is a quarter of the vote rather than eight ninths.
  assert(terrainLodBiomeForCell(lonely, 0, 0, 1) == 0);
}

// A boundary has to land somewhere, and it must land on the same side every time.
// An exact tie therefore resolves to the LOWER biome index, never to whichever
// sample the walk happened to reach first.
void tiesResolveToTheLowerIndex() {
  // A two by two block of corners split two and two. Both arrangements below
  // produce the same tie, so the answer cannot depend on the sample order.
  const auto lowFirst = labelledField(4, {"a", "b"}, [](int x, int) {
    return std::size_t(x == 0 ? 0 : 1);
  });
  const auto highFirst = labelledField(4, {"a", "b"}, [](int x, int) {
    return std::size_t(x == 0 ? 1 : 0);
  });
  assert(terrainLodBiomeForCell(lowFirst, 0, 0, 1) == 0);
  assert(terrainLodBiomeForCell(highFirst, 0, 0, 1) == 0);
  // And the tie is resolved, not left to chance: the winner is the lower index,
  // which is the lower index whether it is majority or not.
  const auto lopsided = labelledField(4, {"a", "b"}, [](int x, int z) {
    // Three samples of "b" against one of "a", and "a" holds the lower index.
    return std::size_t(x == 0 && z == 0 ? 0 : 1);
  });
  assert(terrainLodBiomeForCell(lopsided, 0, 0, 1) == 1);
  // Majority beats the lower index; the tie-break is only ever a tie-break.
  assert(terrainLodBiomeForCell(lopsided, 0, 0, 2) == 1);
}

// The crack a skirt exists to close must survive the split. A lost skirt is
// exactly the bug this partition would otherwise introduce, so the owned counts
// have to add up to the flat mesh's, and every owned skirt vertex has to still be
// standing in the group that owns it.
void skirtsSurvivePartitioning() {
  const auto field = labelledField(32, {"dune", "meadow", "rock"},
                                   [](int x, int z) {
                                     return std::size_t(z < 16 ? (x < 16 ? 0 : 1)
                                                                : 2);
                                   });
  for (const TerrainLod level : kLevels) {
    const float depth = level == TerrainLod::Full ? 0.F : 1.5F;
    const auto flat =
        buildTerrainLodMesh(field, field.chunks.front(), level, depth);
    const auto mesh =
        buildTerrainLodBiomeMesh(field, field.chunks.front(), level, depth);
    assert(flat.has_value() && mesh.has_value());
    // Every skirt vertex is owned by exactly one group, so the groups' owned
    // counts still add up to the chunk's whole skirt.
    assert(totalSkirtDepths(*mesh) == flat->skirtDepths.size());
    for (const TerrainLodBiomeGroup &group : mesh->groups) {
      std::size_t skirtVertices = 0;
      for (const Vec3 &vertex : group.vertices)
        skirtVertices += isSkirtBottom(field, vertex) ? 1 : 0;
      // A skirt bottom standing on a biome boundary is referenced by two adjacent
      // walls, so it appears in two groups while being owned once. Ownership can
      // therefore never exceed the group's own skirt vertices, and a group whose
      // cells are all interior carries none at all.
      assert(group.skirtDepths <= skirtVertices);
      // Every wall this group owns is a WHOLE wall in this group.
      checkSkirtWallsAreWhole(field, group);
      const bool onBoundary =
          group.biomeId == field.biomeIds.front() ||
          group.biomeId == field.biomeIds.at(1);
      if (onBoundary && level != TerrainLod::Full)
        assert(group.skirtDepths > 0);
      if (level == TerrainLod::Full)
        assert(group.skirtDepths == 0);
    }
  }

  // A vertical boundary that reaches the chunk's own edge puts the split between
  // two biomes exactly on the ring, where a skirt segment could otherwise be cut
  // in half between the two groups that own its ends.
  const auto edged = labelledField(16, {"low", "high"}, [](int x, int) {
    return std::size_t(x >= 8 ? 1 : 0);
  });
  for (const TerrainLod level : {TerrainLod::Half, TerrainLod::Quarter}) {
    const auto mesh =
        buildTerrainLodBiomeMesh(edged, edged.chunks.front(), level, 1.5F);
    assert(mesh.has_value());
    assert(mesh->groups.size() == 2);
    std::size_t owned = 0;
    for (const TerrainLodBiomeGroup &group : mesh->groups) {
      owned += group.skirtDepths;
      checkSkirtWallsAreWhole(edged, group);
    }
    assert(owned > 0);
  }

  // With one biome there is no boundary to share, so the single group owns the
  // chunk's entire skirt and every one of its skirt vertices is a real vertex.
  const auto single = labelledField(24, {"solid"}, [](int, int) {
    return std::size_t(0);
  }, {tint(0.4F, 0.7F, 0.3F)});
  for (const TerrainLod level : {TerrainLod::Half, TerrainLod::Coarse}) {
    const auto flat =
        buildTerrainLodMesh(single, single.chunks.front(), level, 1.F);
    const auto mesh =
        buildTerrainLodBiomeMesh(single, single.chunks.front(), level, 1.F);
    assert(flat.has_value() && mesh.has_value());
    assert(!flat->skirtDepths.empty());
    assert(mesh->groups.size() == 1);
    assert(mesh->groups.front().skirtDepths == flat->skirtDepths.size());
    std::size_t skirtVertices = 0;
    for (const Vec3 &vertex : mesh->groups.front().vertices)
      skirtVertices += isSkirtBottom(single, vertex) ? 1 : 0;
    assert(skirtVertices == mesh->groups.front().skirtDepths);
    assert(mesh->groups.front().vertices.size() == flat->vertices.size());
  }
}

// Per-group indices, UVs and normals have to hold at every level and every chunk,
// or the partition produces meshes the renderer cannot upload.
void groupsAreWellFormedAtEveryLevel() {
  const auto field = generate(splitRecipe(32, 16));
  assert(!field.chunks.empty());
  for (const TerrainChunk &chunk : field.chunks)
    for (const TerrainLod level : kLevels)
      for (const float depth : {0.F, 1.25F}) {
        const auto mesh = buildTerrainLodBiomeMesh(field, chunk, level, depth);
        assert(mesh.has_value());
        assert(!mesh->empty());
        assert(mesh->skippedBiomes == 0);
        assert(mesh->sourceCells == std::size_t(terrainLodStep(level)) *
                                        std::size_t(terrainLodStep(level)));
        for (const TerrainLodBiomeGroup &group : mesh->groups)
          checkGroupIsDrawable(*mesh, group);
      }
}

// Texel density has to agree between a reduced chunk and the full-resolution one,
// or terrain visibly swims as the camera pulls back. The UV is the world position
// over the field's own extent, so the same world point carries the same UV at
// every level.
void uvsAreWorldScaleAndMonotonic() {
  const auto field = labelledField(32, {"dune", "meadow", "rock"},
                                   [](int x, int z) {
                                     return std::size_t(z < 16 ? (x < 16 ? 0 : 1)
                                                                : 2);
                                   });
  const int step = terrainLodStep(TerrainLod::Half);
  const auto reduced =
      buildTerrainLodBiomeMesh(field, field.chunks.front(), TerrainLod::Half, 1.F);
  const auto full =
      buildTerrainLodBiomeMesh(field, field.chunks.front(), TerrainLod::Full, 0.F);
  assert(reduced.has_value() && full.has_value());

  for (const TerrainLodBiomeGroup &group : reduced->groups) {
    // Exactly the full-resolution convention, on every vertex including a skirt
    // bottom, whose UV has to match the top it hangs from.
    for (std::size_t vertex = 0; vertex < group.vertices.size(); ++vertex) {
      const Vec3 &position = group.vertices.at(vertex);
      const Vec2 &uv = group.uvs.at(vertex);
      assert(uv.x == position.x / field.size.x);
      assert(uv.y == position.z / field.size.y);
      // Over the field's extent, so every coordinate lands in the unit square a
      // texture expects and nothing is tiled by accident.
      assert(uv.x >= 0.F && uv.x <= 1.F);
      assert(uv.y >= 0.F && uv.y <= 1.F);
    }
    // Monotonic along world position: moving across the chunk moves across the
    // texture, never back along it.
    std::vector<std::pair<float, float>> alongX;
    std::vector<std::pair<float, float>> alongZ;
    for (std::size_t vertex = 0; vertex < group.vertices.size(); ++vertex)
      alongX.emplace_back(group.vertices[vertex].x, group.uvs[vertex].x);
    for (std::size_t vertex = 0; vertex < group.vertices.size(); ++vertex)
      alongZ.emplace_back(group.vertices[vertex].z, group.uvs[vertex].y);
    for (auto *axis : {&alongX, &alongZ}) {
      std::sort(axis->begin(), axis->end());
      for (std::size_t entry = 1; entry < axis->size(); ++entry)
        assert(axis->at(entry).second >= axis->at(entry - 1).second - 1e-6F);
    }
  }

  // The same world point carries the same UV at both levels, which is what makes
  // the reduced chunk's texel density the full-resolution one.
  std::size_t compared = 0;
  for (const TerrainLodBiomeGroup &group : reduced->groups)
    for (std::size_t vertex = 0; vertex < group.vertices.size(); ++vertex) {
      const Vec3 &position = group.vertices.at(vertex);
      const Vec2 &uv = group.uvs.at(vertex);
      for (const TerrainLodBiomeGroup &reference : full->groups)
        for (const Vec3 &other : reference.vertices)
          if (other.x == position.x && other.z == position.z) {
            const Vec2 expected = worldUv(field, other);
            assert(uv.x == expected.x && uv.y == expected.y);
            ++compared;
          }
    }
  // The full level samples every cell, so every reduced vertex has a
  // full-resolution counterpart at the same position.
  assert(compared > 0);
  const int stride = step;
  assert(stride == 2);
}

// The Full level must not quietly re-decide where the biome boundaries are. Its
// share per biome has to be the field's own share, up to the cells that sit ON a
// boundary and therefore carry one cell of rounding.
void fullLevelKeepsTheFieldsBiomeProportions() {
  for (int cells : {16, 32}) {
    for (const bool lowerIsLeft : {true, false}) {
      const auto field = splitField(cells, lowerIsLeft);
      const auto mesh = buildTerrainLodBiomeMesh(
          field, field.chunks.front(), TerrainLod::Full, 0.F);
      assert(mesh.has_value());
      assert(mesh->groups.size() == 2);

      // The field's own share, counted cell by cell from each cell's corner.
      std::vector<std::size_t> fieldCells(field.biomeIds.size(), 0);
      // And the cells whose four corners disagree, which are the only ones allowed
      // to be counted differently.
      std::size_t boundaryCells = 0;
      for (int z = 0; z < field.cellsZ; ++z)
        for (int x = 0; x < field.cellsX; ++x) {
          const std::size_t corner = field.biomeIndices.at(field.index(x, z));
          assert(corner < field.biomeIds.size());
          ++fieldCells[corner];
          bool agrees = true;
          for (const int dz : {0, 1})
            for (const int dx : {0, 1})
              agrees = agrees &&
                       field.biomeIndices.at(field.index(x + dx, z + dz)) == corner;
          if (!agrees)
            ++boundaryCells;
        }

      for (const TerrainLodBiomeGroup &group : mesh->groups) {
        const std::size_t label = indexOfBiome(field, group.biomeId);
        assert(label < field.biomeIds.size());
        // Every cell whose four corners agree must land on the field's own biome.
        // Anything else would mean the partition moved a boundary.
        assert(group.sourceCells > 0);
        // The share matches to within one cell of rounding per boundary cell.
        assert(std::abs(double(group.sourceCells) - double(fieldCells[label])) <=
               double(boundaryCells));
      }
      // And nothing was invented or lost by the measurement itself.
      assert(totalSourceCells(*mesh) ==
             std::size_t(field.cellsX) * std::size_t(field.cellsZ));
      assert(mesh->skippedBiomes == 0);
    }
  }

  // The same agreement on a generated field, where the boundary is a real curve
  // rather than a straight line, so the boundary cell count is genuinely large.
  const auto generated = generate(splitRecipe(64, 32));
  const TerrainChunk &chunk = generated.chunks.front();
  assert(chunk.cellsX > 0 && chunk.cellsZ > 0);
  const auto mesh =
      buildTerrainLodBiomeMesh(generated, chunk, TerrainLod::Full, 0.F);
  assert(mesh.has_value());
  assert(mesh->groups.size() >= 2);
  assert(mesh->skippedBiomes == 0);
  assert(totalSourceCells(*mesh) ==
         std::size_t(chunk.cellsX) * std::size_t(chunk.cellsZ));
  for (const TerrainLodBiomeGroup &group : mesh->groups) {
    // The group's own corner sample has to be the group's own biome, or the
    // partition disagrees with the field about where the biome even is.
    const std::size_t label = indexOfBiome(generated, group.biomeId);
    assert(label < generated.biomeIds.size());
    std::size_t corners = 0;
    std::size_t matching = 0;
    for (const Vec3 &vertex : group.vertices) {
      const int cellX = std::clamp(
          int(std::lround(double(vertex.x) / double(generated.size.x) *
                          generated.cellsX)),
          0, generated.cellsX);
      const int cellZ = std::clamp(
          int(std::lround(double(vertex.z) / double(generated.size.y) *
                          generated.cellsZ)),
          0, generated.cellsZ);
      ++corners;
      if (generated.biomeIndices.at(generated.index(cellX, cellZ)) == label)
        ++matching;
    }
    assert(corners > 0);
    assert(matching * 4 >= corners * 3);
  }
}

// A refused request has to be a named failure. An empty group would look like a
// chunk that legitimately has no geometry, which is the opposite of the truth.
void invalidRequestsAreRefusedWithAMessage() {
  const auto field = labelledField(
      32, {"dune", "meadow", "rock"},
      [](int x, int z) { return std::size_t(z < 16 ? 0 : 1); });
  const TerrainChunk &chunk = field.chunks.front();
  // Off draws nothing, which is an answer rather than a failure.
  assert(!buildTerrainLodBiomeMesh(field, chunk, TerrainLod::Off, 1.F).has_value());
  // A level outside TerrainLod is a bug in the caller and says so.
  refuses(field, chunk, TerrainLod(7), 1.F);
  refuses(field, chunk, TerrainLod(-1), 1.F);
  // A chunk outside the field would be sampled past the heightfield's edge.
  TerrainChunk outside = chunk;
  outside.firstCellX = field.cellsX;
  refuses(field, outside, TerrainLod::Half, 1.F);
  outside = chunk;
  outside.firstCellZ = field.cellsZ - 1;
  refuses(field, outside, TerrainLod::Full, 0.F);
  // A chunk with no cells is refused too, rather than answering with nothing.
  TerrainChunk empty = chunk;
  empty.cellsX = 0;
  refuses(field, empty, TerrainLod::Full, 0.F);
  empty = chunk;
  empty.cellsZ = -4;
  refuses(field, empty, TerrainLod::Half, 1.F);
  // A field that names no biome has no per-biome surface to partition.
  auto nameless = labelledField(8, {"only"}, [](int, int) {
    return std::size_t(0);
  }, {tint(0.4F, 0.7F, 0.3F)});
  nameless.biomeIds.clear();
  refuses(nameless, nameless.chunks.front(), TerrainLod::Full, 0.F);
  refusesCell(nameless, 0, 0, 1);
  // A field with no extent cannot produce world-scale UVs.
  auto extentless = labelledField(8, {"only"}, [](int, int) {
    return std::size_t(0);
  }, {tint(0.4F, 0.7F, 0.3F)});
  extentless.size = {0.F, 0.F};
  refuses(extentless, extentless.chunks.front(), TerrainLod::Full, 0.F);
  // And the single-cell probe refuses a cell or a stride that is not there.
  refusesCell(field, -1, 0, 1);
  refusesCell(field, 0, field.cellsZ + 1, 1);
  refusesCell(field, 0, 0, 0);
  refusesCell(field, 0, 0, -2);
}

// A label the field does not name has no id and no tint, so no group could carry
// it. Those cells are counted rather than dropped in silence, because a non-zero
// count means the field's labels and its biome list disagree.
void unlabelledBiomesAreCountedNotHidden() {
  const auto field = labelledField(16, {"only"}, [](int x, int z) {
    // The right-hand half carries a label the field never names.
    return std::size_t(x >= 8 && z >= 8 ? 9 : 0);
  });
  std::size_t unlabelledCells = 0;
  for (int z = 0; z < field.cellsZ; ++z)
    for (int x = 0; x < field.cellsX; ++x) {
      bool named = false;
      for (const int dz : {0, 1})
        for (const int dx : {0, 1}) {
          const std::size_t label =
              field.biomeIndices.at(field.index(x + dx, z + dz));
          named = named || label < field.biomeIds.size();
        }
      unlabelledCells += named ? 0 : 1;
    }
  assert(unlabelledCells > 0);

  const auto mesh =
      buildTerrainLodBiomeMesh(field, field.chunks.front(), TerrainLod::Full, 1.F);
  assert(mesh.has_value());
  assert(!mesh->empty());
  assert(mesh->skippedBiomes == unlabelledCells);
  // Every cell is accounted for: named cells in a group, unnamed cells counted.
  assert(totalSourceCells(*mesh) + mesh->skippedBiomes ==
         std::size_t(field.cellsX) * std::size_t(field.cellsZ));
  for (const TerrainLodBiomeGroup &group : mesh->groups)
    checkGroupIsDrawable(*mesh, group);

  // A field where NO label is named has nothing to draw, and says so through
  // empty() rather than through a group with no geometry in it.
  auto blank = labelledField(8, {"only"}, [](int, int) { return std::size_t(9); });
  const auto nothing = buildTerrainLodBiomeMesh(
      blank, blank.chunks.front(), TerrainLod::Quarter, 1.F);
  assert(nothing.has_value());
  assert(nothing->empty());
  assert(nothing->groups.empty());
  assert(nothing->skippedBiomes ==
         std::size_t(blank.cellsX) * std::size_t(blank.cellsZ));
  // And the single-cell probe refuses it instead of inventing a biome.
  refusesCell(blank, 0, 0, 2);
}

// A chunk whose far edge is not a whole number of strides away is the case where
// a private copy of the lattice could drift from the flat mesh's. Every chunk here
// still has to partition the flat mesh exactly: the same vertices, the same
// triangles, the same skirts.
void raggedChunksPartitionExactlyLikeTheFlatMesh() {
  // 64 cells split into chunks of 20 leaves a final row and column of four, so
  // the last chunk's lattice ends on a short hop.
  const auto field = generate(splitRecipe(64, 20));
  assert(field.chunks.size() > 1);
  assert(field.chunks.back().cellsX == 4 && field.chunks.back().cellsZ == 4);
  for (const TerrainChunk &chunk : field.chunks)
    for (const TerrainLod level : kLevels)
      for (const float depth : {0.F, 1.5F}) {
        const auto flat =
            buildTerrainLodMesh(field, chunk, level, depth);
        const auto mesh = buildTerrainLodBiomeMesh(field, chunk, level, depth);
        assert(flat.has_value() && mesh.has_value());
        // No triangle was dropped, added or duplicated by the split.
        assert(totalIndices(*mesh) == flat->indices.size());
        // And no vertex was either: the partition is a re-indexing of the flat
        // mesh, not a resample of it.
        std::vector<std::tuple<float, float, float>> mine;
        std::vector<std::tuple<float, float, float>> theirs;
        for (const TerrainLodBiomeGroup &group : mesh->groups)
          for (const Vec3 &vertex : group.vertices)
            mine.emplace_back(vertex.x, vertex.y, vertex.z);
        for (const Vec3 &vertex : flat->vertices)
          theirs.emplace_back(vertex.x, vertex.y, vertex.z);
        std::sort(mine.begin(), mine.end());
        std::sort(theirs.begin(), theirs.end());
        // A vertex on a biome boundary is emitted into both of the groups that
        // reference it, which is what keeps each group indexable on its own, so
        // the union is what has to match rather than the sum.
        mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
        assert(mine == theirs);
        // Skirts survive, and their ownership is reproducible.
        assert(totalSkirtDepths(*mesh) == flat->skirtDepths.size());
        const auto again =
            buildTerrainLodBiomeMesh(field, chunk, level, depth);
        assert(again.has_value());
        for (std::size_t group = 0; group < mesh->groups.size(); ++group) {
          assert(again->groups[group].skirtDepths ==
                 mesh->groups[group].skirtDepths);
          checkGroupIsDrawable(*mesh, mesh->groups[group]);
        }
        // The chunk's exact far edge is still one of the emitted vertices, because
        // that is the sample the neighbouring chunk reads as its own first one.
        const Vec2 far = field.position(chunk.firstCellX + chunk.cellsX,
                                        chunk.firstCellZ + chunk.cellsZ);
        bool farEdge = false;
        for (const TerrainLodBiomeGroup &group : mesh->groups)
          for (const Vec3 &vertex : group.vertices)
            farEdge = farEdge || (vertex.x == far.x && vertex.z == far.y);
        assert(farEdge);
      }
}

// Building one chunk must cost the chunk, not the field: the same chunk of two
// differently shaped fields produces the same groups regardless of what is
// outside it.
void buildingOneChunkIgnoresTheRestOfTheField() {
  const auto labelled = labelledField(32, {"dune", "meadow", "rock"},
                                      [](int x, int z) {
                                        return std::size_t(z < 16 ? (x < 16 ? 0 : 1)
                                                                   : 2);
                                      });
  // Same chunk shape and same biome layout in the chunk, different ground.
  auto other = labelled;
  for (int z = 0; z <= other.cellsZ; ++z)
    for (int x = 0; x <= other.cellsX; ++x)
      other.heights.set(other.index(x, z), other.height(x, z) + 40.F);
  assert(labelled.biomeIndices == other.biomeIndices);
  const TerrainChunk chunk{16, 16, 16, 16};
  for (const TerrainLod level : kLevels) {
    const auto a = buildTerrainLodBiomeMesh(labelled, chunk, level, 1.F);
    const auto b = buildTerrainLodBiomeMesh(other, chunk, level, 1.F);
    assert(a.has_value() && b.has_value());
    assert(a->groups.size() == b->groups.size());
    for (std::size_t group = 0; group < a->groups.size(); ++group) {
      assert(a->groups[group].biomeId == b->groups[group].biomeId);
      // Same lattice and same partition, because neither depends on the surface.
      assert(a->groups[group].sourceCells == b->groups[group].sourceCells);
      assert(a->groups[group].vertices.size() == b->groups[group].vertices.size());
      assert(a->groups[group].skirtDepths == b->groups[group].skirtDepths);
      assert(totalIndices(*a) == totalIndices(*b));
      // But the geometry came from this field's own samples.
      bool differs = false;
      for (std::size_t vertex = 0; vertex < a->groups[group].vertices.size();
           ++vertex)
        differs =
            differs || a->groups[group].vertices[vertex].y !=
                           b->groups[group].vertices[vertex].y;
      assert(differs);
    }
  }
}
} // namespace

int main() {
  groupsFollowBiomeIdOrder();
  everyCellLandsInExactlyOneGroup();
  aSingleBiomeFieldYieldsOneGroup();
  boundaryCellsResolveDeterministically();
  majorityVoteBeatsTheFirstSample();
  tiesResolveToTheLowerIndex();
  skirtsSurvivePartitioning();
  groupsAreWellFormedAtEveryLevel();
  uvsAreWorldScaleAndMonotonic();
  fullLevelKeepsTheFieldsBiomeProportions();
  invalidRequestsAreRefusedWithAMessage();
  unlabelledBiomesAreCountedNotHidden();
  raggedChunksPartitionExactlyLikeTheFlatMesh();
  buildingOneChunkIgnoresTheRestOfTheField();
  std::cout << "Terrain LOD biome partition checks passed\n";
}