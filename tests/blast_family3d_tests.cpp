#include "demi/runtime/destruction/BlastFamily3D.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace demi::runtime;

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Function> void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Invalid operation was not rejected");
}

std::vector<DestructionChunk3D> chunks() {
  return {{"wall", {1, 0, 0}, 1, false},
          {"foundation", {0, 0, 0}, 1, true},
          {"steel", {2, 0, 0}, 2, false}};
}

std::vector<DestructionBond3D> bonds() {
  return {{"wall-steel", "wall", "steel", 10},
          {"foundation-wall", "foundation", "wall", 1}};
}

std::uint64_t damage(BlastFamily3D &family, const std::string &id,
                     float amount) {
  const BondDamage3D hit{id, amount};
  return family.stage(std::span(&hit, 1));
}

void transactions() {
  BlastFamily3D family(chunks(), bonds());
  const auto intact = family.groups();
  check(intact.size() == 1 && intact[0].anchored,
        "Initial graph is not anchored");
  auto token = damage(family, "foundation-wall", 0.6F);
  check(family.revision() == 0 && family.groups() == intact &&
            family.stagedGroups(token) == intact,
        "Partial damage changed live topology");
  check(family.discard(token), "Discard failed");
  check(!family.commit(token), "Cancelled proposal committed");
  // Two cancelled hits must not accumulate damage in the live family.
  token = damage(family, "foundation-wall", 0.6F);
  check(family.stagedGroups(token) == intact, "Discard leaked bond damage");
  rejects([&] { damage(family, "wall-steel", 1); });
  check(!family.commit(token + 1) && !family.discard(token + 1),
        "Wrong token accepted");
  check(family.commit(token) && family.revision() == 1,
        "Partial damage commit failed");
  check(!family.commit(token), "Duplicate commit accepted");
  rejects([&] { (void)family.stagedGroups(token); });
  token = damage(family, "foundation-wall", 0.6F);
  const std::vector<DestructionGroup3D> separated{{{"foundation"}, true},
                                                  {{"steel", "wall"}, false}};
  check(family.stagedGroups(token) == separated && family.groups() == intact,
        "Persistent damage did not split just the weakened bond");
  check(family.commit(token) && family.groups() == separated,
        "Split commit failed");
  // Snapshot/restore must also work on an already split family. The stronger
  // bond survives this amount, then breaks only after cumulative damage.
  token = damage(family, "wall-steel", 6);
  check(family.stagedGroups(token) == separated,
        "Stronger bond broke too early");
  check(family.commit(token), "Post-split damage commit failed");
  token = damage(family, "wall-steel", 6);
  const std::vector<DestructionGroup3D> singles{
      {{"foundation"}, true}, {{"steel"}, false}, {{"wall"}, false}};
  check(family.stagedGroups(token) == singles,
        "Post-split health did not persist");
  check(family.commit(token) && family.groups() == singles,
        "Final split commit failed");
  token = damage(family, "wall-steel", 1);
  check(family.stagedGroups(token) == singles, "Broken bond changed ownership");
  check(family.commit(token), "No-op split commit failed");
}

