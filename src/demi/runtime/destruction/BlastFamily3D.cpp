#include "demi/runtime/destruction/BlastFamily3D.h"

#include <NvBlast.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace demi::runtime {
namespace {

struct alignas(16) Block {
  std::byte bytes[16]{};
};
using Storage = std::vector<Block>;

Storage storage(std::size_t bytes) {
  if (bytes == 0)
    throw std::runtime_error("Blast requested empty storage");
  return Storage((bytes + 15) / 16);
}

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool validId(const std::string &id) {
  const auto alphanumeric = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
  };
  return !id.empty() && id.size() <= 128 && alphanumeric(id.front()) &&
         std::ranges::all_of(id, [&](char c) {
           return alphanumeric(c) || c == '.' || c == '_' || c == '-';
         });
}

std::vector<NvBlastActor *> actors(NvBlastFamily *family) {
  std::vector<NvBlastActor *> result(
      NvBlastFamilyGetActorCount(family, nullptr));
  require(NvBlastFamilyGetActors(result.data(), result.size(), family,
                                 nullptr) == result.size(),
          "Blast actor enumeration failed");
  return result;
}

struct FamilyState {
  Storage memory;
  std::vector<DestructionGroup3D> groups;
  NvBlastFamily *family() {
    return reinterpret_cast<NvBlastFamily *>(memory.data());
  }
};

} // namespace

struct BlastFamily3D::Impl {
  std::vector<DestructionChunk3D> chunks;
  std::vector<DestructionBond3D> bonds;
  std::map<std::string, std::uint32_t> chunkIndices;
  std::map<std::string, std::uint32_t> bondIndices;
  std::vector<std::uint32_t> nodes;
  Storage assetMemory;
  std::unique_ptr<FamilyState> current;
  std::unique_ptr<FamilyState> pending;
  std::uint64_t sequence = 0;
  std::uint64_t pendingToken = 0;
  std::uint64_t revision = 0;

  NvBlastAsset *asset() {
    return reinterpret_cast<NvBlastAsset *>(assetMemory.data());
  }

  void validate() {
    if (chunks.empty() || chunks.size() > 256 || bonds.size() > 2048)
      throw std::invalid_argument(
          "Destruction requires 1..256 chunks and at most 2048 bonds");
    std::ranges::sort(chunks, {}, &DestructionChunk3D::id);
    std::ranges::sort(bonds, {}, &DestructionBond3D::id);
    for (std::uint32_t i = 0; i < chunks.size(); ++i) {
      const auto &chunk = chunks[i];
      if (!validId(chunk.id) || !std::isfinite(chunk.volume) ||
          chunk.volume <= 0 ||
          !std::ranges::all_of(chunk.centroid,
                               [](float x) {
                                 return std::isfinite(x) &&
                                        std::abs(x) <= 1000000;
                               }) ||
          !chunkIndices.emplace(chunk.id, i).second)
        throw std::invalid_argument("Invalid or duplicate destruction chunk");
    }
    std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
    for (std::uint32_t i = 0; i < bonds.size(); ++i) {
      const auto &bond = bonds[i];
      if (!validId(bond.id) || !std::isfinite(bond.health) ||
          bond.health <= 0 || !chunkIndices.contains(bond.firstChunk) ||
          !chunkIndices.contains(bond.secondChunk) ||
          bond.firstChunk == bond.secondChunk ||
          !bondIndices.emplace(bond.id, i).second)
        throw std::invalid_argument("Invalid or duplicate destruction bond");
      const auto a = chunkIndices.at(bond.firstChunk);
      const auto b = chunkIndices.at(bond.secondChunk);
      if (!edges.emplace(std::min(a, b), std::max(a, b)).second)
        throw std::invalid_argument("Duplicate destruction bond endpoints");
    }
    // One initial physical assembly must have one connected support graph.
    std::set<std::uint32_t> reached{0};
    for (std::size_t pass = 0; pass < chunks.size(); ++pass)
      for (const auto &[a, b] : edges) {
        if (reached.contains(a))
          reached.insert(b);
        if (reached.contains(b))
          reached.insert(a);
      }
    if (reached.size() != chunks.size())
      throw std::invalid_argument(
          "Initial destruction graph must be connected");
  }

  std::unique_ptr<FamilyState> emptyFamily() {
    auto state = std::make_unique<FamilyState>();
    state->memory = storage(NvBlastAssetGetFamilyMemorySize(asset(), nullptr));
    require(NvBlastAssetCreateFamily(state->memory.data(), asset(), nullptr),
            "Blast family creation failed");
    return state;
  }

