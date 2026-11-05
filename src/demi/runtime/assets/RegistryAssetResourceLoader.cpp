#include "demi/runtime/assets/RegistryAssetResourceLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>

namespace demi::runtime {
namespace {

struct VerifiedSourcePayload {
  std::size_t sourceBytes = 0;
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
    // Native backends consume stable paths, not this verification buffer.
    // Keeping complete encoded files here duplicates every model and image.
    std::array<char, 64U * 1024U> buffer;
    std::size_t verified = 0;
    while (input) {
      if (isCancelled.load()) {
        error = "Asset source verification was cancelled.";
        return std::nullopt;
      }
      input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      verified += static_cast<std::size_t>(input.gcount());
    }
    if (input.bad() || verified != static_cast<std::size_t>(size)) {
      error = "Could not read complete backend asset source: " + path.string();
      return std::nullopt;
    }
    bytes += verified;
  }
  payload->sourceBytes = bytes;
  error.clear();
  return assets::DecodedAsset{.payload = std::move(payload),
                              .decodedBytes = sizeof(VerifiedSourcePayload),
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
    const double budgetMilliseconds, std::string &error) {
  error.clear();
  if (uploads.empty()) {
    return 0;
  }
  std::vector<std::string> requestedIds;
  for (const auto &upload : uploads) {
    requestedIds.push_back(upload.asset.id);
  }
  const bool staged = backend_.applyStep && budgetMilliseconds >= 0.0;
  if (!pendingUploadIds_.empty() &&
      (!staged || pendingUploadIds_ != requestedIds)) {
    if (staged) {
      return 0;
    }
    const auto cancelledIds = pendingUploadIds_;
    cancelUploadBatch(cancelledIds);
  }
  for (const auto &upload : uploads) {
    if (!upload.decoded.payload) {
      error = "Backend upload received an empty verified-source payload.";
      cancelUploadBatch(requestedIds);
      return 0;
    }
  }
  if (staged && !pendingUploadIds_.empty() && !pendingSourceMatches()) {
    const auto cancelledIds = pendingUploadIds_;
    cancelUploadBatch(cancelledIds);
  }
  if (staged && pendingUploadIds_.empty()) {
    pendingResidentIds_ = residentIds_;
    pendingResidentIds_.insert(requestedIds.begin(), requestedIds.end());
    auto candidate = snapshot(pendingResidentIds_, error);
    if (!error.empty()) {
      clearPendingPublication();
      return 0;
    }
    pendingRegistry_ = std::move(candidate);
    pendingUploadIds_ = requestedIds;
    pendingSourceCount_ = source_.assets.size();
    std::size_t candidateIndex = 0;
    for (std::size_t index = 0;
         index < source_.assets.size() &&
         candidateIndex < pendingRegistry_->assets.size();
         ++index) {
      if (source_.assets[index].id ==
          pendingRegistry_->assets[candidateIndex].id) {
        pendingSourceIndices_.push_back(index);
        ++candidateIndex;
      }
    }
  }
  if (staged) {
    const auto stage =
        backend_.applyStep(*pendingRegistry_, budgetMilliseconds, error);
    if (stage == AssetPublicationStage::Pending) {
      return 0;
    }
    if (stage == AssetPublicationStage::Failed) {
      cancelUploadBatch(requestedIds);
      if (error.empty()) {
        error = "Backend staged asset publication failed.";
      }
      return 0;
    }
    residentIds_ = std::move(pendingResidentIds_);
    clearPendingPublication();
    return uploads.size();
  }
  std::set<std::string> candidateIds = residentIds_;
  candidateIds.insert(requestedIds.begin(), requestedIds.end());
  // Registry replacement is indivisible. Resolve the union once per operation,
  // retaining that snapshot across budgeted backend continuation calls.
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

void RegistryAssetResourceLoader::cancelUploadBatch(
    const std::span<const std::string> assetIds) {
  const bool intersects = std::ranges::any_of(assetIds, [this](const auto &id) {
    return std::ranges::find(pendingUploadIds_, id) != pendingUploadIds_.end();
  });
  if (!intersects) {
    return;
  }
  if (backend_.cancelApply) {
    backend_.cancelApply();
  }
  clearPendingPublication();
}

void RegistryAssetResourceLoader::clearPendingPublication() {
  pendingUploadIds_.clear();
  pendingResidentIds_.clear();
  pendingRegistry_.reset();
  pendingSourceIndices_.clear();
  pendingSourceCount_ = 0;
}

bool RegistryAssetResourceLoader::pendingSourceMatches() const {
  if (!pendingRegistry_ || pendingSourceCount_ != source_.assets.size() ||
      pendingRegistry_->projectDirectory != source_.projectDirectory) {
    return false;
  }
  // Only the captured dependency closure matters. Compare borrowed source
  // values without reconstructing dependencies or copying manifests per poll.
  for (std::size_t index = 0; index < pendingSourceIndices_.size(); ++index) {
    if (pendingRegistry_->assets[index] !=
        source_.assets[pendingSourceIndices_[index]]) {
      return false;
    }
  }
  return true;
}

double RegistryAssetResourceLoader::pendingUploadProgress(
    const std::span<const std::string> assetIds) const {
  if (pendingUploadIds_.empty() || !backend_.applyProgress ||
      !std::ranges::equal(assetIds, pendingUploadIds_)) {
    return 0.0;
  }
  const double fraction = backend_.applyProgress();
  return std::isfinite(fraction) ? std::clamp(fraction, 0.0, 1.0) : 0.0;
}

void RegistryAssetResourceLoader::unload(const std::string_view assetId) {
  const std::array assetIds{std::string(assetId)};
  std::string error;
  (void)unloadBatch(assetIds, error);
}

bool RegistryAssetResourceLoader::unloadBatch(
    const std::span<const std::string> assetIds, std::string &error) {
  error.clear();
  cancelUploadBatch(assetIds);
  std::set<std::string> candidateIds = residentIds_;
  for (const auto &assetId : assetIds) {
    candidateIds.erase(assetId);
  }
  if (candidateIds == residentIds_) {
    return true;
  }
  const auto cancelledIds = pendingUploadIds_;
  cancelUploadBatch(cancelledIds);
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
  const auto cancelledIds = pendingUploadIds_;
  cancelUploadBatch(cancelledIds);
  const AssetRegistry candidate = snapshot(residentIds_, error);
  return error.empty() && backend_.apply && backend_.apply(candidate, error);
}

AssetRegistry
RegistryAssetResourceLoader::snapshot(const std::set<std::string> &roots,
                                      std::string &error) const {
  ++snapshotBuilds_;
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
