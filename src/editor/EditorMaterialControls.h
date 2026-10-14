#pragma once

#include <string>

namespace demi::assets {
enum class MaterialMapSlot;
}

namespace demi::editor {
class EditorWorkspace;
class EditorJsonDocument;

// Assigning or clearing a map is one validated document command. Clearing the
// detail map also resets its dependent strength, preserving all other fields.
bool setEditorMaterialMap(EditorJsonDocument &document,
                          assets::MaterialMapSlot slot,
                          const std::string &reference, std::string &error);

// Integrated ordinary Material surface controls. Mutations use the document's
// validation, Undo/Redo and source-preserving save path.
void drawEditorMaterialSurfaceControls(EditorWorkspace &workspace,
                                       EditorJsonDocument &document,
                                       std::string &notice);
} // namespace demi::editor
