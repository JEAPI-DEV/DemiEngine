#pragma once

#include "demi/assets/AssetRegistry.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace demi::assets {

struct AssetGroupBudget {
  std::size_t residentBytes = 256U * 1024U * 1024U;
  double uploadMillisecondsPerFrame = 3.0;
  std::size_t maximumDecodedBytes = 64U * 1024U * 1024U;
};

struct AssetGroupDescriptor {
  int formatVersion = 1;
  std::string id;
  std::vector<std::string> roots;
  AssetGroupBudget budget;
  std::filesystem::path sourcePath;
};

using AssetGroupRootResolver =
    std::function<std::vector<std::string>(std::string_view, Diagnostics *)>;

[[nodiscard]] std::optional<AssetGroupDescriptor>
loadAssetGroup(const std::filesystem::path &path,
               Diagnostics *diagnostics = nullptr);
[[nodiscard]] std::vector<const AssetManifest *>
resolveAssetGroup(const AssetGroupDescriptor &group,
                  const AssetRegistry &registry,
                  const AssetGroupRootResolver &rootResolver = {},
                  Diagnostics *diagnostics = nullptr);

enum class AssetGroupStage {
  Resolve,
  Read,
  Decode,
  Upload,
  Ready,
  Failed,
  Cancelled
};

[[nodiscard]] std::string_view assetGroupStageName(AssetGroupStage stage);

using AssetGroupRequestHandle = std::uint64_t;

struct DecodedAsset {
  std::shared_ptr<void> payload;
  std::size_t decodedBytes = 0;
  std::size_t residentBytes = 0;
};

class AssetResourceLoader {
public:
  virtual ~AssetResourceLoader() = default;
  [[nodiscard]] virtual bool supports(const AssetManifest &asset) const = 0;
  [[nodiscard]] virtual std::optional<DecodedAsset>
  readAndDecode(const AssetManifest &asset, const std::atomic_bool &isCancelled,
                std::string &error) = 0;
  [[nodiscard]] virtual bool upload(const AssetManifest &asset,
                                    const DecodedAsset &decoded,
                                    std::string &error) = 0;
  virtual void unload(std::string_view assetId) = 0;
  [[nodiscard]] virtual std::string_view backendName() const = 0;
  // Recreates native objects from the loader's resident stable-ID set after a
  // graphics/audio device lifecycle event. Existing resources remain valid if
  // restoration fails.
  [[nodiscard]] virtual bool restore(std::string &error) {
    error.clear();
    return true;
  }
};

struct AssetGroupProgress {
  AssetGroupStage stage = AssetGroupStage::Resolve;
  double fraction = 0.0;
  std::size_t completedAssets = 0;
  std::size_t totalAssets = 0;
  std::size_t pendingBytes = 0;
  std::size_t decodedBytes = 0;
  std::size_t residentBytes = 0;
  std::string error;
};

struct AssetMemoryEntry {
  std::string assetId;
  std::string backend;
  std::size_t residentBytes = 0;
  std::set<std::string> owners;
};

struct AssetMemoryReport {
  std::size_t pendingBytes = 0;
  std::size_t decodedBytes = 0;
  std::size_t residentBytes = 0;
  std::vector<AssetMemoryEntry> assets;
};

class AssetGroupService {
public:
  explicit AssetGroupService(const AssetRegistry &registry,
                             AssetGroupRootResolver rootResolver = {});
  ~AssetGroupService();

  AssetGroupService(const AssetGroupService &) = delete;
  AssetGroupService &operator=(const AssetGroupService &) = delete;

  void registerLoader(std::shared_ptr<AssetResourceLoader> loader);
  [[nodiscard]] AssetGroupRequestHandle
  prepare(const AssetGroupDescriptor &group,
          Diagnostics *diagnostics = nullptr);
  void update(double uploadBudgetMilliseconds = -1.0);
  [[nodiscard]] AssetGroupProgress
  progress(AssetGroupRequestHandle request) const;
  [[nodiscard]] bool activate(AssetGroupRequestHandle request,
                              Diagnostics *diagnostics = nullptr);
  [[nodiscard]] bool cancel(AssetGroupRequestHandle request);
  [[nodiscard]] bool releaseGroup(std::string_view groupId,
                                  Diagnostics *diagnostics = nullptr);
  [[nodiscard]] bool isGroupActive(std::string_view groupId) const;
  // Reuses the selected loader and existing ownership entry. Loader uploads
  // for an existing stable ID must replace atomically or leave it unchanged.
  [[nodiscard]] bool reload(std::string_view assetId,
                            Diagnostics *diagnostics = nullptr);
  [[nodiscard]] bool restoreResources(Diagnostics *diagnostics = nullptr);
  void cancelPending();
  [[nodiscard]] AssetMemoryReport memoryReport() const;

private:
  struct Work;
  struct Request;
  struct Resource;

  void rollback(AssetGroupRequestHandle handle, Request &request);
  [[nodiscard]] std::shared_ptr<AssetResourceLoader>
  loaderFor(const AssetManifest &asset, Diagnostics *diagnostics) const;

  const AssetRegistry &registry_;
  AssetGroupRootResolver rootResolver_;
  std::vector<std::shared_ptr<AssetResourceLoader>> loaders_;
  std::map<AssetGroupRequestHandle, Request> requests_;
  std::map<std::string, Resource> resources_;
  std::map<std::string, AssetGroupRequestHandle> activeGroups_;
  AssetGroupRequestHandle nextRequest_ = 1;
};

} // namespace demi::assets
