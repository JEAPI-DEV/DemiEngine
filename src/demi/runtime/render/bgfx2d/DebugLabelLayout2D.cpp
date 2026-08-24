#include "demi/runtime/render/bgfx2d/DebugLabelLayout2D.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace demi::runtime::render {
namespace {

constexpr float ViewportPadding = 5.0F;
constexpr float AnchorOffset = 13.0F;
constexpr float LaneGap = 4.0F;

bool overlaps(const Rect2D &left, const Rect2D &right) {
  return left.x < right.x + right.width + LaneGap &&
         left.x + left.width + LaneGap > right.x &&
         left.y < right.y + right.height + LaneGap &&
         left.y + left.height + LaneGap > right.y;
}

Rect2D clampToViewport(Rect2D rect, const Vec2 viewport) {
  rect.x = std::clamp(
      rect.x, ViewportPadding,
      std::max(ViewportPadding, viewport.x - rect.width - ViewportPadding));
  rect.y = std::clamp(
      rect.y, ViewportPadding,
      std::max(ViewportPadding, viewport.y - rect.height - ViewportPadding));
  return rect;
}

} // namespace

std::string compactDebugLabel2D(const std::string_view text,
                                const std::size_t maximumCharacters) {
  if (text.size() <= maximumCharacters || maximumCharacters < 4)
    return std::string(text.substr(0, maximumCharacters));
  return std::string(text.substr(0, maximumCharacters - 3)) + "...";
}

std::vector<DebugLabelPlacement2D>
layoutDebugLabels2D(std::vector<DebugLabelCandidate2D> candidates,
                    const Vec2 viewportSize) {
  std::ranges::sort(candidates, [](const auto &left, const auto &right) {
    if (left.anchor.y != right.anchor.y)
      return left.anchor.y < right.anchor.y;
    if (left.anchor.x != right.anchor.x)
      return left.anchor.x < right.anchor.x;
    return left.stableId < right.stableId;
  });
  std::vector<DebugLabelPlacement2D> result;
  result.reserve(candidates.size());
  for (DebugLabelCandidate2D &candidate : candidates) {
    const float laneHeight = candidate.height + LaneGap;
    Rect2D selected;
    bool found = false;
    for (int lane = 0; lane < 24 && !found; ++lane) {
      const int direction = lane % 2 == 0 ? -1 : 1;
      const int distance = lane / 2 + 1;
      Rect2D proposal{
          .x = candidate.anchor.x + AnchorOffset,
          .y = candidate.anchor.y + direction * distance * laneHeight,
          .width = candidate.width,
          .height = candidate.height,
      };
      proposal = clampToViewport(proposal, viewportSize);
      found = std::ranges::none_of(result, [&](const auto &placed) {
        return overlaps(proposal, placed.bounds);
      });
      if (found)
        selected = proposal;
    }
    if (!found) {
      float bestDistance = std::numeric_limits<float>::max();
      const float columnStep = candidate.width + LaneGap;
      for (float y = ViewportPadding;
           y + candidate.height + ViewportPadding <= viewportSize.y;
           y += laneHeight) {
        for (float x = ViewportPadding;
             x + candidate.width + ViewportPadding <= viewportSize.x;
             x += columnStep) {
          const Rect2D proposal{.x = x,
                                .y = y,
                                .width = candidate.width,
                                .height = candidate.height};
          if (std::ranges::any_of(result, [&](const auto &placed) {
                return overlaps(proposal, placed.bounds);
              }))
            continue;
          const float centerX = x + candidate.width * 0.5F;
          const float centerY = y + candidate.height * 0.5F;
          const float distance = std::hypot(centerX - candidate.anchor.x,
                                            centerY - candidate.anchor.y);
          if (distance < bestDistance) {
            bestDistance = distance;
            selected = proposal;
            found = true;
          }
        }
      }
    }
    if (!found) {
      selected = clampToViewport({.x = candidate.anchor.x + AnchorOffset,
                                  .y = candidate.anchor.y - laneHeight,
                                  .width = candidate.width,
                                  .height = candidate.height},
                                 viewportSize);
    }
    result.push_back({.stableId = std::move(candidate.stableId),
                      .text = std::move(candidate.text),
                      .anchor = candidate.anchor,
                      .bounds = selected});
  }
  return result;
}

} // namespace demi::runtime::render
