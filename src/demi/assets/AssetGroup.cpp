#include "demi/assets/AssetGroup.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

namespace demi::assets {
namespace {

void report(Diagnostics *diagnostics, std::string code, std::string message,
            std::string path) {
  if (diagnostics != nullptr)
    diagnostics->push_back({.severity = Severity::Error,
                            .code = std::move(code),
                            .message = std::move(message),
                            .path = std::move(path)});
}

} // namespace

std::optional<AssetGroupDescriptor>
loadAssetGroup(const std::filesystem::path &path, Diagnostics *diagnostics) {
  std::ifstream input(path);
  if (!input) {
    report(diagnostics, "ASSET_GROUP_READ_FAILED",
           "Could not read the asset-group manifest.", path.string());
    return std::nullopt;
  }
  try {
    nlohmann::json document;
    input >> document;
    AssetGroupDescriptor group;
    group.formatVersion = document.value("format_version", 0);
    group.id = document.value("id", "");
    group.roots = document.value("roots", std::vector<std::string>{});
    group.sourcePath = path;
    const auto budget = document.value("budget", nlohmann::json::object());
    const double residentMb = budget.value("resident_mb", 256.0);
    const double decodedMb = budget.value("decoded_mb", 64.0);
    group.budget.residentBytes =
        static_cast<std::size_t>(residentMb * 1024.0 * 1024.0);
    group.budget.maximumDecodedBytes =
        static_cast<std::size_t>(decodedMb * 1024.0 * 1024.0);
    group.budget.uploadMillisecondsPerFrame =
        budget.value("upload_ms_per_frame", 3.0);
    const bool rootsValid =
        std::ranges::all_of(group.roots, [](const auto &root) {
          return (root.starts_with("asset://") && root.size() > 8) ||
                 (root.starts_with("scene://") && root.size() > 8);
        });
    std::set<std::string> uniqueRoots(group.roots.begin(), group.roots.end());
    if (group.formatVersion != 1 || !group.id.starts_with("asset-group://") ||
        group.id.size() <= 14 || group.roots.empty() || !rootsValid ||
        uniqueRoots.size() != group.roots.size() || residentMb <= 0.0 ||
        decodedMb <= 0.0 || group.budget.uploadMillisecondsPerFrame <= 0.0) {
      report(diagnostics, "ASSET_GROUP_INVALID",
             "Asset groups require format_version 1, an asset-group:// ID, "
             "unique asset:// or scene:// roots, and positive budgets.",
             path.string());
      return std::nullopt;
    }
    return group;
  } catch (const nlohmann::json::exception &exception) {
    report(diagnostics, "ASSET_GROUP_INVALID", exception.what(), path.string());
    return std::nullopt;
  }
}

std::vector<const AssetManifest *> resolveAssetGroup(
    const AssetGroupDescriptor &group, const AssetRegistry &registry,
    const AssetGroupRootResolver &rootResolver, Diagnostics *diagnostics) {
  std::map<std::string, const AssetManifest *> assets;
  const auto addAsset = [&](const std::string &assetId) {
    const AssetManifest *asset = findAsset(registry, assetId);
    if (asset == nullptr) {
      report(diagnostics, "ASSET_GROUP_ROOT_NOT_FOUND",
             "Asset-group asset was not found: " + assetId,
             group.sourcePath.string());
      return;
    }
    assets.emplace(asset->id, asset);
    for (const AssetManifest *dependency :
         assetDependencies(registry, *asset, diagnostics))
      assets.emplace(dependency->id, dependency);
  };
  for (const std::string &root : group.roots) {
    if (root.starts_with("asset://")) {
      addAsset(root);
      continue;
    }
    if (!rootResolver) {
      report(diagnostics, "ASSET_GROUP_ROOT_UNSUPPORTED",
             "No runtime resolver supports asset-group root: " + root,
             group.sourcePath.string());
      continue;
    }
    for (const std::string &assetId : rootResolver(root, diagnostics))
      addAsset(assetId);
  }
  std::vector<const AssetManifest *> result;
  for (const auto &[unused, asset] : assets) {
    (void)unused;
    result.push_back(asset);
  }
  return result;
}

std::string_view assetGroupStageName(const AssetGroupStage stage) {
  switch (stage) {
  case AssetGroupStage::Resolve:
    return "resolve";
  case AssetGroupStage::Read:
    return "read";
  case AssetGroupStage::Decode:
    return "decode";
  case AssetGroupStage::Upload:
    return "upload";
  case AssetGroupStage::Ready:
    return "ready";
  case AssetGroupStage::Failed:
    return "failed";
  case AssetGroupStage::Cancelled:
    return "cancelled";
  }
  return "failed";
}

struct AssetGroupService::Work {
  const AssetManifest *asset = nullptr;
  std::shared_ptr<AssetResourceLoader> loader;
  std::future<std::pair<std::optional<DecodedAsset>, std::string>> future;
  std::optional<DecodedAsset> decoded;
  bool uploaded = false;
  std::size_t pendingBytes = 0;
};

struct AssetGroupService::Request {
  AssetGroupDescriptor group;
  AssetGroupProgress progress;
  std::vector<Work> work;
  std::set<std::string> heldAssets;
  std::set<std::string> completedAssets;
  bool isActive = false;
};

struct AssetGroupService::Resource {
  std::shared_ptr<AssetResourceLoader> loader;
  std::size_t residentBytes = 0;
  std::set<std::string> owners;
  std::set<AssetGroupRequestHandle> preparingRequests;
  std::shared_ptr<std::atomic_bool> cancellation =
      std::make_shared<std::atomic_bool>(false);
  std::string error;
  bool resident = false;
  bool failed = false;
};

AssetGroupService::AssetGroupService(const AssetRegistry &registry,
                                     AssetGroupRootResolver rootResolver)
    : registry_(registry), rootResolver_(std::move(rootResolver)) {}

AssetGroupService::~AssetGroupService() {
  for (auto &[handle, request] : requests_) {
    for (Work &work : request.work)
      if (work.future.valid())
        work.future.wait();
    rollback(handle, request);
  }
  for (auto &[assetId, resource] : resources_)
    if (resource.resident)
      resource.loader->unload(assetId);
  resources_.clear();
}

void AssetGroupService::registerLoader(
    std::shared_ptr<AssetResourceLoader> loader) {
  if (loader)
    loaders_.push_back(std::move(loader));
}

std::shared_ptr<AssetResourceLoader>
AssetGroupService::loaderFor(const AssetManifest &asset,
                             Diagnostics *diagnostics) const {
  std::vector<std::shared_ptr<AssetResourceLoader>> matches;
  for (const auto &loader : loaders_)
    if (loader->supports(asset))
      matches.push_back(loader);
  if (matches.size() == 1)
    return matches.front();
  report(diagnostics,
         matches.empty() ? "ASSET_HANDLER_NOT_FOUND"
                         : "ASSET_HANDLER_AMBIGUOUS",
         matches.empty() ? "No resource loader supports " + asset.id
                         : "Multiple resource loaders support " + asset.id,
         asset.manifestPath.string());
  return nullptr;
}

AssetGroupRequestHandle
AssetGroupService::prepare(const AssetGroupDescriptor &group,
                           Diagnostics *diagnostics) {
  const AssetGroupRequestHandle handle = nextRequest_++;
  Request request;
  request.group = group;
  const auto assets =
      resolveAssetGroup(group, registry_, rootResolver_, diagnostics);
  request.progress.totalAssets = assets.size();
  request.progress.stage = AssetGroupStage::Read;
  if (assets.empty()) {
    report(diagnostics, "ASSET_GROUP_EMPTY",
           "Asset group resolved no loadable asset roots.", group.id);
    request.progress.stage = AssetGroupStage::Failed;
    request.progress.error = "Asset group resolved no assets.";
  }
  for (const AssetManifest *asset : assets) {
    if (const auto found = resources_.find(asset->id);
        found != resources_.end()) {
      Resource &shared = found->second;
      if (shared.failed) {
        request.progress.stage = AssetGroupStage::Failed;
        request.progress.error = shared.error;
        continue;
      }
      shared.preparingRequests.insert(handle);
      request.heldAssets.insert(asset->id);
      if (shared.resident) {
        if (request.progress.residentBytes + shared.residentBytes >
            group.budget.residentBytes) {
          request.progress.stage = AssetGroupStage::Failed;
          request.progress.error =
              "Shared resources exceed the asset-group resident budget.";
          continue;
        }
        request.progress.residentBytes += shared.residentBytes;
        request.completedAssets.insert(asset->id);
        ++request.progress.completedAssets;
      }
      continue;
    }
    auto loader = loaderFor(*asset, diagnostics);
    if (!loader) {
      request.progress.stage = AssetGroupStage::Failed;
      request.progress.error = "A resource loader could not be selected.";
      continue;
    }
    Work work{.asset = asset, .loader = std::move(loader)};
    std::error_code sizeError;
    for (const auto &source : asset->sourcePaths) {
      const auto bytes = std::filesystem::file_size(source, sizeError);
      if (!sizeError)
        work.pendingBytes += static_cast<std::size_t>(bytes);
      sizeError.clear();
    }
    request.progress.pendingBytes += work.pendingBytes;
    auto [resource, unused] =
        resources_.emplace(asset->id, Resource{.loader = work.loader,
                                               .preparingRequests = {handle}});
    (void)unused;
    request.heldAssets.insert(asset->id);
    work.future = std::async(
        std::launch::async, [asset, loader = work.loader,
                             cancellation = resource->second.cancellation] {
          std::string error;
          auto decoded = loader->readAndDecode(*asset, *cancellation, error);
          return std::make_pair(std::move(decoded), std::move(error));
        });
    request.work.push_back(std::move(work));
  }
  auto [inserted, unused] = requests_.emplace(handle, std::move(request));
  (void)unused;
  if (inserted->second.progress.stage == AssetGroupStage::Failed) {
    rollback(handle, inserted->second);
  }
  return handle;
}

void AssetGroupService::update(double uploadBudgetMilliseconds) {
  for (auto &[handle, request] : requests_) {
    if (request.progress.stage == AssetGroupStage::Failed ||
        request.progress.stage == AssetGroupStage::Cancelled ||
        request.progress.stage == AssetGroupStage::Ready)
      continue;
    for (Work &work : request.work) {
      if (work.decoded || work.uploaded || !work.future.valid() ||
          work.future.wait_for(std::chrono::seconds(0)) !=
              std::future_status::ready)
        continue;
      auto [decoded, error] = work.future.get();
      request.progress.pendingBytes -= work.pendingBytes;
      if (!decoded) {
        if (const auto resource = resources_.find(work.asset->id);
            resource != resources_.end()) {
          resource->second.failed = true;
          resource->second.error =
              error.empty() ? "Resource decode failed." : error;
        }
        request.progress.stage = AssetGroupStage::Failed;
        request.progress.error =
            error.empty() ? "Resource decode failed." : error;
        rollback(handle, request);
        break;
      }
      request.progress.decodedBytes += decoded->decodedBytes;
      if (request.progress.decodedBytes >
          request.group.budget.maximumDecodedBytes) {
        if (const auto resource = resources_.find(work.asset->id);
            resource != resources_.end()) {
          resource->second.failed = true;
          resource->second.error = "Decoded asset memory exceeds group budget.";
        }
        request.progress.stage = AssetGroupStage::Failed;
        request.progress.error = "Decoded asset memory exceeds group budget.";
        rollback(handle, request);
        break;
      }
      work.decoded = std::move(decoded);
      request.progress.stage = AssetGroupStage::Upload;
    }
    if (request.progress.stage == AssetGroupStage::Failed)
      continue;
    const double budget = uploadBudgetMilliseconds < 0.0
                              ? request.group.budget.uploadMillisecondsPerFrame
                              : uploadBudgetMilliseconds;
    const auto started = std::chrono::steady_clock::now();
    for (Work &work : request.work) {
      if (!work.decoded || work.uploaded)
        continue;
      std::string error;
      if (!work.loader->upload(*work.asset, *work.decoded, error)) {
        if (const auto resource = resources_.find(work.asset->id);
            resource != resources_.end()) {
          resource->second.failed = true;
          resource->second.error =
              error.empty() ? "Resource upload failed." : error;
        }
        request.progress.stage = AssetGroupStage::Failed;
        request.progress.error =
            error.empty() ? "Resource upload failed." : error;
        rollback(handle, request);
        break;
      }
      request.progress.decodedBytes -= work.decoded->decodedBytes;
      Resource &resource = resources_.at(work.asset->id);
      resource.residentBytes = work.decoded->residentBytes;
      resource.resident = true;
      work.decoded.reset();
      work.uploaded = true;
      const auto elapsed = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - started)
                               .count();
      if (elapsed >= budget)
        break;
    }
    if (request.progress.stage == AssetGroupStage::Failed)
      continue;
    for (const std::string &assetId : request.heldAssets) {
      if (request.completedAssets.contains(assetId))
        continue;
      const auto resource = resources_.find(assetId);
      if (resource == resources_.end())
        continue;
      if (resource->second.failed) {
        request.progress.stage = AssetGroupStage::Failed;
        request.progress.error = resource->second.error;
        rollback(handle, request);
        break;
      }
      if (!resource->second.resident)
        continue;
      if (request.progress.residentBytes + resource->second.residentBytes >
          request.group.budget.residentBytes) {
        request.progress.stage = AssetGroupStage::Failed;
        request.progress.error = "Resident asset memory exceeds group budget.";
        rollback(handle, request);
        break;
      }
      request.progress.residentBytes += resource->second.residentBytes;
      request.completedAssets.insert(assetId);
      ++request.progress.completedAssets;
    }
    if (request.progress.completedAssets == request.progress.totalAssets &&
        request.progress.stage != AssetGroupStage::Failed) {
      request.progress.stage = AssetGroupStage::Ready;
      request.progress.fraction = 1.0;
    } else if (request.progress.totalAssets > 0) {
      const double measured =
          static_cast<double>(request.progress.completedAssets) /
          static_cast<double>(request.progress.totalAssets);
      request.progress.fraction = std::max(request.progress.fraction, measured);
    }
  }
}

