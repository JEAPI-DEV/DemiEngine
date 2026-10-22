#pragma once

#include "demi/runtime/render/bgfx3d/MeshInstanceBatch3D.h"

#include <cstddef>
#include <optional>
#include <unordered_map>

namespace demi::runtime {
struct Entity;
}

namespace demi::runtime::render {

// Compiled submission data for the existing immutable camera depth/color pair.
// The batch collector remains the sole grouping authority. No ready result or
// borrowed world/resource identity survives the pair owner's reset.
class OpaqueInstancePlan3D {
public:
  struct Statistics {
    std::uint32_t distanceCulled = 0;
    std::uint32_t mediumLodMeshes = 0;
    std::uint32_t lowLodMeshes = 0;
    std::size_t visibleCopies = 0;
  };

  void reset() {
    groups_.reset();
    sources_.clear();
    statistics_ = {};
    ready_ = false;
  }

  [[nodiscard]] std::optional<bool>
  sourceEligibility(const Entity &entity) const {
    const auto found = sources_.find(&entity);
    return found == sources_.end() ? std::nullopt
                                   : std::optional<bool>(found->second);
  }
  void classifySource(const Entity &entity, bool eligible) {
    sources_.emplace(&entity, eligible);
  }
  [[nodiscard]] bool ready() const noexcept { return ready_; }
  void finish() noexcept { ready_ = true; }
  [[nodiscard]] MeshInstanceBatch3D &groups() noexcept { return groups_; }
  [[nodiscard]] const MeshInstanceBatch3D &groups() const noexcept {
    return groups_;
  }
  [[nodiscard]] Statistics &statistics() noexcept { return statistics_; }
  [[nodiscard]] const Statistics &statistics() const noexcept {
    return statistics_;
  }

private:
  MeshInstanceBatch3D groups_;
  std::unordered_map<const Entity *, bool> sources_;
  Statistics statistics_;
  bool ready_ = false;
};

} // namespace demi::runtime::render
