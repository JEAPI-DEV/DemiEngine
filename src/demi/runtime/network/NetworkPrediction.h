#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace demi::runtime {

// Step 11 latency-hiding primitives. These values are deliberately separate
// from transport, contract enforcement, and ownership policy: prediction never
// grants write permission, and interpolation never trusts a client-reported
// state. Gameplay callbacks define the replayable controller state; this
// module only stores, orders, replays, and measures it.

enum class NetworkCorrectionMarker { Normal, Teleport, Reset };

[[nodiscard]] std::string_view
networkCorrectionMarkerName(NetworkCorrectionMarker marker);

// Authoritative controller state published by the server for one entity.
struct NetworkAuthoritySnapshot {
  std::uint64_t sessionEpoch = 0;
  std::uint64_t ownershipGeneration = 0;
  std::uint64_t serverTick = 0;
  std::uint64_t acknowledgedSequence = 0;
  NetworkCorrectionMarker marker = NetworkCorrectionMarker::Normal;
  nlohmann::json state = nlohmann::json::object();
  std::optional<std::uint64_t> rejectedSequence;
  std::optional<std::string> rejectionCode;
  double receivedAtSeconds = 0.0;
};

// ---------------------------------------------------------------------------
// Server side: sequenced, timestamped owner-input queue.
//
// Inputs are evaluated in strict sequence order at the authoritative fixed
// tick. Duplicate and stale sequences are rejected without stalling the
// acknowledgment; excessively-future sequences are rejected and never
// acknowledged directly, because an acknowledgment must never skip
// unevaluated sequences. A lost input instead blocks only until the
// head-of-line timeout elapses, at which point missing sequences are
// discarded (still advancing the acknowledgment) so a client cannot replay
// them forever.
// ---------------------------------------------------------------------------

enum class NetworkInputRejectCode {
  None,
  Malformed,
  Old,
  Duplicate,
  ExcessiveFuture,
  Capacity,
};

[[nodiscard]] std::string_view
networkInputRejectCodeName(NetworkInputRejectCode code);

struct NetworkQueuedInput {
  std::uint64_t sequence = 0;
  double receivedAtSeconds = 0.0;
  nlohmann::json payload;
};

struct NetworkEvaluatedInput {
  std::uint64_t sequence = 0;
  nlohmann::json payload;
  bool discarded = false;
};

struct NetworkInputQueueCounters {
  std::uint64_t accepted = 0;
  std::uint64_t rejectedMalformed = 0;
  std::uint64_t rejectedOld = 0;
  std::uint64_t rejectedDuplicate = 0;
  std::uint64_t rejectedFuture = 0;
  std::uint64_t rejectedCapacity = 0;
  std::uint64_t discardedGaps = 0;
  std::uint64_t evaluated = 0;
};

class NetworkOwnerInputQueue {
public:
  struct Config {
    std::size_t capacity = 0;
    std::uint64_t futureWindow = 0;
    double headOfLineTimeoutSeconds = 0.0;
  };

  explicit NetworkOwnerInputQueue(Config config);

  [[nodiscard]] NetworkInputRejectCode
  submit(std::uint64_t sequence, double nowSeconds, nlohmann::json payload);
  [[nodiscard]] std::vector<NetworkEvaluatedInput>
  evaluate(double nowSeconds, std::size_t maximumCommands = 32);
  void clear();

  [[nodiscard]] std::uint64_t acknowledgedSequence() const noexcept;
  [[nodiscard]] std::size_t pending() const noexcept;
  [[nodiscard]] const NetworkInputQueueCounters &counters() const noexcept;
  [[nodiscard]] const std::optional<std::pair<std::uint64_t, std::string>> &
  lastRejection() const noexcept;
  void clearRejection();

private:
  Config config_;
  std::map<std::uint64_t, NetworkQueuedInput> pending_;
  std::uint64_t lastEvaluated_ = 0;
  NetworkInputQueueCounters counters_;
  std::optional<std::pair<std::uint64_t, std::string>> lastRejection_;
};

// ---------------------------------------------------------------------------
// Client side: bounded authoritative snapshot buffer and interpolation.
// ---------------------------------------------------------------------------

struct NetworkInterpolatedSample {
  nlohmann::json state;
  bool extrapolated = false;
  bool clamped = false;
  bool snapped = false;
};

struct NetworkSnapshotBufferCounters {
  std::uint64_t accepted = 0;
  std::uint64_t droppedStale = 0;
  std::uint64_t droppedOverflow = 0;
  std::uint64_t clearedForGeneration = 0;
  std::uint64_t interpolated = 0;
  std::uint64_t extrapolated = 0;
  std::uint64_t clamped = 0;
  std::uint64_t snapped = 0;
};

class NetworkSnapshotInterpolator {
public:
  struct Config {
    double interpolationDelaySeconds = 0.0;
    double extrapolationLimitSeconds = 0.0;
    std::size_t capacity = 0;
  };

  explicit NetworkSnapshotInterpolator(Config config);

  // Returns false for duplicate, reordered, or pre-discontinuity snapshots.
  [[nodiscard]] bool push(const NetworkAuthoritySnapshot &snapshot);
  [[nodiscard]] std::optional<NetworkInterpolatedSample>
  sample(double nowSeconds);
  void clear();

