#include "demi/runtime/network/NetworkPrediction.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace demi::runtime {
namespace {

constexpr double kSnapEpsilonSeconds = 1e-6;

[[nodiscard]] bool isFiniteNumber(const nlohmann::json &value) {
  return value.is_number() && std::isfinite(value.get<double>());
}

// Advance position-like numeric fields by velocity-like numeric fields.
void extrapolateByVelocity(nlohmann::json &state, const double deltaSeconds) {
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 3>
      axes{{{"x", "vx"}, {"y", "vy"}, {"z", "vz"}}};
  if (!state.is_object())
    return;
  for (const auto &[position, velocity] : axes) {
    const auto positionField = state.find(position);
    const auto velocityField = state.find(velocity);
    if (positionField != state.end() && velocityField != state.end() &&
        isFiniteNumber(*positionField) && isFiniteNumber(*velocityField)) {
      *positionField = positionField->get<double>() +
                       velocityField->get<double>() * deltaSeconds;
    }
  }
}

} // namespace

std::string_view
networkCorrectionMarkerName(const NetworkCorrectionMarker marker) {
  switch (marker) {
  case NetworkCorrectionMarker::Normal:
    return "normal";
  case NetworkCorrectionMarker::Teleport:
    return "teleport";
  case NetworkCorrectionMarker::Reset:
    return "reset";
  }
  return "normal";
}

std::string_view networkInputRejectCodeName(const NetworkInputRejectCode code) {
  switch (code) {
  case NetworkInputRejectCode::None:
    return "none";
  case NetworkInputRejectCode::Malformed:
    return "malformed";
  case NetworkInputRejectCode::Old:
    return "old";
  case NetworkInputRejectCode::Duplicate:
    return "duplicate";
  case NetworkInputRejectCode::ExcessiveFuture:
    return "excessive_future";
  case NetworkInputRejectCode::Capacity:
    return "capacity";
  }
  return "unknown";
}

double networkStateDistance(const nlohmann::json &left,
                            const nlohmann::json &right) {
  if (!left.is_object() || !right.is_object())
    return left == right ? 0.0 : 1.0;
  double distance = 0.0;
  std::size_t sharedNumeric = 0;
  for (const auto &[key, value] : left.items()) {
    const auto other = right.find(key);
    if (other == right.end()) {
      distance += 1.0;
      continue;
    }
    if (isFiniteNumber(value) && isFiniteNumber(*other)) {
      const double delta = value.get<double>() - other->get<double>();
      distance += delta * delta;
      ++sharedNumeric;
    } else if (value != *other) {
      distance += 1.0;
    }
  }
  for (const auto &[key, value] : right.items()) {
    (void)value;
    if (!left.contains(key))
      distance += 1.0;
  }
  return sharedNumeric > 0 ? std::sqrt(distance) : distance;
}

std::map<std::string, double> networkStateDifference(const nlohmann::json &from,
                                                     const nlohmann::json &to) {
  std::map<std::string, double> difference;
  if (!from.is_object() || !to.is_object())
    return difference;
  for (const auto &[key, value] : to.items()) {
    const auto previous = from.find(key);
    if (previous == from.end() || !isFiniteNumber(value) ||
        !isFiniteNumber(*previous))
      continue;
    difference[key] = previous->get<double>() - value.get<double>();
  }
  return difference;
}

nlohmann::json networkStateLerp(const nlohmann::json &from,
                                const nlohmann::json &to, const double alpha) {
  nlohmann::json result = to;
  if (!from.is_object() || !to.is_object())
    return result;
  const double clamped = std::clamp(alpha, 0.0, 1.0);
  for (const auto &[key, value] : from.items()) {
    const auto target = to.find(key);
    if (target == to.end() || !isFiniteNumber(value) ||
        !isFiniteNumber(*target))
      continue;
    result[key] = value.get<double>() +
                  (target->get<double>() - value.get<double>()) * clamped;
  }
  return result;
}

NetworkOwnerInputQueue::NetworkOwnerInputQueue(Config config)
    : config_(config) {
  if (config_.headOfLineTimeoutSeconds < 0.0)
    config_.headOfLineTimeoutSeconds = 0.0;
}

