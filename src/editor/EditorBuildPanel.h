#pragma once

#include "editor/EditorProjectOperations.h"

#include <string>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

class EditorBuildPanel {
public:
  void draw(EditorWorkspace &workspace, ImVec2 position, ImVec2 size,
            std::string &notice);
  [[nodiscard]] EditorProjectOperationSnapshot operation() const {
    return operations_.snapshot();
  }
  [[nodiscard]] bool linuxTarget() const { return linuxTarget_; }
  [[nodiscard]] bool androidTarget() const { return androidTarget_; }

private:
  EditorProjectOperations operations_;
  std::uint64_t handledOperation_ = 0;
  bool linuxTarget_ = true;
  bool androidTarget_ = false;
};

} // namespace demi::editor
