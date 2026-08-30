#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataDocument.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace demi::assets {

struct DataAssetMetadata {
  std::string schema;
  std::string contentType;
  std::vector<std::string> tags;
};

struct LoadedDataAsset {
  const AssetManifest *manifest = nullptr;
  DataAssetMetadata metadata;
  std::shared_ptr<const DataDocument> document;
};

[[nodiscard]] std::optional<DataAssetMetadata>
dataAssetMetadata(const AssetManifest &manifest,
                  Diagnostics *diagnostics = nullptr);
[[nodiscard]] std::optional<LoadedDataAsset>
loadDataAsset(const AssetManifest &manifest,
              const DataDocumentLimits &limits = {},
              Diagnostics *diagnostics = nullptr);
[[nodiscard]] Diagnostics
validateDataAssets(const AssetRegistry &registry,
                   const DataDocumentLimits &limits = {});
[[nodiscard]] Diagnostics
validateDataAssetDocument(const AssetManifest &manifest,
                          const DataDocument &document,
                          const AssetRegistry &registry);

} // namespace demi::assets
