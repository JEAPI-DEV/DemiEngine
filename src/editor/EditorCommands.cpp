#include "editor/EditorCommands.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace demi::editor {
namespace {
constexpr unsigned Authoring = 1 | 2 | 4;
constexpr std::array<std::string_view, 1> Copy{"Ctrl+C"}, Cut{"Ctrl+X"},
    Paste{"Ctrl+V"}, Duplicate{"Ctrl+D"}, SelectAll{"Ctrl+A"}, Undo{"Ctrl+Z"},
    SaveAll{"Ctrl+S"}, Refresh{"F5"}, Frame{"F"}, Release{"Ctrl+D"},
    Rename{"F2"}, NewEntity{"Ctrl+Shift+N"};
constexpr std::array<std::string_view, 2> Delete{"Delete", "Backspace"},
    Redo{"Ctrl+Shift+Z", "Ctrl+Y"};
const std::array Definitions{
    EditorCommandDefinition{EditorCommand::Copy, "copy", "Copy",
                            "Copy authored selection", Authoring, false, Copy},
    EditorCommandDefinition{EditorCommand::Cut, "cut", "Cut",
                            "Copy and remove authored selection", Authoring,
                            false, Cut},
    EditorCommandDefinition{EditorCommand::Paste, "paste", "Paste",
                            "Paste with fresh stable IDs", Authoring, false,
                            Paste},
    EditorCommandDefinition{EditorCommand::Duplicate, "duplicate", "Duplicate",
                            "Duplicate selection as one undoable edit",
                            Authoring, false, Duplicate},
    EditorCommandDefinition{EditorCommand::Delete, "delete", "Delete",
                            "Remove authored selection", Authoring, false,
                            Delete},
    EditorCommandDefinition{EditorCommand::SelectAll, "select_all",
                            "Select all",
                            "Select authored content in this document",
                            Authoring, false, SelectAll},
    EditorCommandDefinition{EditorCommand::Undo, "undo", "Undo",
                            "Undo in the focused authoring document", Authoring,
                            false, Undo},
    EditorCommandDefinition{EditorCommand::Redo, "redo", "Redo",
                            "Redo in the focused authoring document", Authoring,
                            false, Redo},
    EditorCommandDefinition{EditorCommand::SaveAll, "save_all", "Save all",
                            "Save authored documents", Authoring | 8, true,
                            SaveAll},
    EditorCommandDefinition{EditorCommand::Refresh, "refresh",
                            "Refresh project", "Reload project sources",
                            Authoring, true, Refresh},
    EditorCommandDefinition{
        EditorCommand::FrameSelection, "frame_selection", "Frame selection",
        "Frame selected scene content or graph nodes", 1 | 4, false, Frame},
    EditorCommandDefinition{
        EditorCommand::ReleaseGameInput, "release_game_input",
        "Release Game View input",
        "Detach captured game input; this must remain bound", 8, false, Release,
        true},
    EditorCommandDefinition{EditorCommand::Rename, "rename", "Rename entity",
                            "Rename the selected scene entity", 1, false,
                            Rename},
    EditorCommandDefinition{
        EditorCommand::NewEntity, "new_entity", "Create child entity",
        "Create an empty entity under the selection", 1, false, NewEntity}};
} // namespace

std::span<const EditorCommandDefinition> editorCommandDefinitions() {
  return Definitions;
}

const EditorCommandDefinition &editorCommandDefinition(EditorCommand command) {
  const auto found = std::ranges::find(Definitions, command,
                                       &EditorCommandDefinition::command);
  if (found == Definitions.end())
    throw std::invalid_argument("Unknown editor command");
  return *found;
}

bool editorCommandAvailable(EditorCommand command,
                            EditorCommandContext context) {
  const auto &definition = editorCommandDefinition(command);
  return context == EditorCommandContext::None
             ? definition.global
             : (definition.contexts & static_cast<unsigned>(context)) != 0;
}
} // namespace demi::editor
