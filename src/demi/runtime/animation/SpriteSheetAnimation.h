#pragma once

#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

[[nodiscard]] inline std::unordered_map<std::string, SpriteAnimationClip2D>
makeSpriteSheetRows(const int columns, const int rows,
                    const std::vector<std::string> &rowNames,
                    const float framesPerSecond = 10.0F,
                    const bool loop = true) {
  std::unordered_map<std::string, SpriteAnimationClip2D> result;
  const int validColumns = std::max(columns, 1);
  const int validRows = std::max(rows, 0);
  for (int row = 0;
       row < validRows && row < static_cast<int>(rowNames.size()); ++row) {
    if (rowNames[static_cast<std::size_t>(row)].empty())
      continue;
    result[rowNames[static_cast<std::size_t>(row)]] = {
        .startFrame = row * validColumns,
        .frameCount = validColumns,
        .framesPerSecond = std::max(framesPerSecond, 0.01F),
        .loop = loop,
        .events = {}};
  }
  return result;
}

} // namespace demi::runtime
