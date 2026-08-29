#pragma once

#include "editor/EditorDebugPanel.h"
#include "editor/EditorDiagnosticsModel.h"
#include "editor/EditorProfilerModel.h"
#include "editor/EditorProjectOperations.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ImVec2;

namespace demi::editor {

class EditorPlaySession;
class EditorWorkspace;

class EditorConsolePanel {
public:
  void draw(EditorWorkspace &workspace, EditorPlaySession &playSession,
            ImVec2 position, ImVec2 size,
            const EditorProjectOperationSnapshot &operation,
            std::string &notice);
  [[nodiscard]] std::optional<std::filesystem::path> takeOpenRequest();

private:
  std::array<char, 128> diagnosticFilter_{};
  std::array<char, 128> profilerFilter_{};
  std::array<char, 512> luaCommand_{};
  std::vector<std::string> luaHistory_;
  int luaHistoryCursor_ = -1;
  bool showInfo_ = true;
  bool showWarnings_ = true;
  bool showErrors_ = true;
  bool allProfilerCategories_ = true;
  EditorProfilerCategory profilerCategory_ = EditorProfilerCategory::Frame;
  std::optional<std::filesystem::path> openRequest_;
  EditorDebugPanel debugPanel_;
};

} // namespace demi::editor
