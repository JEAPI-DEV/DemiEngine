#pragma once

#include <string>
#include <string_view>

namespace demi::editor {

class EditorWorkspace;
class EditorJsonDocument;

// Returns true when this content type has native controls. Failed edits surface
// through notice; the document retains its previous valid state and history.
bool drawEditorDataAssetControls(EditorWorkspace &workspace,
                                 EditorJsonDocument &document,
                                 std::string_view contentType,
                                 std::string &notice);

} // namespace demi::editor
