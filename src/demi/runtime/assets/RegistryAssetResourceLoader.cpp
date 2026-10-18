#include "demi/runtime/assets/RegistryAssetResourceLoader.h"

#include <array>
#include <fstream>
#include <map>

namespace demi::runtime {
namespace {

struct VerifiedSourcePayload {
  std::map<std::filesystem::path, std::vector<std::byte>> files;
};

} // namespace

RegistryAssetResourceLoader::RegistryAssetResourceLoader(
    const AssetRegistry &source, RegistryAssetBackend backend)
    : source_(source), backend_(std::move(backend)) {}

bool RegistryAssetResourceLoader::supports(const AssetManifest &asset) const {
  return backend_.assetTypes.contains(asset.type);
}

std::optional<assets::DecodedAsset>
RegistryAssetResourceLoader::readAndDecode(const AssetManifest &asset,
                                           const std::atomic_bool &isCancelled,
                                           std::string &error) {
  auto payload = std::make_shared<VerifiedSourcePayload>();
  std::size_t bytes = 0;
  for (const std::filesystem::path &path : asset.sourcePaths) {
    if (isCancelled.load()) {
      error = "Asset source verification was cancelled.";
      return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
      error = "Could not read backend asset source: " + path.string();
      return std::nullopt;
    }
    const std::streamsize size = input.tellg();
    if (size < 0) {
      error = "Could not determine backend asset source size: " + path.string();
      return std::nullopt;
    }
    input.seekg(0);
    std::vector<std::byte> content(static_cast<std::size_t>(size));
    if (size > 0 &&
        !input.read(reinterpret_cast<char *>(content.data()), size)) {
      error = "Could not read complete backend asset source: " + path.string();
      return std::nullopt;
    }
    bytes += content.size();
    payload->files.emplace(path, std::move(content));
  }
  error.clear();
  return assets::DecodedAsset{.payload = std::move(payload),
                              .decodedBytes = bytes,
                              .residentBytes = bytes};
}

bool RegistryAssetResourceLoader::upload(const AssetManifest &asset,
                                         const assets::DecodedAsset &decoded,
                                         std::string &error) {
  const std::array uploads{assets::AssetResourceUpload{asset, decoded}};
  return uploadBatch(uploads, -1.0, error) == uploads.size();
}

std::size_t RegistryAssetResourceLoader::uploadBatch(
    const std::span<const assets::AssetResourceUpload> uploads,
    const double /*budgetMilliseconds*/, std::string &error) {
  error.clear();
  if (uploads.empty()) {
    return 0;
  }
  std::set<std::string> candidateIds = residentIds_;
  for (const auto &upload : uploads) {
    if (!upload.decoded.payload) {
      error = "Backend upload received an empty verified-source payload.";
      return 0;
    }
    candidateIds.insert(upload.asset.id);
  }
  // Registry replacement is indivisible. Publish one union of ready resources
  // rather than rebuilding every native object for each individual asset.
  const AssetRegistry candidate = snapshot(candidateIds, error);
  if (!error.empty()) {
    return 0;
  }
  if (!backend_.apply || !backend_.apply(candidate, error)) {
    if (error.empty()) {
      error = "Backend asset-registry replacement failed.";
    }
    return 0;
  }
  residentIds_ = std::move(candidateIds);
  return uploads.size();
}

void RegistryAssetResourceLoader::unload(const std::string_view assetId) {
  const std::array assetIds{std::string(assetId)};
  std::string error;
  (void)unloadBatch(assetIds, error);
}

bool RegistryAssetResourceLoader::unloadBatch(
    const std::span<const std::string> assetIds, std::string &error) {
  error.clear();
  std::set<std::string> candidateIds = residentIds_;
  for (const auto &assetId : assetIds) {
    candidateIds.erase(assetId);
  }
  if (candidateIds == residentIds_) {
    return true;
  }
  const AssetRegistry candidate = snapshot(candidateIds, error);
  if (!error.empty()) {
    return false;
  }
  if (!backend_.apply || !backend_.apply(candidate, error)) {
    if (error.empty()) {
      error = "Backend asset-registry release failed.";
    }
    return false;
  }
  residentIds_ = std::move(candidateIds);
  return true;
}

std::string_view RegistryAssetResourceLoader::backendName() const {
  return backend_.name;
}

bool RegistryAssetResourceLoader::restore(std::string &error) {
  const AssetRegistry candidate = snapshot(residentIds_, error);
  return error.empty() && backend_.apply && backend_.apply(candidate, error);
}

AssetRegistry
RegistryAssetResourceLoader::snapshot(const std::set<std::string> &roots,
                                      std::string &error) const {
  std::set<std::string> included = roots;
  for (const std::string &root : roots) {
    const AssetManifest *asset = findAsset(source_, root);
    if (asset == nullptr) {
      error = "Resident backend asset disappeared from the registry: " + root;
      return {.projectDirectory = source_.projectDirectory,
              .assets = {},
              .diagnostics = {}};
    }
    for (const AssetManifest *dependency : assetDependencies(source_, *asset))
      included.insert(dependency->id);
  }

  AssetRegistry result{.projectDirectory = source_.projectDirectory,
                       .assets = {},
                       .diagnostics = {}};
  for (const AssetManifest &asset : source_.assets)
    if (included.contains(asset.id))
      result.assets.push_back(asset);
  error.clear();
  return result;
}

} // namespace demi::runtime
