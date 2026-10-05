#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/MaterialAsset.h"
#include "demi/runtime/terrain/TerrainTextureResidency.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace demi;
using namespace demi::runtime;
using demi::assets::TerrainMaterialMapSlot;

namespace {

constexpr std::string_view SchemaId = "asset://schemas/terrain_material";
constexpr std::string_view RockId = "asset://materials/rock";
constexpr std::string_view GroundId = "asset://materials/ground";
constexpr std::string_view SandId = "asset://materials/sand";
constexpr std::string_view FlatId = "asset://materials/flat";

// One base colour shared by two roles, which is the case the dependent count
// exists for: dropping it degrades both surfaces at once.
constexpr std::string_view SharedBase = "asset://terrain/textures/shared_base";
constexpr std::string_view RockNormal = "asset://terrain/textures/rock_normal";
constexpr std::string_view RockRoughness =
    "asset://terrain/textures/rock_roughness";
constexpr std::string_view RockMetallic =
    "asset://terrain/textures/rock_metallic";
constexpr std::string_view RockOcclusion =
    "asset://terrain/textures/rock_occlusion";
constexpr std::string_view RockHeight = "asset://terrain/textures/rock_height";
constexpr std::string_view RockDetail = "asset://terrain/textures/rock_detail";
constexpr std::string_view RockEmissive =
    "asset://terrain/textures/rock_emissive";
constexpr std::string_view GroundNormal =
    "asset://terrain/textures/ground_normal";
constexpr std::string_view GroundRoughness =
    "asset://terrain/textures/ground_roughness";
constexpr std::string_view GroundOcclusion =
    "asset://terrain/textures/ground_occlusion";
constexpr std::string_view GroundDetail =
    "asset://terrain/textures/ground_detail";
constexpr std::string_view SandBase = "asset://terrain/textures/sand_base";
constexpr std::string_view FlatBase = "asset://terrain/textures/flat_base";
// Assigned but authored with the strength that would make them inert, so the
// plan has to decide they are not textures at all.
constexpr std::string_view FlatNormal = "asset://terrain/textures/flat_normal";
constexpr std::string_view FlatDetail = "asset://terrain/textures/flat_detail";

constexpr std::string_view Textures[]{
    SharedBase,    RockNormal,      RockRoughness,   RockMetallic,
    RockOcclusion, RockHeight,      RockDetail,      RockEmissive,
    GroundNormal,  GroundRoughness, GroundOcclusion, GroundDetail,
    SandBase,      FlatBase,        FlatNormal,      FlatDetail};

std::filesystem::path root;
AssetRegistry registry;

bool write(const std::filesystem::path &path, const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  return output.good();
}

// The fixture hashes its sources exactly as an import would, so a manifest that
// claimed a stale hash could not pass unnoticed.
AssetManifest manifest(const std::string &id, const std::string &type,
                       const std::string &source, std::string settings = "{}",
                       std::vector<std::string> dependencies = {}) {
  const std::optional<std::string> hash =
      demi::assets::hashFiles({root / source});
  assert(hash.has_value());
  return {.id = id,
          .type = type,
          .importer = type == "DataSchema" ? "json_schema" : type,
          .importerVersion = 1,
          .sourceHash = *hash,
          .dependencies = std::move(dependencies),
          .settingsJson = std::move(settings),
          .manifestPath = root / (source + ".asset.json"),
          .sourcePath = root / source,
          .sourcePaths = {root / source}};
}

std::string terrainMaterialSettings() {
  return std::string("{\"content_type\":\"terrain_material\",\"schema\":\"") +
         std::string(SchemaId) + "\",\"tags\":[]}";
}

AssetManifest material(const std::string &id, const std::string &source,
                       const std::vector<std::string_view> &maps) {
  std::vector<std::string> dependencies{std::string(SchemaId)};
  for (const std::string_view map : maps)
    dependencies.push_back(std::string(map));
  std::ranges::sort(dependencies);
  return manifest(id, "DataAsset", source, terrainMaterialSettings(),
                  std::move(dependencies));
}

// One registry serves every case: the material documents are rewritten between
// loads, so a plan proves the planner read the documents under test.
AssetRegistry buildRegistry() {
  AssetRegistry built;
  built.projectDirectory = root;
  built.assets = {
      manifest(std::string(SchemaId), "DataSchema", "schema.json"),
      material(std::string(RockId), "rock.json",
               {SharedBase, RockNormal, RockRoughness, RockMetallic,
                RockOcclusion, RockHeight, RockDetail, RockEmissive}),
      material(std::string(GroundId), "ground.json",
               {SharedBase, GroundNormal, GroundRoughness, GroundOcclusion,
                GroundDetail}),
      material(std::string(SandId), "sand.json", {SandBase}),
      material(std::string(FlatId), "flat.json",
               {FlatBase, FlatNormal, FlatDetail}),
  };
  // The material loader resolves a map by id and never decodes it, so the
  // texture sources are placeholders with real bytes to hash.
  for (const std::string_view texture : Textures)
    built.assets.push_back(manifest(std::string(texture), "Texture",
                                    std::string(texture.substr(9)) + ".png"));
  std::ranges::sort(built.assets, {}, &AssetManifest::id);
  return built;
}

void writeMaterials() {
  assert(write(root / "rock.json", R"({
    "format_version": 1,
    "name": "Cliff Rock",
    "normal_strength": 2.0,
    "detail_strength": 1.5,
    "triplanar": true,
    "maps": [
      {"base_color": ")" + std::string(SharedBase) +
                                       R"("},
      {"normal": ")" + std::string(RockNormal) +
                                       R"("},
      {"roughness": ")" + std::string(RockRoughness) +
                                       R"("},
      {"metallic": ")" + std::string(RockMetallic) +
                                       R"("},
      {"ambient_occlusion": ")" + std::string(RockOcclusion) +
                                       R"("},
      {"height": ")" + std::string(RockHeight) +
                                       R"("},
      {"detail": ")" + std::string(RockDetail) +
                                       R"("},
      {"emissive": ")" + std::string(RockEmissive) +
                                       R"("}
    ]
  })"));
  assert(write(root / "ground.json", R"({
    "format_version": 1,
    "name": "Vegetated Ground",
    "detail_strength": 1.0,
    "maps": [
      {"base_color": ")" + std::string(SharedBase) +
                                         R"("},
      {"normal": ")" + std::string(GroundNormal) +
                                         R"("},
      {"roughness": ")" + std::string(GroundRoughness) +
                                         R"("},
      {"ambient_occlusion": ")" + std::string(GroundOcclusion) +
                                         R"("},
      {"detail": ")" + std::string(GroundDetail) +
                                         R"("}
    ]
  })"));
  assert(write(root / "sand.json", R"({
    "format_version": 1,
    "name": "Dry Sand",
    "maps": [{"base_color": ")" + std::string(SandBase) +
                                       R"("}]
  })"));
  // Both strength settings are zero, so both maps are inert: the detail blend
  // and the normal amplification are off and the maps cannot change the
  // surface.
  assert(write(root / "flat.json", R"({
    "format_version": 1,
    "name": "Flat Silt",
    "normal_strength": 0.0,
    "detail_strength": 0.0,
    "maps": [
      {"base_color": ")" + std::string(FlatBase) +
                                       R"("},
      {"normal": ")" + std::string(FlatNormal) +
                                       R"("},
      {"detail": ")" + std::string(FlatDetail) +
                                       R"("}
    ]
  })"));
}

// The roles a TerrainMaterialSet would bind: one entry per role, material ids
// deliberately repeated where two roles share an imported material.
std::vector<TerrainResidencyMaterial> roleMaterials() {
  std::vector<TerrainResidencyMaterial> materials;
  for (const auto &[role, id] :
       std::vector<std::pair<std::string, std::string_view>>{
           {"rock", RockId},
           // The same document bound to a second role, which is what a set
           // sharing one material between two surfaces looks like.
           {"cliff", RockId},
           {"ground", GroundId},
           {"sand", SandId},
           {"wet_ground", FlatId}})
    materials.push_back(TerrainResidencyMaterial{
        role, *assets::loadTerrainMaterialAsset(registry, id)});
  return materials;
}

TerrainResidencyBudget generous() {
  TerrainResidencyBudget budget;
  // Large enough that nothing is dropped, so a test can separate "the planner
  // wanted this" from "the budget took it".
  budget.textureBytes = std::size_t{512} * 1024 * 1024;
  return budget;
}

// Membership, spelled once because std::ranges::contains is not C++20.
template <typename Range, typename Value>
bool holds(const Range &range, const Value &value) {
  return std::ranges::find(range, value) != range.end();
}

bool keepsSlot(const std::vector<TerrainMaterialMapSlot> &slots,
               const TerrainMaterialMapSlot slot) {
  return std::find(slots.begin(), slots.end(), slot) != slots.end();
}

bool isSubset(const std::vector<TerrainMaterialMapSlot> &inner,
              const std::vector<TerrainMaterialMapSlot> &outer) {
  return std::all_of(inner.begin(), inner.end(),
                     [&](const TerrainMaterialMapSlot slot) {
                       return keepsSlot(outer, slot);
                     });
}

std::vector<std::string> residentIds(const TerrainResidencyPlan &plan) {
  std::vector<std::string> ids;
  for (const TerrainTextureResidency &entry : plan.textures)
    if (entry.resident)
      ids.push_back(entry.assetId);
  std::ranges::sort(ids);
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

std::size_t entriesInSlot(const TerrainResidencyPlan &plan,
                          const TerrainMaterialMapSlot slot) {
  return static_cast<std::size_t>(
      std::count_if(plan.textures.begin(), plan.textures.end(),
                    [&](const TerrainTextureResidency &entry) {
                      return entry.slot == slot;
                    }));
}

std::size_t residentInSlot(const TerrainResidencyPlan &plan,
                           const TerrainMaterialMapSlot slot) {
  return static_cast<std::size_t>(
      std::count_if(plan.textures.begin(), plan.textures.end(),
                    [&](const TerrainTextureResidency &entry) {
                      return entry.slot == slot && entry.resident;
                    }));
}

// The rejection message, or an empty string when the call was accepted.
std::string rejection(const std::function<void()> &call) {
  try {
    call();
  } catch (const std::invalid_argument &failure) {
    return failure.what();
  }
  return {};
}

// ---------------------------------------------------------------------------
// What a material needs at a distance
// ---------------------------------------------------------------------------

void slotsKeepTheBaseColourAndShrinkWithDistance() {
  constexpr TerrainMaterialMapSlot All[]{
      TerrainMaterialMapSlot::BaseColor,
      TerrainMaterialMapSlot::Normal,
      TerrainMaterialMapSlot::Roughness,
      TerrainMaterialMapSlot::Metallic,
      TerrainMaterialMapSlot::AmbientOcclusion,
      TerrainMaterialMapSlot::Height,
      TerrainMaterialMapSlot::Detail,
      TerrainMaterialMapSlot::Emissive};
  constexpr TerrainLod Levels[]{TerrainLod::Full, TerrainLod::Half,
                                TerrainLod::Quarter, TerrainLod::Coarse};

  for (const TerrainMaterialMapSlot slot : All) {
    std::size_t previous = 100;
    for (std::size_t level = 0; level < std::size(Levels); ++level) {
      const std::vector<TerrainMaterialMapSlot> kept =
          terrainSlotsForLod(slot, Levels[level]);
      // Every level that draws a surface keeps the base colour: a chunk without
      // one would wear the palette tint with no texture at all.
      assert(keepsSlot(kept, TerrainMaterialMapSlot::BaseColor));
      // Asking about one slot answers for every slot at that level, so the
      // caller's own map survives iff it is in the kept set.
      if (keepsSlot(kept, slot))
        assert(kept.size() <= previous);
      previous = kept.size();
    }
    // Nesting: a slot gone at some level is gone at every coarser one, which is
    // what makes "fewer slots with distance" true rather than merely likely.
    for (std::size_t fine = 0; fine < std::size(Levels); ++fine)
      for (std::size_t coarse = fine + 1; coarse < std::size(Levels); ++coarse)
        assert(isSubset(terrainSlotsForLod(slot, Levels[coarse]),
                        terrainSlotsForLod(slot, Levels[fine])));
  }

  // The documented progression, read slot by slot rather than by index.
  const auto at = [](const TerrainLod level) {
    return terrainSlotsForLod(TerrainMaterialMapSlot::BaseColor, level);
  };
  assert(at(TerrainLod::Full).size() == 8);
  assert(at(TerrainLod::Half).size() == 6);
  assert(at(TerrainLod::Quarter).size() == 4);
  assert(at(TerrainLod::Coarse).size() == 1);
  // Detail and height are the near-field maps; emissive survives one level
  // further; metallic is the first PBR slot a coarse chunk gives up.
  assert(keepsSlot(
      terrainSlotsForLod(TerrainMaterialMapSlot::Detail, TerrainLod::Full),
      TerrainMaterialMapSlot::Detail));
  assert(!keepsSlot(
      terrainSlotsForLod(TerrainMaterialMapSlot::Detail, TerrainLod::Half),
      TerrainMaterialMapSlot::Detail));
  assert(keepsSlot(
      terrainSlotsForLod(TerrainMaterialMapSlot::Height, TerrainLod::Full),
      TerrainMaterialMapSlot::Height));
  assert(keepsSlot(
      terrainSlotsForLod(TerrainMaterialMapSlot::Emissive, TerrainLod::Half),
      TerrainMaterialMapSlot::Emissive));
  assert(!keepsSlot(
      terrainSlotsForLod(TerrainMaterialMapSlot::Emissive, TerrainLod::Quarter),
      TerrainMaterialMapSlot::Emissive));
  assert(!keepsSlot(
      terrainSlotsForLod(TerrainMaterialMapSlot::Metallic, TerrainLod::Quarter),
      TerrainMaterialMapSlot::Metallic));
  // Off draws no surface, so there is nothing to sample.
  assert(terrainSlotsForLod(TerrainMaterialMapSlot::BaseColor, TerrainLod::Off)
             .empty());
}

void estimatesFollowTheDocumentedNominalTable() {
  constexpr TerrainMaterialMapSlot All[]{
      TerrainMaterialMapSlot::BaseColor,
      TerrainMaterialMapSlot::Normal,
      TerrainMaterialMapSlot::Roughness,
      TerrainMaterialMapSlot::Metallic,
      TerrainMaterialMapSlot::AmbientOcclusion,
      TerrainMaterialMapSlot::Height,
      TerrainMaterialMapSlot::Detail,
      TerrainMaterialMapSlot::Emissive};
  for (const TerrainMaterialMapSlot slot : All) {
    const TerrainResidencyNominalFormat nominal =
        terrainResidencyNominalFormat(slot);
    assert(nominal.edge > 0);
    assert(nominal.channels > 0);
    // Re-derived here from the documented formula rather than copied, so the
    // header, the table and the estimate cannot drift apart.
    const std::size_t base = nominal.edge * nominal.edge * nominal.channels;
    const std::size_t mipped = base + base / 3;
    constexpr std::size_t granularity = 256u * 1024u;
    const std::size_t expected =
        ((mipped + granularity - 1) / granularity) * granularity;
    assert(terrainResidencyEstimateBytes(slot) == expected);
    // Rounded to the allocator's granularity, so a figure can never be a
    // fraction of a possible allocation.
    assert(expected % granularity == 0);
  }
  // The high-frequency maps are the big ones, which is why the budget's first
  // eviction is also the one that buys the most room for the least visible
  // loss.
  assert(terrainResidencyEstimateBytes(TerrainMaterialMapSlot::Detail) ==
         terrainResidencyEstimateBytes(TerrainMaterialMapSlot::BaseColor));
  assert(terrainResidencyEstimateBytes(TerrainMaterialMapSlot::BaseColor) >
         terrainResidencyEstimateBytes(TerrainMaterialMapSlot::Roughness));
  assert(terrainResidencyEstimateBytes(TerrainMaterialMapSlot::Roughness) >
         terrainResidencyEstimateBytes(TerrainMaterialMapSlot::Metallic));
  // Colour-bearing maps and data maps are told apart by the same table, so a
  // normal map can never be planned as sRGB.
  assert(terrainResidencyNominalFormat(TerrainMaterialMapSlot::BaseColor)
             .colourBearing);
  assert(terrainResidencyNominalFormat(TerrainMaterialMapSlot::Emissive)
             .colourBearing);
  assert(!terrainResidencyNominalFormat(TerrainMaterialMapSlot::Normal)
              .colourBearing);
}

void evictionOrderIsTheDocumentedOne() {
  // The order as the header states it, lowest (evicted first) to highest.
  const std::vector<TerrainMaterialMapSlot> documented{
      TerrainMaterialMapSlot::Detail,
      TerrainMaterialMapSlot::Height,
      TerrainMaterialMapSlot::Emissive,
      TerrainMaterialMapSlot::BaseColor,
      TerrainMaterialMapSlot::Normal,
      TerrainMaterialMapSlot::AmbientOcclusion,
      TerrainMaterialMapSlot::Roughness,
      TerrainMaterialMapSlot::Metallic};
  for (std::size_t index = 0; index < documented.size(); ++index)
    assert(terrainResidencyEvictionRank(documented[index]) == index);
  // The rule the header leads with: detail before base colour, base colour
  // before the normal map.
  assert(terrainResidencyEvictionRank(TerrainMaterialMapSlot::Detail) <
         terrainResidencyEvictionRank(TerrainMaterialMapSlot::BaseColor));
  assert(terrainResidencyEvictionRank(TerrainMaterialMapSlot::BaseColor) <
         terrainResidencyEvictionRank(TerrainMaterialMapSlot::Normal));
  assert(terrainResidencyEvictionRank(TerrainMaterialMapSlot::Normal) <
         terrainResidencyEvictionRank(TerrainMaterialMapSlot::Roughness));
}

// ---------------------------------------------------------------------------
// A plan that fits
// ---------------------------------------------------------------------------

void aGenerousBudgetKeepsEverything() {
  const TerrainResidencyPlan plan =
      planTerrainTextureResidency(roleMaterials(), generous());
  assert(plan.withinBudget);
  assert(plan.evicted == 0);
  assert(plan.lostRequired.empty());
  // Nothing was cut, so the only absent requests are flat silt's two maps whose
  // own strength settings make them inert -- and they are named.
  assert(plan.trimmedCount == 2);
  assert(plan.trimmed.size() == 2);
  for (const std::string &reason : plan.trimmed) {
    assert(!reason.empty());
    assert(reason.find("is 0") != std::string::npos);
  }
  assert(std::ranges::any_of(
      plan.trimmed.begin(), plan.trimmed.end(), [](const std::string &reason) {
        return reason.find("wet_ground normal") != std::string::npos;
      }));
  assert(std::ranges::any_of(
      plan.trimmed.begin(), plan.trimmed.end(), [](const std::string &reason) {
        return reason.find("wet_ground detail") != std::string::npos;
      }));

  // Reported in asset-id order, so a renderer reads them the same way twice.
  for (std::size_t index = 1; index < plan.textures.size(); ++index)
    assert(plan.textures[index - 1].assetId <= plan.textures[index].assetId);
  // A shared texture is one upload and one claim on the budget however many
  // roles name it, so the bytes are counted once per distinct asset id.
  std::size_t distinctBytes = 0;
  std::string previous;
  for (const TerrainTextureResidency &entry : plan.textures) {
    if (entry.assetId != previous) {
      distinctBytes += entry.estimatedBytes;
      previous = entry.assetId;
    }
  }
  assert(distinctBytes == plan.requestedBytes);
  assert(plan.plannedBytes == plan.requestedBytes);
  assert(plan.requestedBytes > 0);

  // The dependent count is a count of REQUESTS, so it reads off the fixture:
  // shared_base answers for three roles (rock, cliff and ground all sample it),
  // every other rock map answers for the two roles bound to the rock document,
  // and everything else answers for one.
  for (const TerrainTextureResidency &entry : plan.textures) {
    if (entry.assetId == SharedBase)
      assert(entry.dependents == 3);
    else if (entry.assetId.find("rock_") != std::string::npos)
      assert(entry.dependents == 2);
    else
      assert(entry.dependents == 1);
    assert(entry.resident);
    assert(entry.evictionReason.empty());
  }
  // Three roles really do share it, which is what the count of three rests on.
  std::vector<std::string> sharedRoles;
  for (const TerrainTextureResidency &entry : plan.textures)
    if (entry.assetId == SharedBase)
      sharedRoles.push_back(entry.roleName);
  std::ranges::sort(sharedRoles);
  assert(sharedRoles == std::vector<std::string>{"cliff", "ground", "rock"});
  // The role comes from the set the caller resolved, not from the material
  // document, and the same document bound to two roles is planned twice.
  std::vector<std::string> roles;
  for (const TerrainTextureResidency &entry : plan.textures)
    roles.push_back(entry.roleName);
  std::ranges::sort(roles);
  roles.erase(std::unique(roles.begin(), roles.end()), roles.end());
  assert(roles == std::vector<std::string>{"cliff", "ground", "rock", "sand",
                                           "wet_ground"});
}

void twoRunsProduceIdenticalPlans() {
  const TerrainResidencyPlan first =
      planTerrainTextureResidency(roleMaterials(), generous());
  // A different input order must not change a word of the plan.
  std::vector<TerrainResidencyMaterial> shuffled = roleMaterials();
  std::ranges::reverse(shuffled);
  const TerrainResidencyPlan second =
      planTerrainTextureResidency(shuffled, generous());
  // ... and a second run over the same inputs must not either.
  const TerrainResidencyPlan third =
      planTerrainTextureResidency(roleMaterials(), generous());

  assert(first.textures.size() == second.textures.size());
  assert(first.textures.size() == third.textures.size());
  for (std::size_t index = 0; index < first.textures.size(); ++index) {
    const TerrainTextureResidency &a = first.textures[index];
    const TerrainTextureResidency &b = second.textures[index];
    const TerrainTextureResidency &c = third.textures[index];
    assert(a.assetId == b.assetId && a.assetId == c.assetId);
    assert(a.slot == b.slot && a.slot == c.slot);
    assert(a.roleName == b.roleName && a.roleName == c.roleName);
    assert(a.minimumLod == b.minimumLod && a.minimumLod == c.minimumLod);
    assert(a.estimatedBytes == b.estimatedBytes);
    assert(a.srgb == b.srgb && a.anisotropy == b.anisotropy);
    assert(a.dependents == b.dependents && a.dependents == c.dependents);
    assert(a.evictionPriority == b.evictionPriority);
    assert(a.resident == b.resident);
    assert(a.evictionReason == b.evictionReason);
  }
  assert(first.requestedBytes == third.requestedBytes);
  assert(first.plannedBytes == third.plannedBytes);
  assert(first.evicted == third.evicted);
  assert(first.trimmedCount == third.trimmedCount);
  assert(first.trimmed == third.trimmed);
  assert(first.lostRequired == third.lostRequired);
  assert(first.withinBudget == third.withinBudget);
}

// ---------------------------------------------------------------------------
// A plan that does not fit
// ---------------------------------------------------------------------------

void overBudgetEvictsTheLowestPriorityFirst() {
  const std::vector<TerrainResidencyMaterial> materials = roleMaterials();
  const TerrainResidencyPlan everything =
      planTerrainTextureResidency(materials, generous());
  // Room for every texture the plan judges at least as important as a base
  // colour, and nothing more. The plan has to choose, and the choice it is
  // forced into is exactly the documented one.
  std::size_t keptBytes = 0;
  std::string previous;
  for (const TerrainTextureResidency &entry : everything.textures) {
    if (entry.assetId == previous)
      continue;
    previous = entry.assetId;
    if (terrainResidencyEvictionRank(entry.slot) >=
        terrainResidencyEvictionRank(TerrainMaterialMapSlot::BaseColor))
      keptBytes += entry.estimatedBytes;
  }
  assert(keptBytes < everything.requestedBytes);
  TerrainResidencyBudget budget = generous();
  budget.textureBytes = keptBytes;
  const TerrainResidencyPlan plan =
      planTerrainTextureResidency(materials, budget);

  assert(plan.withinBudget);
  assert(plan.plannedBytes <= budget.textureBytes);
  assert(plan.evicted > 0);
  assert(plan.evicted < plan.textures.size());
  assert(plan.lostRequired.empty());
  assert(plan.trimmedCount == 2);

  // Survivors are exactly the HIGH ranks and the evicted are exactly the LOW
  // ones: a split in the middle would mean the walk kept something it should
  // have dropped, or dropped something it could have kept.
  std::size_t highestResident = 0;
  std::size_t lowestEvicted = plan.textures.size();
  for (const TerrainTextureResidency &entry : plan.textures) {
    if (entry.resident)
      highestResident = std::max(highestResident, entry.evictionPriority);
    else
      lowestEvicted = std::min(lowestEvicted, entry.evictionPriority);
  }
  assert(lowestEvicted < highestResident);

  // Greedy, not merely legal: the lowest-ranked texture that was dropped could
  // not have fitted, so nothing was given up that the budget could have held.
  std::size_t lowestDroppedBytes = 0;
  bool sawLowestDropped = false;
  for (const TerrainTextureResidency &entry : plan.textures)
    if (!entry.resident && entry.evictionPriority == lowestEvicted &&
        (!sawLowestDropped || entry.estimatedBytes < lowestDroppedBytes)) {
      lowestDroppedBytes = entry.estimatedBytes;
      sawLowestDropped = true;
    }
  assert(sawLowestDropped);
  assert(plan.plannedBytes + lowestDroppedBytes > budget.textureBytes);

  // The order is the documented one: at this size the near-field maps are the
  // ones that go, and no base colour does.
  assert(entriesInSlot(plan, TerrainMaterialMapSlot::Detail) > 0);
  assert(residentInSlot(plan, TerrainMaterialMapSlot::Detail) <
         entriesInSlot(plan, TerrainMaterialMapSlot::Detail));
  assert(residentInSlot(plan, TerrainMaterialMapSlot::BaseColor) ==
         entriesInSlot(plan, TerrainMaterialMapSlot::BaseColor));
}

void detailGoesBeforeBaseColour() {
  const std::vector<TerrainResidencyMaterial> materials = roleMaterials();
  const TerrainResidencyPlan generousPlan =
      planTerrainTextureResidency(materials, generous());

  // A budget that is exactly everything except the detail maps, so the walk has
  // to decide between them and everything else rather than evicting blindly.
  std::size_t detailBytes = 0;
  std::string counted;
  for (const TerrainTextureResidency &entry : generousPlan.textures) {
    if (entry.slot != TerrainMaterialMapSlot::Detail ||
        entry.assetId == counted)
      continue;
    counted = entry.assetId;
    // Counted once per texture, because that is how the plan charges for it.
    detailBytes += entry.estimatedBytes;
  }
  assert(detailBytes > 0);
  TerrainResidencyBudget budget = generous();
  budget.textureBytes = generousPlan.requestedBytes - detailBytes;
  const TerrainResidencyPlan plan =
      planTerrainTextureResidency(materials, budget);

  assert(plan.evicted > 0);
  assert(plan.lostRequired.empty());
  // Every detail map went, and no base colour did.
  assert(residentInSlot(plan, TerrainMaterialMapSlot::Detail) == 0);
  assert(residentInSlot(plan, TerrainMaterialMapSlot::BaseColor) ==
         entriesInSlot(plan, TerrainMaterialMapSlot::BaseColor));
  // The reason says which budget bound, and names the map it cost.
  for (const TerrainTextureResidency &entry : plan.textures)
    if (!entry.resident) {
      assert(!entry.evictionReason.empty());
      assert(entry.evictionReason.find("byte budget") != std::string::npos);
    }
  assert(std::ranges::any_of(plan.textures.begin(), plan.textures.end(),
                             [](const TerrainTextureResidency &entry) {
                               return !entry.resident &&
                                      entry.evictionReason.find(std::string(
                                          RockDetail)) != std::string::npos;
                             }));
}

void aLostBaseColourIsReportedNotAbsorbed() {
  const std::vector<TerrainResidencyMaterial> materials = roleMaterials();
  // One byte: over budget by everything, but not a zero budget, so this is a
  // pressure test rather than the refusal of a budget nobody filled in.
  TerrainResidencyBudget budget = generous();
  budget.textureBytes = 1;
  const TerrainResidencyPlan plan =
      planTerrainTextureResidency(materials, budget);

  assert(plan.withinBudget);
  assert(plan.plannedBytes == 0);
  assert(plan.evicted == plan.textures.size());
  // Five roles asked for a base colour, and three of them share one texture, so
  // the report is once per REQUEST rather than once per texture.
  assert(plan.lostRequired.size() == 5);
  for (const std::string &lost : plan.lostRequired) {
    assert(!lost.empty());
    assert(lost.find("base_color") != std::string::npos);
    assert(lost.find("dropped") != std::string::npos);
  }
  assert(std::ranges::is_sorted(plan.lostRequired));
  for (const char *role : {"rock", "cliff", "ground", "sand", "wet_ground"})
    assert(std::ranges::any_of(
        plan.lostRequired.begin(), plan.lostRequired.end(),
        [role](const std::string &lost) {
          return lost.starts_with(role) &&
                 lost.find("base_color") != std::string::npos;
        }));
  assert(std::ranges::any_of(plan.lostRequired.begin(), plan.lostRequired.end(),
                             [](const std::string &lost) {
                               return lost.find("ground") != std::string::npos;
                             }));
  // An evicted texture keeps the rank it had, so the report shows what it would
  // have cost to keep it.
  for (const TerrainTextureResidency &entry : plan.textures) {
    assert(!entry.resident);
    assert(!entry.evictionReason.empty());
    assert(entry.dependents >= 1);
  }
}

void evictionPriorityIsMonotonicAcrossAMixedSet() {
  const TerrainResidencyPlan plan =
      planTerrainTextureResidency(roleMaterials(), generous());

  // Ranks are a permutation of 0..groups-1: every distinct texture has one rank
  // and no rank is skipped or reused.
  std::vector<std::size_t> ranks;
  for (const TerrainTextureResidency &entry : plan.textures)
    if (std::find(ranks.begin(), ranks.end(), entry.evictionPriority) ==
        ranks.end())
      ranks.push_back(entry.evictionPriority);
  std::ranges::sort(ranks);
  assert(ranks.size() == residentIds(plan).size());
  for (std::size_t index = 0; index < ranks.size(); ++index)
    assert(ranks[index] == index);

  // Walked in rank order, the documented sort holds at every step: no slot
  // importance decreases, and within one importance a shared texture never
  // ranks below a single-role one.
  std::vector<const TerrainTextureResidency *> byRank;
  for (const TerrainTextureResidency &entry : plan.textures)
    byRank.push_back(&entry);
  std::ranges::sort(byRank, {}, [](const TerrainTextureResidency *entry) {
    return entry->evictionPriority;
  });
  bool havePrevious = false;
  std::size_t previousImportance = 0;
  std::size_t previousDependents = 0;
  std::string previousAsset;
  for (const TerrainTextureResidency *entry : byRank) {
    const std::size_t importance = terrainResidencyEvictionRank(entry->slot);
    if (havePrevious) {
      assert(importance >= previousImportance);
      if (importance == previousImportance) {
        // Within one importance a texture with more requests outlives one with
        // fewer, and equal requests break the tie on the asset id rather than
        // on iteration order, so two runs can never swap them.
        assert(entry->dependents <= previousDependents);
        if (entry->dependents == previousDependents &&
            entry->assetId != previousAsset)
          assert(previousAsset < entry->assetId);
      }
    }
    previousImportance = importance;
    previousDependents = entry->dependents;
    previousAsset = entry->assetId;
    havePrevious = true;
  }

  // A mixed set really does contain several importances, or the walk above
  // would have proved nothing about the ordering between them.
  assert(entriesInSlot(plan, TerrainMaterialMapSlot::Detail) > 0);
  assert(entriesInSlot(plan, TerrainMaterialMapSlot::Metallic) > 0);
  assert(entriesInSlot(plan, TerrainMaterialMapSlot::BaseColor) > 0);
  // The shared base colour is counted as three requests, which is what lets it
  // outrank the single-role maps it ties with on slot importance.
  for (const TerrainTextureResidency &entry : plan.textures)
    if (entry.assetId == SharedBase) {
      assert(entry.dependents == 3);
      assert(terrainResidencyEvictionRank(entry.slot) ==
             terrainResidencyEvictionRank(TerrainMaterialMapSlot::BaseColor));
    }
}

void everyEvictionExplainsItself() {
  const std::vector<TerrainResidencyMaterial> materials = roleMaterials();
  for (const std::size_t cap : {std::size_t{1}, std::size_t{4}}) {
    TerrainResidencyBudget budget = generous();
    budget.distinctTextures = cap;
    const TerrainResidencyPlan plan =
        planTerrainTextureResidency(materials, budget);
    assert(plan.evicted > 0);
    for (const TerrainTextureResidency &entry : plan.textures) {
      if (entry.resident) {
        assert(entry.evictionReason.empty());
        continue;
      }
      // A drop the author cannot read is a drop they cannot fix.
      assert(!entry.evictionReason.empty());
      assert(entry.evictionReason.find("dropped") != std::string::npos);
      assert(entry.evictionReason.find(entry.assetId) != std::string::npos);
    }
  }
}

void theDistinctTextureCapIsIndependentOfBytes() {
  const std::vector<TerrainResidencyMaterial> materials = roleMaterials();
  // How many bindings a plan needs if every texture it considers at least as
  // important as a base colour is kept. Bytes are never the constraint here: a
  // hundred small textures fit under a byte budget and still cost a sampler
  // state and a binding each.
  const TerrainResidencyPlan everything =
      planTerrainTextureResidency(materials, generous());
  std::vector<std::string> wanted;
  std::string counted;
  for (const TerrainTextureResidency &entry : everything.textures) {
    if (entry.assetId == counted ||
        terrainResidencyEvictionRank(entry.slot) <
            terrainResidencyEvictionRank(TerrainMaterialMapSlot::BaseColor))
      continue;
    counted = entry.assetId;
    wanted.push_back(entry.assetId);
  }
  assert(wanted.size() > 1);
  assert(wanted.size() < residentIds(everything).size());

  TerrainResidencyBudget budget = generous();
  budget.textureBytes = std::size_t{4} * 1024 * 1024 * 1024;
  budget.distinctTextures = wanted.size();
  const TerrainResidencyPlan plan =
      planTerrainTextureResidency(materials, budget);

  assert(plan.withinBudget);
  assert(residentIds(plan) == wanted);
  assert(residentInSlot(plan, TerrainMaterialMapSlot::BaseColor) ==
         entriesInSlot(plan, TerrainMaterialMapSlot::BaseColor));
  assert(plan.lostRequired.empty());
  // Far below the byte cap: the count of bindings is what stopped it, and the
  // reason says so rather than blaming the bytes.
  assert(plan.plannedBytes * 4 < budget.textureBytes);
  assert(plan.evicted > 0);
  std::size_t reported = 0;
  std::size_t kept = 0;
  for (const TerrainTextureResidency &entry : plan.textures) {
    if (entry.resident) {
      ++kept;
      continue;
    }
    ++reported;
    assert(entry.evictionReason.find("distinct texture cap") !=
           std::string::npos);
    assert(entry.evictionReason.find("byte budget") == std::string::npos);
  }
  assert(reported == plan.evicted);
  assert(kept + reported == plan.textures.size());
  // Every kept entry belongs to a texture the cap allowed, and no kept entry
  // names a dropped texture.
  for (const std::string &id : wanted)
    assert(std::find(residentIds(plan).begin(), residentIds(plan).end(), id) !=
           residentIds(plan).end());
}

// ---------------------------------------------------------------------------
// The quality tier
// ---------------------------------------------------------------------------

void previewHoldsStrictlyFewerLevelsAndTextures() {
  const std::vector<TerrainResidencyMaterial> materials = roleMaterials();
  assert(terrainResidencyHeldLevels(TerrainQuality::Standard).size() == 3);
  assert(terrainResidencyHeldLevels(TerrainQuality::Preview).size() == 2);
  // The near level is the difference, and the levels are ordered near to far.
  assert(terrainResidencyHeldLevels(TerrainQuality::Standard).front() ==
         TerrainLod::Full);
  assert(terrainResidencyHeldLevels(TerrainQuality::Preview).front() ==
         TerrainLod::Half);
  assert(!holds(terrainResidencyHeldLevels(TerrainQuality::Preview),
                TerrainLod::Full));

  TerrainResidencyBudget standard = generous();
  const TerrainResidencyPlan full =
      planTerrainTextureResidency(materials, standard);
  TerrainResidencyBudget preview = generous();
  preview.quality = TerrainQuality::Preview;
  const TerrainResidencyPlan reduced =
      planTerrainTextureResidency(materials, preview);

  // Smaller because it holds fewer LEVELS, not because a bool cheapened the
  // same textures: what disappears is exactly the maps only the finest level
  // samples.
  assert(reduced.textures.size() < full.textures.size());
  assert(reduced.requestedBytes < full.requestedBytes);
  assert(residentIds(reduced).size() < residentIds(full).size());
  assert(residentInSlot(reduced, TerrainMaterialMapSlot::Detail) == 0);
  assert(residentInSlot(reduced, TerrainMaterialMapSlot::Height) == 0);
  assert(entriesInSlot(full, TerrainMaterialMapSlot::Detail) > 0);
  assert(entriesInSlot(full, TerrainMaterialMapSlot::Height) > 0);
  // Both tiers are still under the same generous budget, so the difference is
  // what each tier holds and not what either budget could pay for.
  assert(full.evicted == 0);
  assert(reduced.evicted == 0);
  assert(reduced.trimmedCount > full.trimmedCount);
  // A trimmed request is named and is not a lost requirement.
  assert(reduced.trimmed.size() == reduced.trimmedCount);
  assert(std::ranges::is_sorted(reduced.trimmed));
  assert(reduced.lostRequired.empty());

  // What survives is the same material set at a coarser window, so the base
  // colour a coarse chunk needs is still there in preview.
  assert(residentInSlot(reduced, TerrainMaterialMapSlot::BaseColor) ==
         entriesInSlot(reduced, TerrainMaterialMapSlot::BaseColor));
}

void heldLevelsDecideTheMinimumLod() {
  const std::vector<TerrainResidencyMaterial> materials = roleMaterials();
  TerrainResidencyBudget budget = generous();
  budget.quality = TerrainQuality::Standard;
  const TerrainResidencyPlan plan =
      planTerrainTextureResidency(materials, budget);

  const std::vector<TerrainLod> held =
      terrainResidencyHeldLevels(TerrainQuality::Standard);
  for (const TerrainTextureResidency &entry : plan.textures) {
    // The recorded level is the COARSEST held level that still samples the map,
    // so the map survives it and dies at the next one: exactly the pair a
    // renderer needs to know when it may release the texture.
    assert(holds(terrainSlotsForLod(entry.slot, entry.minimumLod), entry.slot));
    assert(holds(held, entry.minimumLod));
    for (const TerrainLod level : held)
      if (terrainLodIndex(level) > terrainLodIndex(entry.minimumLod))
        assert(!holds(terrainSlotsForLod(entry.slot, level), entry.slot));
  }
  // Base colour lives to the coarsest held level; metallic and emissive live to
  // Half; the near-field maps only ever live at Full.
  const auto minimumFor = [&](const TerrainMaterialMapSlot slot) {
    for (const TerrainTextureResidency &entry : plan.textures)
      if (entry.slot == slot)
        return entry.minimumLod;
    return TerrainLod::Off;
  };
  assert(minimumFor(TerrainMaterialMapSlot::BaseColor) == TerrainLod::Quarter);
  assert(minimumFor(TerrainMaterialMapSlot::Normal) == TerrainLod::Quarter);
  assert(minimumFor(TerrainMaterialMapSlot::Roughness) == TerrainLod::Quarter);
  assert(minimumFor(TerrainMaterialMapSlot::Metallic) == TerrainLod::Half);
  assert(minimumFor(TerrainMaterialMapSlot::Emissive) == TerrainLod::Half);
  assert(minimumFor(TerrainMaterialMapSlot::Height) == TerrainLod::Full);
  assert(minimumFor(TerrainMaterialMapSlot::Detail) == TerrainLod::Full);
}

void colourSpaceAndAnisotropyFollowTheSlotAndTheBudget() {
  const std::vector<TerrainResidencyMaterial> materials = roleMaterials();
  TerrainResidencyBudget budget = generous();
  const TerrainResidencyPlan plan =
      planTerrainTextureResidency(materials, budget);
  for (const TerrainTextureResidency &entry : plan.textures) {
    // Only a colour-bearing map is sampled through an sRGB format; decoding a
    // normal vector or a roughness channel that way corrupts the shading.
    if (entry.slot == TerrainMaterialMapSlot::BaseColor ||
        entry.slot == TerrainMaterialMapSlot::Emissive)
      assert(entry.srgb);
    else
      assert(!entry.srgb);
    // Anisotropy is spent where aliasing reads as a visible blur, and nowhere
    // else, because it costs sampling bandwidth rather than bytes.
    switch (entry.slot) {
    case TerrainMaterialMapSlot::BaseColor:
    case TerrainMaterialMapSlot::Normal:
    case TerrainMaterialMapSlot::Height:
    case TerrainMaterialMapSlot::Detail:
      assert(entry.anisotropy == static_cast<int>(budget.anisotropy));
      break;
    default:
      assert(entry.anisotropy == 1);
      break;
    }
  }

  // A runtime that keeps colour maps linear says so in the budget, and no entry
  // may claim an sRGB format afterwards.
  TerrainResidencyBudget linear = generous();
  linear.srgbTextures = false;
  const TerrainResidencyPlan linearPlan =
      planTerrainTextureResidency(materials, linear);
  for (const TerrainTextureResidency &entry : linearPlan.textures)
    assert(!entry.srgb);

  // A budget that asks for more anisotropy than hardware has is clamped, not
  // truncated into a nonsense sample count.
  TerrainResidencyBudget greedy = generous();
  greedy.anisotropy = 99;
  for (const TerrainTextureResidency &entry :
       planTerrainTextureResidency(materials, greedy).textures)
    assert(entry.anisotropy <= 16);
  // And one that asks for none still plans a sampleable texture.
  TerrainResidencyBudget flat = generous();
  flat.anisotropy = 0;
  for (const TerrainTextureResidency &entry :
       planTerrainTextureResidency(materials, flat).textures)
    assert(entry.anisotropy >= 1);
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

void anEmptyMaterialListIsRefused() {
  const std::string message = rejection([] {
    (void)planTerrainTextureResidency(std::vector<TerrainResidencyMaterial>{},
                                      generous());
  });
  assert(!message.empty());
  assert(message.find("at least one material") != std::string::npos);

  const std::vector<std::string> noIds;
  const std::string identityMessage = rejection(
      [&] { (void)planTerrainTextureResidency(noIds, registry, generous()); });
  assert(!identityMessage.empty());
  assert(identityMessage.find("at least one material asset id") !=
         std::string::npos);

  // A material that really does bind no maps is NOT the same mistake: a flat
  // tinted surface is a legitimate authored material, so it plans to nothing
  // and says so instead of refusing.
  const std::vector<TerrainResidencyMaterial> noMaps{TerrainResidencyMaterial{
      "flat",
      demi::assets::TerrainMaterialAsset{.id = "asset://materials/flat"}}};
  const TerrainResidencyPlan flat =
      planTerrainTextureResidency(noMaps, generous());
  assert(flat.textures.empty());
  assert(flat.withinBudget);
  assert(flat.evicted == 0);
  assert(flat.requestedBytes == 0);
}

void anEmptyBudgetIsRefused() {
  TerrainResidencyBudget noBytes = generous();
  noBytes.textureBytes = 0;
  std::string message = rejection(
      [&] { (void)planTerrainTextureResidency(roleMaterials(), noBytes); });
  assert(!message.empty());
  assert(message.find("greater than 0") != std::string::npos);
  assert(message.find("textureBytes") != std::string::npos);

  TerrainResidencyBudget noTextures = generous();
  noTextures.distinctTextures = 0;
  message = rejection(
      [&] { (void)planTerrainTextureResidency(roleMaterials(), noTextures); });
  assert(!message.empty());
  assert(message.find("greater than 0") != std::string::npos);
  assert(message.find("distinctTextures") != std::string::npos);

  // Both refusals happen before any material is read, so an empty list and a
  // zero budget report the empty list first rather than competing.
  message = rejection([&] {
    (void)planTerrainTextureResidency(std::vector<std::string>{}, registry,
                                      noBytes);
  });
  assert(message.find("at least one material asset id") != std::string::npos);
}

void aMaterialWithoutAnIdIsRefused() {
  const std::vector<TerrainResidencyMaterial> materials{
      TerrainResidencyMaterial{"rock", demi::assets::TerrainMaterialAsset{}}};
  const std::string message = rejection(
      [&] { (void)planTerrainTextureResidency(materials, generous()); });
  assert(!message.empty());
  assert(message.find("no asset id") != std::string::npos);
}

void theIdentityOverloadPlansTheSameTextures() {
  const std::vector<std::string> ids{std::string(RockId), std::string(GroundId),
                                     std::string(SandId), std::string(FlatId)};
  const TerrainResidencyPlan plan =
      planTerrainTextureResidency(ids, registry, generous());
  const TerrainResidencyPlan byRole = planTerrainTextureResidency(
      {TerrainResidencyMaterial{
           "rock", *assets::loadTerrainMaterialAsset(registry, RockId)},
       TerrainResidencyMaterial{
           "ground", *assets::loadTerrainMaterialAsset(registry, GroundId)},
       TerrainResidencyMaterial{
           "sand", *assets::loadTerrainMaterialAsset(registry, SandId)},
       TerrainResidencyMaterial{
           "wet_ground", *assets::loadTerrainMaterialAsset(registry, FlatId)}},
      generous());
  assert(plan.textures.size() == byRole.textures.size());
  assert(plan.requestedBytes == byRole.requestedBytes);
  assert(plan.plannedBytes == byRole.plannedBytes);
  for (std::size_t index = 0; index < plan.textures.size(); ++index) {
    assert(plan.textures[index].assetId == byRole.textures[index].assetId);
    assert(plan.textures[index].slot == byRole.textures[index].slot);
    assert(plan.textures[index].estimatedBytes ==
           byRole.textures[index].estimatedBytes);
    assert(plan.textures[index].evictionPriority ==
           byRole.textures[index].evictionPriority);
    // With no TerrainMaterialSet there is no role vocabulary, so the material's
    // own id is the honest label rather than an invented one.
    assert(std::find(ids.begin(), ids.end(), plan.textures[index].roleName) !=
           ids.end());
  }
  // A material id repeated is one request, not two.
  const std::vector<std::string> repeated{std::string(RockId),
                                          std::string(RockId)};
  const TerrainResidencyPlan duplicate =
      planTerrainTextureResidency(repeated, registry, generous());
  assert(
      duplicate.textures.size() ==
      planTerrainTextureResidency({std::string(RockId)}, registry, generous())
          .textures.size());
  for (const TerrainTextureResidency &entry : duplicate.textures)
    assert(entry.dependents == 1);
}

} // namespace

int main() {
  root =
      std::filesystem::temp_directory_path() / "demi_terrain_texture_residency";
  std::error_code filesystemError;
  // Removed up front as well as on success, so a previous failed run cannot
  // leave a tree behind that changes what a hash proves.
  std::filesystem::remove_all(root, filesystemError);
  std::filesystem::create_directories(root, filesystemError);
  assert(write(root / "schema.json", R"({
        "format_version": 1,
        "type": "object",
        "required": ["format_version", "name"],
        "properties": {
          "format_version": {"type": "integer", "enum": [1]},
          "name": {"type": "string"},
          "base_color": {"type": "array", "items": {"type": "number"}},
          "roughness": {"type": "number", "minimum": 0, "maximum": 1},
          "metallic": {"type": "number", "minimum": 0, "maximum": 1},
          "normal_strength": {"type": "number", "minimum": 0, "maximum": 4},
          "tiling": {"type": "number"},
          "detail_strength": {"type": "number", "minimum": 0},
          "triplanar": {"type": "boolean"},
          "maps": {
            "type": "array",
            "items": {
              "type": "object",
              "properties": {
                "base_color": {"type": "string", "reference": "asset"},
                "normal": {"type": "string", "reference": "asset"},
                "roughness": {"type": "string", "reference": "asset"},
                "metallic": {"type": "string", "reference": "asset"},
                "ambient_occlusion": {"type": "string", "reference": "asset"},
                "height": {"type": "string", "reference": "asset"},
                "detail": {"type": "string", "reference": "asset"},
                "emissive": {"type": "string", "reference": "asset"}
              }
            }
          }
        }
      })"));
  for (const std::string_view texture : Textures)
    assert(write(root / (std::string(texture.substr(9)) + ".png"),
                 "placeholder texture bytes for " + std::string(texture)));
  // The material documents exist before the manifests are hashed, so a stale
  // hash cannot be what makes a manifest resolve.
  writeMaterials();
  registry = buildRegistry();

  slotsKeepTheBaseColourAndShrinkWithDistance();
  estimatesFollowTheDocumentedNominalTable();
  evictionOrderIsTheDocumentedOne();
  aGenerousBudgetKeepsEverything();
  twoRunsProduceIdenticalPlans();
  overBudgetEvictsTheLowestPriorityFirst();
  detailGoesBeforeBaseColour();
  aLostBaseColourIsReportedNotAbsorbed();
  evictionPriorityIsMonotonicAcrossAMixedSet();
  everyEvictionExplainsItself();
  theDistinctTextureCapIsIndependentOfBytes();
  previewHoldsStrictlyFewerLevelsAndTextures();
  heldLevelsDecideTheMinimumLod();
  colourSpaceAndAnisotropyFollowTheSlotAndTheBudget();
  anEmptyMaterialListIsRefused();
  anEmptyBudgetIsRefused();
  aMaterialWithoutAnIdIsRefused();
  theIdentityOverloadPlansTheSameTextures();

  std::filesystem::remove_all(root, filesystemError);
  std::cout << "Terrain texture residency checks passed\n";
  return 0;
}