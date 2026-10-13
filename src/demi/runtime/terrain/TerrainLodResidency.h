#pragma once

#include "demi/runtime/terrain/TerrainRenderLod.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

namespace demi::runtime {

class JobSystem;

// Demand-driven CPU render variants. Full terrain and native collision remain
// owned by terrain publication; this cache owns reduced render levels only.
class TerrainLodResidency {
public:
  struct Settings {
    // Tracked retained array/string payload plus estimated preparation reserve.
    // Excludes shared heightfields, allocator overhead and external borrowers;
    // this bounds cache preparation policy, not process RSS.
    std::size_t maximumResidentBytes = 64U * 1024U * 1024U;
    std::size_t maximumPendingJobs = 2;
    std::size_t maximumJobsPerPresentation = 2;
    bool operator==(const Settings &) const = default;
  };

  struct Statistics {
    std::size_t residentBytes = 0;
    std::size_t pendingBytes = 0;
    std::size_t pendingJobs = 0;
    std::size_t readyLevels = 0;
    std::size_t submittedJobs = 0;
    std::size_t discardedJobs = 0;
    std::size_t failedJobs = 0;
    std::size_t budgetPressure = 0;
  };

  using Preparation =
      std::function<std::shared_ptr<const PreparedTerrainLodLevel>(
          const PreparedTerrainChunkLod &, std::size_t, std::stop_token)>;

  explicit TerrainLodResidency(JobSystem &jobs, Preparation preparation = {});
  ~TerrainLodResidency();
  TerrainLodResidency(const TerrainLodResidency &) = delete;
  TerrainLodResidency &operator=(const TerrainLodResidency &) = delete;

  void setSettings(Settings settings);
  // One presentation includes every camera. End cancels absent demand; a
  // variant needed by any camera remains live until the presentation ends.
  void beginPresentation();
  [[nodiscard]] std::shared_ptr<const PreparedTerrainLodLevel>
  request(std::shared_ptr<const PreparedTerrainChunkLod> descriptor,
          std::size_t level);
  void endPresentation();
  // Advances cancellation/retirement without drawing an unchanged viewport.
  void tickInactive();
  void pollCompleted();
  void clear();
  [[nodiscard]] Statistics statistics() const;
  // Includes admitted work and demanded misses that can be admitted next frame.
  // Terminal failures and static budget pressure do not keep an idle view
  // awake.
  [[nodiscard]] bool hasPendingWork() const;
  [[nodiscard]] const std::string &lastError() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime
