#include "demi/runtime/terrain/TerrainMaterialBlend.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace demi::runtime;

namespace {

// A 64x64 world on an 8-cell grid: nine samples per axis at 0, 8, ... 64, so a
// test can name a cell by its grid coordinate and read as a claim about a
// surface rather than about index arithmetic.
constexpr float WorldSize = 64.F;
constexpr int Cells = 8;

std::size_t sampleCount() {
  return std::size_t(Cells + 1) * std::size_t(Cells + 1);
}

struct Scene {
  HeightField field;
  TerrainMasks masks;
};

// Dry flat ground ten units above a water level of zero, far from any water,
// with a modest moisture. Every surface under test is produced by the one mask
// it names, so the baseline sets nothing that any of them reads.
Scene plainScene(float height = 10.F) {
  Scene scene;
  scene.field.size = {WorldSize, WorldSize};
  scene.field.cellsX = scene.field.cellsZ = Cells;
  scene.field.baseHeights.assign(sampleCount(), height);
  scene.field.heights = scene.field.baseHeights;
  scene.field.normals.assign(sampleCount(), Vec3{0, 1, 0});
  scene.field.biomeIndices.assign(sampleCount(), std::size_t{0});
  scene.field.exclusions.assign(sampleCount(), 0.F);
  scene.field.biomeIds = {"default"};
  scene.field.biomeColors = {Color{}};
  scene.masks.slope.assign(sampleCount(), 0.F);
  scene.masks.moisture.assign(sampleCount(), 0.2F);
  scene.masks.waterDistance.assign(sampleCount(), 1000.F);
  scene.masks.flow.assign(sampleCount(), 0.F);
  scene.masks.sediment.assign(sampleCount(), 0.F);
  scene.masks.substrate.assign(sampleCount(), std::size_t{0}); // Soil
  return scene;
}

TerrainMaterialDecision assigned(const char *role, const float score = 0.5F) {
  TerrainMaterialDecision decision;
  decision.roleName = role;
  // Distinct material per role, so a test that compared two roles cannot pass on
  // an id it never meant to check.
  decision.materialAssetId = std::string("asset://materials/terrain/") + role;
  decision.score = score;
  return decision;
}

// One decision per field sample, all the same role: the single-surface field.
std::vector<TerrainMaterialDecision> uniformDecisions(const char *role) {
  std::vector<TerrainMaterialDecision> decisions(sampleCount());
  for (auto &decision : decisions)
    decision = assigned(role);
  return decisions;
}

// A junction across the x axis: `lowRole` on the gentle side, `highRole` on the
// steep one, split at a cell boundary.
std::vector<TerrainMaterialDecision>
splitDecisions(const char *lowRole, const int splitX, const char *highRole) {
  std::vector<TerrainMaterialDecision> decisions(sampleCount());
  for (int z = 0; z <= Cells; ++z)
    for (int x = 0; x <= Cells; ++x)
      decisions[std::size_t(z) * std::size_t(Cells + 1) + std::size_t(x)] =
          assigned(x <= splitX ? lowRole : highRole);
  return decisions;
}

Scene slopeRampScene(float maxSlope = 60.F, float moisture = 0.9F) {
  auto scene = plainScene();
  for (int z = 0; z <= Cells; ++z)
    for (int x = 0; x <= Cells; ++x)
      scene.masks.slope.set(std::size_t(z) * std::size_t(Cells + 1) +
                                std::size_t(x),
                            maxSlope * static_cast<float>(x) /
                                static_cast<float>(Cells));
  scene.masks.moisture.assign(sampleCount(), moisture);
  return scene;
}

// The surface height at a position in cell units, the same bilinear read the
// blend makes. Tests that talk about the water line need it, because the line
// crosses a field between samples rather than on one.
float heightAt(const Scene &scene, const float cellX, const float cellZ) {
  const auto x0 = std::clamp(static_cast<int>(std::floor(cellX)), 0, Cells);
  const auto z0 = std::clamp(static_cast<int>(std::floor(cellZ)), 0, Cells);
  const float tx = std::clamp(cellX - static_cast<float>(x0), 0.F, 1.F);
  const float tz = std::clamp(cellZ - static_cast<float>(z0), 0.F, 1.F);
  const auto at = [&](const int x, const int z) {
    return scene.field.heights[scene.field.index(std::clamp(x, 0, Cells),
                                                  std::clamp(z, 0, Cells))];
  };
  const float bottom = at(x0, z0) * (1.F - tx) + at(x0 + 1, z0) * tx;
  const float top = at(x0, z0 + 1) * (1.F - tx) + at(x0 + 1, z0 + 1) * tx;
  return bottom * (1.F - tz) + top * tz;
}

float weightOf(const TerrainBlendResult &result, const std::size_t vertex,
               const char *role) {
  const auto slot = result.slots.find(role);
  return slot < result.slots.roles.size() ? result.vertices[vertex].weights[slot]
                                          : 0.F;
}

float weightAt(const TerrainBlendResult &result, const std::size_t x,
               const std::size_t z, const char *role) {
  return weightOf(result, z * result.latticeX + x, role);
}

float sumOf(const TerrainBlendVertex &vertex) {
  float sum = 0.F;
  for (const float weight : vertex.weights)
    sum += weight;
  return sum;
}

std::size_t liveSlots(const TerrainBlendVertex &vertex) {
  std::size_t live = 0;
  for (const float weight : vertex.weights)
    if (weight > 0.F)
      ++live;
  return live;
}

// The invariant every consumer of a weight vector depends on: four numbers that
// sum to one and are not all zero. A vertex that failed either would either
// render a hole or divide by zero in the shader, so it is checked everywhere
// rather than in one place.
void everyVertexIsUsable(const TerrainBlendResult &result) {
  assert(!result.vertices.empty());
  assert(result.vertices.size() == result.latticeX * result.latticeZ);
  for (const auto &vertex : result.vertices) {
    assert(std::abs(sumOf(vertex) - 1.F) < 1e-5F);
    assert(liveSlots(vertex) >= 1);
  }
}

void weightsSumToOneWithNoEmptyVertex() {
  const auto scene = slopeRampScene();
  const auto decisions = splitDecisions("grass", 4, "rock");
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions);
  everyVertexIsUsable(result);
  // Two surfaces, so two slots and no folding: nothing here is approximated.
  assert(result.slots.roles.size() == 2);
  assert(result.foldedRoles.empty());
  assert(result.foldedVertices == 0);
  assert(!result.degenerate);
}

