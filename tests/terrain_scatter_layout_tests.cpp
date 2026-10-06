#include "demi/runtime/terrain/TerrainScatterLayout.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace demi::runtime;

namespace {

constexpr std::string_view PaletteId = "asset://terrain/palettes/meadow";

// One placement as the solver would have produced it. Two independent calls to
// this are two runs of the same generation, which is the comparison the identity
// contract is about.
TerrainScatterPlacement placementAt(std::size_t cell, float height,
                                    TerrainPaletteRole role,
                                    std::string_view asset) {
  TerrainScatterPlacement placement;
  placement.role = role;
  placement.asset = std::string(asset);
  placement.biome = 2;
  placement.cell = cell;
  placement.position = {12.5F, height, 30.25F};
  placement.yaw = 1.25F;
  placement.scale = 1.5F;
  placement.collision = TerrainCollisionPolicy::Static;
  placement.lod = 2;
  return placement;
}

TerrainScatterInstance instanceAt(std::size_t cell, TerrainPaletteRole role,
                                  std::string_view asset) {
  return terrainScatterInstanceFrom(placementAt(cell, 4.F, role, asset),
                                    PaletteId, 1337);
}

// The key is a pure function of its inputs, so calling it twice cannot differ,
// and nothing about the order two calls happen in can reach it.
void keyIsDeterministic() {
  const auto first = terrainScatterInstanceKey(PaletteId,
                                               TerrainPaletteRole::Tree, 42);
  const auto second = terrainScatterInstanceKey(PaletteId,
                                                TerrainPaletteRole::Tree, 42);
  assert(first == second);
  // Order independence, expressed by building the same key through a different
  // route and by making sure a fresh call after other work is unchanged.
  const auto third = terrainScatterInstanceKey(
      PaletteId, TerrainPaletteRole::Tree, 42, {}, {});
  assert(first == third);
  (void)terrainScatterInstanceKey(PaletteId, TerrainPaletteRole::Grass, 7);
  assert(terrainScatterInstanceKey(PaletteId, TerrainPaletteRole::Tree, 42) ==
         first);
  // Readable, and built from names rather than ordinals, so adding a role to the
  // enum cannot renumber every existing instance's identity.
  assert(first.rfind("scatter|", 0) == 0);
  assert(first.find("tree") != std::string::npos);
  assert(first.find(PaletteId) != std::string::npos);
  assert(first.find("|42") != std::string::npos);
}

// Every input has to separate, or two different placements would reconcile onto
// one instance and one of them would silently vanish.
void keySeparatesEveryInput() {
  const auto base = terrainScatterInstanceKey(PaletteId,
                                              TerrainPaletteRole::Tree, 42);
  assert(base != terrainScatterInstanceKey("asset://terrain/palettes/other",
                                           TerrainPaletteRole::Tree, 42));
  assert(base != terrainScatterInstanceKey(PaletteId,
                                           TerrainPaletteRole::Bush, 42));
  assert(base != terrainScatterInstanceKey(PaletteId,
                                           TerrainPaletteRole::Tree, 43));
  assert(base != terrainScatterInstanceKey(PaletteId,
                                           TerrainPaletteRole::Tree, 42,
                                           "region"));
  assert(base != terrainScatterInstanceKey(PaletteId,
                                           TerrainPaletteRole::Tree, 42,
                                           {}, "decor"));
  // A region and a layer are separate identities, not one combined slot.
  assert(terrainScatterInstanceKey(PaletteId, TerrainPaletteRole::Tree, 42,
                                   "a", "b") !=
         terrainScatterInstanceKey(PaletteId, TerrainPaletteRole::Tree, 42,
                                   "b", "a"));
  // An id containing the separator must not be able to forge another input set.
  assert(terrainScatterInstanceKey("a|b", TerrainPaletteRole::Tree, 42) !=
         terrainScatterInstanceKey("a", TerrainPaletteRole::Tree, 42, "b"));
  assert(terrainScatterInstanceKey("a\\b", TerrainPaletteRole::Tree, 42) !=
         terrainScatterInstanceKey("a", TerrainPaletteRole::Tree, 42, "\\b"));
}

// The whole contract in one assertion: the same placement resolves to the same
// key and the same hash in two runs, and neither moves when something unrelated
// appears or disappears next to it.
void identitySurvivesRegeneration() {
  const auto first = terrainScatterInstanceFrom(
      placementAt(11, 4.F, TerrainPaletteRole::Tree, "asset://t/pine"),
      PaletteId, 1337);
  const auto second = terrainScatterInstanceFrom(
      placementAt(11, 4.F, TerrainPaletteRole::Tree, "asset://t/pine"),
      PaletteId, 1337);
  assert(first.id == second.id);
  assert(terrainScatterInstanceHash(first) == terrainScatterInstanceHash(second));
  assert(first.seed == second.seed);

  // A neighbour being added, and another neighbour being removed, are what a
  // single region re-scatter looks like from outside that region.
  const auto unrelated = instanceAt(12, TerrainPaletteRole::Tree, "asset://t/pine");
  std::vector<TerrainScatterInstance> instances{first, unrelated};
  auto groups = terrainScatterGroupInstances(instances);
  assert(instances[0].id == first.id);
  assert(terrainScatterInstanceHash(instances[0]) ==
         terrainScatterInstanceHash(first));
  // Grouping is derived from the set, so it must not be part of the identity.
  assert(instances[0].instanceGroup == 0);
  assert(groups.size() == 1);

  std::vector<TerrainScatterInstance> alone{first};
  const auto aloneGroups = terrainScatterGroupInstances(alone);
  assert(aloneGroups.size() == 1);
  assert(alone[0].instanceGroup == 0);
  assert(terrainScatterInstanceHash(alone[0]) == terrainScatterInstanceHash(first));
}

// The identity has to follow every input, or a renderer would keep an uploaded
// instance that no longer matches what the palette asks for.
void hashFollowsEveryInput() {
  const auto base = instanceAt(11, TerrainPaletteRole::Tree, "asset://t/pine");
  const auto reference = terrainScatterInstanceHash(base);
  auto differs = [&](TerrainScatterInstance changed) {
    assert(terrainScatterInstanceHash(changed) != reference);
  };

  auto moved = base;
  moved.position.y += 0.25F;
  differs(moved);
  auto turned = base;
  turned.yaw += 0.1F;
  differs(turned);
  auto resized = base;
  resized.scale *= 2.F;
  differs(resized);
  auto otherAsset = base;
  otherAsset.asset = "asset://t/birch";
  differs(otherAsset);
  auto prefabbed = base;
  prefabbed.prefab = "prefab://tree";
  differs(prefabbed);
  auto recoloured = base;
  recoloured.biome = 3;
  differs(recoloured);
  auto reCell = base;
  reCell.cell = 12;
  differs(reCell);
  auto reSeeded = base;
  reSeeded.seed += 1;
  differs(reSeeded);
  auto relodded = base;
  relodded.lod = 3;
  differs(relodded);
  auto rekeyed = base;
  rekeyed.id = terrainScatterInstanceKey(PaletteId,
                                          TerrainPaletteRole::Bush, 11);
  differs(rekeyed);
}

// Collision policy is a per-instance decision, and the roles that never collide
// are the ones that would cost a physics body each if they were treated as a
// single group.
void collisionFollowsThePalette() {
  auto tree = instanceAt(11, TerrainPaletteRole::Tree, "asset://t/pine");
  assert(tree.collision == TerrainCollisionPolicy::Static);
  assert(terrainScatterWantsCollision(tree));
  // A role that collides explains nothing, because nothing was skipped.
  assert(terrainScatterCollisionSkipReason(tree).empty());

  auto trigger = tree;
  trigger.collision = TerrainCollisionPolicy::Trigger;
  assert(terrainScatterWantsCollision(trigger));
  assert(terrainScatterCollisionSkipReason(trigger).empty());

  auto grass = tree;
  grass.collision = TerrainCollisionPolicy::None;
  assert(!terrainScatterWantsCollision(grass));
  // A decorative role that produces no collider is a decision, and a report that
  // cannot name it is indistinguishable from a bug.
  assert(!terrainScatterCollisionSkipReason(grass).empty());

  // Per instance, not per placement: the two roles differ even though they came
  // from one palette and one pass.
  const auto wanted = terrainScatterWantsCollision(tree);
  assert(wanted != terrainScatterWantsCollision(grass));
}

// Instances of one asset batch together and different assets do not, which is
// the only question an instancer asks.
void groupingBatchesByDrawable() {
  std::vector<TerrainScatterInstance> instances{
      instanceAt(1, TerrainPaletteRole::Tree, "asset://t/pine"),
      instanceAt(2, TerrainPaletteRole::Grass, "asset://t/pine"),
      instanceAt(3, TerrainPaletteRole::Bush, "asset://t/birch"),
  };
  const auto groups = terrainScatterGroupInstances(instances);
  assert(groups.size() == 2);
  // Two of them share an asset, so they share a batch even across roles.
  assert(instances[0].instanceGroup == instances[1].instanceGroup);
  assert(instances[2].instanceGroup != instances[0].instanceGroup);
  std::size_t batched = 0;
  for (const auto &group : groups) {
    assert(group.instances == group.members.size());
    batched += group.instances;
    if (group.asset == "asset://t/pine") {
      assert(group.instances == 2);
      // Members are ordered by instance id, not by position in the vector: the
      // vector order is however the solver happened to produce them, and a
      // batch that depended on it would change with the regeneration that did
      // not move anything.
      assert(group.members.size() == 2);
      assert(instances[group.members[0]].id < instances[group.members[1]].id);
    } else {
      assert(group.instances == 1);
      assert(group.asset == "asset://t/birch");
    }
  }
  assert(batched == instances.size());

  // Group numbers depend only on which drawables exist, so visiting the set in
  // another order produces the same numbering for the same instances.
  std::vector<TerrainScatterInstance> shuffled{
      instances[2], instances[0], instances[1]};
  const auto again = terrainScatterGroupInstances(shuffled);
  assert(again.size() == groups.size());
  for (std::size_t i = 0; i < groups.size(); ++i)
    assert(again[i].asset == groups[i].asset);
  for (const auto &instance : shuffled) {
    const auto original = std::find_if(
        instances.begin(), instances.end(),
        [&](const TerrainScatterInstance &other) {
          return other.id == instance.id;
        });
    assert(original != instances.end());
    assert(instance.instanceGroup == original->instanceGroup);
  }

  // A prefab-only role still draws something, and batching it under an empty
  // asset would merge every prefab role into one batch.
  auto prefabOnly = instanceAt(4, TerrainPaletteRole::Debris, "");
  prefabOnly.prefab = "prefab://rock";
  std::vector<TerrainScatterInstance> mixed{prefabOnly};
  const auto prefabGroups = terrainScatterGroupInstances(mixed);
  assert(prefabGroups.size() == 1);
  assert(prefabGroups[0].asset == "prefab://rock");
}

// LOD is a pure function of the instance and the camera distance, always inside
// the range the palette declared.
void lodStaysInsideTheDeclaredRange() {
  auto instance = instanceAt(11, TerrainPaletteRole::Tree, "asset://t/pine");
  instance.lod = 3; // four levels, 0 through 3

  // Up to the LOD distance the finest level holds.
  assert(terrainScatterLodFor(instance, 0.F, 20.F, 60.F) == 0);
  assert(terrainScatterLodFor(instance, 19.9F, 20.F, 60.F) == 0);
  assert(terrainScatterLodFor(instance, 20.F, 20.F, 60.F) == 0);
  // Between the two distances the levels are distributed across the span, and
  // they only ever get coarser as the camera pulls away.
  assert(terrainScatterLodFor(instance, 21.F, 20.F, 60.F) == 0);
  const auto middle = terrainScatterLodFor(instance, 40.F, 20.F, 60.F);
  assert(middle > 0 && middle < 3);
  assert(terrainScatterLodFor(instance, 60.F, 20.F, 60.F) == 3);

  // No cull distance means lodDistance alone decides: finest up to it, coarsest
  // beyond it.
  assert(terrainScatterLodFor(instance, 20.F, 20.F, 0.F) == 0);
  assert(terrainScatterLodFor(instance, 20.5F, 20.F, 0.F) == 3);

  // A zero LOD distance disables the switching. Reading it as "already past every
  // threshold" would collapse a whole forest to its coarsest mesh.
  assert(terrainScatterLodFor(instance, 0.F, 0.F, 60.F) == 0);
  assert(terrainScatterLodFor(instance, 5000.F, 0.F, 60.F) == 0);
  assert(terrainScatterLodFor(instance, 5000.F, -1.F, 60.F) == 0);

  // Whatever the camera does, the answer indexes a mesh array the palette built.
  for (float distance = -10.F; distance < 500.F; distance += 3.5F)
    for (const auto lod : {0, 1, 2, 3}) {
      instance.lod = lod;
      const auto level =
          terrainScatterLodFor(instance, distance, 20.F, 60.F);
      assert(level >= 0 && level <= lod);
    }

  // A palette that declares no extra levels is pinned to level 0, and a negative
  // declaration cannot produce a negative index.
  instance.lod = 0;
  assert(terrainScatterLodFor(instance, 5000.F, 20.F, 60.F) == 0);
  instance.lod = -1;
  assert(terrainScatterLodFor(instance, 5000.F, 20.F, 60.F) == 0);

  // A negative distance is a camera on the wrong side of the terrain, and it
  // resolves to the nearest level rather than into the array.
  instance.lod = 3;
  assert(terrainScatterLodFor(instance, -500.F, 20.F, 60.F) == 0);
}

// The seed is part of the identity, so it must be derived from the placement's
// own inputs and separate roles and cells that would otherwise share a stream.
void seedIsPerPlacement() {
  const auto tree = terrainScatterInstanceSeed(1337, TerrainPaletteRole::Tree, 11);
  assert(tree == terrainScatterInstanceSeed(1337, TerrainPaletteRole::Tree, 11));
  assert(tree != terrainScatterInstanceSeed(1337, TerrainPaletteRole::Bush, 11));
  assert(tree != terrainScatterInstanceSeed(1337, TerrainPaletteRole::Tree, 12));
  // A different world seed reshuffles, which is what a new terrain is for.
  assert(tree != terrainScatterInstanceSeed(4242, TerrainPaletteRole::Tree, 11));
  // Never zero: a derived seed has to be distinguishable from an unset one.
  assert(tree != 0);

  const auto instance = terrainScatterInstanceFrom(
      placementAt(11, 4.F, TerrainPaletteRole::Tree, "asset://t/pine"),
      PaletteId, 1337);
  assert(instance.seed == tree);
}

// Two equal reals must hash equally and two non-finite values must not depend on
// their payloads, or an instance would look changed while nothing about it was.
void hashCanonicalisesFloats() {
  auto reference = instanceAt(11, TerrainPaletteRole::Tree, "asset://t/pine");
  const auto digest = terrainScatterInstanceHash(reference);

  // A sculpted ground can land an instance at exactly zero, and the arithmetic
  // that puts it there can produce either signed zero. Both are the same
  // placement.
  auto zero = reference;
  zero.position.y = 0.F;
  auto negativeZero = zero;
  negativeZero.position.y = -0.F;
  const auto zeroDigest = terrainScatterInstanceHash(zero);
  assert(terrainScatterInstanceHash(negativeZero) == zeroDigest);
  assert(zeroDigest != digest);

  auto first = reference;
  first.scale = std::numeric_limits<float>::quiet_NaN();
  auto second = reference;
  second.scale = std::bit_cast<float>(std::uint32_t{0x7FC00001U});
  assert(terrainScatterInstanceHash(first) ==
         terrainScatterInstanceHash(second));

  auto infinity = reference;
  infinity.yaw = std::numeric_limits<float>::infinity();
  auto negative = reference;
  negative.yaw = -std::numeric_limits<float>::infinity();
  assert(terrainScatterInstanceHash(infinity) !=
         terrainScatterInstanceHash(negative));
}
} // namespace

int main() {
  keyIsDeterministic();
  keySeparatesEveryInput();
  identitySurvivesRegeneration();
  hashFollowsEveryInput();
  collisionFollowsThePalette();
  groupingBatchesByDrawable();
  lodStaysInsideTheDeclaredRange();
  seedIsPerPlacement();
  hashCanonicalisesFloats();
  std::cout << "Terrain scatter layout checks passed\n";
}
