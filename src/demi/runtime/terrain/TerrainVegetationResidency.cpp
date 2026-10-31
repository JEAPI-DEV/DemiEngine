#include "demi/runtime/terrain/TerrainVegetationResidency.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/terrain/PreparedTerrainCell.h"

#include "demi/runtime/concurrency/JobSystem.h"

#include <chrono>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
#include <utility>

namespace demi::runtime {
namespace {
std::size_t addBytes(std::size_t a, std::size_t b) {
  if (b > std::numeric_limits<std::size_t>::max() - a) {
    throw std::overflow_error("Vegetation cell payload size overflow");
  }
  return a + b;
}

std::size_t
placementBytes(const TerrainVegetationResidency::Placements &items) {
  std::size_t bytes = sizeof(items);
  if (items.capacity() > std::numeric_limits<std::size_t>::max() /
                             sizeof(TerrainScatterPlacement)) {
    throw std::overflow_error("Vegetation placement capacity overflow");
  }
  bytes = addBytes(bytes, items.capacity() * sizeof(TerrainScatterPlacement));
  for (const auto &item : items) {
    // Conservatively charge string capacities even when SSO stores them inline.
    for (const auto *text :
         {&item.ruleId, &item.model, &item.prefab, &item.biomeName}) {
      bytes = addBytes(bytes, addBytes(text->capacity(), 1));
    }
  }
  return bytes;
}
} // namespace

struct TerrainVegetationResidency::Impl {
  struct Cell {
    Placements placements;
    std::shared_ptr<const PreparedTerrainCell> prepared;
  };
  using Cache = AsyncResidency<TerrainVegetationCellKey, Cell>;
  Cache cache;
  Impl(JobSystem &jobs, Settings settings, Load load, Prepare prepare)
      : cache(jobs, settings,
              [load = std::move(load), prepare = std::move(prepare)](
                  const auto &key, std::stop_token stop) {
                if (!load) {
                  throw std::invalid_argument("Missing vegetation loader");
                }
                const auto start = std::chrono::steady_clock::now();
                auto result = load(key, stop);
                const auto loaded = std::chrono::steady_clock::now();
                if (result.truncated) {
                  throw std::runtime_error(
                      "Vegetation cell generation was truncated");
                }
                Cell cell;
                if (!stop.stop_requested() && prepare) {
                  cell.prepared = prepare(key, result.placements, stop);
                }
                const auto end = std::chrono::steady_clock::now();
                const auto bytes =
                    addBytes(placementBytes(result.placements),
                             cell.prepared ? cell.prepared->accountedBytes : 0);
                cell.placements = std::move(result.placements);
                return Cache::Loaded{
                    std::move(cell), bytes,
                    std::chrono::duration<double, std::milli>(loaded - start)
                        .count(),
                    std::chrono::duration<double, std::milli>(end - loaded)
                        .count()};
              }) {}
};

TerrainVegetationResidency::TerrainVegetationResidency(JobSystem &jobs,
                                                       Settings settings,
                                                       Load load,
                                                       Prepare prepare) {
  if (!load) {
    throw std::invalid_argument("Missing vegetation loader");
  }
  impl_ = std::make_unique<Impl>(jobs, settings, std::move(load),
                                 std::move(prepare));
}
TerrainVegetationResidency::~TerrainVegetationResidency() = default;

void TerrainVegetationResidency::update(std::span<const Demand> demand) {
  std::set<std::tuple<std::int64_t, std::int64_t, std::uint8_t>> coordinates;
  std::vector<Impl::Cache::Demand> requests;
  requests.reserve(demand.size());
  for (const auto &request : demand) {
    if ((request.key.storage != TerrainVegetationStorage::Stored &&
         request.key.storage != TerrainVegetationStorage::Procedural) ||
        !coordinates.emplace(request.key.x, request.key.z, request.key.layer).second) {
      throw std::invalid_argument(
          "Invalid or duplicate vegetation cell demand");
    }
    requests.push_back({request.key, request.preparationBytes});
  }
  impl_->cache.update(requests);
}
const TerrainVegetationResidency::Placements *
TerrainVegetationResidency::find(const TerrainVegetationCellKey &key) const {
  const auto *cell = impl_->cache.find(key);
  return cell ? &cell->placements : nullptr;
}
const PreparedTerrainCell *TerrainVegetationResidency::prepared(
    const TerrainVegetationCellKey &key) const {
  const auto *cell = impl_->cache.find(key);
  return cell ? cell->prepared.get() : nullptr;
}
std::string
TerrainVegetationResidency::error(const TerrainVegetationCellKey &key) const {
  return impl_->cache.error(key);
}
TerrainVegetationResidency::Statistics
TerrainVegetationResidency::statistics() const {
  return impl_->cache.statistics();
}
void TerrainVegetationResidency::clear() { impl_->cache.clear(); }
} // namespace demi::runtime