// A field whose decisions never disagree has nothing to blend, and says so
// rather than inventing a junction out of float noise.
void identicalDecisionsReportDegenerate() {
  const auto scene = plainScene();
  const auto decisions = uniformDecisions("ground");
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions);
  assert(result.degenerate);
  assert(result.slots.roles.size() == 1);
  assert(result.slots.roles.front() == "ground");
  for (const auto &vertex : result.vertices) {
    assert(sumOf(vertex) == 1.F);
    assert(vertex.weights[0] > 0.999F);
    for (std::size_t slot = 1; slot < terrainBlendLayerCount; ++slot)
      assert(vertex.weights[slot] == 0.F);
  }
  // Reported honestly: a uniform field has no frozen vertices and no folding,
  // because nothing was frozen or folded. Degeneracy is not an error state.
  assert(result.frozenVertices == 0);
  assert(result.maxFoldedWeight == 0.F);
}

// A junction has to produce intermediate weights, not a step, and it has to keep
// doing so as the sample lattice gets finer. This is the claim the whole stage
// exists for: one vertex per cell still shows the transition, four per cell
// shows it better.
void boundaryBlendsMonotonicallyTowardRock() {
  const auto scene = slopeRampScene();
  const auto decisions = splitDecisions("grass", 4, "rock");

  const auto perCell = blendTerrainMaterials(scene.field, scene.masks, decisions);
  everyVertexIsUsable(perCell);
  const auto row = perCell.latticeZ / 2;
  std::size_t mixed = 0;
  float previous = -1.F;
  for (std::size_t x = 0; x < perCell.latticeX; ++x) {
    const float grass = weightAt(perCell, x, row, "grass");
    const float rock = weightAt(perCell, x, row, "rock");
    if (grass > 0.F && rock > 0.F)
      ++mixed;
    // Monotonic toward the rock: never a step back the other way, and the far
    // end really is rock rather than a tie.
    assert(rock >= previous - 1e-6F);
    previous = rock;
    assert(grass <= 1.F);
  }
  assert(mixed > 0);
  assert(previous > 0.9F);
  assert(weightAt(perCell, 0, row, "grass") > 0.9F);

  // The same field at resolution 0.5 has four times the samples, and the
  // transition is visible across more of them rather than being resampled from
  // the coarse lattice.
  TerrainBlendSettings fine;
  fine.resolution = 0.5F;
  const auto finer = blendTerrainMaterials(scene.field, scene.masks, decisions,
                                           fine);
  everyVertexIsUsable(finer);
  assert(finer.vertices.size() == perCell.vertices.size() * 4);
  std::size_t mixedFine = 0;
  for (const auto &vertex : finer.vertices) {
    const float grass = weightOf(finer,
                                 (&vertex - finer.vertices.data()),
                                 "grass");
    const float rock = weightOf(finer, (&vertex - finer.vertices.data()), "rock");
    if (grass > 0.F && rock > 0.F)
      ++mixedFine;
  }
  assert(mixedFine > mixed);
}

