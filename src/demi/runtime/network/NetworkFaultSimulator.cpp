#include "demi/runtime/network/NetworkFaultSimulator.h"

#include <algorithm>

namespace demi::runtime {

NetworkFaultSimulator::NetworkFaultSimulator(NetworkFaultConfig config)
    : config_(config) {
  config_.reorderWindow = std::max<std::size_t>(1, config_.reorderWindow);
  config_.maximumQueuedPackets =
      std::max<std::size_t>(1, config_.maximumQueuedPackets);
}

bool NetworkFaultSimulator::submit(const std::uint64_t packetId,
                                   const std::span<const std::uint8_t> bytes,
                                   const std::uint64_t nowTick) {
  ++stats_.submitted;
  if (config_.dropEvery != 0 && stats_.submitted % config_.dropEvery == 0) {
    ++stats_.dropped;
    return true;
  }
  const std::size_t copies =
      config_.duplicateEvery != 0 &&
              stats_.submitted % config_.duplicateEvery == 0
          ? 2
          : 1;
  if (queue_.size() + copies > config_.maximumQueuedPackets) {
    ++stats_.rejectedAtCapacity;
    return false;
  }
  for (std::size_t copy = 0; copy < copies; ++copy) {
    queue_.push_back({.packet = {.id = packetId,
                                .bytes = {bytes.begin(), bytes.end()}},
                      .deliverAt = nowTick + config_.delayTicks,
                      .order = nextOrder_++});
  }
  if (copies == 2)
    ++stats_.duplicated;
  return true;
}

std::vector<SimulatedNetworkPacket>
NetworkFaultSimulator::drain(const std::uint64_t nowTick) {
  std::vector<QueuedPacket> due;
  std::erase_if(queue_, [&](QueuedPacket &queued) {
    if (queued.deliverAt > nowTick)
      return false;
    due.push_back(std::move(queued));
    return true;
  });
  std::ranges::sort(due, {}, &QueuedPacket::order);
  for (std::size_t start = 0; start < due.size();
       start += config_.reorderWindow) {
    const std::size_t end =
        std::min(due.size(), start + config_.reorderWindow);
    std::reverse(due.begin() + static_cast<std::ptrdiff_t>(start),
                 due.begin() + static_cast<std::ptrdiff_t>(end));
  }
  std::vector<SimulatedNetworkPacket> result;
  result.reserve(due.size());
  for (auto &queued : due)
    result.push_back(std::move(queued.packet));
  return result;
}

void NetworkFaultSimulator::reset() {
  queue_.clear();
  stats_ = {};
  nextOrder_ = 0;
}

std::size_t NetworkFaultSimulator::queued() const { return queue_.size(); }

const NetworkFaultStats &NetworkFaultSimulator::stats() const {
  return stats_;
}

} // namespace demi::runtime