AssetGroupProgress
AssetGroupService::progress(const AssetGroupRequestHandle request) const {
  const auto found = requests_.find(request);
  if (found == requests_.end())
    return {.stage = AssetGroupStage::Failed,
            .error = "Stale asset-group request handle."};
  return found->second.progress;
}

bool AssetGroupService::activate(const AssetGroupRequestHandle handle,
                                 Diagnostics *diagnostics) {
  const auto found = requests_.find(handle);
  if (found == requests_.end() ||
      found->second.progress.stage != AssetGroupStage::Ready) {
    report(diagnostics, "ASSET_GROUP_NOT_READY",
           "Only a ready asset-group request can be activated.",
           std::to_string(handle));
    return false;
  }
  Request &request = found->second;
  if (request.isActive || activeGroups_.contains(request.group.id)) {
    report(diagnostics, "ASSET_GROUP_ALREADY_ACTIVE",
           "Asset group is already active.", request.group.id);
    return false;
  }
  for (const std::string &assetId : request.heldAssets) {
    Resource &resource = resources_.at(assetId);
    resource.preparingRequests.erase(handle);
    resource.owners.insert(request.group.id);
  }
  request.isActive = true;
  activeGroups_.emplace(request.group.id, handle);
  return true;
}

void AssetGroupService::rollback(const AssetGroupRequestHandle handle,
                                 Request &request) {
  for (const std::string &assetId : request.heldAssets) {
    const auto found = resources_.find(assetId);
    if (found == resources_.end())
      continue;
    found->second.preparingRequests.erase(handle);
    if (found->second.preparingRequests.empty() &&
        found->second.owners.empty()) {
      found->second.cancellation->store(true);
      if (found->second.resident)
        found->second.loader->unload(assetId);
      resources_.erase(found);
    }
  }
  request.heldAssets.clear();
}

