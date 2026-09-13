#include "BlastFixture.h"
#include <NvBlast.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>

namespace {
struct alignas(16) Block {
  std::byte bytes[16];
};
auto storage(std::size_t bytes) {
  return std::vector<Block>((bytes + 15) / 16);
}
void logBlast(int, const char *message, const char *, int) {
  std::cerr << "Blast: " << message << '\n';
}
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace

BlastFixtureResult runBlastFixture(const BlastFixtureOptions &options) {
  require(options.chunks >= 2 && options.chunks <= 16384 &&
              options.damageBatch > 0,
          "invalid probe dimensions");
  BlastFixtureResult result;
  const auto elapsed = [](auto start) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - start)
        .count();
  };
  std::vector<NvBlastChunkDesc> chunks(options.chunks);
  for (std::uint32_t i = 0; i < chunks.size(); ++i) {
    chunks[i].centroid[0] =
        static_cast<float>(options.rowWidth ? i % options.rowWidth : i);
    chunks[i].centroid[1] =
        static_cast<float>(options.rowWidth ? i / options.rowWidth : 0);
    chunks[i].volume = 1.0F;
    chunks[i].parentChunkDescIndex = UINT32_MAX;
    chunks[i].flags = NvBlastChunkDesc::SupportFlag;
    chunks[i].userData = i;
  }
  std::vector<NvBlastBondDesc> bonds;
  const auto addBond = [&](std::uint32_t from, std::uint32_t to, int axis) {
    NvBlastBondDesc bond{};
    bond.chunkIndices[0] = from;
    bond.chunkIndices[1] = to;
    bond.bond.area = 1.0F;
    bond.bond.normal[axis] = 1.0F;
    for (int component = 0; component < 3; ++component)
      bond.bond.centroid[component] =
          (chunks[from].centroid[component] + chunks[to].centroid[component]) *
          0.5F;
    bonds.push_back(bond);
  };
  for (std::uint32_t i = 0; i < options.chunks; ++i) {
    if (i + 1 < options.chunks &&
        (!options.rowWidth || (i + 1) % options.rowWidth != 0))
      addBond(i, i + 1, 0);
    if (options.rowWidth && i + options.rowWidth < options.chunks)
      addBond(i, i + options.rowWidth, 1);
  }
  result.bonds = static_cast<std::uint32_t>(bonds.size());
  NvBlastAssetDesc descriptor{options.chunks, chunks.data(), result.bonds,
                              bonds.data()};
  const auto assetStart = std::chrono::steady_clock::now();
  auto assetMemory = storage(NvBlastGetAssetMemorySize(&descriptor, logBlast));
  auto scratch =
      storage(NvBlastGetRequiredScratchForCreateAsset(&descriptor, logBlast));
  auto *asset = NvBlastCreateAsset(assetMemory.data(), &descriptor,
                                   scratch.data(), logBlast);
  require(asset != nullptr, "create asset failed");
  result.assetMs = elapsed(assetStart);
  auto familyMemory = storage(NvBlastAssetGetFamilyMemorySize(asset, logBlast));
  auto *family = NvBlastAssetCreateFamily(familyMemory.data(), asset, logBlast);
  require(family != nullptr, "create family failed");
  scratch = storage(
      NvBlastFamilyGetRequiredScratchForCreateFirstActor(family, logBlast));
  NvBlastActorDesc actorDescriptor{1.0F, nullptr, 1.0F, nullptr};
  auto *actor = NvBlastFamilyCreateFirstActor(family, &actorDescriptor,
                                              scratch.data(), logBlast);
  require(actor != nullptr, "create actor failed");
  const auto graph = NvBlastAssetGetSupportGraph(asset, logBlast);
  std::vector<std::uint32_t> nodes(options.chunks);
  for (std::uint32_t node = 0; node < graph.nodeCount; ++node) {
    require(graph.chunkIndices[node] < nodes.size(), "unexpected graph chunk");
    nodes[graph.chunkIndices[node]] = node;
  }
  std::vector<NvBlastBondFractureData> fractures;
  for (const auto &bond : bonds)
    if (options.groupSize == 0 ||
        bond.chunkIndices[0] / options.groupSize != bond.chunkIndices[1] / options.groupSize)
      fractures.push_back({0, nodes[bond.chunkIndices[0]],
                           nodes[bond.chunkIndices[1]], options.damage});
  const auto fractureCount = static_cast<std::uint32_t>(fractures.size());
  for (std::uint32_t offset = 0; offset < fractureCount;) {
    const auto count = std::min(options.damageBatch, fractureCount - offset);
    NvBlastFractureBuffers commands{count, 0, fractures.data() + offset,
                                    nullptr};
    const auto start = std::chrono::steady_clock::now();
    NvBlastActorApplyFracture(nullptr, actor, &commands, logBlast, nullptr);
    const double duration = elapsed(start);
    result.applyTotalMs += duration;
    result.applyMaxMs = std::max(result.applyMaxMs, duration);
    ++result.applyCalls;
    offset += count;
  }
  scratch = storage(NvBlastActorGetRequiredScratchForSplit(actor, logBlast));
  std::vector<NvBlastActor *> actors(options.chunks);
  NvBlastActorSplitEvent split{nullptr, actors.data()};
  const auto splitStart = std::chrono::steady_clock::now();
  const auto count = NvBlastActorSplit(&split, actor, options.chunks,
                                       scratch.data(), logBlast, nullptr);
  result.splitMs = elapsed(splitStart);
  if (count == 0)
    actors[0] = actor;
  for (std::uint32_t i = 0; i < std::max(count, 1U); ++i) {
    std::vector<std::uint32_t> visible(
        NvBlastActorGetVisibleChunkCount(actors[i], logBlast));
    require(NvBlastActorGetVisibleChunkIndices(visible.data(), visible.size(),
                                               actors[i],
                                               logBlast) == visible.size(),
            "chunk extraction failed");
    std::ranges::sort(visible);
    result.groups.push_back(std::move(visible));
    require(NvBlastActorDeactivate(actors[i], logBlast),
            "actor cleanup failed");
  }
  std::ranges::sort(result.groups);
  return result;
}

std::vector<std::vector<std::uint32_t>> splitBlastFixture(float damage) {
  return runBlastFixture({.damage = damage}).groups;
}
