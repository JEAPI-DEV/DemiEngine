#include "demi/filesystem/ProjectPaths.h"

#include <algorithm>
#include <string>

namespace demi {

bool hasExtension(const std::filesystem::path& path, const char* extension) {
  return path.extension() == extension;
}

bool isProjectFile(const std::filesystem::path& path) {
  const std::string name = path.filename().string();
  return name.ends_with(".project.json") || name == "demi.project.json";
}

bool isSceneFile(const std::filesystem::path& path) {
  return path.filename().string().ends_with(".scene.json");
}

bool isHudFile(const std::filesystem::path& path) {
  return path.filename().string().ends_with(".hud.json");
}

bool isSaveFile(const std::filesystem::path& path) {
  return path.filename().string().ends_with(".save.json");
}

bool isAssetFile(const std::filesystem::path& path) {
  return path.filename().string().ends_with(".asset.json");
}

std::vector<std::filesystem::path> collectKnownSourceFiles(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  if (!std::filesystem::exists(root)) {
    return files;
  }

  if (std::filesystem::is_regular_file(root)) {
    if (isProjectFile(root) || isSceneFile(root) || isHudFile(root) || isSaveFile(root) || isAssetFile(root)) {
      files.push_back(root);
    }
    return files;
  }

  for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    const std::filesystem::path path = entry.path();
    if (isProjectFile(path) || isSceneFile(path) || isHudFile(path) || isSaveFile(path) || isAssetFile(path)) {
      files.push_back(path);
    }
  }

  std::ranges::sort(files);
  return files;
}

} // namespace demi
