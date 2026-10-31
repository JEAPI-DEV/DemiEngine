#pragma once

#include "demi/runtime/concurrency/AsyncResidency.h"
#include "demi/runtime/terrain/TerrainScatter.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace demi::runtime {
class JobSystem;
struct PreparedTerrainCell;

enum class TerrainVegetationStorage { Stored, Procedural };

// One service owns one region. Revision identifies all generation inputs,
// including graph input surface, palette, overrides and algorithm version.
struct TerrainVegetationCellKey {
  std::int64_t x = 0;
  std::int64_t z = 0;
  std::uint64_t revision = 0;
  TerrainVegetationStorage storage = TerrainVegetationStorage::Stored;
  // Independent categories may occupy the same grid coordinates without
  // replacing one another. Zero is the ordinary ground-cover layer.
  std::uint8_t layer = 0;
  auto operator<=>(const TerrainVegetationCellKey &) const = default;
};

// Main-thread owner of CPU placements; loaders do CPU/IO work on JobSystem.
// Does not mutate the World, upload GPU data or infer regional hydrology.
class TerrainVegetationResidency {
public:
  using Settings = ResidencySettings;
  struct Demand {
    TerrainVegetationCellKey key;
    std::size_t preparationBytes = 0;
  };
  using Statistics = ResidencyStatistics;
  using Placements = std::vector<TerrainScatterPlacement>;
  // Both storage modes use this boundary. A source should load/generate only
  // the requested cell and obey cancellation; it must own its captured inputs.
  using Load = std::function<TerrainScatterResult(
      const TerrainVegetationCellKey &, std::stop_token)>;

  // Optional runtime preparation can remove visual records from placements
  // after constructing immutable batches. Remaining records use normal scene
  // prefab publication. No live-world or shared service access is permitted.
  using Prepare = std::function<std::shared_ptr<const PreparedTerrainCell>(
      const TerrainVegetationCellKey &, Placements &, std::stop_token)>;

  // The worker pool must outlive this service.
  TerrainVegetationResidency(JobSystem &jobs, Settings settings, Load load,
                             Prepare prepare = {});
  ~TerrainVegetationResidency();
  TerrainVegetationResidency(const TerrainVegetationResidency &) = delete;
  TerrainVegetationResidency &
  operator=(const TerrainVegetationResidency &) = delete;

  // Demand order is admission priority. Repeated demand retains ready output
  // and does not call the loader again. Missing cells are retired immediately;
  // their cancelled work stays charged until the worker actually completes.
  // Changed revisions/modes never publish results from the previous request.
  void update(std::span<const Demand> demand);
  // Returned pointers last until the next update. Consumers must release their
  // own render/collision references when a cell leaves demand.
  [[nodiscard]] const Placements *
  find(const TerrainVegetationCellKey &key) const;
  [[nodiscard]] const PreparedTerrainCell *
  prepared(const TerrainVegetationCellKey &key) const;
  [[nodiscard]] std::string error(const TerrainVegetationCellKey &key) const;
  [[nodiscard]] Statistics statistics() const;
  // Nonblocking cancellation and eviction; destruction also joins owned jobs.
  void clear();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace demi::runtime