bool AssetGroupService::cancel(const AssetGroupRequestHandle handle) {
  const auto found = requests_.find(handle);
  if (found == requests_.end() || found->second.isActive)
    return false;
  for (Work &work : found->second.work) {
    if (work.uploaded)
      continue;
    const auto resource = resources_.find(work.asset->id);
    if (resource == resources_.end() ||
        resource->second.preparingRequests.size() <= 1)
      continue;
    const auto successor = std::ranges::find_if(
        resource->second.preparingRequests,
        [&](const AssetGroupRequestHandle candidate) {
          return candidate != handle && requests_.contains(candidate);
        });
    if (successor != resource->second.preparingRequests.end())
      requests_.at(*successor).work.push_back(std::move(work));
  }
  rollback(handle, found->second);
  found->second.progress.stage = AssetGroupStage::Cancelled;
  found->second.progress.error.clear();
  return true;
}

bool AssetGroupService::releaseGroup(const std::string_view groupId,
                                     Diagnostics *diagnostics) {
  const auto active = activeGroups_.find(std::string(groupId));
  if (active == activeGroups_.end()) {
    report(diagnostics, "ASSET_GROUP_NOT_ACTIVE",
           "Cannot release an asset group that is not active.",
           std::string(groupId));
    return false;
  }
  Request &request = requests_.at(active->second);
  for (const std::string &assetId : request.heldAssets) {
    auto resource = resources_.find(assetId);
    if (resource == resources_.end())
      continue;
    resource->second.owners.erase(std::string(groupId));
    if (resource->second.owners.empty() &&
        resource->second.preparingRequests.empty()) {
      resource->second.loader->unload(assetId);
      resources_.erase(resource);
    }
  }
  request.heldAssets.clear();
  request.isActive = false;
  activeGroups_.erase(active);
  return true;
}

