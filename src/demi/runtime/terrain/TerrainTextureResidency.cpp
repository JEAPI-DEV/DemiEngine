#include "demi/runtime/terrain/TerrainTextureResidency.h"

#include "demi/assets/AssetRegistry.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace demi::runtime {
namespace {

using assets::TerrainMaterialMapSlot;

// The granularity a real driver rounds an allocation up to: mip chains and
// block-aligned rows never produce an arbitrary byte count, so pretending they
// do would let a plan claim two allocations where there is one.
constexpr std::size_t AllocatorGranularity = 256u * 1024u;
// Hardware tops out well below this; a larger authored value is clamped rather
// than truncated into a nonsense sample count.
constexpr std::size_t MaximumAnisotropy = 16u;

struct SlotProfile {
  TerrainMaterialMapSlot slot;
  std::size_t evictionRank;
  std::size_t edge;
  std::size_t channels;
  // Colour-bearing maps are authored in a display space and sampled through an
  // sRGB format. Data maps are never sampled that way: decoding a normal
  // vector through sRGB corrupts it and the shading breaks.
  bool colourBearing;
  // True where aliasing is visible as a recognisable blur along a ground plane,
  // so the budget's anisotropy is worth its sampling cost. False where the
  // aliasing reads as shading noise instead, and the extra bandwidth buys
  // nothing.
  bool distanceAnisotropy;
};

// Canonical slot order: the order `maps` iterates in, the order a renderer
// binds slots in, and the order the plan reports entries in.
constexpr SlotProfile Slots[]{
    {TerrainMaterialMapSlot::BaseColor, 3, 2048, 4, true, true},
    {TerrainMaterialMapSlot::Normal, 4, 2048, 4, false, true},
    {TerrainMaterialMapSlot::Roughness, 6, 1024, 4, false, false},
    {TerrainMaterialMapSlot::Metallic, 7, 512, 4, false, false},
    {TerrainMaterialMapSlot::AmbientOcclusion, 5, 1024, 4, false, false},
    {TerrainMaterialMapSlot::Height, 1, 1024, 4, false, true},
    {TerrainMaterialMapSlot::Detail, 0, 2048, 4, false, true},
    {TerrainMaterialMapSlot::Emissive, 2, 1024, 4, true, false},
};

// The vocabulary is closed and the table above covers it, so an unknown slot
// can only arrive from a corrupted call. Falling back to base colour keeps the
// function total rather than throwing from a lookup, matching the slot-name
// helper the material asset itself uses.
const SlotProfile &profile(const TerrainMaterialMapSlot slot) {
  for (const SlotProfile &entry : Slots)
    if (entry.slot == slot)
      return entry;
  return Slots[0];
}

std::size_t slotIndex(const TerrainMaterialMapSlot slot) {
  for (std::size_t index = 0; index < std::size(Slots); ++index)
    if (Slots[index].slot == slot)
      return index;
  return 0;
}

// The coarsest level each slot still contributes at. Survival is NESTED: a slot
// gone at some level is gone at every coarser one, which is what makes
// "progressively fewer slots with distance" true by construction rather than by
// eight independent rules agreeing. The first three levels are the ones
// TerrainLodSettings' default distance table can select for a visible chunk;
// Coarse is included because a far chunk is still drawn, and it keeps only the
// one map a surface cannot do without.
constexpr std::pair<TerrainMaterialMapSlot, TerrainLod> LastLevelKept[]{
    {TerrainMaterialMapSlot::BaseColor, TerrainLod::Coarse},
    {TerrainMaterialMapSlot::Normal, TerrainLod::Quarter},
    {TerrainMaterialMapSlot::Roughness, TerrainLod::Quarter},
    {TerrainMaterialMapSlot::Metallic, TerrainLod::Half},
    {TerrainMaterialMapSlot::AmbientOcclusion, TerrainLod::Quarter},
    {TerrainMaterialMapSlot::Height, TerrainLod::Full},
    {TerrainMaterialMapSlot::Detail, TerrainLod::Full},
    {TerrainMaterialMapSlot::Emissive, TerrainLod::Half},
};

bool survives(const TerrainMaterialMapSlot slot, const TerrainLod level) {
  for (const auto &[kept, last] : LastLevelKept)
    if (kept == slot)
      return terrainLodIndex(level) <= terrainLodIndex(last);
  return false;
}

std::size_t estimateBytes(const SlotProfile &slot) {
  const std::size_t base = slot.edge * slot.edge * slot.channels;
  // 4/3 for a full mip chain, truncated to whole bytes rather than rounded up:
  // the granularity step below already absorbs the difference.
  const std::size_t mipped = base + base / 3;
  return ((mipped + AllocatorGranularity - 1) / AllocatorGranularity) *
         AllocatorGranularity;
}

int anisotropyFor(const TerrainResidencyBudget &budget,
                  const SlotProfile &slot) {
  if (!slot.distanceAnisotropy)
    return 1;
  const std::size_t requested =
      std::clamp<std::size_t>(budget.anisotropy, 1u, MaximumAnisotropy);
  return static_cast<int>(requested);
}

// A map whose own authored strength is zero cannot change the surface, so
// sampling it would be resident state bought for nothing. Two slots can be in
// that state and no other: the detail blend and the normal amplification.
bool strengthEnables(const TerrainMaterialMapSlot slot,
                     const assets::TerrainMaterialAsset &material) {
  switch (slot) {
  case TerrainMaterialMapSlot::Detail:
    return material.detailStrength > 0.F;
  case TerrainMaterialMapSlot::Normal:
    return material.normalStrength > 0.F;
  default:
    return true;
  }
}

std::string strengthField(const TerrainMaterialMapSlot slot) {
  return slot == TerrainMaterialMapSlot::Detail ? "detail_strength"
                                                : "normal_strength";
}

[[noreturn]] void fail(std::string message) {
  throw std::invalid_argument(std::move(message));
}

std::string join(const std::vector<std::string> &values,
                 const std::string_view separator) {
  std::string joined;
  for (const std::string &value : values) {
    if (!joined.empty())
      joined += separator;
    joined += value;
  }
  return joined;
}

// One GPU texture: every planned entry that names this asset id. Eviction moves
// whole groups, because a texture still sampled by any entry cannot be
// released.
struct TextureGroup {
  std::string assetId;
  std::vector<std::size_t> entries;
  std::size_t bytes = 0;
  std::size_t dependents = 0;
  // The MOST important slot any of its entries samples. A texture one material
  // reads as a detail break-up and another as its base colour is kept for the
  // base colour: importance is judged by the best use it serves.
  std::size_t importance = 0;
};

// The slot whose importance decided this group's rank, named in the eviction
// reason so the message says which USE of a multi-slot texture it is reporting.
// Ties resolve to the canonically later slot, which is the one whose map the
// budget could least afford to lose.
TerrainMaterialMapSlot
mostImportantSlot(const std::vector<TerrainTextureResidency> &textures,
                  const TextureGroup &group) {
  TerrainMaterialMapSlot best = TerrainMaterialMapSlot::BaseColor;
  std::size_t bestRank = 0;
  for (const std::size_t index : group.entries) {
    const std::size_t rank = terrainResidencyEvictionRank(textures[index].slot);
    if (rank >= bestRank) {
      bestRank = rank;
      best = textures[index].slot;
    }
  }
  return best;
}

} // namespace

