#include "editor/EditorShell.h"
#include "editor/EditorShortcutInput.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace demi::editor {
namespace {
bool copyToClipboard(std::string_view text, std::string &error) {
  const std::string owned(text);
  ImGui::SetClipboardText(owned.c_str());
  const char *stored = ImGui::GetClipboardText();
  if (!stored || text != stored) {
    error = "Could not write the clipboard. The selection was not removed.";
    return false;
  }
  return true;
}
bool graphCommand(EditorCommand command) {
  switch (command) {
  case EditorCommand::Copy:
  case EditorCommand::Cut:
  case EditorCommand::Paste:
  case EditorCommand::Duplicate:
  case EditorCommand::Delete:
  case EditorCommand::SelectAll:
  case EditorCommand::Undo:
  case EditorCommand::Redo:
  case EditorCommand::FrameSelection:
    return true;
  default:
    return false;
  }
}
} // namespace

EditorCommandContext EditorShell::commandContext() const {
  if (showGameView_ && gameViewFocused())
    return EditorCommandContext::Game;
  const auto *window = ImGui::GetCurrentContext()->NavWindow;
  if (!window)
    return EditorCommandContext::None;
  const std::string_view name = window->Name;
  if (name.starts_with("Game View"))
    return gameViewFocused() ? EditorCommandContext::Game
                             : EditorCommandContext::None;
  if (name.starts_with("Terrain Graph") || name.starts_with("Terrain Nodes"))
    return EditorCommandContext::TerrainGraph;
  if (showGameView_ && playSession_.runtimeWorld() &&
      (name.starts_with("Hierarchy") || name.starts_with("Inspector")))
    return EditorCommandContext::None;
  const bool documentPanel =
      name.starts_with("Viewport") || name.starts_with("Prefab") ||
      name.starts_with("HUD") || name.starts_with("Terrain Asset") ||
      name.starts_with("Hierarchy") || name.starts_with("Inspector") ||
      name.starts_with("UI Palette") || name.starts_with("Terrain Nodes");
  if (!documentPanel)
    return EditorCommandContext::None;
  if (workspace().activeDocument() == EditorWorkspaceDocument::TerrainAsset)
    return EditorCommandContext::None;
  if (workspace().activeDocument() == EditorWorkspaceDocument::Hud)
    return EditorCommandContext::Hud;
  return EditorCommandContext::Scene;
}

void EditorShell::executeCommand(EditorCommand command,
                                 EditorCommandContext context) {
  if (!editorCommandAvailable(command, context))
    return;
  std::string error;
  bool success = false;
  if (context == EditorCommandContext::TerrainGraph && graphCommand(command)) {
    success = graphWorkspace_ && terrainGraphPanel_.executeCommand(
                                     *graphWorkspace_, command, error);
  } else {
    switch (command) {
    case EditorCommand::Copy:
    case EditorCommand::Cut: {
      const auto content = workspace().exportSelection(error);
      success = content && copyToClipboard(*content, error);
      if (success && command == EditorCommand::Cut)
        success = workspace().deleteSelection(error);
      break;
    }
    case EditorCommand::Paste: {
      const char *text = ImGui::GetClipboardText();
      success = workspace().pasteSelection(text ? text : "", error);
      break;
    }
    case EditorCommand::Duplicate:
      success = workspace().duplicateSelection(error);
      break;
    case EditorCommand::Delete:
      success = workspace().deleteSelection(error);
      break;
    case EditorCommand::SelectAll:
      success = workspace().selectAllAuthored(error);
      break;
    case EditorCommand::Undo:
      success = workspace().undo(error);
      break;
    case EditorCommand::Redo:
      success = workspace().redo(error);
      break;
    case EditorCommand::SaveAll:
      success = documents_.saveAll(error) &&
                specializedPanel_.saveActive(workspace(), error);
      break;
    case EditorCommand::Refresh:
      success = workspace().refresh(error);
      break;
    case EditorCommand::FrameSelection:
      if (workspace().viewDimension() ==
          EditorSceneViewDimension::TwoDimensional) {
        if (workspace().selectedIsoGridCell())
          success = workspace().sceneView2D().frameGridCell(
              workspace().project().world, *workspace().selectedIsoGridCell());
        else
          success = workspace().sceneView2D().frameEntity(
              workspace().project().world, workspace().selectedEntityId());
      } else {
        success = workspace().sceneView().frameEntity(
            workspace().project().world, workspace().selectedEntityId());
      }
      if (!success)
        error = "Select an entity to frame.";
      break;
    case EditorCommand::ReleaseGameInput:
      gameInputDetached_ = true;
      success = true;
      break;
    case EditorCommand::Rename:
      if (workspace().selectedEntityId().empty()) {
        error = "Select an entity to rename.";
      } else {
        hierarchyPanel_.requestRename(
            std::string(workspace().selectedEntityId()));
        dockingWorkspace_.visibility().hierarchy = true;
        success = true;
      }
      break;
    case EditorCommand::NewEntity: {
      const auto parent = workspace().selectedEntityId();
      success = workspace().createEntity(
          error,
          parent.empty() ? std::nullopt : std::optional<std::string>{parent});
      break;
    }
    }
  }
  notice_ =
      success ? std::string(editorCommandDefinition(command).label) : error;
  if (success && command == EditorCommand::ReleaseGameInput)
    notice_ = "Game input released. Click Game View to resume.";
}

void EditorShell::dispatchShortcuts() {
  const auto context = commandContext();
  if (context != EditorCommandContext::None &&
      context != EditorCommandContext::Game) {
    lastAuthoringContext_ = context;
    if (!playSession_.runtimeWorld())
      showGameView_ = false;
  }
  const auto &input = ImGui::GetIO();
  if (context == EditorCommandContext::Game) {
    if (editorShortcutPressed(preferences_.keyBindings,
                              EditorCommand::ReleaseGameInput, context))
      executeCommand(EditorCommand::ReleaseGameInput, context);
    return;
  }
  if (input.WantTextInput || ImGui::IsAnyItemActive() ||
      shortcutSettings_.recording ||
      ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
      ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
      ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
      ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    return;
  const auto *window = ImGui::GetCurrentContext()->NavWindow;
  if (window && std::string_view(window->Name).starts_with("Editor Settings"))
    return;
  for (const auto &definition : editorCommandDefinitions()) {
    if (editorShortcutPressed(preferences_.keyBindings, definition.command,
                              context)) {
      executeCommand(definition.command, context);
      break;
    }
  }
}
} // namespace demi::editor
