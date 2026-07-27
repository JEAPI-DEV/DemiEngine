#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace demi::runtime::input {

enum class GestureType {
  Tap,
  DoubleTap,
  LongPress,
  Drag,
  Pinch,
  Rotate
};

struct GestureEvent {
  GestureType type = GestureType::Tap;
  std::int64_t pointerId = 0;
  Vec2 position;
  Vec2 delta;
  float value = 0.0F;
};

struct GestureSettings {
  float tapSeconds = 0.25F;
  float doubleTapSeconds = 0.35F;
  float longPressSeconds = 0.55F;
  float movementThreshold = 12.0F;
};

class TouchGestureRecognizer {
public:
  explicit TouchGestureRecognizer(GestureSettings settings = {});
  [[nodiscard]] std::vector<GestureEvent>
  update(const std::vector<TouchPoint> &touches, float deltaSeconds);
  void reset();

private:
  struct Tracking {
    Vec2 start;
    Vec2 previous;
    float age = 0.0F;
    bool dragging = false;
    bool longPressSent = false;
  };

  GestureSettings settings_;
  std::unordered_map<std::int64_t, Tracking> tracking_;
  float sinceLastTap_ = 1000.0F;
  Vec2 lastTapPosition_;
  float previousPinchDistance_ = 0.0F;
  float previousRotation_ = 0.0F;
};

} // namespace demi::runtime::input
