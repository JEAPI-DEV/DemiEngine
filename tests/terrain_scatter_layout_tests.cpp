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
// this are two runs of the same generation, which is the comparison the
// identity contract is about.
TerrainScatterPlacement placementAt(std::size_t cell, float height,
                                    std::string ruleId,
                                    std::string_view asset) {
  TerrainScatterPlacement placement;
  placement.ruleId = ruleId;
  placement.model = std::string(asset);
  placement.biome = 2;
  placement.cell = cell;
  placement.candidateX = static_cast<int>(cell);
  placement.candidateZ = 0;
  placement.position = {12.5F, height, 30.25F};
  placement.yaw = 1.25F;
  placement.scale = 1.5F;
  placement.collision = TerrainCollisionPolicy::Static;
  return placement;
}

TerrainScatterInstance instanceAt(std::size_t cell, std::string ruleId,
                                  std::string_view asset) {
  return terrainScatterInstanceFrom(placementAt(cell, 4.F, ruleId, asset),
                                    PaletteId, 1337);
}

// The key is a pure function of its inputs, so calling it twice cannot differ,
// and nothing about the order two calls happen in can reach it.
void keyIsDeterministic() {
  const auto first = terrainScatterInstanceKey(PaletteId, "tree", 42, 0);
  const auto second = terrainScatterInstanceKey(PaletteId, "tree", 42, 0);
  assert(first == second);
  // Order independence, expressed by building the same key through a different
  // route and by making sure a fresh call after other work is unchanged.
  const auto third =
      terrainScatterInstanceKey(PaletteId, "tree", 42, 0, {}, {});
  assert(first == third);
  (void)terrainScatterInstanceKey(PaletteId, "grass", 7, 0);
  assert(terrainScatterInstanceKey(PaletteId, "tree", 42, 0) == first);
  // Readable, and built from names rather than ordinals, so adding a ruleId to
  // the enum cannot renumber every existing instance's identity.
  assert(first.rfind("scatter|", 0) == 0);
  assert(first.find("tree") != std::string::npos);
  assert(first.find(PaletteId) != std::string::npos);
  assert(first.find("|42") != std::string::npos);
}

// Every input has to separate, or two different placements would reconcile onto
// one instance and one of them would silently vanish.
void keySeparatesEveryInput() {
  const auto base = terrainScatterInstanceKey(PaletteId, "tree", 42, 0);
  assert(base != terrainScatterInstanceKey("asset://terrain/palettes/other",
                                           "tree", 42, 0));
  assert(base != terrainScatterInstanceKey(PaletteId, "bush", 42, 0));
  assert(base != terrainScatterInstanceKey(PaletteId, "tree", 43, 0));
  assert(base != terrainScatterInstanceKey(PaletteId, "tree", 42, 1));
  assert(base != terrainScatterInstanceKey(PaletteId, "tree", 42, 0, "region"));
  assert(base !=
         terrainScatterInstanceKey(PaletteId, "tree", 42, 0, {}, "decor"));
  // A region and a layer are separate identities, not one combined slot.
  assert(terrainScatterInstanceKey(PaletteId, "tree", 42, 0, "a", "b") !=
         terrainScatterInstanceKey(PaletteId, "tree", 42, 0, "b", "a"));
  // An id containing the separator must not be able to forge another input set.
  assert(terrainScatterInstanceKey("a|b", "tree", 42, 0) !=
         terrainScatterInstanceKey("a", "tree", 42, 0, "b"));
  assert(terrainScatterInstanceKey("a\\b", "tree", 42, 0) !=
         terrainScatterInstanceKey("a", "tree", 42, 0, "\\b"));
}

// The whole contract in one assertion: the same placement resolves to the same
// key and the same hash in two runs, and neither moves when something unrelated
// appears or disappears next to it.
void identitySurvivesRegeneration() {
  const auto first = terrainScatterInstanceFrom(
      placementAt(11, 4.F, "tree", "asset://t/pine"), PaletteId, 1337);
  const auto second = terrainScatterInstanceFrom(
      placementAt(11, 4.F, "tree", "asset://t/pine"), PaletteId, 1337);
  assert(first.id == second.id);
  assert(terrainScatterInstanceHash(first) ==
         terrainScatterInstanceHash(second));
  assert(first.seed == second.seed);

  auto resampled = placementAt(11, 4.F, "tree", "asset://t/pine");
  resampled.cell = 999;
  const auto refined = terrainScatterInstanceFrom(resampled, PaletteId, 1337);
  assert(refined.id == first.id && refined.seed == first.seed);

  // A neighbour being added, and another neighbour being removed, are what a
  // single region re-scatter looks like from outside that region.
  const auto unrelated = instanceAt(12, "tree", "asset://t/pine");
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
  assert(terrainScatterInstanceHash(alone[0]) ==
         terrainScatterInstanceHash(first));
}

