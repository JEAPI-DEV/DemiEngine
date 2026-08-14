#include "demi/runtime/assets/RegistryAssetResourceLoader.h"

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
  if (!decoded.payload) {
    error = "Backend upload received an empty verified-source payload.";
    return false;
  }
  std::set<std::string> candidateIds = residentIds_;
  candidateIds.insert(asset.id);
  const AssetRegistry candidate = snapshot(candidateIds, error);
  if (!error.empty() || !backend_.apply || !backend_.apply(candidate, error))
    return false;
  residentIds_ = std::move(candidateIds);
  return true;
}

void RegistryAssetResourceLoader::unload(const std::string_view assetId) {
  std::set<std::string> candidateIds = residentIds_;
  if (candidateIds.erase(std::string(assetId)) == 0)
    return;
  std::string error;
  const AssetRegistry candidate = snapshot(candidateIds, error);
  if (error.empty() && backend_.apply && backend_.apply(candidate, error))
    residentIds_ = std::move(candidateIds);
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