void validation() {
  auto c = chunks();
  auto b = bonds();
  rejects([&] { BlastFamily3D f({}, b); });
  rejects([&] { BlastFamily3D f(c, {}); });
  c[0].id = c[1].id;
  rejects([&] { BlastFamily3D f(c, b); });
  c = chunks();
  c[0].volume = std::numeric_limits<float>::infinity();
  rejects([&] { BlastFamily3D f(c, b); });
  c = chunks();
  c[0].centroid[0] = std::numeric_limits<float>::quiet_NaN();
  rejects([&] { BlastFamily3D f(c, b); });
  c = chunks();
  b[0].secondChunk = "missing";
  rejects([&] { BlastFamily3D f(c, b); });
  b = bonds();
  b.push_back({"duplicate-edge", "steel", "wall", 1});
  rejects([&] { BlastFamily3D f(c, b); });
  b = bonds();
  b[0].health = 0;
  rejects([&] { BlastFamily3D f(c, b); });
  BlastFamily3D family(chunks(), bonds());
  rejects([&] { damage(family, "unknown", 1); });
  rejects([&] { damage(family, "wall-steel", -1); });
  rejects([&] {
    damage(family, "wall-steel", std::numeric_limits<float>::infinity());
  });
  const BondDamage3D duplicate[]{{"wall-steel", 1}, {"wall-steel", 1}};
  rejects([&] { (void)family.stage(duplicate); });
  check(family.revision() == 0, "Invalid request mutated family");
  const auto token = damage(family, "foundation-wall", 2);
  check(family.stagedGroups(token).size() == 2,
        "Invalid request poisoned next proposal");
  // Destruction with a pending proposal releases both states via ownership.
}

void anchorTransactions() {
  const DestructionChunk3D chunk{"base", {}, 1, true};
  BlastFamily3D family(std::span(&chunk, 1), {});
  const AnchorDamage3D hit{"base", 0.6F};
  auto token = family.stage({}, std::span(&hit, 1));
  check(family.stagedGroups(token)[0].anchored, "Partial anchor hit released support");
  check(family.discard(token), "Anchor discard failed");
  token = family.stage({}, std::span(&hit, 1));
  check(family.commit(token) && family.anchored("base"), "Anchor health leaked across discard");
  token = family.stage({}, std::span(&hit, 1));
  check(!family.stagedGroups(token)[0].anchored && family.anchored("base"),
        "Anchor proposal mutated live support or failed to release");
  check(family.discard(token) && family.anchored("base"), "Cancelled release leaked");
  token = family.stage({}, std::span(&hit, 1));
  check(family.commit(token) && !family.anchored("base"), "Anchor release failed");
  const AnchorDamage3D missing{"missing", 1};
  rejects([&] { (void)family.stage({}, std::span(&missing, 1)); });
}

void deterministicAndBounded() {
  auto c = chunks();
  auto b = bonds();
  BlastFamily3D first(c, b);
  std::ranges::reverse(c);
  std::ranges::reverse(b);
  BlastFamily3D second(c, b);
  const BondDamage3D a[]{{"wall-steel", 11}, {"foundation-wall", 2}};
  const BondDamage3D reversed[]{a[1], a[0]};
  const auto ta = first.stage(a);
  const auto tb = second.stage(reversed);
  check(first.stagedGroups(ta) == second.stagedGroups(tb),
        "Input order changed partition");
  c.clear();
  b.clear();
  std::vector<BondDamage3D> hits;
  for (int i = 0; i < 256; ++i) {
    c.push_back({"chunk-" + std::to_string(i), {float(i), 0, 0}, 1, i == 0});
    if (i > 0) {
      const auto id = "bond-" + std::to_string(i);
      b.push_back({id, c[i - 1].id, c[i].id, 1});
      hits.push_back({id, 2});
    }
  }
  BlastFamily3D largest(c, b);
  const auto token = largest.stage(hits);
  check(largest.stagedGroups(token).size() == 256,
        "Maximum supported family lost chunks");
  check(largest.discard(token) && largest.groups().size() == 1,
        "Maximum split rollback failed");
  c.push_back({"overflow", {256, 0, 0}});
  rejects([&] { BlastFamily3D tooLarge(c, b); });
  const DestructionChunk3D singleton{"single"};
  BlastFamily3D single(std::span(&singleton, 1), {});
  check(single.groups().size() == 1, "Singleton family failed");
}
} // namespace

int main() {
  try {
    validation();
    anchorTransactions();
    deterministicAndBounded();
    for (int iteration = 0; iteration < 40; ++iteration)
      transactions();
    std::cout
        << "Blast family validation, transactions and repeated damage passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
