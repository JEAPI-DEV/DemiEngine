#pragma once

#include "editor/EditorProjectOperations.h"

#include "demi/runtime/scene/ProjectBuildSettings.h"

#include <array>
#include <string>
#include <vector>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

class EditorBuildPanel {
public:
  void open() { show_ = true; }
  void draw(EditorWorkspace &workspace, std::string &notice);
  [[nodiscard]] EditorProjectOperationSnapshot operation() const {
    return operations_.snapshot();
  }
  [[nodiscard]] bool linuxTarget() const { return linuxTarget_; }
  [[nodiscard]] bool androidTarget() const { return androidTarget_; }

private:
  struct BuildSettingsDraft {
    std::array<char, 160> applicationId{};
    std::array<char, 160> displayName{};
    std::array<char, 96> executableName{};
    std::array<char, 48> versionName{};
    std::array<char, 160> icon{};
    std::array<char, 160> splash{};
    std::array<char, 160> newPermission{};
    int versionCode = 1;
    int windowWidth = 1280;
    int windowHeight = 720;
    int minimumSdk = 26;
    std::string windowMode = "windowed";
    std::string orientation = "unspecified";
    std::vector<std::string> permissions;
    bool arm64 = true;
    bool x86_64 = false;
    bool initialized = false;
    bool dirty = false;
    std::string source;
  };

  void syncBuildSettings(const EditorWorkspace &workspace);
  void drawBuildSettings(EditorWorkspace &workspace, std::string &notice);
  [[nodiscard]] runtime::ProjectBuildSettings editedBuildSettings() const;

  EditorProjectOperations operations_;
  BuildSettingsDraft settings_;
  std::uint64_t handledOperation_ = 0;
  bool linuxTarget_ = true;
  bool androidTarget_ = false;
  bool releaseBuild_ = false;
  bool androidBundle_ = false;
  bool show_ = false;
};

} // namespace demi::editor