NetworkInputRejectCode
NetworkOwnerInputQueue::submit(const std::uint64_t sequence,
                               const double nowSeconds,
                               nlohmann::json payload) {
  if (sequence == 0) {
    ++counters_.rejectedMalformed;
    lastRejection_ = {sequence, std::string(networkInputRejectCodeName(
                                    NetworkInputRejectCode::Malformed))};
    return NetworkInputRejectCode::Malformed;
  }
  if (config_.capacity == 0) {
    ++counters_.rejectedCapacity;
    lastRejection_ = {sequence, std::string(networkInputRejectCodeName(
                                    NetworkInputRejectCode::Capacity))};
    return NetworkInputRejectCode::Capacity;
  }
  if (sequence <= lastEvaluated_) {
    // Already covered by the current acknowledgment; the client learns from
    // the next snapshot that this input will never be evaluated again.
    ++counters_.rejectedOld;
    lastRejection_ = {sequence, std::string(networkInputRejectCodeName(
                                    NetworkInputRejectCode::Old))};
    return NetworkInputRejectCode::Old;
  }
  if (pending_.contains(sequence) || rejected_.contains(sequence)) {
    ++counters_.rejectedDuplicate;
    lastRejection_ = {sequence, std::string(networkInputRejectCodeName(
                                    NetworkInputRejectCode::Duplicate))};
    return NetworkInputRejectCode::Duplicate;
  }
  if (sequence > lastEvaluated_ + config_.futureWindow) {
    // Never acknowledged directly: an acknowledgment must not skip
    // unevaluated sequences. The head-of-line timeout bounds how long a
    // client can keep replaying a permanently lost input.
    ++counters_.rejectedFuture;
    lastRejection_ = {sequence, std::string(networkInputRejectCodeName(
                                    NetworkInputRejectCode::ExcessiveFuture))};
    return NetworkInputRejectCode::ExcessiveFuture;
  }
  if (pending_.size() >= config_.capacity) {
    ++counters_.rejectedCapacity;
    lastRejection_ = {sequence, std::string(networkInputRejectCodeName(
                                    NetworkInputRejectCode::Capacity))};
    // The future window bounds this set independently from the payload queue.
    // Remember the rejected sequence so the authoritative acknowledgment can
    // eventually advance past it even if the rejection notice is lost.
    rejected_.emplace(sequence, nowSeconds);
    return NetworkInputRejectCode::Capacity;
  }
  pending_.emplace(sequence, NetworkQueuedInput{.sequence = sequence,
                                                .receivedAtSeconds = nowSeconds,
                                                .payload = std::move(payload)});
  ++counters_.accepted;
  return NetworkInputRejectCode::None;
}

std::vector<NetworkEvaluatedInput>
NetworkOwnerInputQueue::evaluate(const double nowSeconds,
                                 const std::size_t maximumCommands) {
  std::vector<NetworkEvaluatedInput> commands;
  if (maximumCommands == 0)
    return commands;
  while (commands.size() < maximumCommands) {
    const std::uint64_t expected = lastEvaluated_ + 1;
    if (rejected_.erase(expected) != 0) {
      lastEvaluated_ = expected;
      ++counters_.discardedRejected;
      ++counters_.evaluated;
      commands.push_back(NetworkEvaluatedInput{
          .sequence = expected, .payload = nullptr, .discarded = true});
      continue;
    }
    const auto next = pending_.begin();
    if (next != pending_.end() && next->first == lastEvaluated_ + 1) {
      NetworkEvaluatedInput command{.sequence = next->first,
                                    .payload = std::move(next->second.payload),
                                    .discarded = false};
      pending_.erase(next);
      lastEvaluated_ = command.sequence;
      ++counters_.evaluated;
      commands.push_back(std::move(command));
      continue;
    }
    const auto rejected = rejected_.begin();
    if (next == pending_.end() && rejected == rejected_.end())
      break;
    // A gap blocks the queue only until the oldest pending input has waited
    // past the head-of-line timeout. Missing sequences are then discarded
    // (advancing the acknowledgment) so the client stops replaying them.
    const bool rejectedIsOldest =
        rejected != rejected_.end() &&
        (next == pending_.end() || rejected->first < next->first);
    const std::uint64_t oldestSequence =
        rejectedIsOldest ? rejected->first : next->first;
    const double oldestReceivedAt =
        rejectedIsOldest ? rejected->second : next->second.receivedAtSeconds;
    if (nowSeconds - oldestReceivedAt < config_.headOfLineTimeoutSeconds)
      break;
    while (lastEvaluated_ + 1 < oldestSequence &&
           commands.size() < maximumCommands) {
      ++lastEvaluated_;
      ++counters_.discardedGaps;
      ++counters_.evaluated;
      commands.push_back(NetworkEvaluatedInput{
          .sequence = lastEvaluated_, .payload = nullptr, .discarded = true});
    }
  }
  return commands;
}

void NetworkOwnerInputQueue::clear() {
  pending_.clear();
  rejected_.clear();
  lastEvaluated_ = 0;
  counters_ = {};
  lastRejection_.reset();
}

