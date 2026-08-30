#include "editor/EditorWorkspace.h"

#include "editor/EditorDocumentStore.h"

#include "demi/assets/AssetImporter.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace demi::editor {

void EditorWorkspace::discoverSources() {
  sources_.clear();
  std::error_code filesystemError;
  std::filesystem::recursive_directory_iterator iterator(
      project_->project.projectDirectory,
      std::filesystem::directory_options::skip_permission_denied,
      filesystemError);
  const std::filesystem::recursive_directory_iterator end;
  for (; !filesystemError && iterator != end;
       iterator.increment(filesystemError)) {
    if (iterator->is_directory()) {
      const std::string name = iterator->path().filename().string();
      if (name == "generated" || name == "build" || name == ".demi" ||
          name == ".git")
        iterator.disable_recursion_pending();
      continue;
    }
    if (iterator->is_regular_file())
      sources_.push_back(iterator->path());
  }
  std::ranges::sort(sources_);
}

void EditorWorkspace::refreshAssetIndex() {
  assetIndex_.refresh(project_->project.projectDirectory, sources_);
}

bool EditorWorkspace::saveProject(std::string &error) {
  if (!projectDocument_.save(error))
    return false;
  auto loaded = runtime::loadProject(projectPath_, error);
  if (!loaded)
    return false;
  project_ = std::move(loaded);
  discoverSources();
  refreshAssetIndex();
  loadPreviewTilemaps();
  refreshDiagnostics();
  return true;
}

bool EditorWorkspace::projectUndo(std::string &error) {
  return projectDocument_.undo(error);
}

bool EditorWorkspace::projectRedo(std::string &error) {
  return projectDocument_.redo(error);
}

bool EditorWorkspace::setPreloadedAssets(std::vector<std::string> assets,
                                         std::string &error) {
  const auto exists = [&](const std::string &id) {
    if (id.starts_with("asset://"))
      return std::ranges::any_of(assetIndex_.assets(), [&](const auto &asset) {
        return asset.manifest.id == id;
      });
    return std::ranges::any_of(assetIndex_.groups(), [&](const auto &group) {
      return group.id == id;
    });
  };
  if (std::ranges::any_of(assets,
                          [&](const std::string &id) { return !exists(id); })) {
    error = "Every preload entry must resolve to an authored asset or group.";
    return false;
  }
  return projectDocument_.setPreloadedAssets(std::move(assets), error);
}

bool EditorWorkspace::setProjectBuildSettings(
    runtime::ProjectBuildSettings settings, std::string &error) {
  const auto validateBranding = [&](const std::string &id) {
    if (id.empty())
      return true;
    const auto asset = std::ranges::find_if(
        assetIndex_.assets(), [&](const EditorAssetRecord &record) {
          return record.manifest.id == id;
        });
    return asset != assetIndex_.assets().end() &&
           (asset->manifest.type == "Texture2D" ||
            asset->manifest.type == "SvgTexture2D");
  };
  if (!validateBranding(settings.icon) ||
      !validateBranding(settings.splash)) {
    error = "Icon and splash must reference an authored 2D image asset.";
    return false;
  }
  return projectDocument_.setBuildSettings(std::move(settings), error);
}

bool EditorWorkspace::addProjectScene(std::string id,
                                      std::filesystem::path path,
                                      std::string &error) {
  if (!std::filesystem::is_regular_file(project_->project.projectDirectory /
                                        path)) {
    error = "The scene source file does not exist.";
    return false;
  }
  return projectDocument_.addScene(std::move(id), std::move(path), error);
}

bool EditorWorkspace::removeProjectScene(const std::string_view id,
                                         std::string &error) {
  return projectDocument_.removeScene(id, error);
}

bool EditorWorkspace::importAsset(const assets::AssetImportRequest &request,
                                  std::string &error) {
  assets::AssetImportRequest effective = request;
  effective.projectDirectory = project_->project.projectDirectory;
  const assets::AssetImportResult result = assets::importAsset(effective);
  if (hasErrors(result.diagnostics)) {
    error = result.diagnostics.front().message;
    diagnostics_.insert(diagnostics_.end(), result.diagnostics.begin(),
                        result.diagnostics.end());
    return false;
  }
  refreshAssetMetadata();
  return true;
}

bool EditorWorkspace::reimportAsset(const std::filesystem::path &manifest,
                                    std::string &error) {
  const Diagnostics result = assets::reimportAsset(manifest);
  if (hasErrors(result)) {
    error = result.front().message;
    diagnostics_.insert(diagnostics_.end(), result.begin(), result.end());
    return false;
  }
  refreshAssetMetadata();
  return true;
}

bool EditorWorkspace::createAssetGroup(std::string id,
                                       std::vector<std::string> roots,
                                       std::string &error) {
  if (!id.starts_with("asset-group://") || id.size() <= 14 || roots.empty()) {
    error = "Asset groups require an asset-group:// ID and at least one root.";
    return false;
  }
  std::ranges::sort(roots);
  if (std::ranges::adjacent_find(roots) != roots.end()) {
    error = "Asset-group roots must be unique.";
    return false;
  }
  const auto validRoot = [&](const std::string &root) {
    if (root.starts_with("asset://"))
      return std::ranges::any_of(assetIndex_.assets(), [&](const auto &asset) {
        return asset.manifest.id == root;
      });
    if (root.starts_with("scene://"))
      return std::ranges::any_of(
          projectDocument_.scenes(),
          [&](const auto &scene) { return scene.id == root; });
    return false;
  };
  if (std::ranges::any_of(
          roots, [&](const std::string &root) { return !validRoot(root); })) {
    error = "Every asset-group root must resolve to an asset or project scene.";
    return false;
  }
  std::string safeName = id.substr(std::string("asset-group://").size());
  std::ranges::replace_if(
      safeName,
      [](const char value) {
        return !std::isalnum(static_cast<unsigned char>(value)) &&
               value != '-' && value != '_';
      },
      '_');
  const std::filesystem::path path = project_->project.projectDirectory /
                                     "assets/groups" /
                                     (safeName + ".asset-group.json");
  const nlohmann::json document{{"format_version", 1},
                                {"id", id},
                                {"roots", std::move(roots)},
                                {"budget",
                                 {{"resident_mb", 256.0},
                                  {"decoded_mb", 64.0},
                                  {"upload_ms_per_frame", 3.0}}}};
  std::error_code directoryError;
  std::filesystem::create_directories(path.parent_path(), directoryError);
  if (directoryError) {
    error = "Could not create the asset-group directory: " +
            directoryError.message();
    return false;
  }
  if (!EditorDocumentStore{}.writeNew(path, document.dump(2) + '\n', error))
    return false;
  refreshAssetMetadata();
  return true;
}

void EditorWorkspace::refreshAssetMetadata() {
  discoverSources();
  refreshAssetIndex();
  refreshDiagnostics();
}

} // namespace demi::editor
