#pragma once
#include "editor/EditorStructuredValue.h"
#include <nlohmann/json_fwd.hpp>

namespace demi::editor {
// Edits a candidate. The Inspector owns validation, commands and persistence.
StructuredValueEdit drawEditorMaterialProperties(nlohmann::json &properties);
} // namespace demi::editor