std::vector<TerrainLod>
terrainResidencyHeldLevels(const TerrainQuality quality) {
  // Standard holds three levels, near to far. Preview holds two and drops the
  // FULL level entirely: a tier that kept Full would still sample the detail,
  // height and emissive maps, so "preview" would be a bool that renamed the
  // budget instead of changing what the terrain holds.
  if (quality == TerrainQuality::Preview)
    return {TerrainLod::Half, TerrainLod::Quarter};
  return {TerrainLod::Full, TerrainLod::Half, TerrainLod::Quarter};
}

std::vector<TerrainMaterialMapSlot>
terrainSlotsForLod(const TerrainMaterialMapSlot slot, const TerrainLod level) {
  // The caller's slot is deliberately not filtered out of the answer: the
  // result is every slot the LEVEL keeps, and finding `slot` in it is exactly
  // how a caller asks whether its own map survives that level.
  (void)slot;
  std::vector<TerrainMaterialMapSlot> kept;
  for (const SlotProfile &entry : Slots)
    if (survives(entry.slot, level))
      kept.push_back(entry.slot);
  return kept;
}

std::size_t terrainResidencyEvictionRank(const TerrainMaterialMapSlot slot) {
  return profile(slot).evictionRank;
}

TerrainResidencyNominalFormat
terrainResidencyNominalFormat(const TerrainMaterialMapSlot slot) {
  const SlotProfile &entry = profile(slot);
  return {.edge = entry.edge,
          .channels = entry.channels,
          .colourBearing = entry.colourBearing};
}