// A discrete decision's SCORE is a comparison between roles at one cell. Reading
// it into the weights would reproduce the cell-sized step, so two fields that
// differ only in their scores must blend identically.
void theDiscreteScoreIsNotAnInput() {
  const auto scene = slopeRampScene();
  auto quiet = splitDecisions("grass", 4, "rock");
  auto loud = quiet;
  for (auto &decision : loud)
    decision.score = 5.F;
  const auto one = blendTerrainMaterials(scene.field, scene.masks, quiet);
  const auto two = blendTerrainMaterials(scene.field, scene.masks, loud);
  assert(one.vertices.size() == two.vertices.size());
  for (std::size_t index = 0; index < one.vertices.size(); ++index) {
    for (std::size_t slot = 0; slot < terrainBlendLayerCount; ++slot)
      assert(one.vertices[index].weights[slot] ==
             two.vertices[index].weights[slot]);
  }
}

// Curvature is what slope cannot express. Both runs below share every mask and
// every decision, and differ only in the shape of the ground, so the difference
// in the weights can only have come from the second derivative: a gully keeps
// what a ridge sheds.
void curvatureMovesTheMixWithoutChangingTheMasks() {
  auto flat = plainScene();
  auto gully = plainScene();
  for (int z = 0; z <= Cells; ++z)
    for (int x = 4; x <= 5; ++x)
      gully.field.heights.set(gully.field.index(x, z), 7.F);
  for (auto *scene : {&flat, &gully}) {
    scene->masks.slope.assign(sampleCount(), 10.F);
    scene->masks.moisture.assign(sampleCount(), 0.2F);
    scene->masks.sediment.assign(sampleCount(), 1.F);
  }
  const auto decisions = splitDecisions("ground", 3, "sediment");
  const auto level = blendTerrainMaterials(flat.field, flat.masks, decisions);
  const auto dipped = blendTerrainMaterials(gully.field, gully.masks, decisions);
  everyVertexIsUsable(level);
  everyVertexIsUsable(dipped);
  // The sample that straddles the split, where both roles have real presence, so
  // the comparison is not just "presence changed".
  const auto x = level.latticeX / 2;
  const auto row = level.latticeZ / 2;
  const float levelShare = weightAt(level, x, row, "sediment");
  const float gullyShare = weightAt(dipped, x, row, "sediment");
  assert(levelShare > 0.F && levelShare < 1.F);
  assert(gullyShare > levelShare);
  assert(weightAt(dipped, x, row, "ground") < weightAt(level, x, row, "ground"));
}

// Convexity is the other half: it is what strips a ridge even when the slope is
// the same as the shoulder beside it. The cliff override is pushed out of the
// way so the comparison is about curvature and nothing else.
void convexityLiftsRockOnARidge() {
  auto level = plainScene();
  auto ridge = plainScene();
  for (int z = 0; z <= Cells; ++z)
    for (int x = 4; x <= 5; ++x)
      ridge.field.heights.set(ridge.field.index(x, z), 13.F);
  for (auto *scene : {&level, &ridge}) {
    scene->masks.slope.assign(sampleCount(), 60.F);
    scene->masks.moisture.assign(sampleCount(), 0.2F);
  }
  // A single cliff decision in the middle of the field, so both runs have a face
  // material with presence where the comparison is made.
  std::vector<TerrainMaterialDecision> decisions(sampleCount());
  for (int z = 0; z <= Cells; ++z)
    for (int x = 0; x <= Cells; ++x)
      decisions[level.field.index(x, z)] =
          assigned(x == 4 ? "cliff" : "rock");
  TerrainBlendSettings settings;
  settings.cliffSlope = 85.F;
  const auto flat = blendTerrainMaterials(level.field, level.masks, decisions,
                                          settings);
  const auto peaked = blendTerrainMaterials(ridge.field, ridge.masks, decisions,
                                            settings);
  everyVertexIsUsable(flat);
  everyVertexIsUsable(peaked);
  const auto row = flat.latticeZ / 2;
  const float shoulder = weightAt(flat, flat.latticeX / 2, row, "rock");
  const float crest = weightAt(peaked, peaked.latticeX / 2, row, "rock");
  assert(shoulder > 0.F && shoulder < 1.F);
  assert(crest > shoulder);
  assert(crest > 0.5F);
}

