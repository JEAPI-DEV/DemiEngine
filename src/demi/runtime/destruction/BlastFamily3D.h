#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace demi::runtime {

// Internal destruction boundary. No SDK indices, handles or storage escape it.
struct DestructionChunk3D {
  std::string id;
  std::array<float, 3> centroid{};
  float volume = 1;
  bool anchored = false;
};

struct DestructionBond3D {
  std::string id;
  std::string firstChunk;
  std::string secondChunk;
  float health = 1;
};

struct BondDamage3D {
  std::string bondId;
  float damage = 0;
};

struct AnchorDamage3D {
  std::string chunkId;
  float damage = 0;
};

struct DestructionGroup3D {
  std::vector<std::string> chunks;
  bool anchored = false;
  bool operator==(const DestructionGroup3D &) const = default;
};

// Owns one persistent flat support-chunk family and at most one staged copy.
// Single-thread owned. Stage may allocate/throw; commit and discard do not.
// Token is local to this family, invalid after commit/discard. Destroying the
// family cancels its proposal. No Jolt or World mutation happens here.
class BlastFamily3D {
public:
  BlastFamily3D(std::span<const DestructionChunk3D> chunks,
                std::span<const DestructionBond3D> bonds);
  ~BlastFamily3D();
  BlastFamily3D(const BlastFamily3D &) = delete;
  BlastFamily3D &operator=(const BlastFamily3D &) = delete;

  [[nodiscard]] std::uint64_t stage(std::span<const BondDamage3D> damage,
                                  std::span<const AnchorDamage3D> anchors = {});
  [[nodiscard]] bool anchored(const std::string &chunk) const;
  [[nodiscard]] bool commit(std::uint64_t token) noexcept;
  [[nodiscard]] bool discard(std::uint64_t token) noexcept;
  [[nodiscard]] const std::vector<DestructionGroup3D> &groups() const;
  [[nodiscard]] const std::vector<DestructionChunk3D> &chunks() const;
  [[nodiscard]] const std::vector<DestructionGroup3D> &
  stagedGroups(std::uint64_t token) const;
  // Advances for every accepted damage batch, including non-splitting damage.
  [[nodiscard]] std::uint64_t revision() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime
