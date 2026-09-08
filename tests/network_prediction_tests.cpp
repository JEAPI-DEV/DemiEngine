#include "demi/runtime/network/NetworkFaultSimulator.h"
#include "demi/runtime/network/NetworkPrediction.h"
#include "demi/runtime/network/NetworkQueryHistory2D.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace demi::runtime;

constexpr double Epsilon = 1e-6;

void applyMove(NetworkPredictedController &controller,
               const NetworkReplayCommand &command) {
  nlohmann::json state = controller.state();
  state["x"] = state.value("x", 0.0) + command.payload.value("x", 0.0);
  controller.setPredictedState(std::move(state));
}

void testDisabledDefaults() {
  NetworkOwnerInputQueue queue({});
  assert(queue.submit(1, 0.0, {{"move", 1.0}}) ==
         NetworkInputRejectCode::Capacity);
  assert(queue.pending() == 0);

  NetworkSnapshotInterpolator interpolator({});
  assert(!interpolator.push(
      {.serverTick = 1, .state = {{"x", 0.0}}, .receivedAtSeconds = 0.0}));
  assert(!interpolator.sample(0.0).has_value());

  NetworkPredictedController controller({});
  controller.enable(1, 1, {{"x", 0.0}});
  assert(controller.recordLocalInput({{"x", 1.0}}) == 0);
}

void testSequencedInputQueue() {
  NetworkOwnerInputQueue queue(
      {.capacity = 4, .futureWindow = 4, .headOfLineTimeoutSeconds = 0.1});
  assert(queue.submit(2, 0.0, {{"x", 1.0}}) == NetworkInputRejectCode::None);
  assert(queue.submit(2, 0.0, {{"x", 1.0}}) ==
         NetworkInputRejectCode::Duplicate);
  assert(queue.submit(8, 0.0, {{"x", 1.0}}) ==
         NetworkInputRejectCode::ExcessiveFuture);
  assert(queue.evaluate(0.05, 4).empty());
  const auto evaluated = queue.evaluate(0.2, 4);
  assert(evaluated.size() == 2);
  assert(evaluated[0].sequence == 1 && evaluated[0].discarded);
  assert(evaluated[1].sequence == 2 && !evaluated[1].discarded);
  assert(queue.acknowledgedSequence() == 2);
  assert(queue.submit(2, 0.3, {{"x", 1.0}}) == NetworkInputRejectCode::Old);

  NetworkOwnerInputQueue capacityQueue(
      {.capacity = 1, .futureWindow = 4, .headOfLineTimeoutSeconds = 10.0});
  assert(capacityQueue.submit(2, 0.0, {{"x", 2.0}}) ==
         NetworkInputRejectCode::None);
  assert(capacityQueue.submit(1, 0.0, {{"x", 1.0}}) ==
         NetworkInputRejectCode::Capacity);
  const auto capacityResult = capacityQueue.evaluate(0.0, 4);
  assert(capacityResult.size() == 2);
  assert(capacityResult[0].sequence == 1 && capacityResult[0].discarded);
  assert(capacityResult[1].sequence == 2 && !capacityResult[1].discarded);
  assert(capacityQueue.acknowledgedSequence() == 2);
  assert(capacityQueue.counters().discardedRejected == 1);

  NetworkOwnerInputQueue trailingRejection(
      {.capacity = 1, .futureWindow = 4, .headOfLineTimeoutSeconds = 1.0});
  assert(trailingRejection.submit(1, 0.0, {{"x", 1.0}}) ==
         NetworkInputRejectCode::None);
  assert(trailingRejection.submit(3, 0.0, {{"x", 3.0}}) ==
         NetworkInputRejectCode::Capacity);
  assert(trailingRejection.evaluate(0.0, 4).size() == 1);
  const auto trailing = trailingRejection.evaluate(2.0, 4);
  assert(trailing.size() == 2 && trailing[0].sequence == 2 &&
         trailing[0].discarded && trailing[1].sequence == 3 &&
         trailing[1].discarded);
  assert(trailingRejection.acknowledgedSequence() == 3);
}