// Above the cliff slope a face is rock, whatever the cell below it decided, and
// the vertex says that the material is to be sampled triplanar.
void cliffsOverrideTheDiscreteDecision() {
  auto scene = plainScene();
  // One face in the middle of a field the discrete stage would have called
  // grass all the way to the edge.
  for (int z = 0; z <= Cells; ++z)
    for (int x = 0; x <= Cells; ++x)
      if (x >= 4 && x <= 5)
        scene.masks.slope.set(scene.field.index(x, z), 70.F);
  const auto decisions = uniformDecisions("grass");
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions);
  everyVertexIsUsable(result);
  // Rock had no cell of its own anywhere on this field and still holds the
  // face: that is the override, not a leaked role.
  assert(result.slots.find("rock") < result.slots.roles.size());
  const auto face = result.latticeZ / 2 * result.latticeX + result.latticeX / 2;
  assert(weightOf(result, face, "rock") > 0.99F);
  assert(weightOf(result, face, "grass") == 0.F);
  assert(result.vertices[face].triplanar);
  // Away from the face the field is ordinary grass, and the flag is off, so the
  // renderer is never told to triplanar a flat field.
  const auto plain = result.latticeZ / 2 * result.latticeX;
  assert(weightOf(result, plain, "grass") > 0.99F);
  assert(!result.vertices[plain].triplanar);
  // Turning the flag off stops the record without changing the weights: the
  // selection is this stage's business, the sampling is the renderer's.
  TerrainBlendSettings planar;
  planar.triplanarCliffs = false;
  const auto flat = blendTerrainMaterials(scene.field, scene.masks, decisions,
                                          planar);
  assert(!flat.vertices[face].triplanar);
  assert(flat.vertices[face].weights == result.vertices[face].weights);
}

// Below the water line there is no surface to blend. A submerged vertex is the
// bed, alone, and says so instead of mixing rock with grass.
void submergedVerticesDoNotBlendWithGrass() {
  auto scene = plainScene();
  scene.masks.moisture.assign(sampleCount(), 0.9F);
  for (int z = 0; z <= Cells; ++z)
    for (int x = 0; x <= Cells; ++x)
      if (x >= 5) {
        scene.field.heights.set(scene.field.index(x, z), -2.F);
        scene.masks.waterDistance.set(scene.field.index(x, z), 0.F);
        scene.masks.substrate.set(scene.field.index(x, z), std::size_t{3});
      }
  std::vector<TerrainMaterialDecision> decisions(sampleCount());
  for (int z = 0; z <= Cells; ++z)
    for (int x = 0; x <= Cells; ++x)
      decisions[scene.field.index(x, z)] =
          assigned(x >= 5 ? "underwater" : "grass");
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions);
  everyVertexIsUsable(result);
  std::size_t bed = 0;
  const auto bedSlot = result.slots.find("underwater");
  assert(bedSlot < result.slots.roles.size());
  for (std::size_t z = 0; z < result.latticeZ; ++z) {
    for (std::size_t x = 0; x < result.latticeX; ++x) {
      const float cellX = static_cast<float>(x) + 0.5F;
      const float cellZ = static_cast<float>(z) + 0.5F;
      const auto &vertex = result.vertices[z * result.latticeX + x];
      if (heightAt(scene, cellX, cellZ) <= 0.F) {
        // Every vertex over the lake is one role, at 1, and frozen.
        ++bed;
        assert(liveSlots(vertex) == 1);
        assert(vertex.frozen);
        for (std::size_t slot = 0; slot < terrainBlendLayerCount; ++slot)
          if (slot != bedSlot)
            assert(vertex.weights[slot] == 0.F);
        assert(vertex.weights[bedSlot] > 0.999F);
        continue;
      }
      // Dry ground: the bed is not a thing there at all, so the shore can be
      // whatever it likes without the lake bleeding up the bank.
      assert(weightAt(result, x, z, "underwater") == 0.F);
    }
  }
  assert(bed > 0);
  assert(result.frozenVertices == bed);
}

