#include "demi/runtime/input/TouchGestureRecognizer.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::input {
namespace {

float distance(const Vec2 a, const Vec2 b) {
  const float x = a.x - b.x;
  const float y = a.y - b.y;
  return std::sqrt(x * x + y * y);
}

float angle(const Vec2 a, const Vec2 b) {
  return std::atan2(b.y - a.y, b.x - a.x);
}

} // namespace

TouchGestureRecognizer::TouchGestureRecognizer(GestureSettings settings)
    : settings_(settings) {}

std::vector<GestureEvent>
TouchGestureRecognizer::update(const std::vector<TouchPoint> &touches,
                               const float deltaSeconds) {
  std::vector<GestureEvent> events;
  sinceLastTap_ += std::max(deltaSeconds, 0.0F);
  for (auto &[id, tracking] : tracking_) {
    (void)id;
    tracking.age += std::max(deltaSeconds, 0.0F);
  }

  for (const TouchPoint &touch : touches) {
    if (touch.phase == TouchPhase::Began) {
      tracking_[touch.id] = {.start = touch.position,
                             .previous = touch.position};
      continue;
    }
    const auto found = tracking_.find(touch.id);
    if (found == tracking_.end())
      continue;
    Tracking &tracking = found->second;
    const float movement = distance(tracking.start, touch.position);
    if ((touch.phase == TouchPhase::Moved ||
         touch.phase == TouchPhase::Stationary) &&
        movement >= settings_.movementThreshold) {
      tracking.dragging = true;
      events.push_back({.type = GestureType::Drag,
                        .pointerId = touch.id,
                        .position = touch.position,
                        .delta = touch.delta});
    }
    if (!tracking.dragging && !tracking.longPressSent &&
        tracking.age >= settings_.longPressSeconds) {
      tracking.longPressSent = true;
      events.push_back({.type = GestureType::LongPress,
                        .pointerId = touch.id,
                        .position = touch.position,
                        .delta = {}});
    }
    if (touch.phase == TouchPhase::Ended) {
      if (!tracking.dragging && !tracking.longPressSent &&
          tracking.age <= settings_.tapSeconds &&
          movement < settings_.movementThreshold) {
        const bool doubleTap =
            sinceLastTap_ <= settings_.doubleTapSeconds &&
            distance(lastTapPosition_, touch.position) <
                settings_.movementThreshold;
        events.push_back({.type = doubleTap ? GestureType::DoubleTap
                                            : GestureType::Tap,
                          .pointerId = touch.id,
                          .position = touch.position,
                          .delta = {}});
        sinceLastTap_ = 0.0F;
        lastTapPosition_ = touch.position;
      }
      tracking_.erase(found);
      continue;
    }
    if (touch.phase == TouchPhase::Cancelled) {
      tracking_.erase(found);
      continue;
    }
    tracking.previous = touch.position;
  }

  std::vector<const TouchPoint *> active;
  for (const TouchPoint &touch : touches)
    if (touch.phase != TouchPhase::Ended &&
        touch.phase != TouchPhase::Cancelled)
      active.push_back(&touch);
  if (active.size() >= 2) {
    const float currentDistance =
        distance(active[0]->position, active[1]->position);
    const float currentRotation =
        angle(active[0]->position, active[1]->position);
    if (previousPinchDistance_ > 0.0F &&
        std::abs(currentDistance - previousPinchDistance_) > 0.001F)
      events.push_back({.type = GestureType::Pinch,
                        .pointerId = active[0]->id,
                        .position = active[0]->position,
                        .delta = {},
                        .value = currentDistance / previousPinchDistance_});
    if (previousPinchDistance_ > 0.0F &&
        std::abs(currentRotation - previousRotation_) > 0.0001F)
      events.push_back({.type = GestureType::Rotate,
                        .pointerId = active[0]->id,
                        .position = active[0]->position,
                        .delta = {},
                        .value = currentRotation - previousRotation_});
    previousPinchDistance_ = currentDistance;
    previousRotation_ = currentRotation;
  } else {
    previousPinchDistance_ = 0.0F;
    previousRotation_ = 0.0F;
  }
  return events;
}

void TouchGestureRecognizer::reset() {
  tracking_.clear();
  sinceLastTap_ = 1000.0F;
  previousPinchDistance_ = 0.0F;
  previousRotation_ = 0.0F;
}

} // namespace demi::runtime::input