void testSnapshotInterpolation() {
  NetworkSnapshotInterpolator interpolator({.interpolationDelaySeconds = 0.1,
                                            .extrapolationLimitSeconds = 0.05,
                                            .capacity = 3});
  const NetworkAuthoritySnapshot first{.sessionEpoch = 1,
                                       .ownershipGeneration = 1,
                                       .serverTick = 1,
                                       .state = {{"x", 0.0}, {"vx", 10.0}},
                                       .receivedAtSeconds = 0.0};
  const NetworkAuthoritySnapshot second{.sessionEpoch = 1,
                                        .ownershipGeneration = 1,
                                        .serverTick = 2,
                                        .state = {{"x", 10.0}, {"vx", 10.0}},
                                        .receivedAtSeconds = 0.1};
  assert(interpolator.push(first));
  assert(interpolator.push(second));
  const auto halfway = interpolator.sample(0.15);
  assert(halfway &&
         std::abs(halfway->state["x"].get<double>() - 5.0) < Epsilon);
  const auto extrapolated = interpolator.sample(0.225);
  assert(extrapolated && extrapolated->extrapolated);
  assert(std::abs(extrapolated->state["x"].get<double>() - 10.25) < Epsilon);
  const auto clamped = interpolator.sample(0.3);
  assert(clamped && clamped->clamped && clamped->state["x"] == 10.0);
  assert(!interpolator.push(second));

  assert(interpolator.push({.sessionEpoch = 2,
                            .ownershipGeneration = 1,
                            .serverTick = 1,
                            .state = {{"x", 20.0}},
                            .receivedAtSeconds = 1.0}));
  assert(interpolator.depth() == 1);
  assert(interpolator.counters().clearedForEpoch == 1);
  assert(interpolator.push({.sessionEpoch = 2,
                            .ownershipGeneration = 2,
                            .serverTick = 1,
                            .marker = NetworkCorrectionMarker::Teleport,
                            .state = {{"x", 30.0}},
                            .receivedAtSeconds = 2.0}));
  assert(interpolator.depth() == 1);
  assert(interpolator.counters().clearedForGeneration == 1);
  const auto snapped = interpolator.sample(2.1);
  assert(snapped && snapped->snapped && snapped->state["x"] == 30.0);
}

