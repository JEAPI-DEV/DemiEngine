#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {

// Detached, bounded collision samples for authoritative lag-compensated
// queries. Recording and querying never mutate or rewind the live world.
struct NetworkHistoricalCircle2D {
  std::string entityId;
  std::string layer;
  double x = 0.0;
  double y = 0.0;
  double radius = 0.0;
};

struct NetworkHistoricalRaycastHit2D {
  std::string entityId;
  std::string layer;
  std::uint64_t sampledTick = 0;
  double x = 0.0;
  double y = 0.0;
  double normalX = 0.0;
  double normalY = 0.0;
  double distance = 0.0;
};

struct NetworkQueryHistoryCounters2D {
  std::uint64_t recorded = 0;
  std::uint64_t rejected = 0;
  std::uint64_t dropped = 0;
  std::uint64_t queries = 0;
  std::uint64_t clampedQueries = 0;
  std::uint64_t misses = 0;
};

class NetworkQueryHistory2D {
public:
  struct Config {
    std::size_t snapshotCapacity = 0;
    std::size_t maximumCirclesPerSnapshot = 0;
    std::uint64_t maximumRewindTicks = 0;
  };

  explicit NetworkQueryHistory2D(Config config);

  [[nodiscard]] bool record(std::uint64_t serverTick,
                            std::vector<NetworkHistoricalCircle2D> circles);
  [[nodiscard]] std::optional<NetworkHistoricalRaycastHit2D>
  raycast(std::uint64_t requestedTick, double originX, double originY,
          double directionX, double directionY, double maximumDistance,
          std::string_view layer = {}, std::string_view ignoredEntityId = {});

  void clear();
  [[nodiscard]] std::size_t depth() const noexcept;
  [[nodiscard]] std::uint64_t latestTick() const noexcept;
  [[nodiscard]] const NetworkQueryHistoryCounters2D &counters() const noexcept;

private:
  struct Snapshot {
    std::uint64_t serverTick = 0;
    std::vector<NetworkHistoricalCircle2D> circles;
  };

  [[nodiscard]] const Snapshot *snapshotFor(std::uint64_t requestedTick,
                                            bool &clamped) const;

  Config config_;
  std::deque<Snapshot> snapshots_;
  NetworkQueryHistoryCounters2D counters_;
};

} // namespace demi::runtime
