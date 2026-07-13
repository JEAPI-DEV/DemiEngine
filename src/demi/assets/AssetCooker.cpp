#include "demi/assets/AssetCooker.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/schema/Validation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

namespace demi::assets {
namespace {

bool skippedRoot(const std::filesystem::path &relative) {
  if (relative.empty())
    return false;
  const std::string first = relative.begin()->string();
  return first == "assets" || first == "build" || first == "generated" ||
         first == ".git" || first == "saves" || first == "tests";
}

void addProjectFiles(const std::filesystem::path &projectDirectory,
                     std::set<std::filesystem::path> &files,
                     Diagnostics &diagnostics) {
  std::error_code code;
  for (std::filesystem::recursive_directory_iterator
           iterator(projectDirectory, code),
       end;
       iterator != end; iterator.increment(code)) {
    if (code) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "COOK_SCAN_FAILED",
                             .message = code.message(),
                             .path = projectDirectory.string()});
      return;
    }
    const auto relative =
        std::filesystem::relative(iterator->path(), projectDirectory, code);
    if (skippedRoot(relative)) {
      if (iterator->is_directory())
        iterator.disable_recursion_pending();
      continue;
    }
    if (iterator->is_regular_file())
      files.insert(iterator->path());
  }
}

} // namespace

Diagnostics cookProject(const CookRequest &request) {
  Diagnostics diagnostics;
  if (request.platform != "linux" && request.platform != "linux_server") {
    diagnostics.push_back(
        {.severity = Severity::Error,
         .code = "COOK_PLATFORM_UNSUPPORTED",
         .message =
             "Cooking is not implemented for platform: " + request.platform,
         .path = request.projectFile.string(),
         .suggestion = "Use --platform linux. Android cooking remains "
                       "deferred until its runtime path is covered."});
    return diagnostics;
  }
  const auto absoluteProject = std::filesystem::absolute(request.projectFile);
  const auto summary = validatePath(absoluteProject);
  diagnostics.insert(diagnostics.end(), summary.diagnostics.begin(),
                     summary.diagnostics.end());
  const auto projectDirectory = absoluteProject.parent_path();
  const AssetRegistry registry = loadAssetRegistry(projectDirectory);
  if (hasErrors(diagnostics))
    return diagnostics;

  std::set<std::filesystem::path> files;
  addProjectFiles(projectDirectory, files, diagnostics);
  for (const auto &asset : registry.assets)
    for (const auto &file : collectAssetFiles(asset)) {
      if (!pathIsInside(projectDirectory, file))
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "COOK_EXTERNAL_ASSET_FILE",
                               .message = "Cooked assets must be inside the "
                                          "project directory.",
                               .path = file.string()});
      else
        files.insert(file);
    }
  if (hasErrors(diagnostics))
    return diagnostics;

  std::error_code code;
  std::filesystem::remove_all(request.outputDirectory, code);
  code.clear();
  std::filesystem::create_directories(request.outputDirectory, code);
  nlohmann::json cookedFiles = nlohmann::json::array();
  for (const auto &source : files) {
    const auto relative = std::filesystem::relative(source, projectDirectory);
    const auto target = request.outputDirectory / relative;
    std::filesystem::create_directories(target.parent_path(), code);
    if (!code)
      std::filesystem::copy_file(
          source, target, std::filesystem::copy_options::overwrite_existing,
          code);
    if (code) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "COOK_COPY_FAILED",
                             .message = code.message(),
                             .path = source.string()});
      code.clear();
      continue;
    }
    cookedFiles.push_back(
        {{"path", relative.generic_string()}, {"hash", *hashFile(target)}});
  }
  if (hasErrors(diagnostics))
    return diagnostics;
  const nlohmann::json manifest{
      {"format_version", 1},
      {"platform", request.platform},
      {"project", absoluteProject.filename().generic_string()},
      {"files", std::move(cookedFiles)},
  };
  std::ofstream output(request.outputDirectory / "cook.manifest.json");
  if (!output)
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "COOK_MANIFEST_WRITE_FAILED",
                           .message = "Could not write cook manifest.",
                           .path = request.outputDirectory.string()});
  else
    output << manifest.dump(2) << '\n';
  return diagnostics;
}

} // namespace demi::assets
