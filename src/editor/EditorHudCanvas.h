#pragma once

#include "demi/runtime/ui/UiModel.h"

namespace demi::editor {

// Screen-independent geometry shared by HUD viewport picking and tests.
[[nodiscard]] runtime::ui::Rect
editorHudEditableRect(const runtime::ui::UiNode &node);
[[nodiscard]] bool editorHudRectContains(runtime::ui::Rect rect,
                                         runtime::Vec2 point);
[[nodiscard]] const runtime::ui::UiNode *
pickEditorHudNode(const runtime::ui::UiDocument &document,
                  runtime::Vec2 authoredPoint);

} // namespace demi::editor
