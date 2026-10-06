#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <string>
#include <string_view>
#include <vector>

namespace demi {
struct AssetRegistry;
}

namespace demi::assets {
class DataDocument;

struct DataAssetContentResult {
  Diagnostics diagnostics;
  // Only asset:// IDs belong in an AssetManifest dependency graph.
  std::vector<std::string> dependencies;
};

// Inspect native DataAsset content using the same parsers as runtime loading.
// Unrecognized content types are ordinary data; their string values do not
// imply dependencies. Schema-declared references are handled by DataAsset.
[[nodiscard]] DataAssetContentResult inspectDataAssetContent(
    std::string_view contentType, const DataDocument &document,
    const AssetRegistry &registry, std::string_view assetId);

} // namespace demi::assets
