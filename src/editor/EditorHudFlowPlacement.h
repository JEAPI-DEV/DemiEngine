#pragma once
#include "demi/runtime/ui/UiModel.h"
#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

namespace demi::editor {
struct EditorHudFlowPlacement {
  bool flow = false;
  std::size_t index = 0;
};
// position is relative to the parent's padded content origin, matching the
// workspace drop conversion. Returned indices address authored children,
// including hidden controls and prefab instances.
EditorHudFlowPlacement editorHudFlowPlacement(
    const runtime::ui::UiDocument &preview, std::string_view parentId,
    const nlohmann::json &children, std::optional<runtime::Vec2> position);
} // namespace demi::editor