  void extract(FamilyState &state) {
    state.groups.clear();
    std::vector<bool> seen(chunks.size());
    for (auto *actor : actors(state.family())) {
      std::vector<std::uint32_t> visible(
          NvBlastActorGetVisibleChunkCount(actor, nullptr));
      require(!visible.empty() && NvBlastActorGetVisibleChunkIndices(
                                      visible.data(), visible.size(), actor,
                                      nullptr) == visible.size(),
              "Blast visible chunk extraction failed");
      DestructionGroup3D group;
      for (const auto index : visible) {
        require(index < chunks.size() && !seen[index],
                "Blast returned invalid chunk ownership");
        seen[index] = true;
        group.chunks.push_back(chunks[index].id);
        group.anchored = group.anchored || chunks[index].anchored;
      }
      std::ranges::sort(group.chunks);
      state.groups.push_back(std::move(group));
    }
    require(std::ranges::all_of(seen, [](bool value) { return value; }),
            "Blast lost chunk ownership");
    std::ranges::sort(state.groups, {}, &DestructionGroup3D::chunks);
  }

  void initialize() {
    validate();
    std::vector<NvBlastChunkDesc> nativeChunks(chunks.size());
    for (std::uint32_t i = 0; i < chunks.size(); ++i) {
      auto &native = nativeChunks[i];
      std::ranges::copy(chunks[i].centroid, native.centroid);
      native.volume = chunks[i].volume;
      native.parentChunkDescIndex = UINT32_MAX;
      native.flags = NvBlastChunkDesc::SupportFlag;
      native.userData = i;
    }
    std::vector<NvBlastBondDesc> nativeBonds(bonds.size());
    for (std::uint32_t i = 0; i < bonds.size(); ++i) {
      auto &native = nativeBonds[i];
      const auto a = chunkIndices.at(bonds[i].firstChunk);
      const auto b = chunkIndices.at(bonds[i].secondChunk);
      native.chunkIndices[0] = a;
      native.chunkIndices[1] = b;
      native.bond.area = 1;
      native.bond.normal[0] = 1; // Explicit bond commands do not use normals.
      native.bond.userData = i;
      for (int axis = 0; axis < 3; ++axis)
        native.bond.centroid[axis] =
            (chunks[a].centroid[axis] + chunks[b].centroid[axis]) * 0.5F;
    }
    NvBlastAssetDesc descriptor{
        static_cast<std::uint32_t>(chunks.size()), nativeChunks.data(),
        static_cast<std::uint32_t>(bonds.size()), nativeBonds.data()};
    assetMemory = storage(NvBlastGetAssetMemorySize(&descriptor, nullptr));
    auto scratch =
        storage(NvBlastGetRequiredScratchForCreateAsset(&descriptor, nullptr));
    require(NvBlastCreateAsset(assetMemory.data(), &descriptor, scratch.data(),
                               nullptr),
            "Blast asset creation failed");
    const auto graph = NvBlastAssetGetSupportGraph(asset(), nullptr);
    nodes.resize(chunks.size());
    require(graph.nodeCount == chunks.size(),
            "Blast support graph size mismatch");
    for (std::uint32_t node = 0; node < graph.nodeCount; ++node) {
      require(graph.chunkIndices[node] < chunks.size(),
              "Blast support chunk mismatch");
      nodes[graph.chunkIndices[node]] = node;
    }
    std::vector<float> healths(bonds.size());
    const auto *assetBonds = NvBlastAssetGetBonds(asset(), nullptr);
    for (std::size_t i = 0; i < bonds.size(); ++i) {
      require(assetBonds[i].userData < bonds.size(),
              "Blast bond identity mismatch");
      healths[i] = bonds[assetBonds[i].userData].health;
    }
    current = emptyFamily();
    scratch = storage(NvBlastFamilyGetRequiredScratchForCreateFirstActor(
        current->family(), nullptr));
    NvBlastActorDesc actorDesc{1, healths.empty() ? nullptr : healths.data(),
                               std::numeric_limits<float>::max(), nullptr};
    require(NvBlastFamilyCreateFirstActor(current->family(), &actorDesc,
                                          scratch.data(), nullptr),
            "Blast initial actor creation failed");
    extract(*current);
  }

