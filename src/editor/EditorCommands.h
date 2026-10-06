#pragma once

#include <span>
#include <string_view>

namespace demi::editor {

enum class EditorCommand {
  Copy,
  Cut,
  Paste,
  Duplicate,
  Delete,
  SelectAll,
  Undo,
  Redo,
  SaveAll,
  Refresh,
  FrameSelection,
  ReleaseGameInput,
  Rename,
  NewEntity
};

enum class EditorCommandContext : unsigned {
  None = 0,
  Scene = 1,
  Hud = 2,
  TerrainGraph = 4,
  Game = 8
};

struct EditorCommandDefinition {
  EditorCommand command;
  std::string_view id;
  std::string_view label;
  std::string_view description;
  unsigned contexts;
  bool global;
  std::span<const std::string_view> defaults;
  bool required = false;
};

std::span<const EditorCommandDefinition> editorCommandDefinitions();
const EditorCommandDefinition &editorCommandDefinition(EditorCommand command);
bool editorCommandAvailable(EditorCommand command,
                            EditorCommandContext context);

} // namespace demi::editor