void testPredictionAndReconciliation() {
  NetworkPredictedController controller({.inputHistoryLimit = 8,
                                         .visualOffsetDecayPerSecond = 10.0,
                                         .correctionEpsilon = 0.001});
  controller.enable(1, 1, {{"x", 0.0}});
  assert(controller.recordLocalInput({{"x", 1.0}}) == 1);
  controller.setPredictedState({{"x", 1.0}});

  auto replay = controller.reconcile({.sessionEpoch = 1,
                                      .ownershipGeneration = 1,
                                      .serverTick = 1,
                                      .acknowledgedSequence = 0,
                                      .state = {{"x", 0.0}}});
  assert(replay.applied && replay.replay.size() == 1);
  applyMove(controller, replay.replay.front());
  assert(!controller.commitReplay());

  const std::uint64_t secondSequence =
      controller.recordLocalInput({{"x", 2.0}});
  const std::uint64_t thirdSequence = controller.recordLocalInput({{"x", 3.0}});
  assert(secondSequence == 2 && thirdSequence == 3);
  applyMove(controller, {.sequence = secondSequence, .payload = {{"x", 2.0}}});
  applyMove(controller, {.sequence = thirdSequence, .payload = {{"x", 3.0}}});
  replay = controller.reconcile({.sessionEpoch = 1,
                                 .ownershipGeneration = 1,
                                 .serverTick = 2,
                                 .acknowledgedSequence = 1,
                                 .state = {{"x", 1.0}}});
  assert(replay.replay.size() == 2);
  assert(replay.replay[0].sequence == 2 && replay.replay[1].sequence == 3);
  for (const auto &command : replay.replay)
    applyMove(controller, command);
  (void)controller.commitReplay();

  const auto stale = controller.reconcile({.sessionEpoch = 1,
                                           .ownershipGeneration = 1,
                                           .serverTick = 2,
                                           .acknowledgedSequence = 3,
                                           .state = {{"x", 100.0}}});
  assert(!stale.applied && controller.pendingReplayCount() == 2);

  const auto corrected = controller.reconcile({.sessionEpoch = 1,
                                               .ownershipGeneration = 1,
                                               .serverTick = 3,
                                               .acknowledgedSequence = 3,
                                               .state = {{"x", 5.5}}});
  assert(corrected.applied && corrected.replay.empty());
  assert(controller.state()["x"] == 5.5);
  assert(controller.counters().corrections == 1);
  assert(!controller.visualOffset().empty());
  controller.updateVisualOffset(1.0);
  assert(std::abs(controller.visualOffset().at("x")) < 0.001);

  assert(controller.recordLocalInput({{"x", 1.0}}) == 4);
  const auto teleport =
      controller.reconcile({.sessionEpoch = 1,
                            .ownershipGeneration = 1,
                            .serverTick = 4,
                            .acknowledgedSequence = 3,
                            .marker = NetworkCorrectionMarker::Teleport,
                            .state = {{"x", 50.0}}});
  assert(teleport.snapped && controller.pendingReplayCount() == 0);

  assert(controller.recordLocalInput({{"x", 1.0}}) == 5);
  const auto transfer = controller.reconcile({.sessionEpoch = 1,
                                              .ownershipGeneration = 2,
                                              .serverTick = 1,
                                              .state = {{"x", 60.0}}});
  assert(transfer.rebased && transfer.snapped);
  assert(controller.nextSequence() == 1);

  const auto reconnect = controller.reconcile({.sessionEpoch = 2,
                                               .ownershipGeneration = 2,
                                               .serverTick = 1,
                                               .state = {{"x", 0.0}}});
  assert(reconnect.rebased && reconnect.snapped);
}

void testIncompleteHistorySnaps() {
  NetworkPredictedController controller({.inputHistoryLimit = 2});
  controller.enable(1, 1, {{"x", 0.0}});
  for (int index = 0; index < 3; ++index) {
    assert(controller.recordLocalInput({{"x", 1.0}}) != 0);
    nlohmann::json state = controller.state();
    state["x"] = state.value("x", 0.0) + 1.0;
    controller.setPredictedState(std::move(state));
  }
  const auto correction = controller.reconcile({.sessionEpoch = 1,
                                                .ownershipGeneration = 1,
                                                .serverTick = 1,
                                                .acknowledgedSequence = 0,
                                                .state = {{"x", 0.0}}});
  assert(correction.snapped && correction.replay.empty());
  assert(controller.pendingReplayCount() == 0);
  assert(controller.state()["x"] == 0.0);
}

