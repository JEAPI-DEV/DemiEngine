#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>

namespace demi::runtime::ui {
struct UiImageSkin {
  Vec2 start{};
  Vec2 end{};
  float scale = 1.0F;
  [[nodiscard]] bool sliced() const {
    return start.x > 0 || start.y > 0 || end.x > 0 || end.y > 0;
  }
};

// Shared by parsing and expanded-document validation, including prefab
// overrides.
[[nodiscard]] std::optional<UiImageSkin>
parseUiImageSkin(const nlohmann::json &node);
} // namespace demi::runtime::ui
