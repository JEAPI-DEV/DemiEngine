#pragma once
#include "demi/diagnostics/Diagnostic.h"
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>
namespace demi::runtime
{
  struct ProjectWindowSettings
  {
    int width = 1280;
    int height = 720;
    std::string mode = "windowed";
  };
  struct ProjectAndroidSettings
  {
    std::string orientation = "unspecified";
    int minimumSdk = 26;
    std::vector<std::string> abis{"arm64-v8a"};
    std::vector<std::string> permissions;
  };
  struct ProjectBuildSettings
  {
    bool authored = false;
    std::string applicationId;
    std::string displayName;
    std::string executableName;
    std::string versionName = "0.1.0";
    int versionCode = 1;
    std::string icon;
    std::string splash;
    ProjectWindowSettings window;
    ProjectAndroidSettings android;
  };
  struct ProjectBuildSettingsResult
  {
    ProjectBuildSettings settings;
    Diagnostics diagnostics;
  };
  [[nodiscard]] ProjectBuildSettingsResult parseProjectBuildSettings(const nlohmann::json &, const std::filesystem::path & = {});
  [[nodiscard]] nlohmann::json projectBuildSettingsJson(const ProjectBuildSettings &);
}