bool AssetGroupService::isGroupActive(const std::string_view groupId) const {
  return activeGroups_.contains(std::string(groupId));
}

bool AssetGroupService::reload(const std::string_view assetId,
                               Diagnostics *diagnostics) {
  const AssetManifest *asset = findAsset(registry_, std::string(assetId));
  const auto resource = resources_.find(std::string(assetId));
  if (asset == nullptr || resource == resources_.end()) {
    report(diagnostics, "ASSET_RELOAD_NOT_RESIDENT",
           "Only a resident asset can be hot reloaded through its owner path.",
           std::string(assetId));
    return false;
  }
  std::atomic_bool isCancelled = false;
  std::string reloadError;
  auto decoded =
      resource->second.loader->readAndDecode(*asset, isCancelled, reloadError);
  if (decoded)
    for (const std::string &owner : resource->second.owners) {
      const auto active = activeGroups_.find(owner);
      if (active == activeGroups_.end())
        continue;
      const Request &ownerRequest = requests_.at(active->second);
      const std::size_t currentWithoutReloaded =
          ownerRequest.progress.residentBytes >= resource->second.residentBytes
              ? ownerRequest.progress.residentBytes -
                    resource->second.residentBytes
              : 0;
      if (currentWithoutReloaded + decoded->residentBytes >
          ownerRequest.group.budget.residentBytes) {
        reloadError = "Reloaded resource exceeds an owning group's budget.";
        decoded.reset();
        break;
      }
    }
  if (!decoded ||
      !resource->second.loader->upload(*asset, *decoded, reloadError)) {
    report(diagnostics, "ASSET_RELOAD_FAILED",
           reloadError.empty() ? "The resource backend rejected hot reload."
                               : reloadError,
           std::string(assetId));
    return false;
  }
  const std::size_t previousBytes = resource->second.residentBytes;
  resource->second.residentBytes = decoded->residentBytes;
  for (const std::string &owner : resource->second.owners) {
    const auto active = activeGroups_.find(owner);
    if (active == activeGroups_.end())
      continue;
    Request &ownerRequest = requests_.at(active->second);
    ownerRequest.progress.residentBytes = ownerRequest.progress.residentBytes -
                                          previousBytes +
                                          decoded->residentBytes;
  }
  return true;
}

