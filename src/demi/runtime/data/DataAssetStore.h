#pragma once

#include "demi/assets/DataAsset.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime {

struct DataAssetSnapshot {
  std::string id;
  std::string contentType;
  std::vector<std::string> tags;
  std::string sourceHash;
  std::uint64_t revision = 0;
  std::shared_ptr<const assets::DataDocument> document;
};

struct DataAssetQuery {
  std::string contentType;
  std::vector<std::string> tags;
};

struct DataAssetReloadEvent {
  std::string id;
  std::uint64_t oldRevision = 0;
  std::uint64_t newRevision = 0;
  std::vector<std::string> affectedDependents;
};

class DataAssetStore {
public:
  [[nodiscard]] Diagnostics replace(const AssetRegistry &registry);
  [[nodiscard]] std::shared_ptr<const DataAssetSnapshot>
  load(std::string_view id) const;
  [[nodiscard]] std::vector<std::shared_ptr<const DataAssetSnapshot>>
  query(DataAssetQuery query) const;
  [[nodiscard]] std::uint64_t revision(std::string_view id) const;

  [[nodiscard]] bool acquire(std::string owner,
                             std::span<const std::string> assetIds,
                             std::string &error);
  void release(std::string_view owner);
  [[nodiscard]] std::size_t referenceCount(std::string_view id) const;
  [[nodiscard]] const std::vector<DataAssetReloadEvent> &reloadEvents() const;

private:
  std::map<std::string, std::shared_ptr<const DataAssetSnapshot>, std::less<>>
      snapshots_;
  std::unordered_map<std::string, std::unordered_set<std::string>> owners_;
  std::vector<DataAssetReloadEvent> reloadEvents_;
};

} // namespace demi::runtime