void testBoundedHistoricalQueries() {
  NetworkQueryHistory2D disabled({});
  assert(!disabled.record(1, {{.entityId = "target", .radius = 1.0}}));

  NetworkQueryHistory2D history({.snapshotCapacity = 2,
                                 .maximumCirclesPerSnapshot = 4,
                                 .maximumRewindTicks = 1});
  assert(history.record(
      1,
      {{.entityId = "target", .layer = "players", .x = 5.0, .radius = 1.0}}));
  assert(history.record(
      2,
      {{.entityId = "target", .layer = "players", .x = 10.0, .radius = 1.0},
       {.entityId = "ignored", .layer = "players", .x = 3.0, .radius = 1.0}}));
  const auto oldHit = history.raycast(1, 0.0, 0.0, 1.0, 0.0, 20.0, "players");
  assert(oldHit && oldHit->entityId == "target" && oldHit->sampledTick == 1);
  assert(std::abs(oldHit->distance - 4.0) < Epsilon);
  const auto ignoredHit =
      history.raycast(2, 0.0, 0.0, 1.0, 0.0, 20.0, "players", "ignored");
  assert(ignoredHit && ignoredHit->entityId == "target");

  assert(history.record(
      3,
      {{.entityId = "target", .layer = "players", .x = 15.0, .radius = 1.0}}));
  assert(history.depth() == 2 && history.counters().dropped == 1);
  const auto clamped =
      history.raycast(1, 0.0, 0.0, 1.0, 0.0, 20.0, "players", "ignored");
  assert(clamped && clamped->sampledTick == 2);
  assert(history.counters().clampedQueries == 1);
  assert(!history.record(3, {{.entityId = "duplicate", .radius = 1.0}}));
  assert(!history.record(4, {{.entityId = "same", .radius = 1.0},
                             {.entityId = "same", .radius = 1.0}}));
}

void testPauseResumeAndLifecycleClears() {
  const std::vector<std::uint8_t> packet{1};
  NetworkFaultSimulator lifecycleLink(
      {.delayTicks = 1, .jitterTicks = 1, .maximumQueuedPackets = 4});
  assert(lifecycleLink.submit(1, packet, 0));
  assert(lifecycleLink.submit(2, packet, 0));
  const auto first = lifecycleLink.drain(1);
  assert(first.size() == 1 && first.front().id == 2);
  // A long pause/background interval does not lose the remaining bounded
  // packet; resume drains it deterministically without using wall-clock time.
  const auto resumed = lifecycleLink.drain(100);
  assert(resumed.size() == 1 && resumed.front().id == 1);
  lifecycleLink.reset();
  assert(lifecycleLink.queued() == 0);

  NetworkPredictedController controller({.inputHistoryLimit = 4});
  controller.enable(1, 1, {{"x", 0.0}});
  assert(controller.recordLocalInput({{"x", 1.0}}) == 1);
  controller.disable();
  assert(!controller.enabled() && controller.pendingReplayCount() == 0);
  assert(controller.recordLocalInput({{"x", 1.0}}) == 0);
  controller.clear();
  assert(controller.sessionEpoch() == 0 && controller.nextSequence() == 1);

  NetworkSnapshotInterpolator interpolation(
      {.interpolationDelaySeconds = 0.1, .capacity = 2});
  assert(interpolation.push({.sessionEpoch = 1,
                             .ownershipGeneration = 1,
                             .serverTick = 1,
                             .state = {{"x", 0.0}}}));
  interpolation.clear();
  assert(interpolation.depth() == 0 && interpolation.counters().accepted == 0);
}

