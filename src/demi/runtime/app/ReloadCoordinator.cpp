#include "demi/runtime/app/ReloadCoordinator.h"

#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/schema/Validation.h"

#include <ranges>
#include <unordered_set>

namespace demi::runtime {
namespace {

bool named(const std::filesystem::path &path, const std::string_view suffix) {
  return path.filename().string().ends_with(suffix);
}

void failure(Diagnostics &diagnostics, std::string code, std::string message,
             const std::filesystem::path &path) {
  diagnostics.push_back(
      {.severity = Severity::Error,
       .code = std::move(code),
       .message = std::move(message),
       .path = path.string(),
       .suggestion = "The running project was left unchanged."});
}

} // namespace

ReloadCoordinator::ReloadCoordinator(std::filesystem::path projectFile,
                                     ReloadCallbacks callbacks)
    : projectFile_(std::move(projectFile)), callbacks_(std::move(callbacks)) {}

ReloadResult
ReloadCoordinator::process(const platform::ProjectFileChangeBatch &batch) {
  ReloadResult result{.generation = batch.generation,
                      .applied = false,
                      .luaChanged = false,
                      .diagnostics = {}};
  if (batch.empty() || batch.generation <= processedGeneration_)
    return result;
  processedGeneration_ = batch.generation;

  bool sceneChanged = false;
  bool assetChanged = false;
  bool projectChanged = false;
  std::unordered_set<std::string> checkedLua;
  for (const auto &path : batch.changed) {
    const bool lua = path.extension() == ".lua";
    const bool scene = named(path, ".scene.json") || named(path, ".hud.json") ||
                       named(path, ".ui.prefab.json") ||
                       named(path, ".project.json");
    result.luaChanged |= lua;
    sceneChanged |= scene;
    projectChanged |= named(path, ".project.json");
    assetChanged |= named(path, ".asset.json") ||
                    (!lua && !scene && path.filename() != ".luarc.json");
    if (lua) {
      checkedLua.insert(path.string());
      Diagnostics scriptDiagnostics = LuaScriptHost::checkScriptSyntax(path);
      result.diagnostics.insert(result.diagnostics.end(),
                                scriptDiagnostics.begin(),
                                scriptDiagnostics.end());
    }
  }
  for (const auto &path : batch.removed) {
    const bool lua = path.extension() == ".lua";
    const bool scene = named(path, ".scene.json") || named(path, ".hud.json") ||
                       named(path, ".ui.prefab.json") ||
                       named(path, ".project.json");
    result.luaChanged |= lua;
    sceneChanged |= scene;
    projectChanged |= named(path, ".project.json");
    assetChanged |= !lua && !scene && path.filename() != ".luarc.json";
  }

  if (sceneChanged) {
    std::error_code scanError;
    std::filesystem::recursive_directory_iterator iterator(
        projectFile_.parent_path(),
        std::filesystem::directory_options::skip_permission_denied, scanError);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
      const auto relative =
          iterator->path().lexically_relative(projectFile_.parent_path());
      const std::string first =
          relative.empty() ? std::string{} : (*relative.begin()).string();
      if (iterator->is_directory() &&
          (first == "build" || first == "generated" || first == ".git" ||
           first == ".demi" || first == "saves")) {
        iterator.disable_recursion_pending();
      }
      if (!iterator->is_regular_file() ||
          iterator->path().extension() != ".lua" ||
          !checkedLua.insert(iterator->path().string()).second) {
        iterator.increment(scanError);
        if (scanError)
          scanError.clear();
        continue;
      }
      Diagnostics scriptDiagnostics =
          LuaScriptHost::checkScriptSyntax(iterator->path());
      result.diagnostics.insert(result.diagnostics.end(),
                                scriptDiagnostics.begin(),
                                scriptDiagnostics.end());
      iterator.increment(scanError);
      if (scanError)
        scanError.clear();
    }
  }

  const ValidationSummary validation = validatePath(projectFile_.parent_path());
  result.diagnostics.insert(result.diagnostics.end(),
                            validation.diagnostics.begin(),
                            validation.diagnostics.end());
  std::string loadError;
  if (!hasErrors(result.diagnostics) &&
      !loadProject(projectFile_, loadError).has_value())
    failure(result.diagnostics, "RELOAD_PROJECT_PREPARE_FAILED", loadError,
            projectFile_);
  if (hasErrors(result.diagnostics))
    return result;
  if (projectChanged) {
    failure(result.diagnostics, "RELOAD_PROJECT_RESTART_REQUIRED",
            "Project configuration changed and requires a runtime restart.",
            projectFile_);
    return result;
  }

  // LuaScriptHost prepares each changed table before replacing the live one;
  // no coordinator callback is necessary for script-only edits.
  std::string error;
  if (sceneChanged && callbacks_.reloadScene &&
      !callbacks_.reloadScene(error)) {
    failure(result.diagnostics, "RELOAD_SCENE_REJECTED", error, projectFile_);
    return result;
  }
  if (assetChanged && callbacks_.reloadAssets &&
      !callbacks_.reloadAssets(error)) {
    if (sceneChanged && callbacks_.cancelSceneReload)
      callbacks_.cancelSceneReload();
    failure(result.diagnostics, "RELOAD_ASSETS_REJECTED", error, projectFile_);
    return result;
  }
  result.applied = true;
  return result;
}

} // namespace demi::runtime