std::uint64_t NetworkOwnerInputQueue::acknowledgedSequence() const noexcept {
  return lastEvaluated_;
}

std::size_t NetworkOwnerInputQueue::pending() const noexcept {
  return pending_.size();
}

const NetworkInputQueueCounters &
NetworkOwnerInputQueue::counters() const noexcept {
  return counters_;
}

const std::optional<std::pair<std::uint64_t, std::string>> &
NetworkOwnerInputQueue::lastRejection() const noexcept {
  return lastRejection_;
}

void NetworkOwnerInputQueue::clearRejection() { lastRejection_.reset(); }

NetworkSnapshotInterpolator::NetworkSnapshotInterpolator(Config config)
    : config_(config) {
  if (config_.interpolationDelaySeconds < 0.0)
    config_.interpolationDelaySeconds = 0.0;
  if (config_.extrapolationLimitSeconds < 0.0)
    config_.extrapolationLimitSeconds = 0.0;
}

bool NetworkSnapshotInterpolator::push(
    const NetworkAuthoritySnapshot &snapshot) {
  if (config_.capacity == 0) {
    ++counters_.droppedOverflow;
    return false;
  }
  if (!buffer_.empty()) {
    const bool epochChanged = snapshot.sessionEpoch != epoch_;
    const bool generationChanged = snapshot.ownershipGeneration != generation_;
    if (epochChanged || generationChanged) {
      // Reconnects and ownership transfers are discontinuities. Never blend
      // buffered state across either authority boundary.
      buffer_.clear();
      if (epochChanged)
        ++counters_.clearedForEpoch;
      if (generationChanged)
        ++counters_.clearedForGeneration;
    }
  }
  if (buffer_.empty()) {
    epoch_ = snapshot.sessionEpoch;
    generation_ = snapshot.ownershipGeneration;
  }
  if (!buffer_.empty() && snapshot.serverTick <= buffer_.back().serverTick) {
    // Duplicate or reordered snapshots never move rendered state backward.
    ++counters_.droppedStale;
    return false;
  }
  buffer_.push_back(snapshot);
  ++counters_.accepted;
  while (buffer_.size() > config_.capacity) {
    buffer_.pop_front();
    ++counters_.droppedOverflow;
  }
  return true;
}

std::optional<NetworkInterpolatedSample>
NetworkSnapshotInterpolator::sample(const double nowSeconds) {
  if (buffer_.empty())
    return std::nullopt;
  const double renderTime = nowSeconds - config_.interpolationDelaySeconds;
  const NetworkAuthoritySnapshot &oldest = buffer_.front();
  const NetworkAuthoritySnapshot &newest = buffer_.back();
  if (renderTime <= oldest.receivedAtSeconds + kSnapEpsilonSeconds) {
    if (oldest.marker != NetworkCorrectionMarker::Normal) {
      ++counters_.snapped;
      return NetworkInterpolatedSample{.state = oldest.state, .snapped = true};
    }
    // Buffer underrun: hold the oldest authoritative state.
    NetworkInterpolatedSample held{.state = oldest.state};
    ++counters_.interpolated;
    return held;
  }
  for (std::size_t index = buffer_.size() - 1; index > 0; --index) {
    const NetworkAuthoritySnapshot &from = buffer_[index - 1];
    const NetworkAuthoritySnapshot &to = buffer_[index];
    if (renderTime < to.receivedAtSeconds) {
      if (renderTime >= from.receivedAtSeconds) {
        if (to.marker != NetworkCorrectionMarker::Normal) {
          // Never interpolate into a discontinuity; snap when render time
          // reaches the marker snapshot.
          ++counters_.snapped;
          return NetworkInterpolatedSample{.state = to.state, .snapped = true};
        }
        const double span = to.receivedAtSeconds - from.receivedAtSeconds;
        const double alpha = span <= kSnapEpsilonSeconds
                                 ? 1.0
                                 : (renderTime - from.receivedAtSeconds) / span;
        ++counters_.interpolated;
        return NetworkInterpolatedSample{
            .state = networkStateLerp(from.state, to.state, alpha)};
      }
      break;
    }
  }
  if (renderTime >= newest.receivedAtSeconds - kSnapEpsilonSeconds) {
    if (newest.marker != NetworkCorrectionMarker::Normal) {
      ++counters_.snapped;
      return NetworkInterpolatedSample{.state = newest.state, .snapped = true};
    }
    const double deltaSeconds = renderTime - newest.receivedAtSeconds;
    if (deltaSeconds > config_.extrapolationLimitSeconds) {
      // Bounded extrapolation: hold the newest authoritative state.
      ++counters_.clamped;
      return NetworkInterpolatedSample{.state = newest.state, .clamped = true};
    }
    NetworkInterpolatedSample projected{.state = newest.state,
                                        .extrapolated = true};
    extrapolateByVelocity(projected.state, deltaSeconds);
    ++counters_.extrapolated;
    return projected;
  }
  ++counters_.interpolated;
  return NetworkInterpolatedSample{.state = newest.state};
}