// Two runs over equal inputs agree byte for byte, including the slot set. Nothing
// in the path is allowed to depend on visit order, hashing or a RNG.
void twoRunsAreIdentical() {
  const auto scene = slopeRampScene();
  const auto decisions = splitDecisions("grass", 4, "rock");
  const auto one = blendTerrainMaterials(scene.field, scene.masks, decisions);
  const auto two = blendTerrainMaterials(scene.field, scene.masks, decisions);
  assert(one.vertices.size() == two.vertices.size());
  assert(std::memcmp(one.vertices.data(), two.vertices.data(),
                     one.vertices.size() * sizeof(TerrainBlendVertex)) == 0);
  assert(one.slots.roles == two.slots.roles);
  assert(one.slots.materialAssetIds == two.slots.materialAssetIds);
  assert(one.latticeX == two.latticeX && one.latticeZ == two.latticeZ);
  assert(one.degenerate == two.degenerate);
  assert(one.frozenVertices == two.frozenVertices);
  assert(one.foldedVertices == two.foldedVertices);
  assert(one.maxFoldedWeight == two.maxFoldedWeight);
}

// The slot set is a function of WHICH role was chosen and how often, never of
// the order the cells were visited in or of which cell happened to be first.
void slotsAreDeterministicAndOrderStable() {
  auto scene = plainScene();
  auto forward = uniformDecisions("ground");
  // Three cells of each, which is above one lattice sample's share of the field
  // and therefore a real candidate: see the resolution test below for the case
  // where it is not.
  for (int z = 5; z <= 7; ++z)
    for (int x = 5; x <= 7; ++x) {
      scene.masks.slope.set(scene.field.index(x, z), 45.F);
      forward[scene.field.index(x, z)] = assigned("rock");
    }
  for (int z = 1; z <= 3; ++z)
    for (int x = 1; x <= 3; ++x) {
      scene.masks.moisture.set(scene.field.index(x, z), 0.9F);
      forward[scene.field.index(x, z)] = assigned("grass");
    }
  const auto expected = resolveTerrainBlendSlots(forward, 81);
  // Ground was chosen on most of the samples and rock and grass on the same few
  // each, so the order is ground first and the tie resolves by role name.
  assert(expected.roles.size() == 3);
  assert(expected.roles[0] == "ground");
  assert(expected.roles[1] == "grass");
  assert(expected.roles[2] == "rock");

  std::vector<TerrainMaterialDecision> reversed(forward.rbegin(),
                                                forward.rend());
  const auto back = resolveTerrainBlendSlots(reversed, 81);
  assert(back.roles == expected.roles);
  assert(back.materialAssetIds == expected.materialAssetIds);
  std::vector<TerrainMaterialDecision> rotated;
  rotated.assign(forward.begin() + 40, forward.end());
  rotated.insert(rotated.end(), forward.begin(), forward.begin() + 40);
  assert(resolveTerrainBlendSlots(rotated, 81).roles == expected.roles);

  // The blend's slot set depends on how much of the field each role was chosen
  // for, not on where the roles sit: the same counts with the two patches
  // swapped resolve the same four slots.
  //
  // Note what is NOT being claimed here: reversing the decisions vector is not
  // a reordering. That vector is the field's per-cell data, so reversing it
  // moves every role to a different cell and describes a different field. Visit
  // order inside the blend is fixed and row-major, and its determinism is the
  // two-run claim above.
  auto swapped = plainScene();
  auto swappedDecisions = uniformDecisions("ground");
  for (int z = 5; z <= 7; ++z)
    for (int x = 5; x <= 7; ++x) {
      swapped.masks.moisture.set(swapped.field.index(x, z), 0.9F);
      swappedDecisions[swapped.field.index(x, z)] = assigned("grass");
      swapped.masks.slope.set(swapped.field.index(1 + x - 5, z), 45.F);
      swappedDecisions[swapped.field.index(1 + x - 5, z)] = assigned("rock");
    }
  const auto mirrored =
      blendTerrainMaterials(swapped.field, swapped.masks, swappedDecisions);
  assert(mirrored.slots.roles == expected.roles);
  const auto one = blendTerrainMaterials(scene.field, scene.masks, forward);

  // find() is the handle a renderer uses, and an absent role is a miss rather
  // than slot zero.
  assert(one.slots.find("rock") < one.slots.roles.size());
  assert(one.slots.find("snow") == one.slots.roles.size());
  assert(one.slots.materialAssetIds[one.slots.find("rock")] ==
         "asset://materials/terrain/rock");
}

