#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace demi::runtime {

// Deterministic transport fault injection for tests and local diagnostics.
// It deliberately operates below the protocol gateway and never ships as an
// authority or gameplay policy mechanism.
struct NetworkFaultConfig {
  std::uint32_t dropEvery = 0;
  std::uint32_t duplicateEvery = 0;
  std::uint64_t delayTicks = 0;
  std::size_t reorderWindow = 1;
  std::size_t maximumQueuedPackets = 1024;
};

struct NetworkFaultStats {
  std::uint64_t submitted = 0;
  std::uint64_t dropped = 0;
  std::uint64_t duplicated = 0;
  std::uint64_t rejectedAtCapacity = 0;
};

struct SimulatedNetworkPacket {
  std::uint64_t id = 0;
  std::vector<std::uint8_t> bytes;
};

class NetworkFaultSimulator {
public:
  explicit NetworkFaultSimulator(NetworkFaultConfig config = {});

  [[nodiscard]] bool submit(std::uint64_t packetId,
                            std::span<const std::uint8_t> bytes,
                            std::uint64_t nowTick);
  [[nodiscard]] std::vector<SimulatedNetworkPacket>
  drain(std::uint64_t nowTick);
  void reset();

  [[nodiscard]] std::size_t queued() const;
  [[nodiscard]] const NetworkFaultStats &stats() const;

private:
  struct QueuedPacket {
    SimulatedNetworkPacket packet;
    std::uint64_t deliverAt = 0;
    std::uint64_t order = 0;
  };

  NetworkFaultConfig config_;
  NetworkFaultStats stats_;
  std::uint64_t nextOrder_ = 0;
  std::vector<QueuedPacket> queue_;
};

} // namespace demi::runtime