void NetworkSnapshotInterpolator::clear() {
  buffer_.clear();
  epoch_ = 0;
  generation_ = 0;
  counters_ = {};
}

std::size_t NetworkSnapshotInterpolator::depth() const noexcept {
  return buffer_.size();
}

const NetworkSnapshotBufferCounters &
NetworkSnapshotInterpolator::counters() const noexcept {
  return counters_;
}

NetworkPredictedController::NetworkPredictedController(Config config)
    : config_(config) {
  if (config_.visualOffsetDecayPerSecond < 0.0)
    config_.visualOffsetDecayPerSecond = 0.0;
}

void NetworkPredictedController::enable(const std::uint64_t sessionEpoch,
                                        const std::uint64_t ownershipGeneration,
                                        nlohmann::json initialState) {
  epoch_ = sessionEpoch;
  generation_ = ownershipGeneration;
  state_ = std::move(initialState);
  if (!state_.is_object())
    state_ = nlohmann::json::object();
  enabled_ = true;
  nextSequence_ = 1;
  acknowledged_ = 0;
  lastTick_ = 0;
  history_.clear();
  historyFloor_ = 0;
  visualOffset_.clear();
  stateBeforeReconcile_ = nlohmann::json::object();
  pendingVisualCorrection_ = false;
}

void NetworkPredictedController::rebaseState(nlohmann::json state) {
  history_.clear();
  historyFloor_ = 0;
  visualOffset_.clear();
  pendingVisualCorrection_ = false;
  state_ = std::move(state);
  if (!state_.is_object())
    state_ = nlohmann::json::object();
}

void NetworkPredictedController::disable() {
  enabled_ = false;
  history_.clear();
  historyFloor_ = 0;
  visualOffset_.clear();
  pendingVisualCorrection_ = false;
}

bool NetworkPredictedController::enabled() const noexcept { return enabled_; }

std::uint64_t
NetworkPredictedController::recordLocalInput(nlohmann::json payload) {
  if (!enabled_ || config_.inputHistoryLimit == 0)
    return 0;
  const std::uint64_t sequence = nextSequence_++;
  history_.push_back(NetworkReplayCommand{.sequence = sequence,
                                          .payload = std::move(payload)});
  ++counters_.localInputs;
  if (history_.size() > config_.inputHistoryLimit) {
    historyFloor_ = history_.front().sequence;
    history_.pop_front();
    ++counters_.droppedHistory;
    ++counters_.discardedInputs;
  }
  return sequence;
}

