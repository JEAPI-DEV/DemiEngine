#include "demi/runtime/animation/AnimationRuntime.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace demi::runtime {

std::vector<AnimationWeightedSample>
sampleBlendSpace1D(const AnimationBlendSpace &space, const float x) {
  if (space.points.empty())
    return {};
  std::vector<AnimationBlendPoint> points = space.points;
  std::ranges::sort(points, {}, &AnimationBlendPoint::x);
  if (x <= points.front().x)
    return {{points.front().state, 1.0F}};
  if (x >= points.back().x)
    return {{points.back().state, 1.0F}};
  const auto upper = std::ranges::upper_bound(points, x, {},
                                               &AnimationBlendPoint::x);
  const auto lower = std::prev(upper);
  const float width = upper->x - lower->x;
  if (width <= 0.000001F)
    return {{upper->state, 1.0F}};
  const float amount = std::clamp((x - lower->x) / width, 0.0F, 1.0F);
  return {{lower->state, 1.0F - amount}, {upper->state, amount}};
}

std::vector<AnimationWeightedSample>
sampleBlendSpace2D(const AnimationBlendSpace &space, const float x,
                   const float y) {
  if (space.points.empty())
    return {};
  struct Candidate {
    std::string state;
    float inverseDistance = 0.0F;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(space.points.size());
  for (const AnimationBlendPoint &point : space.points) {
    const float dx = x - point.x;
    const float dy = y - point.y;
    const float distanceSquared = dx * dx + dy * dy;
    if (distanceSquared <= 0.000001F)
      return {{point.state, 1.0F}};
    candidates.push_back(
        {.state = point.state,
         .inverseDistance = 1.0F / std::sqrt(distanceSquared)});
  }
  std::ranges::sort(candidates, std::greater{},
                    &Candidate::inverseDistance);
  if (candidates.size() > 3)
    candidates.resize(3);
  float total = 0.0F;
  for (const Candidate &candidate : candidates)
    total += candidate.inverseDistance;
  std::vector<AnimationWeightedSample> result;
  for (const Candidate &candidate : candidates)
    result.push_back(
        {.state = candidate.state,
         .weight = total > 0.0F ? candidate.inverseDistance / total : 0.0F});
  return result;
}

AnimationPreview
animationPreview(const AnimationStateMachineComponent &machine) {
AnimationPreview result{.state = machine.state,
                          .time = machine.time,
                          .normalizedTime = machine.normalizedTime,
                          .transition = machine.activeTransition,
                          .samples = {},
                          .layers = machine.layers};
  for (const auto &[state, weight] : machine.blendSamples)
    result.samples.push_back({state, weight});
  return result;
}

namespace {

Vec3 subtract(const Vec3 &left, const Vec3 &right) {
  return {.x = left.x - right.x,
          .y = left.y - right.y,
          .z = left.z - right.z};
}

Vec3 add(const Vec3 &left, const Vec3 &right) {
  return {.x = left.x + right.x,
          .y = left.y + right.y,
          .z = left.z + right.z};
}

Vec3 scale(const Vec3 &value, const float amount) {
  return {.x = value.x * amount,
          .y = value.y * amount,
          .z = value.z * amount};
}

Vec3 sampleRootMotion(const std::vector<Vec3> &track,
                      const float normalizedTime) {
  if (track.empty())
    return {};
  if (track.size() == 1)
    return track.front();
  const float position =
      std::clamp(normalizedTime, 0.0F, 1.0F) *
      static_cast<float>(track.size() - 1);
  const auto lower = static_cast<std::size_t>(std::floor(position));
  const auto upper = std::min(lower + 1, track.size() - 1);
  const float amount = position - static_cast<float>(lower);
  return add(track[lower], scale(subtract(track[upper], track[lower]), amount));
}

} // namespace

Vec3 extractRootMotion(const std::vector<Vec3> &track,
                       const float previousTime, const float currentTime,
                       const float duration, const bool loop) {
  if (track.size() < 2 || duration <= 0.0F ||
      currentTime <= previousTime)
    return {};
  if (!loop) {
    return subtract(
        sampleRootMotion(track, std::clamp(currentTime / duration, 0.0F, 1.0F)),
        sampleRootMotion(track,
                         std::clamp(previousTime / duration, 0.0F, 1.0F)));
  }
  const int previousLoop =
      static_cast<int>(std::floor(std::max(previousTime, 0.0F) / duration));
  const int currentLoop =
      static_cast<int>(std::floor(std::max(currentTime, 0.0F) / duration));
  const float previousLocal =
      std::fmod(std::max(previousTime, 0.0F), duration) / duration;
  const float currentLocal =
      std::fmod(std::max(currentTime, 0.0F), duration) / duration;
  const Vec3 cycle = subtract(track.back(), track.front());
  return add(subtract(sampleRootMotion(track, currentLocal),
                      sampleRootMotion(track, previousLocal)),
             scale(cycle, static_cast<float>(currentLoop - previousLoop)));
}

std::vector<std::string>
validateAnimationMachine(const AnimationStateMachineComponent &machine) {
  std::vector<std::string> errors;
  if (machine.states.empty())
    errors.emplace_back("states must not be empty");
  if (!machine.state.empty() && !machine.states.contains(machine.state))
    errors.emplace_back("initial_state references a missing state: " +
                        machine.state);
  for (const AnimationTransition &transition : machine.transitions) {
    if (!transition.from.empty() && transition.from != "*" &&
        !machine.states.contains(transition.from))
      errors.emplace_back("transition references missing from state: " +
                          transition.from);
    if (!machine.states.contains(transition.to))
      errors.emplace_back("transition references missing to state: " +
                          transition.to);
  }
  for (const auto &[name, state] : machine.states) {
    if (!state.rootMotionTrack.empty() && state.rootMotionTrack.size() < 2)
      errors.emplace_back("state " + name +
                          " root_motion_track requires at least two samples");
    if (!state.rootMotionTrack.empty() && state.duration <= 0.0F)
      errors.emplace_back("state " + name +
                          " root_motion_track requires a positive duration");
  }
  for (const auto &[name, space] : machine.blendSpaces) {
    if (space.parameterX.empty())
      errors.emplace_back("blend space " + name + " has no parameter_x");
    if (!space.parameterY.empty() && space.points.size() < 3)
      errors.emplace_back("2D blend space " + name +
                          " requires at least three points");
    for (const AnimationBlendPoint &point : space.points)
      if (!machine.states.contains(point.state))
        errors.emplace_back("blend space " + name +
                            " references missing state: " + point.state);
  }
  std::unordered_set<std::string> layerNames;
  for (const AnimationLayer &layer : machine.layers) {
    if (!layerNames.insert(layer.name).second)
      errors.emplace_back("duplicate animation layer: " + layer.name);
    if (!machine.states.contains(layer.state))
      errors.emplace_back("layer " + layer.name +
                          " references missing state: " + layer.state);
  }
  return errors;
}

} // namespace demi::runtime
