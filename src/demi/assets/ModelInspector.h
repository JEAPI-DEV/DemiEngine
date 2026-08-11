#pragma once

#include "demi/assets/AssetRegistry.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>

namespace demi::assets {

struct ModelInspectionRequest {
  const AssetManifest *asset = nullptr;
  std::set<std::string> sections;
};

struct ModelInspectionReport {
  nlohmann::json document;
  Diagnostics diagnostics;
};

// Read-only inspection shared by the CLI and future editor panels. It parses
// source metadata and normalized engine-space geometry without a GPU device.
[[nodiscard]] ModelInspectionReport
inspectModel(const ModelInspectionRequest &request);

} // namespace demi::assets