NetworkReconciliation NetworkPredictedController::reconcile(
    const NetworkAuthoritySnapshot &snapshot) {
  NetworkReconciliation result;
  if (!enabled_)
    return result;

  if (snapshot.sessionEpoch != epoch_ ||
      snapshot.ownershipGeneration != generation_) {
    // Reconnect, ownership transfer, or a new epoch: old sequences and state
    // can never be reused. Clear history and restart the sequence space.
    const bool ownershipChanged = snapshot.ownershipGeneration != generation_;
    history_.clear();
    historyFloor_ = 0;
    visualOffset_.clear();
    pendingVisualCorrection_ = false;
    epoch_ = snapshot.sessionEpoch;
    generation_ = snapshot.ownershipGeneration;
    state_ = snapshot.state;
    if (!state_.is_object())
      state_ = nlohmann::json::object();
    acknowledged_ = snapshot.acknowledgedSequence;
    nextSequence_ = 1;
    lastTick_ = snapshot.serverTick;
    ++counters_.rebases;
    if (ownershipChanged)
      ++counters_.ownershipChanges;
    result.applied = true;
    result.snapped = true;
    result.rebased = true;
    result.acknowledgedSequence = acknowledged_;
    return result;
  }

  if (snapshot.serverTick <= lastTick_) {
    // Reordered or duplicated snapshot: the acknowledgment never moves
    // backward and stale corrections are ignored.
    ++counters_.staleSnapshots;
    return result;
  }
  lastTick_ = snapshot.serverTick;
  result.applied = true;

  const std::uint64_t acknowledged =
      std::max(acknowledged_, snapshot.acknowledgedSequence);
  result.acknowledgedSequence = acknowledged;

  if (snapshot.marker != NetworkCorrectionMarker::Normal) {
    // Teleport/reset: clear history instead of replaying through the
    // discontinuity.
    history_.clear();
    historyFloor_ = 0;
    visualOffset_.clear();
    pendingVisualCorrection_ = false;
    state_ = snapshot.state;
    if (!state_.is_object())
      state_ = nlohmann::json::object();
    acknowledged_ = acknowledged;
    nextSequence_ = std::max(nextSequence_, snapshot.acknowledgedSequence + 1);
    ++counters_.snaps;
    result.snapped = true;
    return result;
  }

  if (historyFloor_ != 0 && acknowledged < historyFloor_) {
    // At least one unacknowledged command fell out of the bounded history.
    // Replaying only the remaining suffix would invent a state neither peer
    // simulated, so snap to authority and restart from that known state.
    counters_.discardedInputs += history_.size();
    history_.clear();
    historyFloor_ = 0;
    visualOffset_.clear();
    pendingVisualCorrection_ = false;
    state_ = snapshot.state;
    if (!state_.is_object())
      state_ = nlohmann::json::object();
    acknowledged_ = acknowledged;
    nextSequence_ = std::max(nextSequence_, acknowledged + 1);
    ++counters_.snaps;
    result.snapped = true;
    return result;
  }
  if (historyFloor_ != 0 && acknowledged >= historyFloor_)
    historyFloor_ = 0;

  // Discard acknowledged inputs; they are evaluated and never replayed.
  while (!history_.empty() && history_.front().sequence <= acknowledged) {
    history_.pop_front();
    ++counters_.discardedInputs;
  }

  // The authoritative snapshot repairs any divergence immediately. A visible
  // correction is only measured after the replay outcome is committed.
  stateBeforeReconcile_ = state_;
  const double divergence =
      networkStateDistance(stateBeforeReconcile_, snapshot.state);
  counters_.lastDivergence = divergence;
  result.divergence = divergence;
  state_ = snapshot.state;
  if (!state_.is_object())
    state_ = nlohmann::json::object();
  acknowledged_ = acknowledged;
  pendingVisualCorrection_ = true;

  result.replay.assign(history_.begin(), history_.end());
  if (result.replay.empty())
    (void)commitReplay();
  else
    counters_.replayedCommands += result.replay.size();
  return result;
}

bool NetworkPredictedController::commitReplay() {
  if (!pendingVisualCorrection_)
    return false;
  pendingVisualCorrection_ = false;
  const double distance = networkStateDistance(stateBeforeReconcile_, state_);
  if (distance <= config_.correctionEpsilon)
    return false;
  ++counters_.corrections;
  counters_.lastCorrectionDistance = distance;
  if (config_.visualOffsetDecayPerSecond > 0.0)
    visualOffset_ = networkStateDifference(stateBeforeReconcile_, state_);
  else
    visualOffset_.clear();
  return true;
}

void NetworkPredictedController::updateVisualOffset(const double deltaSeconds) {
  if (visualOffset_.empty() || deltaSeconds <= 0.0)
    return;
  const double decay =
      std::exp(-config_.visualOffsetDecayPerSecond * deltaSeconds);
  for (auto it = visualOffset_.begin(); it != visualOffset_.end();) {
    it->second *= decay;
    if (std::fabs(it->second) < 1e-6)
      it = visualOffset_.erase(it);
    else
      ++it;
  }
}

const std::map<std::string, double> &
NetworkPredictedController::visualOffset() const noexcept {
  return visualOffset_;
}

const nlohmann::json &NetworkPredictedController::state() const noexcept {
  return state_;
}

void NetworkPredictedController::setPredictedState(nlohmann::json state) {
  if (state.is_object())
    state_ = std::move(state);
}

void NetworkPredictedController::clear() {
  disable();
  epoch_ = 0;
  generation_ = 0;
  nextSequence_ = 1;
  acknowledged_ = 0;
  lastTick_ = 0;
  state_ = nlohmann::json::object();
  stateBeforeReconcile_ = nlohmann::json::object();
  counters_ = {};
}

std::uint64_t NetworkPredictedController::sessionEpoch() const noexcept {
  return epoch_;
}

std::uint64_t NetworkPredictedController::ownershipGeneration() const noexcept {
  return generation_;
}

std::uint64_t NetworkPredictedController::nextSequence() const noexcept {
  return nextSequence_;
}

std::size_t NetworkPredictedController::pendingReplayCount() const noexcept {
  return history_.size();
}

const NetworkPredictionCounters &
NetworkPredictedController::counters() const noexcept {
  return counters_;
}

} // namespace demi::runtime
