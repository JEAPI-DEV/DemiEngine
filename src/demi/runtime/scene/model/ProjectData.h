#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace demi::runtime {

struct LuaActionHandler {
  std::string action;
  std::string functionName;
};
struct LuaEventHandler {
  std::string eventName;
  std::string functionName;
};

struct SceneEntry {
  std::string id;
  std::filesystem::path path;
};

struct ProjectData {
  std::filesystem::path projectPath;
  std::filesystem::path projectDirectory;
  std::string name;
  std::string mainScene;
  std::string scriptEntry;
  std::vector<std::string> scriptModules;
  std::vector<SceneEntry> scenes;
};

} // namespace demi::runtime
