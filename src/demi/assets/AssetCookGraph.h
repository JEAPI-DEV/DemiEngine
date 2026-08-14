#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace demi::assets {

struct AssetCookNode {
  std::string assetId;
  std::string importer;
  int importerVersion = 1;
  int settingsSchemaVersion = 1;
  std::string normalizedSettings = "{}";
  std::vector<std::string> sourceHashes;
  std::vector<std::string> dependencies;
  std::string platform;
  std::string profile = "default";
  std::string sourcePackage;
  std::string packageContentHash;
};

struct AssetCookDecision {
  std::string assetId;
  std::string key;
  bool isCacheHit = false;
  std::string reason;
  std::vector<std::filesystem::path> outputs;
};

class AssetCookGraph {
public:
  [[nodiscard]] bool addNode(AssetCookNode node,
                             Diagnostics *diagnostics = nullptr);
  [[nodiscard]] bool finalize(Diagnostics *diagnostics = nullptr);
  [[nodiscard]] std::optional<std::string> key(std::string_view assetId) const;
  [[nodiscard]] std::set<std::string>
  reverseReachable(const std::set<std::string> &changed) const;
  [[nodiscard]] const std::map<std::string, AssetCookNode> &nodes() const;

private:
  [[nodiscard]] std::optional<std::string>
  calculateKey(const std::string &assetId, std::set<std::string> &visiting,
               Diagnostics *diagnostics);

  std::map<std::string, AssetCookNode> nodes_;
  std::map<std::string, std::string> keys_;
  std::map<std::string, std::set<std::string>> reverseEdges_;
};

class AssetCookCache {
public:
  explicit AssetCookCache(std::filesystem::path directory);

  [[nodiscard]] AssetCookDecision inspect(std::string assetId,
                                          std::string key) const;
  [[nodiscard]] Diagnostics store(const AssetCookDecision &decision,
                                  std::string sourcePackage = {},
                                  std::string contentHash = {}) const;

private:
  std::filesystem::path directory_;
};

} // namespace demi::assets
