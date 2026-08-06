#include "demi/runtime/data/DataAssetStore.h"

#include <algorithm>

namespace demi::runtime {

Diagnostics DataAssetStore::replace(const AssetRegistry &registry) {
  Diagnostics diagnostics = assets::validateDataAssets(registry);
  if (hasErrors(diagnostics))
    return diagnostics;

  decltype(snapshots_) candidate;
  std::vector<DataAssetReloadEvent> events;
  for (const AssetManifest &manifest : registry.assets) {
    if (manifest.type != "DataAsset")
      continue;
    auto loaded = assets::loadDataAsset(manifest, {}, &diagnostics);
    if (!loaded)
      continue;
    const auto previous = snapshots_.find(manifest.id);
    if (previous != snapshots_.end() &&
        previous->second->contentType != loaded->metadata.contentType) {
      diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "DATA_CONTENT_TYPE_CHANGED",
           .message = "Reload cannot change a data asset's declared content "
                      "type from " +
                      previous->second->contentType + " to " +
                      loaded->metadata.contentType + ".",
           .path = manifest.manifestPath.string(),
           .suggestion = "Create a new stable asset ID or restore the previous "
                         "content_type."});
      continue;
    }
    const bool unchanged =
        previous != snapshots_.end() && previous->second->document &&
        previous->second->contentType == loaded->metadata.contentType &&
        previous->second->tags == loaded->metadata.tags &&
        previous->second->sourceHash == manifest.sourceHash;
    if (unchanged) {
      candidate.emplace(manifest.id, previous->second);
      continue;
    }
    const std::uint64_t oldRevision =
        previous == snapshots_.end() ? 0 : previous->second->revision;
    const std::uint64_t newRevision = oldRevision + 1;
    candidate.emplace(
        manifest.id,
        std::make_shared<const DataAssetSnapshot>(
            DataAssetSnapshot{.id = manifest.id,
                              .contentType = loaded->metadata.contentType,
                              .tags = loaded->metadata.tags,
                              .sourceHash = manifest.sourceHash,
                              .revision = newRevision,
                              .document = loaded->document}));
    if (oldRevision != 0) {
      std::vector<std::string> dependents;
      for (const AssetManifest &other : registry.assets)
        if (std::ranges::find(other.dependencies, manifest.id) !=
            other.dependencies.end())
          dependents.push_back(other.id);
      std::ranges::sort(dependents);
      events.push_back({.id = manifest.id,
                        .oldRevision = oldRevision,
                        .newRevision = newRevision,
                        .affectedDependents = std::move(dependents)});
    }
  }
  if (hasErrors(diagnostics))
    return diagnostics;

  for (const auto &[owner, ids] : owners_)
    for (const std::string &id : ids)
      if (!candidate.contains(id)) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "DATA_ASSET_STILL_REFERENCED",
                               .message = "Reload removes a referenced data "
                                          "asset: " +
                                          id,
                               .path = owner,
                               .suggestion =
                                   "Release the owning resource group before "
                                   "removing the data asset."});
      }
  if (hasErrors(diagnostics))
    return diagnostics;
  snapshots_ = std::move(candidate);
  reloadEvents_ = std::move(events);
  return diagnostics;
}

std::shared_ptr<const DataAssetSnapshot>
DataAssetStore::load(const std::string_view id) const {
  const auto found = snapshots_.find(id);
  return found == snapshots_.end() ? nullptr : found->second;
}

std::vector<std::shared_ptr<const DataAssetSnapshot>>
DataAssetStore::query(DataAssetQuery query) const {
  std::ranges::sort(query.tags);
  query.tags.erase(std::unique(query.tags.begin(), query.tags.end()),
                   query.tags.end());
  std::vector<std::shared_ptr<const DataAssetSnapshot>> result;
  for (const auto &[id, snapshot] : snapshots_) {
    (void)id;
    if (!query.contentType.empty() &&
        snapshot->contentType != query.contentType)
      continue;
    if (!std::ranges::all_of(query.tags, [&](const std::string &tag) {
          return std::ranges::binary_search(snapshot->tags, tag);
        }))
      continue;
    result.push_back(snapshot);
  }
  return result;
}

std::uint64_t DataAssetStore::revision(const std::string_view id) const {
  const auto snapshot = load(id);
  return snapshot ? snapshot->revision : 0;
}

bool DataAssetStore::acquire(std::string owner,
                             const std::span<const std::string> assetIds,
                             std::string &error) {
  std::unordered_set<std::string> candidate;
  for (const std::string &id : assetIds) {
    if (!snapshots_.contains(id)) {
      error = "Data asset not found: " + id;
      return false;
    }
    candidate.insert(id);
  }
  owners_[std::move(owner)] = std::move(candidate);
  error.clear();
  return true;
}

void DataAssetStore::release(const std::string_view owner) {
  owners_.erase(std::string(owner));
}

std::size_t DataAssetStore::referenceCount(const std::string_view id) const {
  return static_cast<std::size_t>(
      std::ranges::count_if(owners_, [&](const auto &entry) {
        return entry.second.contains(std::string(id));
      }));
}

const std::vector<DataAssetReloadEvent> &DataAssetStore::reloadEvents() const {
  return reloadEvents_;
}

} // namespace demi::runtime