std::size_t terrainResidencyEstimateBytes(const TerrainMaterialMapSlot slot) {
  return estimateBytes(profile(slot));
}

TerrainResidencyPlan planTerrainTextureResidency(
    const std::vector<TerrainResidencyMaterial> &materials,
    const TerrainResidencyBudget &budget) {
  // Refusals first. A zero cap is not "make everything evict": it is a budget
  // nobody filled in, and a plan built from it looks like a deliberate decision
  // to drop the entire surface.
  if (materials.empty())
    fail(
        "Terrain texture residency needs at least one material: a terrain with "
        "no material has no texture to keep resident, and an empty plan would "
        "read as a successful decision to keep none. Resolve the "
        "TerrainMaterialSet and pass the materials its roles bind.");
  if (budget.textureBytes == 0)
    fail(
        "Terrain texture residency needs a texture byte budget greater than 0, "
        "but was given 0. A zero budget evicts every texture and reports it as "
        "a plan; raise TerrainResidencyBudget::textureBytes to what the "
        "platform quality tier can afford.");
  if (budget.distinctTextures == 0)
    fail("Terrain texture residency needs a distinct texture cap greater than "
         "0, "
         "but was given 0. Every texture costs a binding and a sampler state "
         "whatever its size, so a cap of zero admits no texture at all; raise "
         "TerrainResidencyBudget::distinctTextures instead.");

  const std::vector<TerrainLod> held =
      terrainResidencyHeldLevels(budget.quality);
  const std::string tier(terrainQualityName(budget.quality));

  // Sorted before anything is read, so two runs over the same set of materials
  // visit them in the same order and cannot disagree through an iteration
  // order.
  std::vector<TerrainResidencyMaterial> ordered = materials;
  std::ranges::sort(ordered, {}, [](const TerrainResidencyMaterial &material) {
    return std::tie(material.roleName, material.material.id);
  });

  TerrainResidencyPlan plan;
  std::vector<std::size_t> planIndex;
  std::vector<std::string> trimmedReasons;

  TerrainResidencyMaterial previous{"", {}};
  bool havePrevious = false;
  for (const TerrainResidencyMaterial &material : ordered) {
    if (material.material.id.empty())
      fail(
          "Terrain texture residency was given a material with no asset id for "
          "role \"" +
          material.roleName +
          "\". A material without an id cannot be traced back to its document, "
          "so its textures could not be reported or re-planned.");
    // The same role naming the same material twice is a redundant request, not
    // a second dependant: the upload is shared either way.
    if (havePrevious && previous.roleName == material.roleName &&
        previous.material.id == material.material.id)
      continue;
    previous = material;
    havePrevious = true;

    for (const auto &[slot, reference] : material.material.maps) {
      const std::string slotName(assets::terrainMaterialMapSlotName(slot));
      const std::string label = material.roleName + " " + slotName;
      // A material built without the loader can carry an empty reference. It is
      // named rather than skipped in silence, because a missing request and an
      // absent one must not look the same in a report.
      if (reference.empty()) {
        trimmedReasons.push_back(
            label + ": the map has no asset reference, so there is "
                    "no texture to keep resident.");
        continue;
      }

      if (!strengthEnables(slot, material.material)) {
        trimmedReasons.push_back(
            label + " " + reference + ": " + strengthField(slot) +
            " is 0, so the map cannot change the surface and is not made "
            "resident.");
        continue;
      }

      // The coarsest HELD level that still samples this slot. Held levels are
      // contiguous, so this is the last element of the held set that survives.
      TerrainLod minimumLod = TerrainLod::Off;
      bool sampled = false;
      for (const TerrainLod level : held)
        if (survives(slot, level)) {
          minimumLod = level;
          sampled = true;
        }
      if (!sampled) {
        trimmedReasons.push_back(label + " " + reference + ": no " + tier +
                                 " level samples it, so it is trimmed rather "
                                 "than made resident.");
        continue;
      }

      TerrainTextureResidency entry;
      entry.assetId = reference;
      entry.slot = slot;
      entry.roleName = material.roleName;
      entry.minimumLod = minimumLod;
      entry.srgb = profile(slot).colourBearing && budget.srgbTextures;
      entry.anisotropy = anisotropyFor(budget, profile(slot));
      // Counted below, once every request is known; the default of 1 is the
      // standalone case, not the one that survives every material.
      entry.dependents = 0;
      planIndex.push_back(plan.textures.size());
      plan.textures.push_back(std::move(entry));
    }
  }

  // Dependents, then report order. Both are decided before any byte is
  // charged, because a texture two roles sample is one upload and one claim on
  // the budget however many entries name it. Every entry naming a texture is
  // given the WHOLE count of requests against it, not its own ordinal in the
  // list: the question is how many surfaces break when it goes, and that is one
  // number for the texture rather than a number per request.
  std::map<std::string, std::size_t, std::less<>> demand;
  for (const std::size_t index : planIndex)
    ++demand[plan.textures[index].assetId];
  for (const std::size_t index : planIndex)
    plan.textures[index].dependents = demand[plan.textures[index].assetId];

  std::ranges::stable_sort(
      planIndex, [&](const std::size_t left, const std::size_t right) {
        const TerrainTextureResidency &first = plan.textures[left];
        const TerrainTextureResidency &second = plan.textures[right];
        return first.assetId != second.assetId
                   ? first.assetId < second.assetId
                   : slotIndex(first.slot) < slotIndex(second.slot);
      });
  std::vector<TerrainTextureResidency> orderedEntries;
  orderedEntries.reserve(planIndex.size());
  for (const std::size_t index : planIndex)
    orderedEntries.push_back(std::move(plan.textures[index]));
  plan.textures = std::move(orderedEntries);

  // Groups in asset-id order, which makes the eviction walk below a stable sort
  // rather than a sort of whatever the map iteration produced.
  std::vector<TextureGroup> groups;
  for (std::size_t index = 0; index < plan.textures.size(); ++index) {
    TerrainTextureResidency &entry = plan.textures[index];
    if (groups.empty() || groups.back().assetId != entry.assetId) {
      TextureGroup group;
      group.assetId = entry.assetId;
      groups.push_back(std::move(group));
    }
    TextureGroup &group = groups.back();
    group.entries.push_back(index);
    group.dependents = entry.dependents;
    group.importance =
        std::max(group.importance, terrainResidencyEvictionRank(entry.slot));
    // One upload serves every slot that samples the texture, so the texture's
    // size is the LARGEST per-slot estimate among them, charged once. Summing
    // the per-entry estimates would charge the same bytes several times.
    group.bytes =
        std::max(group.bytes, terrainResidencyEstimateBytes(entry.slot));
    // Same figure on every entry, so a consumer summing entries cannot believe
    // a shared upload was charged per role.
    entry.estimatedBytes = group.bytes;
  }
  plan.requestedBytes = 0;
  for (const TextureGroup &group : groups)
    plan.requestedBytes += group.bytes;

  // Eviction order: slot importance, then a shared texture outlives a
  // single-role one, then the asset id so equal requests never depend on sort
  // stability to break the tie.
  std::vector<std::size_t> order(groups.size());
  for (std::size_t index = 0; index < groups.size(); ++index)
    order[index] = index;
  std::ranges::sort(order,
                    [&](const std::size_t left, const std::size_t right) {
                      const TextureGroup &first = groups[left];
                      const TextureGroup &second = groups[right];
                      if (first.importance != second.importance)
                        return first.importance < second.importance;
                      if (first.dependents != second.dependents)
                        return first.dependents > second.dependents;
                      return first.assetId < second.assetId;
                    });
  // Every entry carries its texture's rank, resident or not, so "how safe is
  // this texture" is readable from the plan without first asking what survived.
  for (std::size_t rank = 0; rank < order.size(); ++rank)
    for (const std::size_t entry : groups[order[rank]].entries)
      plan.textures[entry].evictionPriority = rank;

  // Keep from the MOST important end and stop at the first texture that does
  // not fit, dropping it and everything below it. Walking the other way round
  // would fill the budget with the textures the plan ranks lowest, which is the
  // one way round that can be described as "evicting the lowest priority" while
  // doing the exact opposite.
  //
  // Survivors are therefore a SUFFIX of the order: "the lowest priority went
  // first" is a claim a test reads off the plan, not a claim about intent.
  plan.plannedBytes = 0;
  std::size_t residentTextures = 0;
  const auto evict = [&](const TextureGroup &group) {
    std::vector<std::string> roles;
    for (const std::size_t entry : group.entries) {
      plan.textures[entry].resident = false;
      ++plan.evicted;
      const std::string &role = plan.textures[entry].roleName;
      if (std::find(roles.begin(), roles.end(), role) == roles.end())
        roles.push_back(role);
    }
    std::ranges::sort(roles);

    // Both causes are named when both bind, because a budget that is too small
    // AND too few bindings is one mistake with two reasons and fixing either
    // alone would not have helped.
    const bool bytesBind =
        plan.plannedBytes + group.bytes > budget.textureBytes;
    const bool countBind = residentTextures + 1 > budget.distinctTextures;
    std::string reason = "dropped: ";
    if (bytesBind)
      reason += "the byte budget of " + std::to_string(budget.textureBytes) +
                " cannot hold " + std::to_string(group.bytes) +
                " more bytes against " + std::to_string(plan.plannedBytes) +
                " already planned";
    if (bytesBind && countBind)
      reason += "; ";
    if (countBind)
      reason += "the distinct texture cap of " +
                std::to_string(budget.distinctTextures) +
                " is already reached by " + std::to_string(residentTextures) +
                " textures";
    reason += " (the " +
              std::string(assets::terrainMaterialMapSlotName(
                  mostImportantSlot(plan.textures, group))) +
              " use of " + group.assetId + " for " + join(roles, ", ") + ")";

    for (const std::size_t entry : group.entries) {
      plan.textures[entry].evictionReason = reason;
      // Losing the base colour is a visible failure rather than a quality
      // reduction, so it is named rather than absorbed into the eviction count.
      if (plan.textures[entry].slot == TerrainMaterialMapSlot::BaseColor)
        plan.lostRequired.push_back(
            plan.textures[entry].roleName + " " +
            std::string(assets::terrainMaterialMapSlotName(
                TerrainMaterialMapSlot::BaseColor)) +
            " " + plan.textures[entry].assetId + ": " + reason);
    }
  };

  for (std::size_t position = order.size(); position-- > 0;) {
    const TextureGroup &group = groups[order[position]];
    if (plan.plannedBytes + group.bytes <= budget.textureBytes &&
        residentTextures + 1 <= budget.distinctTextures) {
      plan.plannedBytes += group.bytes;
      ++residentTextures;
      continue;
    }
    for (std::size_t lower = position + 1; lower-- > 0;)
      evict(groups[order[lower]]);
    break;
  }

  plan.trimmed = std::move(trimmedReasons);
  std::ranges::sort(plan.trimmed);
  std::ranges::sort(plan.lostRequired);
  plan.trimmedCount = plan.trimmed.size();
  plan.withinBudget = plan.plannedBytes <= budget.textureBytes &&
                      residentTextures <= budget.distinctTextures;
  return plan;
}

TerrainResidencyPlan
planTerrainTextureResidency(const std::vector<std::string> &materialAssetIds,
                            const demi::AssetRegistry &registry,
                            const TerrainResidencyBudget &budget) {
  if (materialAssetIds.empty())
    fail("Terrain texture residency needs at least one material asset id: a "
         "terrain with no material has no texture to keep resident, and an "
         "empty plan would read as a successful decision to keep none. Resolve "
         "the TerrainMaterialSet and pass the ids its roles bind.");
  std::vector<TerrainResidencyMaterial> materials;
  materials.reserve(materialAssetIds.size());
  for (const std::string &id : materialAssetIds)
    // The loader's own rejection names the document and the field, so it is
    // better than anything this stage could add on top of it.
    materials.push_back(TerrainResidencyMaterial{
        id, *assets::loadTerrainMaterialAsset(registry, id)});
  return planTerrainTextureResidency(materials, budget);
}

} // namespace demi::runtime