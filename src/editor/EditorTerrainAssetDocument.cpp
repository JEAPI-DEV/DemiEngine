#include "editor/EditorTerrainAssetDocument.h"

#include "demi/assets/AssetImporter.h"
#include "demi/assets/TerrainAsset.h"

#include <exception>
#include <stdexcept>

namespace demi::editor {
namespace {

Diagnostics validateTerrainSource(const std::filesystem::path &path,
                                  const nlohmann::json &document,
                                  const std::string &expectedId) {
  try {
    const auto parsed = assets::parseTerrainAssetSource(document);
    if (parsed.id != expectedId)
      throw std::invalid_argument(
          "Terrain source ID differs from its asset manifest.");
    return {};
  } catch (const std::exception &failure) {
    return {{.severity = Severity::Error,
             .code = "EDITOR_TERRAIN_SOURCE_INVALID",
             .message = failure.what(),
             .path = path.string()}};
  }
}

} // namespace

bool EditorTerrainAssetDocument::open(const AssetManifest &manifest,
                                      std::string &error) {
  if (manifest.type != "Terrain" || manifest.sourcePath.empty()) {
    error = "Select an imported Terrain asset source.";
    return false;
  }
  EditorJsonDocument candidate;
  if (!candidate.open(
          manifest.sourcePath,
          [id = manifest.id](const auto &path, const auto &document) {
            return validateTerrainSource(path, document, id);
          },
          error))
    return false;
  if (hasErrors(candidate.diagnostics())) {
    error = candidate.diagnostics().front().message;
    return false;
  }
  source_ = std::move(candidate);
  manifestPath_ = manifest.manifestPath;
  id_ = manifest.id;
  return true;
}

bool EditorTerrainAssetDocument::setRecipe(nlohmann::json recipe,
                                           std::string &error) {
  return source_.set("/recipe", std::move(recipe), error);
}

bool EditorTerrainAssetDocument::restore(nlohmann::json source,
                                         std::string &error) {
  return source_.replace(std::move(source), error);
}

bool EditorTerrainAssetDocument::save(std::string &error) {
  if (!source_.save(error))
    return false;
  const Diagnostics diagnostics = assets::reimportAsset(manifestPath_);
  if (hasErrors(diagnostics)) {
    error = "Terrain source was saved, but reimport failed: " +
            diagnostics.front().message;
    return false;
  }
  return true;
}

} // namespace demi::editor
