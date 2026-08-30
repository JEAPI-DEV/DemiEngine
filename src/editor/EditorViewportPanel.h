#pragma once

#include "editor/EditorUiHost.h"

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

#include <string>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

struct EditorHudViewportState {
  enum class Drag { None, Move, Resize };
  Drag drag = Drag::None;
  std::string nodeId;
  runtime::Vec2 startMouse{};
  runtime::Vec2 startPosition{};
  runtime::Vec2 startSize{};
};

void drawEditorViewport(EditorWorkspace &workspace, ImVec2 position,
                        ImVec2 size, EditorViewportArea &viewportArea,
                        EditorHudViewportState &hudState, std::string &notice);

} // namespace demi::editor