// The identity has to follow every input, or a renderer would keep an uploaded
// instance that no longer matches what the palette asks for.
void hashFollowsEveryInput() {
  const auto base = instanceAt(11, "tree", "asset://t/pine");
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
  otherAsset.model = "asset://t/birch";
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
  auto otherCandidate = base;
  otherCandidate.candidateZ = 1;
  differs(otherCandidate);
  auto reSeeded = base;
  reSeeded.seed += 1;
  differs(reSeeded);
  auto rekeyed = base;
  rekeyed.id = terrainScatterInstanceKey(PaletteId, "bush", 11, 0);
  differs(rekeyed);
}

// Collision policy is a per-instance decision, and the roles that never collide
// are the ones that would cost a physics body each if they were treated as a
// single group.
void collisionFollowsThePalette() {
  auto tree = instanceAt(11, "tree", "asset://t/pine");
  assert(tree.collision == TerrainCollisionPolicy::Static);
  assert(terrainScatterWantsCollision(tree));
  // A ruleId that collides explains nothing, because nothing was skipped.
  assert(terrainScatterCollisionSkipReason(tree).empty());

  auto trigger = tree;
  trigger.collision = TerrainCollisionPolicy::Trigger;
  assert(terrainScatterWantsCollision(trigger));
  assert(terrainScatterCollisionSkipReason(trigger).empty());

  auto grass = tree;
  grass.collision = TerrainCollisionPolicy::None;
  assert(!terrainScatterWantsCollision(grass));
  // A decorative ruleId that produces no collider is a decision, and a report
  // that cannot name it is indistinguishable from a bug.
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
      instanceAt(1, "tree", "asset://t/pine"),
      instanceAt(2, "grass", "asset://t/pine"),
      instanceAt(3, "bush", "asset://t/birch"),
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
    if (group.model == "asset://t/pine") {
      assert(group.instances == 2);
      // Members are ordered by instance id, not by position in the vector: the
      // vector order is however the solver happened to produce them, and a
      // batch that depended on it would change with the regeneration that did
      // not move anything.
      assert(group.members.size() == 2);
      assert(instances[group.members[0]].id < instances[group.members[1]].id);
    } else {
      assert(group.instances == 1);
      assert(group.model == "asset://t/birch");
    }
  }
  assert(batched == instances.size());

  // Group numbers depend only on which drawables exist, so visiting the set in
  // another order produces the same numbering for the same instances.
  std::vector<TerrainScatterInstance> shuffled{instances[2], instances[0],
                                               instances[1]};
  const auto again = terrainScatterGroupInstances(shuffled);
  assert(again.size() == groups.size());
  for (std::size_t i = 0; i < groups.size(); ++i)
    assert(again[i].model == groups[i].model);
  for (const auto &instance : shuffled) {
    const auto original =
        std::find_if(instances.begin(), instances.end(),
                     [&](const TerrainScatterInstance &other) {
                       return other.id == instance.id;
                     });
    assert(original != instances.end());
    assert(instance.instanceGroup == original->instanceGroup);
  }

  // A prefab-only ruleId still draws something, and batching it under an empty
  // asset would merge every prefab ruleId into one batch.
  auto prefabOnly = instanceAt(4, "debris", "");
  prefabOnly.prefab = "prefab://rock";
  std::vector<TerrainScatterInstance> mixed{prefabOnly};
  const auto prefabGroups = terrainScatterGroupInstances(mixed);
  assert(prefabGroups.size() == 1);
  assert(prefabGroups[0].model == "prefab://rock");
}

// The seed is part of the identity, so it must be derived from the placement's
// own inputs and separate roles and cells that would otherwise share a stream.
void seedIsPerPlacement() {
  const auto tree = terrainScatterInstanceSeed(1337, "tree", 11, 0);
  assert(tree == terrainScatterInstanceSeed(1337, "tree", 11, 0));
  assert(tree != terrainScatterInstanceSeed(1337, "bush", 11, 0));
  assert(tree != terrainScatterInstanceSeed(1337, "tree", 12, 0));
  // A different world seed reshuffles, which is what a new terrain is for.
  assert(tree != terrainScatterInstanceSeed(4242, "tree", 11, 0));
  // Never zero: a derived seed has to be distinguishable from an unset one.
  assert(tree != 0);

  const auto instance = terrainScatterInstanceFrom(
      placementAt(11, 4.F, "tree", "asset://t/pine"), PaletteId, 1337);
  assert(instance.seed == tree);
}

// Two equal reals must hash equally and two non-finite values must not depend
// on their payloads, or an instance would look changed while nothing about it
// was.
void hashCanonicalisesFloats() {
  auto reference = instanceAt(11, "tree", "asset://t/pine");
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
  seedIsPerPlacement();
  hashCanonicalisesFloats();
  std::cout << "Terrain scatter layout checks passed\n";
}
