#include "editor/EditorAssetIndex.h"

#include "demi/filesystem/ProjectPaths.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

namespace demi::editor {
namespace {

bool changedAfter(const AssetManifest &asset,
                  const std::filesystem::file_time_type cookedAt) {
  std::error_code error;
  if (std::filesystem::last_write_time(asset.manifestPath, error) > cookedAt)
    return true;
  error.clear();
  for (const std::filesystem::path &source : asset.sourcePaths) {
    if (std::filesystem::last_write_time(source, error) > cookedAt)
      return true;
    error.clear();
  }
  return false;
}

} // namespace

void EditorAssetIndex::refresh(
    const std::filesystem::path &projectDirectory,
    const std::vector<std::filesystem::path> &sources) {
  assets_.clear();
  groups_.clear();
  const AssetRegistry registry = loadAssetRegistry(projectDirectory);
  diagnostics_ = validateAssetRegistry(registry);

  const std::filesystem::path cookDirectory =
      projectDirectory / "generated/cooked/linux";
  const std::filesystem::path cookManifest =
      cookDirectory / "cook.manifest.json";
  std::set<std::string> cookedAssets;
  std::filesystem::file_time_type cookedAt{};
  std::error_code timeError;
  if (std::filesystem::is_regular_file(cookManifest)) {
    cookedAt = std::filesystem::last_write_time(cookManifest, timeError);
    try {
      std::ifstream input(cookManifest);
      nlohmann::json document;
      input >> document;
      for (const nlohmann::json &entry :
           document.value("assets", nlohmann::json::array()))
        if (entry.is_object())
          cookedAssets.insert(entry.value("asset", ""));
    } catch (const nlohmann::json::exception &exception) {
      diagnostics_.push_back({.severity = Severity::Warning,
                              .code = "EDITOR_COOK_MANIFEST_INVALID",
                              .message = exception.what(),
                              .path = cookManifest.string()});
    }
  }

  for (const AssetManifest &manifest : registry.assets) {
    EditorAssetRecord record{.manifest = manifest};
    for (const Diagnostic &diagnostic : diagnostics_)
      if (diagnostic.path == manifest.manifestPath.string() ||
          std::ranges::any_of(manifest.sourcePaths,
                              [&](const std::filesystem::path &source) {
                                return source.string() == diagnostic.path;
                              }))
        record.diagnostics.push_back(diagnostic);
    const bool sourceStale = std::ranges::any_of(
        record.diagnostics, [](const Diagnostic &diagnostic) {
          return diagnostic.code == "ASSET_SOURCE_STALE" ||
                 diagnostic.code == "ASSET_GENERATED_OUTPUT_STALE" ||
                 diagnostic.severity == Severity::Error;
        });
    if (!cookedAssets.contains(manifest.id)) {
      record.cookState = EditorAssetCookState::Uncooked;
      record.cookReason = "No Linux cook contains this asset.";
    } else if (sourceStale ||
               (!timeError && changedAfter(manifest, cookedAt))) {
      record.cookState = EditorAssetCookState::Stale;
      record.cookReason = "Authored source changed after the last Linux cook.";
    } else {
      record.cookState = EditorAssetCookState::Current;
      record.cookReason = "Included by the current Linux cook manifest.";
    }
    assets_.push_back(std::move(record));
  }

  for (const std::filesystem::path &source : sources) {
    if (!isAssetGroupFile(source))
      continue;
    if (auto group = assets::loadAssetGroup(source, &diagnostics_))
      groups_.push_back(std::move(*group));
  }
  std::ranges::sort(groups_, {}, &assets::AssetGroupDescriptor::id);
}

const EditorAssetRecord *
EditorAssetIndex::findByManifest(const std::filesystem::path &path) const {
  const auto found =
      std::ranges::find(assets_, path, [](const EditorAssetRecord &record) {
        return record.manifest.manifestPath;
      });
  return found == assets_.end() ? nullptr : &*found;
}

std::vector<std::string> EditorAssetIndex::types() const {
  std::set<std::string> unique;
  for (const EditorAssetRecord &asset : assets_)
    unique.insert(asset.manifest.type);
  return {unique.begin(), unique.end()};
}

const char *editorAssetCookStateName(const EditorAssetCookState state) {
  switch (state) {
  case EditorAssetCookState::Uncooked:
    return "Uncooked";
  case EditorAssetCookState::Current:
    return "Current";
  case EditorAssetCookState::Stale:
    return "Stale";
  }
  return "Uncooked";
}

} // namespace demi::editor
