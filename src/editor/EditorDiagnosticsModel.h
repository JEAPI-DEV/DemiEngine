#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <string_view>
#include <vector>

namespace demi::editor {

enum class EditorDiagnosticOrigin { Project, Build };

struct EditorDiagnosticRecord {
  Diagnostic diagnostic;
  EditorDiagnosticOrigin origin = EditorDiagnosticOrigin::Project;
  std::string entityId;
  std::string component;
  std::string field;
};

[[nodiscard]] std::vector<EditorDiagnosticRecord>
collectEditorDiagnostics(const Diagnostics &project,
                         const Diagnostics &build = {});
[[nodiscard]] std::vector<EditorDiagnosticRecord>
filterEditorDiagnostics(const std::vector<EditorDiagnosticRecord> &records,
                        std::string_view query, bool showInfo,
                        bool showWarnings, bool showErrors);

} // namespace demi::editor