  [[nodiscard]] std::size_t depth() const noexcept;
  [[nodiscard]] const NetworkSnapshotBufferCounters &counters() const noexcept;

private:
  Config config_;
  std::deque<NetworkAuthoritySnapshot> buffer_;
  std::uint64_t generation_ = 0;
  NetworkSnapshotBufferCounters counters_;
};

// ---------------------------------------------------------------------------
// Client side: opt-in local prediction and reconciliation for the owning
// peer. The predicted state is an opaque serializable JSON document owned by
// gameplay callbacks; the controller sequences inputs, restores authoritative
// state, and reports the commands that still need replay.
// ---------------------------------------------------------------------------

struct NetworkReplayCommand {
  std::uint64_t sequence = 0;
  nlohmann::json payload;
};

struct NetworkReconciliation {
  bool applied = false;
  bool snapped = false;
  bool rebased = false;
  // Raw distance between the authoritative state and the pre-reconcile
  // prediction. Nonzero whenever inputs are still in flight; a visible
  // correction is only reported by commitReplay() after the replay result
  // differs from what was predicted.
  double divergence = 0.0;
  std::uint64_t acknowledgedSequence = 0;
  std::vector<NetworkReplayCommand> replay;
};

struct NetworkPredictionCounters {
  std::uint64_t localInputs = 0;
  std::uint64_t droppedHistory = 0;
  std::uint64_t corrections = 0;
  std::uint64_t replayedCommands = 0;
  std::uint64_t discardedInputs = 0;
  std::uint64_t snaps = 0;
  std::uint64_t rebases = 0;
  std::uint64_t ownershipChanges = 0;
  std::uint64_t staleSnapshots = 0;
  double lastCorrectionDistance = 0.0;
  double lastDivergence = 0.0;
};

class NetworkPredictedController {
public:
  struct Config {
    std::size_t inputHistoryLimit = 0;
    double visualOffsetDecayPerSecond = 0.0;
    double correctionEpsilon = 0.0;
  };

  explicit NetworkPredictedController(Config config);

  void enable(std::uint64_t sessionEpoch, std::uint64_t ownershipGeneration,
              nlohmann::json initialState);
  void disable();
  [[nodiscard]] bool enabled() const noexcept;

  // Clears replay history and re-bases the predicted state without changing
  // the sequence space. Used for scene transitions inside one session epoch
  // where the server input queue persists.
  void rebaseState(nlohmann::json state);

  // Records one locally applied input and returns its sequence. Zero when
  // prediction is disabled.
  [[nodiscard]] std::uint64_t recordLocalInput(nlohmann::json payload);

  // Restores authoritative simulation state immediately and returns the
  // still-unacknowledged inputs, in original order, for replay by gameplay
  // callbacks. Duplicate or reordered snapshots never move the
  // acknowledgment backward. After every replayed command has been applied
  // through setPredictedState, call commitReplay(): it reports whether the
  // replay outcome diverged from the prediction (a visible correction) and,
  // when it did, seeds the render-only visual offset.
  [[nodiscard]] NetworkReconciliation
  reconcile(const NetworkAuthoritySnapshot &snapshot);
  [[nodiscard]] bool commitReplay();

  // Decay the render-only visual offset. Simulation state is corrected
  // immediately; only presentation is smoothed.
  void updateVisualOffset(double deltaSeconds);
  [[nodiscard]] const std::map<std::string, double> &
  visualOffset() const noexcept;

  [[nodiscard]] const nlohmann::json &state() const noexcept;
  void setPredictedState(nlohmann::json state);
  void clear();

  [[nodiscard]] std::uint64_t sessionEpoch() const noexcept;
  [[nodiscard]] std::uint64_t ownershipGeneration() const noexcept;
  [[nodiscard]] std::uint64_t nextSequence() const noexcept;
  [[nodiscard]] std::size_t pendingReplayCount() const noexcept;
  [[nodiscard]] const NetworkPredictionCounters &counters() const noexcept;

private:
  Config config_;
  bool enabled_ = false;
  std::uint64_t epoch_ = 0;
  std::uint64_t generation_ = 0;
  std::uint64_t nextSequence_ = 1;
  std::uint64_t acknowledged_ = 0;
  std::uint64_t lastTick_ = 0;
  nlohmann::json state_ = nlohmann::json::object();
  nlohmann::json stateBeforeReconcile_ = nlohmann::json::object();
  bool pendingVisualCorrection_ = false;
  std::deque<NetworkReplayCommand> history_;
  std::map<std::string, double> visualOffset_;
  NetworkPredictionCounters counters_;
};

// Deterministic helpers shared by interpolation and reconciliation.
[[nodiscard]] double networkStateDistance(const nlohmann::json &left,
                                          const nlohmann::json &right);
[[nodiscard]] std::map<std::string, double>
networkStateDifference(const nlohmann::json &from, const nlohmann::json &to);
[[nodiscard]] nlohmann::json networkStateLerp(const nlohmann::json &from,
                                              const nlohmann::json &to,
                                              double alpha);

} // namespace demi::runtime