void testReferenceActionMovementUnderFaults() {
  NetworkPredictedController client({.inputHistoryLimit = 32});
  client.enable(1, 1, {{"x", 0.0}});
  NetworkOwnerInputQueue server(
      {.capacity = 16, .futureWindow = 32, .headOfLineTimeoutSeconds = 1.0});
  NetworkFaultSimulator link({.dropEvery = 4,
                              .duplicateEvery = 3,
                              .delayTicks = 2,
                              .jitterTicks = 2,
                              .reorderWindow = 3,
                              .maximumQueuedPackets = 64});
  NetworkFaultSimulator snapshots({.dropEvery = 5,
                                   .duplicateEvery = 4,
                                   .delayTicks = 2,
                                   .jitterTicks = 1,
                                   .reorderWindow = 2,
                                   .maximumQueuedPackets = 64});

  double authorityX = 0.0;
  std::vector<std::uint64_t> acceptedLog;
  const auto deliverSnapshots = [&](const std::uint64_t tick) {
    for (const SimulatedNetworkPacket &packet : snapshots.drain(tick)) {
      const nlohmann::json payload = nlohmann::json::parse(
          std::string(packet.bytes.begin(), packet.bytes.end()));
      const auto reconciliation =
          client.reconcile({.sessionEpoch = 1,
                            .ownershipGeneration = 1,
                            .serverTick = payload["tick"],
                            .acknowledgedSequence = payload["ack"],
                            .state = {{"x", payload["x"]}}});
      for (const NetworkReplayCommand &command : reconciliation.replay)
        applyMove(client, command);
      (void)client.commitReplay();
    }
  };
  for (std::uint64_t tick = 1; tick <= 12; ++tick) {
    const double before = client.state().value("x", 0.0);
    const std::uint64_t sequence = client.recordLocalInput({{"x", 1.0}});
    applyMove(client, {.sequence = sequence, .payload = {{"x", 1.0}}});
    assert(client.state().value("x", 0.0) == before + 1.0);

    const std::string encoded =
        nlohmann::json({{"seq", sequence}, {"x", 1.0}}).dump();
    assert(link.submit(sequence,
                       {reinterpret_cast<const std::uint8_t *>(encoded.data()),
                        encoded.size()},
                       tick));
    for (const SimulatedNetworkPacket &packet : link.drain(tick)) {
      const nlohmann::json input = nlohmann::json::parse(
          std::string(packet.bytes.begin(), packet.bytes.end()));
      (void)server.submit(input["seq"], static_cast<double>(tick), input);
    }
    for (const NetworkEvaluatedInput &input :
         server.evaluate(static_cast<double>(tick), 32)) {
      if (!input.discarded) {
        authorityX += input.payload.value("x", 0.0);
        acceptedLog.push_back(input.sequence);
      }
    }
    const std::string snapshot =
        nlohmann::json({{"tick", tick},
                        {"ack", server.acknowledgedSequence()},
                        {"x", authorityX}})
            .dump();
    assert(snapshots.submit(
        tick,
        {reinterpret_cast<const std::uint8_t *>(snapshot.data()),
         snapshot.size()},
        tick));
    deliverSnapshots(tick);
  }

  for (std::uint64_t tick = 13; tick <= 20; ++tick) {
    for (const SimulatedNetworkPacket &packet : link.drain(tick)) {
      const nlohmann::json input = nlohmann::json::parse(
          std::string(packet.bytes.begin(), packet.bytes.end()));
      (void)server.submit(input["seq"], static_cast<double>(tick), input);
    }
    for (const NetworkEvaluatedInput &input :
         server.evaluate(static_cast<double>(tick), 32)) {
      if (!input.discarded) {
        authorityX += input.payload.value("x", 0.0);
        acceptedLog.push_back(input.sequence);
      }
    }
    deliverSnapshots(tick);
  }

  // A following input reveals a lost tail sequence to the bounded gap policy.
  (void)server.submit(13, 21.0, {{"x", 0.0}});
  for (const auto &input : server.evaluate(23.0, 32)) {
    if (!input.discarded) {
      authorityX += input.payload.value("x", 0.0);
      acceptedLog.push_back(input.sequence);
    }
  }
  const auto final =
      client.reconcile({.sessionEpoch = 1,
                        .ownershipGeneration = 1,
                        .serverTick = 23,
                        .acknowledgedSequence = server.acknowledgedSequence(),
                        .state = {{"x", authorityX}}});
  assert(final.applied && final.replay.empty());
  assert(client.state().value("x", -1.0) == authorityX);
  assert(authorityX == static_cast<double>(acceptedLog.size() - 1));
  assert(link.stats().dropped > 0 && link.stats().duplicated > 0);
  assert(snapshots.stats().dropped > 0 && snapshots.stats().duplicated > 0);
}

} // namespace

int main() {
  testDisabledDefaults();
  testSequencedInputQueue();
  testSnapshotInterpolation();
  testPredictionAndReconciliation();
  testIncompleteHistorySnaps();
  testBoundedHistoricalQueries();
  testPauseResumeAndLifecycleClears();
  testReferenceActionMovementUnderFaults();
  return 0;
}