// Four is a cap, and a cap that hides what it dropped is a silent truncation.
// Six surfaces on one field, four slots, and the two that lost are named.
void theSlotCapIsVisible() {
  auto scene = plainScene();
  // A shore-to-summit field: two rows of face, then the dry bands, then the
  // wet ones, then the plateau. Every role below has a band where its own masks
  // make it the sensible answer, so the folded roles really do carry weight
  // rather than being folded at zero.
  const char *roles[] = {"rock",   "grass",      "ground",
                         "sand",   "wet_ground", "rock",
                         "snow",   "grass",      "ground"};
  for (int z = 0; z <= Cells; ++z) {
    for (int x = 0; x <= Cells; ++x) {
      const auto cell = std::size_t(z) * std::size_t(Cells + 1) + std::size_t(x);
      const auto *role = roles[std::size_t(z) % std::size(roles)];
      const bool face = std::string(role) == "rock";
      const bool snow = std::string(role) == "snow" ||
                       std::string(role) == "grass" ||
                       std::string(role) == "ground";
      scene.masks.slope.set(cell, face ? 60.F : 0.F);
      scene.masks.moisture.set(cell, std::string(role) == "grass" ? 0.9F : 0.2F);
      scene.masks.waterDistance.set(cell, std::string(role) == "sand"
                                              ? 5.F
                                              : std::string(role) == "wet_ground"
                                                    ? 1.F
                                                    : 1000.F);
      scene.field.heights.set(cell, snow ? 13.F : 7.F);
    }
  }
  std::vector<TerrainMaterialDecision> decisions(sampleCount());
  for (int z = 0; z <= Cells; ++z)
    for (int x = 0; x <= Cells; ++x)
      decisions[std::size_t(z) * std::size_t(Cells + 1) + std::size_t(x)] =
          assigned(roles[std::size_t(z) % std::size(roles)]);
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions);
  everyVertexIsUsable(result);
  assert(result.slots.roles.size() == terrainBlendLayerCount);
  // Snow and wet ground each hold a row's worth of cells and still lose to the
  // three roles that hold two rows apiece.
  assert(result.foldedRoles.size() == 2);
  assert(std::find(result.foldedRoles.begin(), result.foldedRoles.end(),
                   "snow") != result.foldedRoles.end());
  assert(std::find(result.foldedRoles.begin(), result.foldedRoles.end(),
                   "wet_ground") != result.foldedRoles.end());
  // Sorted, so two runs report the same gap in the same order.
  assert(std::is_sorted(result.foldedRoles.begin(), result.foldedRoles.end()));
  for (const auto &role : result.foldedRoles)
    assert(result.slots.find(role) == result.slots.roles.size());
  // The folded roles were not thrown away: they carry weight, and the report
  // says how much at its worst vertex.
  assert(result.foldedVertices > 0);
  assert(result.maxFoldedWeight > terrainBlendFoldedReportThreshold);
  // Even with six roles wanted, no vertex ever grows a fifth weight.
  for (const auto &vertex : result.vertices) {
    assert(liveSlots(vertex) <= terrainBlendLayerCount);
    assert(std::abs(sumOf(vertex) - 1.F) < 1e-5F);
  }
}

// A role chosen for less of the field than one lattice sample is not resolvable
// by that lattice, so it must not be able to steal a slot from the surfaces that
// actually shape the terrain. It is still reported, not dropped.
void aRoleBelowTheSampleResolutionIsReportedNotRetained() {
  auto scene = plainScene();
  auto decisions = uniformDecisions("ground");
  decisions[scene.field.index(4, 4)] = assigned("snow");
  // A field with 81 decisions and a one-sample-per-cell lattice: one cell is
  // under a sample's share of it, so snow is not a candidate.
  const auto coarse = resolveTerrainBlendSlots(decisions, 64);
  assert(coarse.roles.size() == 1);
  assert(coarse.roles.front() == "ground");
  // With the lattice too small to resolve it, the caller passes 0 and every
  // present role is a candidate again.
  const auto unsized = resolveTerrainBlendSlots(decisions, 0);
  assert(unsized.roles.size() == 2);
  assert(unsized.roles[1] == "snow");
  // And the blend reports the gap rather than hiding it.
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions);
  assert(result.foldedRoles == std::vector<std::string>{"snow"});
}

