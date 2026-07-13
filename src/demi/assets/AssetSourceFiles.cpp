#include "demi/assets/AssetSourceFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>

namespace demi::assets {
namespace {

void collectUris(const nlohmann::json &value, const std::filesystem::path &base,
                 std::set<std::filesystem::path> &files) {
  if (value.is_object()) {
    for (const auto &[key, child] : value.items()) {
      if (key == "uri" && child.is_string() &&
          !child.get<std::string>().starts_with("data:"))
        files.insert(base / child.get<std::string>());
      else
        collectUris(child, base, files);
    }
  } else if (value.is_array()) {
    for (const auto &child : value)
      collectUris(child, base, files);
  }
}

} // namespace

bool pathIsInside(const std::filesystem::path &root,
                  const std::filesystem::path &path) {
  std::error_code error;
  const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
  if (error)
    return false;
  const auto canonicalPath = std::filesystem::weakly_canonical(path, error);
  if (error)
    return false;
  const auto relative =
      std::filesystem::relative(canonicalPath, canonicalRoot, error);
  return !error && !relative.empty() && !relative.is_absolute() &&
         *relative.begin() != "..";
}

std::vector<std::filesystem::path>
collectReferencedSourceFiles(const std::filesystem::path &source) {
  std::set<std::filesystem::path> files;
  files.insert(source);
  const std::string extension = source.extension().string();
  if (extension == ".gltf" || extension == ".json") {
    try {
      std::ifstream input(source);
      if (input)
        collectUris(nlohmann::json::parse(input), source.parent_path(), files);
    } catch (const nlohmann::json::exception &) {
      // Validation of the source format belongs to its importer.
    }
  }
  return {files.begin(), files.end()};
}

std::vector<std::filesystem::path>
collectAssetFiles(const AssetManifest &manifest) {
  std::set<std::filesystem::path> files;
  files.insert(manifest.manifestPath);
  for (const auto &source : manifest.sourcePaths) {
    const auto referenced = collectReferencedSourceFiles(source);
    files.insert(referenced.begin(), referenced.end());
  }
  if (manifest.texturePath)
    files.insert(*manifest.texturePath);
  if (manifest.atlasPath)
    files.insert(*manifest.atlasPath);
  if (manifest.licensePath)
    files.insert(*manifest.licensePath);
  if (manifest.generatedOutputPath)
    files.insert(*manifest.generatedOutputPath);

  std::vector<std::filesystem::path> result(files.begin(), files.end());
  std::ranges::sort(result);
  return result;
}

} // namespace demi::assets
