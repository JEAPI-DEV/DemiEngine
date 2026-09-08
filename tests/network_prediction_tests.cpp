#include "demi/runtime/network/NetworkPrediction.h"

#include <cassert>
#include <cmath>

int main() {
  using namespace demi::runtime;

  NetworkOwnerInputQueue disabledQueue({});
  assert(disabledQueue.submit(1, 0.0, {{"move", 1.0}}) ==
         NetworkInputRejectCode::Capacity);
  assert(disabledQueue.pending() == 0);

  NetworkSnapshotInterpolator disabledInterpolator({});
  NetworkAuthoritySnapshot initialSnapshot{
      .serverTick = 1,
      .state = {{"x", 0.0}},
      .receivedAtSeconds = 0.0,
  };
  assert(!disabledInterpolator.push(initialSnapshot));
  assert(!disabledInterpolator.sample(0.0).has_value());

  NetworkPredictedController disabledController({});
  disabledController.enable(1, 1, {{"x", 0.0}});
  assert(disabledController.recordLocalInput({{"x", 1.0}}) == 0);
  assert(disabledController.pendingReplayCount() == 0);

  NetworkOwnerInputQueue queue(
      {.capacity = 4, .futureWindow = 4, .headOfLineTimeoutSeconds = 0.1});
  assert(queue.submit(2, 0.0, {{"move", 1.0}}) == NetworkInputRejectCode::None);
  assert(queue.evaluate(0.05, 4).empty());
  const auto evaluated = queue.evaluate(0.2, 4);
  assert(evaluated.size() == 2);
  assert(evaluated[0].sequence == 1 && evaluated[0].discarded);
  assert(evaluated[1].sequence == 2 && !evaluated[1].discarded);
  assert(queue.acknowledgedSequence() == 2);
  assert(queue.submit(2, 0.3, {{"move", 1.0}}) == NetworkInputRejectCode::Old);
  assert(queue.lastRejection().has_value());
  assert(queue.lastRejection()->second == "old");

  NetworkSnapshotInterpolator interpolator({.interpolationDelaySeconds = 0.1,
                                            .extrapolationLimitSeconds = 0.05,
                                            .capacity = 3});
  NetworkAuthoritySnapshot secondSnapshot{
      .serverTick = 2,
      .state = {{"x", 10.0}},
      .receivedAtSeconds = 0.1,
  };
  assert(interpolator.push(initialSnapshot));
  assert(interpolator.push(secondSnapshot));
  const auto interpolated = interpolator.sample(0.15);
  assert(interpolated.has_value());
  assert(std::abs(interpolated->state["x"].get<double>() - 5.0) < 1e-6);

  NetworkPredictedController controller({.inputHistoryLimit = 8,
                                         .visualOffsetDecayPerSecond = 10.0,
                                         .correctionEpsilon = 0.001});
  controller.enable(1, 1, {{"x", 0.0}});
  assert(controller.recordLocalInput({{"move", 1.0}}) == 1);
  controller.setPredictedState({{"x", 1.0}});
  const NetworkReconciliation replay =
      controller.reconcile({.sessionEpoch = 1,
                            .ownershipGeneration = 1,
                            .serverTick = 1,
                            .acknowledgedSequence = 0,
                            .state = {{"x", 0.0}}});
  assert(replay.applied && replay.replay.size() == 1);
  controller.setPredictedState({{"x", 1.0}});
  assert(!controller.commitReplay());

  const NetworkReconciliation correction =
      controller.reconcile({.sessionEpoch = 1,
                            .ownershipGeneration = 1,
                            .serverTick = 2,
                            .acknowledgedSequence = 1,
                            .state = {{"x", 0.5}}});
  assert(correction.applied && correction.replay.empty());
  assert(controller.counters().corrections == 1);
  assert(!controller.visualOffset().empty());
  return 0;
}