// resolution is a claim about how many samples per cell the lattice has, and the
// lattice has to honour it exactly rather than approximately.
void resolutionAndPreviewControlTheLattice() {
  const auto scene = slopeRampScene();
  const auto decisions = splitDecisions("grass", 4, "rock");

  const auto standard =
      blendTerrainMaterials(scene.field, scene.masks, decisions);
  assert(standard.latticeX == std::size_t(Cells));
  assert(standard.latticeZ == std::size_t(Cells));
  assert(standard.cellsX == std::size_t(Cells));

  TerrainBlendSettings fine;
  fine.resolution = 0.5F;
  const auto finer = blendTerrainMaterials(scene.field, scene.masks, decisions,
                                           fine);
  assert(finer.latticeX == standard.latticeX * 2);
  assert(finer.vertices.size() == standard.vertices.size() * 4);

  // Preview halves the lattice in each axis, so it costs a quarter of the blend.
  TerrainBlendSettings preview;
  preview.quality = TerrainQuality::Preview;
  const auto cheap = blendTerrainMaterials(scene.field, scene.masks, decisions,
                                           preview);
  assert(cheap.latticeX == std::max<std::size_t>(1, standard.latticeX / 2));
  assert(cheap.vertices.size() == 16);
  // Preview at the fine resolution is still half of that lattice, not the same
  // one: the tier divides whatever the resolution asked for.
  preview.resolution = 0.5F;
  const auto cheapFine =
      blendTerrainMaterials(scene.field, scene.masks, decisions, preview);
  assert(cheapFine.latticeX == finer.latticeX / 2);

  // A resolution finer than one sample per cell is honoured; one that does not
  // divide evenly is rounded down, so the lattice is never finer than asked for.
  TerrainBlendSettings coarse;
  coarse.resolution = 0.3F;
  const auto coarsened =
      blendTerrainMaterials(scene.field, scene.masks, decisions, coarse);
  assert(coarsened.latticeX == std::size_t(Cells) * 3);
  everyVertexIsUsable(coarsened);
}

// The two world-unit thresholds are shared with the discrete stage rather than
// invented a second time, so the blend and the decision cannot mean different
// water levels or different snow lines.
void worldUnitThresholdsComeFromTheDiscreteStage() {
  TerrainMaterialLayerSettings layers;
  layers.snowLine = 12.F;
  layers.waterLevel = 0.F;
  const auto settings =
      TerrainBlendSettings::withMaterialLayers({}, layers);
  assert(settings.snowLine == 12.F);
  assert(settings.waterLevel == 0.F);

  auto scene = plainScene(8.F);
  scene.masks.slope.assign(sampleCount(), 5.F);
  for (int z = 0; z <= Cells; ++z)
    for (int x = 4; x <= Cells; ++x)
      scene.field.heights.set(scene.field.index(x, z), 20.F);
  auto decisions = uniformDecisions("ground");
  for (int z = 0; z <= Cells; ++z)
    for (int x = 4; x <= Cells; ++x)
      decisions[scene.field.index(x, z)] = assigned("snow");
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions,
                                            settings);
  everyVertexIsUsable(result);
  // The plateau is above the authored line and wears it; the plain ten units
  // below it does not, even though the derived line would have been elsewhere.
  const auto row = result.latticeZ / 2;
  assert(weightAt(result, result.latticeX - 1, row, "snow") > 0.9F);
  assert(weightAt(result, 0, row, "snow") == 0.F);
}

// The blend never names a role the discrete stage did not choose somewhere. A
// material appearing from nowhere is the failure that a smoothed decision cannot
// be debugged for, because there is no cell to point at.
void theBlendInventsNoRoles() {
  auto scene = plainScene();
  scene.masks.moisture.assign(sampleCount(), 0.9F);
  scene.masks.slope.set(scene.field.index(7, 7), 40.F);
  auto decisions = uniformDecisions("grass");
  decisions[scene.field.index(7, 7)] = assigned("rock");
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions);
  for (std::size_t index = 0; index < result.vertices.size(); ++index) {
    for (std::size_t slot = 0; slot < result.slots.roles.size(); ++slot)
      if (result.vertices[index].weights[slot] > 0.F)
        assert(result.slots.roles[slot] == "grass" ||
               result.slots.roles[slot] == "rock");
  }
}

