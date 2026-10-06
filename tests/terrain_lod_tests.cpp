#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainLod.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>

using namespace demi::runtime;

namespace {
// A flat field is the controlled case for every rule that is about DISTANCE: its
// decimation error is zero, so nothing but the threshold table can move a level.
// The steep field is the controlled case for the opposite rule.
TerrainRecipe flatRecipe(int cells, int chunkCells) {
  TerrainRecipe recipe;
  recipe.size = {float(cells), float(cells)};
  recipe.cellsX = recipe.cellsZ = cells;
  recipe.chunkCells = chunkCells;
  recipe.landforms.at("default").baseHeight = 8;
  recipe.landforms.at("default").heightVariation = 0;
  return recipe;
}

TerrainRecipe steepRecipe() {
  auto recipe = flatRecipe(64, 16);
  auto &landform = recipe.landforms.at("default");
  landform.heightVariation = 14;
  landform.featureSize = 8;
  landform.roughness = 1;
  landform.octaves = 5;
  return recipe;
}

HeightField generate(const TerrainRecipe &recipe) {
  auto field = TerrainGenerator::generate(recipe);
  assert(field.has_value());
  return *field;
}

float nearestDistance(const HeightField &field, const TerrainChunk &chunk,
                      Vec2 camera) {
  const Vec2 first = field.position(chunk.firstCellX, chunk.firstCellZ);
  const Vec2 last =
      field.position(chunk.firstCellX + chunk.cellsX, chunk.firstCellZ + chunk.cellsZ);
  const float dx = std::max({0.F, first.x - camera.x, camera.x - last.x});
  const float dz = std::max({0.F, first.y - camera.y, camera.y - last.y});
  return std::hypot(dx, dz);
}

float centreDistance(const HeightField &field, const TerrainChunk &chunk,
                     Vec2 camera) {
  const Vec2 centre = field.position(chunk.firstCellX + chunk.cellsX / 2,
                                     chunk.firstCellZ + chunk.cellsZ / 2);
  return std::hypot(centre.x - camera.x, centre.y - camera.y);
}

bool roughlyEqual(float a, float b, float tolerance) {
  return std::abs(a - b) <= tolerance;
}

// The whole point of nearest-point distance: a chunk is visible well before its
// centre is close. If this used the centre, the near edge of every large chunk
// would already be reduced, and that is exactly where the seam pops.
void nearestPointBeatsCentre() {
  const auto field = generate(flatRecipe(64, 64));
  assert(field.chunks.size() == 1);
  const auto &chunk = field.chunks.front();
  const Vec2 camera{2, 80};
  const float nearest = nearestDistance(field, chunk, camera);
  const float centre = centreDistance(field, chunk, camera);
  // The two rules genuinely disagree here, so this test would fail loudly if the
  // selector quietly swapped back to the centre.
  assert(nearest < 40.F && centre >= 40.F);
  assert(centre >= 40.F && centre < 110.F);
  assert(selectTerrainLod(field, camera) == TerrainLod::Full);
  // The stored level for that same chunk follows the caller's distance, which is
  // measured the same way.
  assert(terrainLodForChunk(field, chunk, nearest) == TerrainLod::Full);
  assert(terrainLodForChunk(field, chunk, centre) == TerrainLod::Half);
}

// Nearer chunks must select a finer level than farther ones, and a level is never
// produced past the thresholds' size however far away the camera is.
void distanceOrdersLevels() {
  const auto field = generate(flatRecipe(64, 16));
  const auto &chunk = field.chunks.front();
  const TerrainLodSettings settings;
  assert(terrainLodUsableLevels(settings) ==
         int(settings.distanceThresholds.size()));

  int previous = -1;
  for (const float distance : {0.F, 39.F, 40.F, 109.F, 110.F, 259.F, 260.F, 599.F,
                               600.F, 5000.F}) {
    const int index =
        terrainLodIndex(terrainLodForChunk(field, chunk, distance, settings));
    assert(index >= previous);
    assert(index < terrainLodLevelCount);
    previous = index;
  }
  assert(terrainLodForChunk(field, chunk, 0.F, settings) == TerrainLod::Full);
  assert(terrainLodForChunk(field, chunk, 300.F, settings) == TerrainLod::Coarse);
  // Past the last threshold the chunk is off, and stays off: no distance makes
  // an index larger than the table can address.
  assert(terrainLodForChunk(field, chunk, 1e6F, settings) == TerrainLod::Off);

  // A caller that only wants two coarse levels never sees the third, however far
  // away the chunk is.
  auto limited = settings;
  limited.maximumLevel = 2;
  for (const float distance : {0.F, 120.F, 400.F, 900.F}) {
    const int index =
        terrainLodIndex(terrainLodForChunk(field, chunk, distance, limited));
    assert(index <= terrainLodIndex(TerrainLod::Quarter));
    assert(index < terrainLodLevelCount);
  }
  // maximumLevel is a level count and is clamped into range, not trusted.
  auto silly = settings;
  silly.maximumLevel = 99;
  assert(terrainLodUsableLevels(silly) == terrainLodLevelCount - 1);
  auto negative = settings;
  negative.maximumLevel = -4;
  assert(terrainLodUsableLevels(negative) == 0);
  assert(terrainLodForChunk(field, chunk, 0.F, negative) == TerrainLod::Full);
}

// A static camera must not flap: the same chunk, the same distance and the same
// settings have to give the same answer every time, because a level that changed
// on its own would rebuild the chunk for no reason.
void selectionIsStable() {
  const auto field = generate(steepRecipe());
  const auto &chunk = field.chunks.front();
  const TerrainLodSettings settings;
  for (int repeat = 0; repeat < 8; ++repeat) {
    assert(selectTerrainLod(field, Vec2{9, 9}) ==
           selectTerrainLod(field, Vec2{9, 9}));
    assert(terrainLodForChunk(field, chunk, 130.F, settings) ==
           terrainLodForChunk(field, chunk, 130.F, settings));
  }
  // Two separately generated identical fields answer identically, so the answer
  // depends on the geometry rather than on object identity or iteration order.
  const auto twin = generate(steepRecipe());
  assert(selectTerrainLod(field, Vec2{9, 9}) == selectTerrainLod(twin, Vec2{9, 9}));
  assert(terrainLodForChunk(field, chunk, 130.F, settings) ==
         terrainLodForChunk(twin, chunk, 130.F, settings));
}

// The stored level is a function of the chunk and its maximum distance, never of
// the camera itself. A camera that moves without leaving a band leaves the
// stored level alone.
void storedLevelIgnoresTheCamera() {
  const auto field = generate(flatRecipe(64, 16));
  const auto &chunk = field.chunks.front();
  const TerrainLodSettings settings;
  int level = -1;
  // Every one of these is inside the same band, so the stored level must not
  // move even though the camera does.
  for (const Vec2 camera : std::vector<Vec2>{{4, 70},
                                            {60, 20},
                                            {60, 45},
                                            {10, 75}}) {
    const float nearest = nearestDistance(field, chunk, camera);
    assert(nearest > 40.F && nearest < 110.F);
    const int selected =
        terrainLodIndex(terrainLodForChunk(field, chunk, nearest, settings));
    if (level < 0)
      level = selected;
    assert(selected == level);
  }
  assert(level == terrainLodIndex(TerrainLod::Half));
}

// The geometric error is what keeps a reduced mesh honest. A flat field can drop
// to the coarsest level the table allows; a steep field at the same distance must
// stay finer, because dropping there would move the surface by more than the
// tolerance. A zero tolerance refuses every reduction at all.
void geometricErrorKeepsSteepChunksDetailed() {
  const auto flat = generate(flatRecipe(64, 16));
  const auto steep = generate(steepRecipe());
  const auto &flatChunk = flat.chunks.front();
  const auto &steepChunk = steep.chunks.front();
  const TerrainLodSettings settings;

  const int flatIndex = terrainLodIndex(terrainLodForChunk(flat, flatChunk, 300.F, settings));
  const int steepIndex =
      terrainLodIndex(terrainLodForChunk(steep, steepChunk, 300.F, settings));
  assert(flatIndex == terrainLodIndex(TerrainLod::Coarse));
  assert(steepIndex < flatIndex);

  auto intolerant = settings;
  intolerant.geometricError = 0.F;
  assert(terrainLodForChunk(flat, flatChunk, 0.F, intolerant) == TerrainLod::Full);
  assert(terrainLodForChunk(steep, steepChunk, 300.F, intolerant) == TerrainLod::Full);
  // The walk only ever moves FINER than the distance asked for, never coarser: a
  // tolerance nothing can violate leaves the distance table in charge, and that
  // is the coarsest answer the walk is allowed to produce.
  auto generous = settings;
  generous.geometricError = 1e6F;
  assert(terrainLodForChunk(steep, steepChunk, 300.F, generous) ==
         terrainLodForChunk(flat, flatChunk, 300.F, generous));
  assert(terrainLodForChunk(steep, steepChunk, 300.F, settings) <
         terrainLodForChunk(steep, steepChunk, 300.F, generous));
  // Flat ground keeps the coarse level at every distance the table allows, which
  // is what makes the mesh cost actually fall as the camera pulls away.
  assert(terrainLodForChunk(flat, flatChunk, 1000.F, settings) == TerrainLod::Off);
  assert(terrainLodForChunk(flat, flatChunk, 300.F, settings) == TerrainLod::Coarse);
}

// The full level must be the chunk's own cells, not an approximation of them, so
// a renderer can keep using it as the reference the reduced levels differ from.
void fullLevelIsTheChunksOwnCells() {
  const auto field = generate(steepRecipe());
  const auto &chunk = field.chunks.front();
  assert(chunk.cellsX == 16 && chunk.cellsZ == 16);
  const auto mesh = buildTerrainLodMesh(field, chunk, TerrainLod::Full, 2.F);
  assert(mesh.has_value());
  assert(mesh->vertices.size() == std::size_t(chunk.cellsX + 1) * std::size_t(chunk.cellsZ + 1));
  assert(mesh->normals.size() == mesh->vertices.size());
  assert(mesh->indices.size() == std::size_t(chunk.cellsX) * std::size_t(chunk.cellsZ) * 6);
  assert(mesh->sourceCells == 1);
  // A full-resolution boundary is the full-resolution edge, so it cannot crack
  // against a neighbour and never carries a skirt.
  assert(mesh->skirtDepths.empty());
}

// Each level samples every 2, 4 then 8 cells. The strides are exact powers of
// two so every sample is a real field sample rather than a position between two.
void reducedLevelsUsePowerOfTwoSteps() {
  const auto field = generate(steepRecipe());
  const auto &chunk = field.chunks.front();
  const std::array<TerrainLod, 4> levels{TerrainLod::Full, TerrainLod::Half,
                                         TerrainLod::Quarter, TerrainLod::Coarse};
  int previousSide = 0;
  for (const TerrainLod level : levels) {
    const int step = terrainLodStep(level);
    assert(step > 0 && (step & (step - 1)) == 0);
    const int side = chunk.cellsX / step + 1;
    const auto mesh = buildTerrainLodMesh(field, chunk, level, 1.F);
    assert(mesh.has_value());
    // Half a chunk's linear cells, a quarter, an eighth.
    assert(mesh->sourceCells == std::size_t(step) * std::size_t(step));
    assert(side < previousSide || level == TerrainLod::Full);
    previousSide = side;
    const auto plain = buildTerrainLodMesh(field, chunk, level, 0.F);
    assert(plain.has_value());
    assert(plain->vertices.size() == std::size_t(side) * std::size_t(chunk.cellsZ / step + 1));
  }
}

// The classic LOD bug is a reduced mesh that floats above or sinks below the
// real surface. Every reduced vertex is read straight from the field, so where
// the two share a sample the positions are the same bits, not merely close.
void reducedVerticesLieOnTheSurface() {
  const auto field = generate(steepRecipe());
  const auto &chunk = field.chunks.front();
  const int step = terrainLodStep(TerrainLod::Quarter);
  const auto mesh = buildTerrainLodMesh(field, chunk, TerrainLod::Quarter, 1.F);
  assert(mesh.has_value());
  const int columns = chunk.cellsX / step + 1;
  for (std::size_t row = 0; row < std::size_t(chunk.cellsZ / step + 1); ++row)
    for (std::size_t column = 0; column < std::size_t(columns); ++column) {
      const int cellX = chunk.firstCellX + int(column) * step;
      const int cellZ = chunk.firstCellZ + int(row) * step;
      const Vec2 position = field.position(cellX, cellZ);
      const Vec3 vertex = mesh->vertices.at(row * std::size_t(columns) + column);
      assert(vertex.x == position.x && vertex.z == position.y);
      assert(vertex.y == field.height(cellX, cellZ));
    }
  // The same cells are present in the full-resolution mesh at the same values,
  // so the two surfaces are verifiably the same surface.
  const auto full = buildTerrainLodMesh(field, chunk, TerrainLod::Full, 0.F);
  assert(full.has_value());
  for (std::size_t row = 0; row < std::size_t(chunk.cellsZ / step + 1); ++row)
    for (std::size_t column = 0; column < std::size_t(columns); ++column) {
      const Vec3 reduced = mesh->vertices.at(row * std::size_t(columns) + column);
      const std::size_t reference = (row * std::size_t(step)) *
                                        std::size_t(chunk.cellsX + 1) +
                                    column * std::size_t(step);
      const Vec3 exact = full->vertices.at(reference);
      assert(reduced.x == exact.x && reduced.y == exact.y && reduced.z == exact.z);
    }
  // Normals are recomputed at the reduced resolution, not borrowed from the fine
  // field: each one has to describe the REDUCED geometry it belongs to.
  for (const Vec3 normal : mesh->normals) {
    const float length =
        std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    assert(roughlyEqual(length, 1.F, 1e-4F));
  }
  const auto rows = std::size_t(chunk.cellsZ / step + 1);
  bool differsFromFineNormals = false;
  for (std::size_t row = 1; row + 1 < rows; ++row)
    for (std::size_t column = 1; column + 1 < std::size_t(columns); ++column) {
      // The tangent of the reduced surface at this vertex spans one reduced step,
      // which is four field cells, not one.
      const Vec3 a = mesh->vertices.at(row * std::size_t(columns) + column - 1);
      const Vec3 b = mesh->vertices.at(row * std::size_t(columns) + column + 1);
      const Vec3 c = mesh->vertices.at((row - 1) * std::size_t(columns) + column);
      const Vec3 d = mesh->vertices.at((row + 1) * std::size_t(columns) + column);
      const Vec3 tangentX{b.x - a.x, b.y - a.y, b.z - a.z};
      const Vec3 tangentZ{d.x - c.x, d.y - c.y, d.z - c.z};
      double nx = double(tangentZ.y) * tangentX.z - double(tangentZ.z) * tangentX.y;
      double ny = double(tangentZ.z) * tangentX.x - double(tangentX.z) * tangentZ.x;
      double nz = double(tangentZ.x) * tangentX.y - double(tangentZ.y) * tangentX.x;
      const double length = std::hypot(nx, ny, nz);
      assert(length > 0);
      const Vec3 normal =
          mesh->normals.at(row * std::size_t(columns) + column);
      assert(roughlyEqual(normal.x, float(nx / length), 1e-3F));
      assert(roughlyEqual(normal.y, float(ny / length), 1e-3F));
      assert(roughlyEqual(normal.z, float(nz / length), 1e-3F));
      // A normal sampled from the field would describe the fine surface instead,
      // which on this terrain differs by far more than the tolerance.
      const Vec3 fine = field.normal(chunk.firstCellX + int(column) * step,
                                     chunk.firstCellZ + int(row) * step);
      differsFromFineNormals =
          differsFromFineNormals || !roughlyEqual(normal.x, fine.x, 1e-3F) ||
          !roughlyEqual(normal.z, fine.z, 1e-3F);
    }
  assert(differsFromFineNormals);
}

// Recomputing must not change the convention. On the full level the reduced
// mesh's interior normals have to agree with the field's own, or the surface
// lights differently from the collision and from the un-reduced chunk.
void fullLevelNormalsMatchTheField() {
  const auto field = generate(steepRecipe());
  const auto &chunk = field.chunks.front();
  const auto mesh = buildTerrainLodMesh(field, chunk, TerrainLod::Full, 0.F);
  assert(mesh.has_value());
  const std::size_t side = std::size_t(chunk.cellsX + 1);
  for (std::size_t row = 1; row + 1 < side; ++row)
    for (std::size_t column = 1; column + 1 < side; ++column) {
      // Border vertices are excluded on purpose: the field's normal there uses
      // the neighbour chunk's sample, while a chunk mesh cannot read past its own
      // edge, so the two legitimately differ there.
      const Vec3 normal = mesh->normals.at(row * side + column);
      const Vec3 reference =
          field.normal(chunk.firstCellX + int(column), chunk.firstCellZ + int(row));
      assert(roughlyEqual(normal.x, reference.x, 1e-3F));
      assert(roughlyEqual(normal.y, reference.y, 1e-3F));
      assert(roughlyEqual(normal.z, reference.z, 1e-3F));
      assert(normal.y > 0.F);
    }
}

// The seam safety mechanism. A reduced chunk hangs a skirt from every boundary
// vertex, and it reaches below the deepest full-resolution height on the edges it
// shares, so a mismatch against a finer neighbour is filled with geometry
// instead of showing sky. A full-level chunk never needs one.
void skirtsCoverEveryBoundaryOfAReducedChunk() {
  const auto field = generate(steepRecipe());
  const auto &chunk = field.chunks.front();
  const float skirtDepth = 0.5F;
  const int step = terrainLodStep(TerrainLod::Half);
  const auto mesh = buildTerrainLodMesh(field, chunk, TerrainLod::Half, skirtDepth);
  assert(mesh.has_value());
  assert(!mesh->skirtDepths.empty());

  const int columns = chunk.cellsX / step + 1;
  const int rows = chunk.cellsZ / step + 1;
  const std::size_t surface = std::size_t(columns) * std::size_t(rows);
  // One skirt vertex per boundary lattice vertex, once each.
  const std::size_t boundary = std::size_t(2 * (columns - 1) + 2 * (rows - 1));
  assert(mesh->vertices.size() == surface + boundary);
  assert(mesh->skirtDepths.size() == boundary);
  assert(mesh->normals.size() == mesh->vertices.size());

  float edgeMinimum = field.height(chunk.firstCellX, chunk.firstCellZ);
  for (int x = 0; x <= chunk.cellsX; ++x)
    for (const int z : {0, chunk.cellsZ})
      edgeMinimum = std::min(edgeMinimum, field.height(chunk.firstCellX + x,
                                                        chunk.firstCellZ + z));
  for (int z = 0; z <= chunk.cellsZ; ++z)
    for (const int x : {0, chunk.cellsX})
      edgeMinimum = std::min(edgeMinimum, field.height(chunk.firstCellX + x,
                                                        chunk.firstCellZ + z));

  // Every skirt bottom reaches below the deepest full-resolution sample on the
  // edges this chunk shares, which is what makes a level mismatch filled with
  // geometry instead of sky. A caller-supplied depth is only a floor.
  for (std::size_t slot = 0; slot < boundary; ++slot) {
    const float depth = mesh->skirtDepths[slot];
    assert(depth > 0.F);
    assert(depth >= skirtDepth);
    assert(mesh->vertices.at(surface + slot).y < edgeMinimum);
  }
  // Every skirt vertex is directly below its own top vertex, so the skirt is a
  // wall rather than a stray polygon.
  for (std::size_t slot = 0; slot < boundary; ++slot) {
    const Vec3 bottom = mesh->vertices.at(surface + slot);
    bool matched = false;
    for (std::size_t top = 0; top < surface; ++top) {
      const Vec3 candidate = mesh->vertices.at(top);
      if (candidate.x == bottom.x && candidate.z == bottom.z &&
          roughlyEqual(candidate.y - bottom.y, mesh->skirtDepths[slot], 1e-3F))
        matched = true;
    }
    assert(matched);
  }
  // A skirt that faces inward is back-face culled, and the crack it was supposed to
  // hide is visible again. Every skirt triangle has to face out of the chunk.
  const auto skirtTriangles = boundary * 2;
  const std::size_t surfaceTriangles = mesh->indices.size() / 3 - skirtTriangles;
  const Vec2 centre =
      field.position(chunk.firstCellX + chunk.cellsX / 2, chunk.firstCellZ + chunk.cellsZ / 2);
  for (std::size_t triangle = 0; triangle < skirtTriangles; ++triangle) {
    const std::size_t base = (surfaceTriangles + triangle) * 3;
    const Vec3 a = mesh->vertices.at(mesh->indices.at(base));
    const Vec3 b = mesh->vertices.at(mesh->indices.at(base + 1));
    const Vec3 c = mesh->vertices.at(mesh->indices.at(base + 2));
    const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 v{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3 normal{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z,
                     u.x * v.y - u.y * v.x};
    const float outwardX = (a.x + b.x + c.x) / 3.F - centre.x;
    const float outwardZ = (a.z + b.z + c.z) / 3.F - centre.y;
    assert(normal.x * outwardX + normal.z * outwardZ > 0.F);
  }
  // The surface itself is wound to face up, like the full-resolution chunk
  // builder's triangles, so a reduced chunk is not lit inside out.
  for (std::size_t triangle = 0; triangle < surfaceTriangles; ++triangle) {
    const std::size_t base = triangle * 3;
    const Vec3 a = mesh->vertices.at(mesh->indices.at(base));
    const Vec3 b = mesh->vertices.at(mesh->indices.at(base + 1));
    const Vec3 c = mesh->vertices.at(mesh->indices.at(base + 2));
    const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 v{c.x - a.x, c.y - a.y, c.z - a.z};
    // The Y component of cross(u, v), which is what points a triangle at the sky.
    assert(u.z * v.x - u.x * v.z > 0.F);
  }
  // Skirt triangles are appended, so the reduced surface underneath is untouched.
  const auto plain = buildTerrainLodMesh(field, chunk, TerrainLod::Half, 0.F);
  assert(plain.has_value());
  assert(plain->indices.size() + boundary * 6 == mesh->indices.size());
  assert(std::equal(plain->indices.begin(), plain->indices.end(),
                    mesh->indices.begin()));

  // A full-level chunk shares the same edge samples the neighbour does, so it
  // never carries a skirt whatever depth it is offered.
  const auto full = buildTerrainLodMesh(field, chunk, TerrainLod::Full, skirtDepth);
  assert(full.has_value());
  assert(full->skirtDepths.empty());
}

// One level across the whole field is the case where nothing can crack, so no
// skirt is emitted and the mesh is exactly the reduced surface.
void uniformLevelsNeedNoSkirt() {
  const auto field = generate(steepRecipe());
  for (const auto &chunk : field.chunks) {
    const auto mesh = buildTerrainLodMesh(field, chunk, TerrainLod::Coarse, 0.F);
    assert(mesh.has_value());
    assert(mesh->skirtDepths.empty());
    assert(mesh->indices.size() % 3 == 0);
    const auto withSkirt = buildTerrainLodMesh(field, chunk, TerrainLod::Coarse, 1.F);
    assert(withSkirt.has_value());
    assert(withSkirt->vertices.size() > mesh->vertices.size());
  }
  // A mismatched pair really is the configuration the skirt exists for: the
  // reduced chunk has one, the full chunk next to it does not.
  const auto &chunk = field.chunks.front();
  assert(!buildTerrainLodMesh(field, chunk, TerrainLod::Half, 1.F)->skirtDepths.empty());
  assert(buildTerrainLodMesh(field, chunk, TerrainLod::Full, 1.F)->skirtDepths.empty());
}

// Every triangle has to reference a vertex that exists, and the mesh has to be
// whole triangles. A LOD mesh that does not satisfy this is a corrupt draw.
void indicesAreWellFormed() {
  const auto field = generate(steepRecipe());
  for (const auto &chunk : field.chunks)
    for (const TerrainLod level :
         {TerrainLod::Full, TerrainLod::Half, TerrainLod::Quarter, TerrainLod::Coarse})
      for (const float depth : {0.F, 1.5F}) {
        const auto mesh = buildTerrainLodMesh(field, chunk, level, depth);
        assert(mesh.has_value());
        assert(mesh->indices.size() % 3 == 0);
        assert(!mesh->indices.empty());
        for (const std::size_t index : mesh->indices) {
          assert(index < mesh->vertices.size());
        }
        for (const Vec3 vertex : mesh->vertices) {
          assert(std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
                 std::isfinite(vertex.z));
        }
        assert(level != TerrainLod::Full || mesh->skirtDepths.empty());
      }
}

// Off draws nothing, and a chunk that does not fit inside the field is refused
// rather than sampled past the heightfield's edge.
void invalidRequestsBuildNothing() {
  const auto field = generate(steepRecipe());
  const auto &chunk = field.chunks.front();
  assert(!buildTerrainLodMesh(field, chunk, TerrainLod::Off, 1.F).has_value());

  TerrainChunk outside = chunk;
  outside.firstCellX = field.cellsX;
  assert(!buildTerrainLodMesh(field, outside, TerrainLod::Half, 1.F).has_value());
  assert(!buildTerrainLodMesh(field, outside, TerrainLod::Full, 0.F).has_value());
  assert(terrainLodForChunk(field, outside, 1.F) == TerrainLod::Off);

  TerrainChunk empty = chunk;
  empty.cellsX = 0;
  assert(!buildTerrainLodMesh(field, empty, TerrainLod::Full, 0.F).has_value());
}

// The far edge of a chunk is not always a whole number of steps from its first
// cell. The trailing sample still has to land exactly on the chunk's last cell,
// because that sample is what the neighbouring chunk reads too.
void reducedChunkKeepsItsExactFarEdge() {
  // 64 cells split by chunks of 20 leaves a final row and column of 4.
  const auto field = generate(flatRecipe(64, 20));
  const TerrainChunk &chunk = field.chunks.back();
  assert(chunk.cellsX == 4 && chunk.cellsZ == 4);
  const Vec2 far = field.position(chunk.firstCellX + chunk.cellsX,
                                 chunk.firstCellZ + chunk.cellsZ);
  for (const TerrainLod level : {TerrainLod::Half, TerrainLod::Quarter,
                                 TerrainLod::Coarse}) {
    const auto mesh = buildTerrainLodMesh(field, chunk, level, 0.F);
    assert(mesh.has_value());
    // Regular samples every `step` cells, plus the chunk's exact far edge when that
    // edge is not a whole number of strides away.
    const int step = terrainLodStep(level);
    const auto columns =
        std::size_t(chunk.cellsX / step + 1 + (chunk.cellsX % step != 0 ? 1 : 0));
    const auto rows =
        std::size_t(chunk.cellsZ / step + 1 + (chunk.cellsZ % step != 0 ? 1 : 0));
    assert(columns >= 2 && rows >= 2);
    for (std::size_t row = 0; row < rows; ++row)
      for (std::size_t column = 0; column < columns; ++column) {
        const Vec3 vertex = mesh->vertices.at(row * columns + column);
        // Every column of the lattice is the chunk's last cell, which is the
        // sample the neighbouring chunk reads as its own first one.
        if (column + 1 == columns)
          assert(vertex.x == far.x);
        if (row + 1 == rows)
          assert(vertex.z == far.y);
      }
  }
  // Coarse is the interesting case: a stride of eight over a four-cell chunk
  // leaves a short last hop, and that hop must still land on the shared sample
  // rather than eight cells past the end of the field.
  const auto coarse = buildTerrainLodMesh(field, chunk, TerrainLod::Coarse, 0.F);
  assert(coarse.has_value());
  assert(coarse->vertices.size() == 4);
  assert(coarse->vertices.back().x == far.x && coarse->vertices.back().z == far.y);
}

// The field-wide selector names no chunk, so it has to agree with the per-chunk
// one for the chunk it actually measured. A camera standing on chunk B must not
// be handed the level that chunk A's steepness would justify.
void fieldWideSelectionAgreesWithTheNearestChunk() {
  const auto field = generate(steepRecipe());
  for (const Vec2 camera : std::vector<Vec2>{{1, 1},
                                            {20, 12},
                                            {63, 63},
                                            {64, 40},
                                            {-40, -10},
                                            {200, 200}}) {
    const TerrainChunk *nearest = nullptr;
    float distance = 0;
    for (const auto &chunk : field.chunks) {
      const float candidate = nearestDistance(field, chunk, camera);
      if (!nearest || candidate < distance) {
        distance = candidate;
        nearest = &chunk;
      }
    }
    assert(nearest != nullptr);
    assert(selectTerrainLod(field, camera) ==
           terrainLodForChunk(field, *nearest, distance));
  }
}

// Building one chunk must cost the chunk, not the field. The same chunk of two
// differently shaped fields has to produce the same lattice and the same vertex
// count while reading nothing but its own cells, which is the only way a chunk
// can be built without walking the whole field first.
void buildingOneChunkIgnoresTheRestOfTheField() {
  const auto steep = generate(steepRecipe());
  const auto flat = generate(flatRecipe(64, 16));
  assert(steep.heights != flat.heights);
  assert(steep.chunks.size() == flat.chunks.size());
  for (std::size_t index = 0; index < steep.chunks.size(); ++index) {
    const auto &chunk = steep.chunks[index];
    const auto &other = flat.chunks[index];
    assert(chunk.firstCellX == other.firstCellX &&
           chunk.firstCellZ == other.firstCellZ);
    const auto a = buildTerrainLodMesh(steep, chunk, TerrainLod::Quarter, 1.F);
    const auto b = buildTerrainLodMesh(flat, other, TerrainLod::Quarter, 1.F);
    assert(a.has_value() && b.has_value());
    // Same lattice, therefore the same vertex count, regardless of the surface.
    assert(a->vertices.size() == b->vertices.size());
    assert(a->indices.size() == b->indices.size());
    assert(a->sourceCells == b->sourceCells);
    // But the surface itself came from the chunk's own cells.
    bool differs = false;
    for (std::size_t vertex = 0; vertex < a->vertices.size(); ++vertex)
      differs = differs || a->vertices[vertex].y != b->vertices[vertex].y;
    assert(differs);
    // And every one of them is a real sample of its own field, which is only
    // possible if nothing outside the chunk was consulted.
    const int step = terrainLodStep(TerrainLod::Quarter);
    const auto side = std::size_t(chunk.cellsX / step + 1);
    for (std::size_t vertex = 0; vertex < side * side; ++vertex) {
      const Vec3 sample = a->vertices[vertex];
      const int cellX = chunk.firstCellX + int(vertex % side) * step;
      const int cellZ = chunk.firstCellZ + int(vertex / side) * step;
      assert(sample.y == steep.height(cellX, cellZ));
    }
  }
}
} // namespace

int main() {
  nearestPointBeatsCentre();
  distanceOrdersLevels();
  selectionIsStable();
  storedLevelIgnoresTheCamera();
  geometricErrorKeepsSteepChunksDetailed();
  fullLevelIsTheChunksOwnCells();
  reducedLevelsUsePowerOfTwoSteps();
  reducedVerticesLieOnTheSurface();
  fullLevelNormalsMatchTheField();
  skirtsCoverEveryBoundaryOfAReducedChunk();
  uniformLevelsNeedNoSkirt();
  indicesAreWellFormed();
  invalidRequestsBuildNothing();
  reducedChunkKeepsItsExactFarEdge();
  fieldWideSelectionAgreesWithTheNearestChunk();
  buildingOneChunkIgnoresTheRestOfTheField();
  std::cout << "Terrain LOD checks passed\n";
}