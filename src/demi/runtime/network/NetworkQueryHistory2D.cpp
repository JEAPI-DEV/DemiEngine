#include "demi/runtime/network/NetworkQueryHistory2D.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace demi::runtime {

namespace {

constexpr double DirectionEpsilon = 1e-9;
constexpr double HitTieEpsilon = 1e-9;

[[nodiscard]] bool isValid(const NetworkHistoricalCircle2D &circle) {
  return !circle.entityId.empty() && std::isfinite(circle.x) &&
         std::isfinite(circle.y) && std::isfinite(circle.radius) &&
         circle.radius >= 0.0;
}

} // namespace

NetworkQueryHistory2D::NetworkQueryHistory2D(Config config) : config_(config) {}

bool NetworkQueryHistory2D::record(
    const std::uint64_t serverTick,
    std::vector<NetworkHistoricalCircle2D> circles) {
  if (config_.snapshotCapacity == 0 || config_.maximumCirclesPerSnapshot == 0 ||
      serverTick == 0 || circles.size() > config_.maximumCirclesPerSnapshot ||
      (!snapshots_.empty() && serverTick <= snapshots_.back().serverTick)) {
    ++counters_.rejected;
    return false;
  }

  std::unordered_set<std::string> entityIds;
  entityIds.reserve(circles.size());
  for (const NetworkHistoricalCircle2D &circle : circles) {
    if (!isValid(circle) || !entityIds.insert(circle.entityId).second) {
      ++counters_.rejected;
      return false;
    }
  }
  std::ranges::sort(circles, {}, &NetworkHistoricalCircle2D::entityId);
  snapshots_.push_back(
      Snapshot{.serverTick = serverTick, .circles = std::move(circles)});
  ++counters_.recorded;
  while (snapshots_.size() > config_.snapshotCapacity) {
    snapshots_.pop_front();
    ++counters_.dropped;
  }
  return true;
}

std::optional<NetworkHistoricalRaycastHit2D> NetworkQueryHistory2D::raycast(
    const std::uint64_t requestedTick, const double originX,
    const double originY, double directionX, double directionY,
    const double maximumDistance, const std::string_view layer,
    const std::string_view ignoredEntityId) {
  ++counters_.queries;
  const double directionLength =
      std::sqrt(directionX * directionX + directionY * directionY);
  if (!std::isfinite(originX) || !std::isfinite(originY) ||
      !std::isfinite(directionLength) || directionLength <= DirectionEpsilon ||
      !std::isfinite(maximumDistance) || maximumDistance <= 0.0) {
    ++counters_.misses;
    return std::nullopt;
  }

  bool clamped = false;
  const Snapshot *snapshot = snapshotFor(requestedTick, clamped);
  if (snapshot == nullptr) {
    ++counters_.misses;
    return std::nullopt;
  }
  if (clamped)
    ++counters_.clampedQueries;

  directionX /= directionLength;
  directionY /= directionLength;
  std::optional<NetworkHistoricalRaycastHit2D> closest;
  for (const NetworkHistoricalCircle2D &circle : snapshot->circles) {
    if (circle.entityId == ignoredEntityId ||
        (!layer.empty() && circle.layer != layer))
      continue;
    const double relativeX = originX - circle.x;
    const double relativeY = originY - circle.y;
    const double projection = relativeX * directionX + relativeY * directionY;
    const double discriminant = projection * projection -
                                (relativeX * relativeX + relativeY * relativeY -
                                 circle.radius * circle.radius);
    if (discriminant < 0.0)
      continue;
    double distance = -projection - std::sqrt(discriminant);
    if (distance < 0.0)
      distance = -projection + std::sqrt(discriminant);
    if (distance < 0.0 || distance > maximumDistance)
      continue;
    if (closest && distance > closest->distance - HitTieEpsilon)
      continue;
    const double hitX = originX + directionX * distance;
    const double hitY = originY + directionY * distance;
    closest = NetworkHistoricalRaycastHit2D{
        .entityId = circle.entityId,
        .layer = circle.layer,
        .sampledTick = snapshot->serverTick,
        .x = hitX,
        .y = hitY,
        .normalX = circle.radius > DirectionEpsilon
                       ? (hitX - circle.x) / circle.radius
                       : 0.0,
        .normalY = circle.radius > DirectionEpsilon
                       ? (hitY - circle.y) / circle.radius
                       : 0.0,
        .distance = distance};
  }
  if (!closest)
    ++counters_.misses;
  return closest;
}

void NetworkQueryHistory2D::clear() {
  snapshots_.clear();
  counters_ = {};
}

std::size_t NetworkQueryHistory2D::depth() const noexcept {
  return snapshots_.size();
}

std::uint64_t NetworkQueryHistory2D::latestTick() const noexcept {
  return snapshots_.empty() ? 0 : snapshots_.back().serverTick;
}

const NetworkQueryHistoryCounters2D &
NetworkQueryHistory2D::counters() const noexcept {
  return counters_;
}

const NetworkQueryHistory2D::Snapshot *
NetworkQueryHistory2D::snapshotFor(const std::uint64_t requestedTick,
                                   bool &clamped) const {
  if (snapshots_.empty())
    return nullptr;
  const std::uint64_t latest = snapshots_.back().serverTick;
  const std::uint64_t oldestAllowed = latest > config_.maximumRewindTicks
                                          ? latest - config_.maximumRewindTicks
                                          : 0;
  const std::uint64_t boundedTick =
      std::clamp(requestedTick, oldestAllowed, latest);
  clamped = boundedTick != requestedTick;
  for (auto iterator = snapshots_.rbegin(); iterator != snapshots_.rend();
       ++iterator) {
    if (iterator->serverTick <= boundedTick)
      return &*iterator;
  }
  clamped = true;
  return &snapshots_.front();
}

} // namespace demi::runtime