// The readable form carries both the role and the material behind it, and it is
// the compact weight vector that stays authoritative.
void weightsReadAsNamedRolesAndMaterials() {
  const auto scene = slopeRampScene();
  const auto decisions = splitDecisions("grass", 4, "rock");
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions);
  std::size_t named = 0;
  for (std::size_t index = 0; index < result.vertices.size(); ++index) {
    const auto weights = terrainBlendVertexWeights(result, index);
    float sum = 0.F;
    for (std::size_t entry = 0; entry < weights.size(); ++entry) {
      const auto slot = result.slots.find(weights[entry].roleName);
      assert(slot < result.slots.roles.size());
      assert(weights[entry].weight == result.vertices[index].weights[slot]);
      assert(weights[entry].materialAssetId ==
             result.slots.materialAssetIds[slot]);
      // Zero weights are omitted, so the list is only what the vertex wears.
      assert(weights[entry].weight > 0.F);
      sum += weights[entry].weight;
      ++named;
    }
    assert(std::abs(sum - 1.F) < 1e-5F);
  }
  assert(named > result.vertices.size());
  bool threw = false;
  try {
    (void)terrainBlendVertexWeights(result, result.vertices.size());
  } catch (const std::out_of_range &) {
    threw = true;
  }
  assert(threw);
}

// The lattice is a continuous field sampled on a grid, so a mesh vertex between
// two lattice samples reads a blend rather than nothing.
void samplingOffTheLatticeStaysNormalised() {
  const auto scene = slopeRampScene();
  const auto decisions = splitDecisions("grass", 4, "rock");
  const auto result = blendTerrainMaterials(scene.field, scene.masks, decisions);
  for (float cellX = 0.F; cellX <= float(Cells); cellX += 0.25F)
    for (float cellZ = 0.F; cellZ <= float(Cells); cellZ += 0.25F) {
      const auto weights = sampleTerrainBlendWeights(result, cellX, cellZ);
      float sum = 0.F;
      for (const float weight : weights)
        sum += weight;
      assert(std::abs(sum - 1.F) < 1e-5F);
    }
  // On a lattice sample the sampler reproduces the vertex exactly, so a mesh
  // that happens to land on the grid is not double-blended.
  const auto sampled =
      sampleTerrainBlendWeights(result, 0.5F, 0.5F);
  for (std::size_t slot = 0; slot < terrainBlendLayerCount; ++slot)
    assert(sampled[slot] == result.vertices[0].weights[slot]);
}

// Every rejected input is rejected loudly. A blend resolved against the wrong
// grid, or with no role at all, produces a plausible-looking surface that no log
// anywhere can explain.
void unusableInputsAreRefused() {
  const auto scene = plainScene();
  const auto decisions = uniformDecisions("ground");

  bool threw = false;
  try {
    (void)blendTerrainMaterials(scene.field, scene.masks,
                                std::vector<TerrainMaterialDecision>(3));
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    auto shortMasks = scene.masks;
    TerrainSamples<float> slope;
    slope.assign(3, 0.F);
    shortMasks.slope = std::move(slope);
    (void)blendTerrainMaterials(scene.field, shortMasks, decisions);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    (void)blendTerrainMaterials(scene.field, scene.masks,
                                std::vector<TerrainMaterialDecision>(sampleCount()));
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    TerrainBlendSettings bad;
    bad.resolution = 0.F;
    (void)blendTerrainMaterials(scene.field, scene.masks, decisions, bad);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    TerrainBlendSettings bad;
    bad.transitionWidth = -1.F;
    (void)blendTerrainMaterials(scene.field, scene.masks, decisions, bad);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    HeightField empty;
    (void)blendTerrainMaterials(empty, scene.masks, decisions);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  // A role name outside the vocabulary is refused rather than blended as an
  // arbitrary surface: a decision stage that grows a role without scoring it
  // would otherwise reach this stage unnoticed.
  threw = false;
  try {
    auto nonsense = decisions;
    nonsense[0].roleName = "lava";
    (void)resolveTerrainBlendSlots(nonsense, 64);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);
}

} // namespace

int main() {
  weightsSumToOneWithNoEmptyVertex();
  identicalDecisionsReportDegenerate();
  boundaryBlendsMonotonicallyTowardRock();
  theDiscreteScoreIsNotAnInput();
  curvatureMovesTheMixWithoutChangingTheMasks();
  convexityLiftsRockOnARidge();
  cliffsOverrideTheDiscreteDecision();
  submergedVerticesDoNotBlendWithGrass();
  twoRunsAreIdentical();
  slotsAreDeterministicAndOrderStable();
  theSlotCapIsVisible();
  aRoleBelowTheSampleResolutionIsReportedNotRetained();
  resolutionAndPreviewControlTheLattice();
  worldUnitThresholdsComeFromTheDiscreteStage();
  theBlendInventsNoRoles();
  weightsReadAsNamedRolesAndMaterials();
  samplingOffTheLatticeStaysNormalised();
  unusableInputsAreRefused();
  std::cout << "Terrain material blend checks passed\n";
}