  std::unique_ptr<FamilyState> cloneCurrent() {
    auto copy = emptyFamily();
    // Use supported actor serialization, not a memcpy of private SDK pointers.
    for (auto *actor : actors(current->family())) {
      const auto bytes = NvBlastActorGetSerializationSize(actor, nullptr);
      auto buffer = storage(bytes);
      require(NvBlastActorSerialize(buffer.data(), bytes, actor, nullptr) ==
                  bytes,
              "Blast actor snapshot failed");
      require(
          NvBlastFamilyDeserializeActor(copy->family(), buffer.data(), nullptr),
          "Blast actor snapshot restore failed");
    }
    return copy;
  }
};

BlastFamily3D::BlastFamily3D(std::span<const DestructionChunk3D> chunks,
                             std::span<const DestructionBond3D> bonds)
    : impl_(std::make_unique<Impl>()) {
  if (chunks.empty() || chunks.size() > 256 || bonds.size() > 2048)
    throw std::invalid_argument("Destruction family exceeds chunk/bond limits");
  impl_->chunks.assign(chunks.begin(), chunks.end());
  impl_->bonds.assign(bonds.begin(), bonds.end());
  impl_->initialize();
}

BlastFamily3D::~BlastFamily3D() = default;

std::uint64_t BlastFamily3D::stage(std::span<const BondDamage3D> damage) {
  auto &state = *impl_;
  if (state.pending)
    throw std::logic_error(
        "Commit or discard the existing destruction proposal first");
  if (damage.empty() || damage.size() > state.bonds.size())
    throw std::invalid_argument("Invalid destruction damage batch size");
  std::map<std::uint32_t, float> orderedDamage;
  for (const auto &entry : damage) {
    const auto bond = state.bondIndices.find(entry.bondId);
    if (bond == state.bondIndices.end() || !std::isfinite(entry.damage) ||
        entry.damage <= 0 ||
        !orderedDamage.emplace(bond->second, entry.damage).second)
      throw std::invalid_argument("Damage requires unique existing bond IDs "
                                  "and finite positive amounts");
  }
  if (state.sequence == UINT64_MAX || state.revision == UINT64_MAX)
    throw std::overflow_error("Destruction generation exhausted");
  auto proposal = state.cloneCurrent();
  for (auto *actor : actors(proposal->family())) {
    std::vector<NvBlastBondFractureData> commands;
    for (const auto &[index, amount] : orderedDamage) {
      const auto &bond = state.bonds[index];
      const auto a = state.chunkIndices.at(bond.firstChunk);
      const auto b = state.chunkIndices.at(bond.secondChunk);
      if (NvBlastFamilyGetChunkActor(proposal->family(), a, nullptr) == actor &&
          NvBlastFamilyGetChunkActor(proposal->family(), b, nullptr) == actor)
        commands.push_back({0, state.nodes[a], state.nodes[b], amount});
    }
    if (commands.empty())
      continue;
    NvBlastFractureBuffers buffer{static_cast<std::uint32_t>(commands.size()),
                                  0, commands.data(), nullptr};
    NvBlastActorApplyFracture(nullptr, actor, &buffer, nullptr, nullptr);
    auto scratch =
        storage(NvBlastActorGetRequiredScratchForSplit(actor, nullptr));
    std::vector<NvBlastActor *> replacements(state.chunks.size());
    NvBlastActorSplitEvent split{nullptr, replacements.data()};
    NvBlastActorSplit(&split, actor, replacements.size(), scratch.data(),
                      nullptr, nullptr);
  }
  state.extract(*proposal);
  state.pending = std::move(proposal);
  state.pendingToken = ++state.sequence;
  return state.pendingToken;
}

bool BlastFamily3D::commit(std::uint64_t token) noexcept {
  if (!impl_->pending || impl_->pendingToken != token)
    return false;
  impl_->current.swap(impl_->pending);
  impl_->pending.reset();
  impl_->pendingToken = 0;
  ++impl_->revision;
  return true;
}

bool BlastFamily3D::discard(std::uint64_t token) noexcept {
  if (!impl_->pending || impl_->pendingToken != token)
    return false;
  impl_->pending.reset();
  impl_->pendingToken = 0;
  return true;
}

const std::vector<DestructionGroup3D> &BlastFamily3D::groups() const {
  return impl_->current->groups;
}

const std::vector<DestructionChunk3D> &BlastFamily3D::chunks() const {
  return impl_->chunks;
}

const std::vector<DestructionGroup3D> &
BlastFamily3D::stagedGroups(std::uint64_t token) const {
  if (!impl_->pending || impl_->pendingToken != token)
    throw std::invalid_argument("Stale destruction proposal");
  return impl_->pending->groups;
}

std::uint64_t BlastFamily3D::revision() const { return impl_->revision; }

} // namespace demi::runtime
