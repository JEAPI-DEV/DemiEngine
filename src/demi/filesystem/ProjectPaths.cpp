#include "demi/filesystem/ProjectPaths.h"

#include <algorithm>
#include <string>

namespace demi {

bool isInternalProjectDirectory(const std::string_view name) {
  return name.starts_with('.') || name == "build" || name == "generated" ||
         name == "node_modules" || name == "__pycache__";
}

bool hasExtension(const std::filesystem::path &path, const char *extension) {
  return path.extension() == extension;
}

bool isProjectFile(const std::filesystem::path &path) {
  const std::string name = path.filename().string();
  return name.ends_with(".project.json") || name == "demi.project.json";
}

bool isSceneFile(const std::filesystem::path &path) {
  return path.filename().string().ends_with(".scene.json");
}

bool isHudFile(const std::filesystem::path &path) {
  return path.filename().string().ends_with(".hud.json");
}

bool isSaveFile(const std::filesystem::path &path) {
  return path.filename().string().ends_with(".save.json");
}

bool isAssetFile(const std::filesystem::path &path) {
  return path.filename().string().ends_with(".asset.json");
}

bool isColliderShapeFile(const std::filesystem::path &path) {
  return path.filename().string().ends_with(".collider.json");
}

bool isPrefabFile(const std::filesystem::path &path) {
  return path.filename().string().ends_with(".prefab.json") &&
         !isUiPrefabFile(path);
}

bool isUiPrefabFile(const std::filesystem::path &path) {
  return path.filename().string().ends_with(".ui.prefab.json");
}

bool isInputReplayFile(const std::filesystem::path &path) {
  return path.filename().string().ends_with(".replay.json");
}

bool isPackageManifestFile(const std::filesystem::path &path) {
  return path.filename() == "demi.package.json";
}

bool isAssetGroupFile(const std::filesystem::path &path) {
  return path.filename().string().ends_with(".asset-group.json");
}

std::vector<std::filesystem::path>
collectKnownSourceFiles(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files;
  if (!std::filesystem::exists(root)) {
    return files;
  }

  if (std::filesystem::is_regular_file(root)) {
    if (isProjectFile(root) || isSceneFile(root) || isHudFile(root) ||
        isSaveFile(root) || isAssetFile(root) || isColliderShapeFile(root) ||
        isPrefabFile(root) || isUiPrefabFile(root) || isInputReplayFile(root) ||
        isPackageManifestFile(root) || isAssetGroupFile(root)) {
      files.push_back(root);
    }
    return files;
  }

  for (std::filesystem::recursive_directory_iterator iterator(root), end;
       iterator != end; ++iterator) {
    const auto &entry = *iterator;
    if (entry.is_directory() &&
        isInternalProjectDirectory(entry.path().filename().string())) {
      iterator.disable_recursion_pending();
      continue;
    }
    if (!entry.is_regular_file()) {
      continue;
    }

    const std::filesystem::path path = entry.path();
    if (isProjectFile(path) || isSceneFile(path) || isHudFile(path) ||
        isSaveFile(path) || isAssetFile(path) || isColliderShapeFile(path) ||
        isPrefabFile(path) || isUiPrefabFile(path) || isInputReplayFile(path) ||
        isPackageManifestFile(path) || isAssetGroupFile(path)) {
      files.push_back(path);
    }
  }

  std::ranges::sort(files);
  return files;
}

} // namespace demi
