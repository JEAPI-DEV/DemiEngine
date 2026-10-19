#pragma once

#include "demi/assets/AssetGroup.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace demi::runtime {

using ApplyAssetRegistry =
    std::function<bool(const AssetRegistry &, std::string &error)>;

enum class AssetPublicationStage { Pending, Ready, Failed };

struct RegistryAssetBackend {
  std::string name;
  std::set<std::string> assetTypes;
  ApplyAssetRegistry apply;
  std::function<AssetPublicationStage(const AssetRegistry &, double,
                                      std::string &)>
      applyStep = {};
  std::function<void()> cancelApply = {};
  std::function<double()> applyProgress = {};
};

// Adapts a backend that can atomically replace an asset-registry snapshot to
// AssetGroupService's per-resource ownership contract. The loader owns only
// stable IDs; renderer/audio implementations continue to own native objects.
class RegistryAssetResourceLoader final : public assets::AssetResourceLoader {
public:
  RegistryAssetResourceLoader(const AssetRegistry &source,
                              RegistryAssetBackend backend);

  [[nodiscard]] bool supports(const AssetManifest &asset) const override;
  [[nodiscard]] std::optional<assets::DecodedAsset>
  readAndDecode(const AssetManifest &asset, const std::atomic_bool &isCancelled,
                std::string &error) override;
  [[nodiscard]] bool upload(const AssetManifest &asset,
                            const assets::DecodedAsset &decoded,
                            std::string &error) override;
  [[nodiscard]] std::size_t
  uploadBatch(std::span<const assets::AssetResourceUpload> uploads,
              double budgetMilliseconds, std::string &error) override;
  void cancelUploadBatch(std::span<const std::string> assetIds) override;
  [[nodiscard]] bool hasPendingUploadBatch() const override {
    return !pendingUploadIds_.empty();
  }
  [[nodiscard]] double
  pendingUploadProgress(std::span<const std::string> assetIds) const override;
  void unload(std::string_view assetId) override;
  [[nodiscard]] bool unloadBatch(std::span<const std::string> assetIds,
                                 std::string &error) override;
  [[nodiscard]] std::string_view backendName() const override;
  [[nodiscard]] bool restore(std::string &error) override;

private:
  [[nodiscard]] AssetRegistry snapshot(const std::set<std::string> &roots,
                                       std::string &error) const;

  const AssetRegistry &source_;
  RegistryAssetBackend backend_;
  std::set<std::string> residentIds_;
  std::vector<std::string> pendingUploadIds_;
  std::set<std::string> pendingResidentIds_;
};

} // namespace demi::runtime