bool AssetGroupService::restoreResources(Diagnostics *diagnostics) {
  std::set<AssetResourceLoader *> restored;
  bool success = true;
  for (const auto &[assetId, resource] : resources_) {
    (void)assetId;
    if (!resource.resident || !restored.insert(resource.loader.get()).second)
      continue;
    std::string error;
    if (!resource.loader->restore(error)) {
      report(diagnostics, "ASSET_BACKEND_RESTORE_FAILED",
             error.empty() ? "A resource backend could not restore assets."
                           : error,
             std::string(resource.loader->backendName()));
      success = false;
    }
  }
  return success;
}

void AssetGroupService::cancelPending() {
  std::vector<AssetGroupRequestHandle> pending;
  for (const auto &[handle, request] : requests_)
    if (!request.isActive &&
        request.progress.stage != AssetGroupStage::Cancelled &&
        request.progress.stage != AssetGroupStage::Failed)
      pending.push_back(handle);
  for (const AssetGroupRequestHandle handle : pending)
    (void)cancel(handle);
}

AssetMemoryReport AssetGroupService::memoryReport() const {
  AssetMemoryReport report;
  for (const auto &[unused, request] : requests_) {
    (void)unused;
    report.decodedBytes += request.progress.decodedBytes;
    report.pendingBytes += request.progress.pendingBytes;
  }
  for (const auto &[assetId, resource] : resources_) {
    report.residentBytes += resource.residentBytes;
    report.assets.push_back(
        {.assetId = assetId,
         .backend = std::string(resource.loader->backendName()),
         .residentBytes = resource.residentBytes,
         .owners = resource.owners});
  }
  return report;
}

} // namespace demi::assets
