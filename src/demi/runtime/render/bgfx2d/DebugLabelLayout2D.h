#pragma once

#include "demi/runtime/render/backend/Canvas2D.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::render {

struct DebugLabelCandidate2D {
  std::string stableId;
  std::string text;
  Vec2 anchor;
  float width = 0.0F;
  float height = 0.0F;
};

struct DebugLabelPlacement2D {
  std::string stableId;
  std::string text;
  Vec2 anchor;
  Rect2D bounds;
};

[[nodiscard]] std::string
compactDebugLabel2D(std::string_view text, std::size_t maximumCharacters = 34);
[[nodiscard]] std::vector<DebugLabelPlacement2D>
layoutDebugLabels2D(std::vector<DebugLabelCandidate2D> candidates,
                    Vec2 viewportSize);

} // namespace demi::runtime::render
