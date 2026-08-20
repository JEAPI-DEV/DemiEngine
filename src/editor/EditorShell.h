#pragma once

#include "editor/EditorHierarchyPanel.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorWorkspace.h"

#include <array>
#include <filesystem>
#include <string>

namespace demi::editor {

class EditorShell {
public:
  explicit EditorShell(EditorWorkspace &workspace);

  void draw(int width, int height, std::string_view rendererName);
  [[nodiscard]] bool wantsExit() const { return wantsExit_; }
  [[nodiscard]] EditorViewportArea viewportArea() const {
    return viewportArea_;
  }
  void setNotice(std::string notice) { notice_ = std::move(notice); }

private:
  EditorWorkspace &workspace_;
  EditorPlaySession playSession_;
  EditorViewportArea viewportArea_;
  EditorHierarchyPanel hierarchyPanel_;
  std::array<char, 128> assetFilter_{};
  std::filesystem::path selectedSource_;
  std::string notice_;
  bool wantsExit_ = false;
  bool linuxTarget_ = true;
  bool androidTarget_ = false;
};

} // namespace demi::editor
