#pragma once

#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include <string>
#include <vector>

namespace demi::runtime {

struct AnimationWeightedSample {
  std::string state;
  float weight = 0.0F;
};

struct AnimationPreview {
  std::string state;
  float time = 0.0F;
  float normalizedTime = 0.0F;
  AnimationTransitionState transition;
  std::vector<AnimationWeightedSample> samples;
  std::vector<AnimationLayer> layers;
};

[[nodiscard]] std::vector<AnimationWeightedSample>
sampleBlendSpace1D(const AnimationBlendSpace &space, float x);
[[nodiscard]] std::vector<AnimationWeightedSample>
sampleBlendSpace2D(const AnimationBlendSpace &space, float x, float y);
[[nodiscard]] AnimationPreview
animationPreview(const AnimationStateMachineComponent &machine);
[[nodiscard]] Vec3 extractRootMotion(
    const std::vector<Vec3> &track, float previousTime, float currentTime,
    float duration, bool loop);
[[nodiscard]] std::vector<std::string>
validateAnimationMachine(const AnimationStateMachineComponent &machine);

} // namespace demi::runtime
