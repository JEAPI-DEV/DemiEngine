#pragma once

#include "editor/EditorJsonDocument.h"

#include <array>
#include <string>

namespace demi::editor {

void drawEditorJsonSource(EditorJsonDocument &document,
                          std::string &selectedPointer,
                          std::array<char, 1024> &editBuffer,
                          std::string &editBufferPointer, std::string &notice);
void drawEditorDocumentDiagnostics(const Diagnostics &diagnostics);

} // namespace demi::editor
