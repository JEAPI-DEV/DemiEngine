#include "demi/runtime/scene/ResourceLifetimeRegistry.h"

#include "demi/runtime/scene/ComponentRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace demi::runtime {

ResourceLifetimeRegistry::ResourceLifetimeRegistry(Acquire acquire,
                                                   Release release)
    : acquire_(std::move(acquire)), release_(std::move(release)) {}

std::unordered_set<std::string> ResourceLifetimeRegistry::collect(
    const std::string_view owner,
    const std::span<const Entity> entities) const {
  std::unordered_set<std::string> assets;
  for (const Entity &entity : entities) {
    if (entity.sceneOwner != owner)
      continue;
    // Authored component JSON is the source of truth for scene-loaded
    // entities; serializedComponents is only populated for prefab-instantiated
    // and runtime-created entities (see RuntimePrefabService,
    // RuntimeObjectModel). serializedComponents is the fallback.
    for (const std::shared_ptr<const Component> &authored :
         entity.authoredComponents) {
      if (authored == nullptr)
        continue;
      const auto references = extractAssetReferences(std::string(authored->json()));
      assets.insert(references.begin(), references.end());
    }
    for (const auto &descriptor :
         scene_loading::componentDescriptors()) {
      const auto serialized =
          entity.serializedComponents.find(std::string(descriptor.name));
      if (serialized == entity.serializedComponents.end())
        continue;
      const nlohmann::json values =
          nlohmann::json::parse(serialized->second, nullptr, false);
      if (!values.is_object())
        continue;
      for (const ComponentFieldDescriptor &field : descriptor.fields) {
        if (field.referenceKind != ComponentReferenceKind::Asset ||
            !values.contains(field.name) ||
            !values[field.name].is_string())
          continue;
        const std::string asset = values[field.name].get<std::string>();
        if (asset.starts_with("asset://"))
          assets.insert(asset);
      }
    }
  }
  return assets;
}

std::size_t ResourceLifetimeRegistry::referenceCountExcluding(
    const std::string_view assetId,
    const std::string_view excludedOwner) const {
  return static_cast<std::size_t>(
      std::ranges::count_if(groups_, [&](const auto &group) {
        return group.first != excludedOwner &&
               group.second.contains(std::string(assetId));
      }));
}

void ResourceLifetimeRegistry::capture(
    std::string owner, const std::span<const Entity> entities) {
  std::string error;
  (void)tryCapture(std::move(owner), entities, error);
}

bool ResourceLifetimeRegistry::tryCapture(
    std::string owner, const std::span<const Entity> entities,
    std::string &error) {
  auto assets = collect(owner, entities);
  std::vector<std::string> acquisitions;
  for (const std::string &asset : assets)
    if (referenceCountExcluding(asset, owner) == 0 &&
        (!groups_.contains(owner) || !groups_.at(owner).contains(asset)))
      acquisitions.push_back(asset);
  std::ranges::sort(acquisitions);

  std::vector<std::string> acquired;
  for (const std::string &asset : acquisitions) {
    if (acquire_ && !acquire_(asset, error)) {
      if (release_)
        for (auto rollback = acquired.rbegin(); rollback != acquired.rend();
             ++rollback)
          release_(*rollback);
      return false;
    }
    acquired.push_back(asset);
  }

  const auto previous = groups_.find(owner);
  if (previous != groups_.end() && release_)
    for (const std::string &asset : previous->second)
      if (!assets.contains(asset) &&
          referenceCountExcluding(asset, owner) == 0)
        release_(asset);
  groups_[std::move(owner)] = std::move(assets);
  error.clear();
  return true;
}

void ResourceLifetimeRegistry::release(const std::string_view owner) {
  const auto found = groups_.find(std::string(owner));
  if (found == groups_.end())
    return;
  if (release_)
    for (const std::string &asset : found->second)
      if (referenceCount(asset) == 1)
        release_(asset);
  groups_.erase(std::string(owner));
}

void ResourceLifetimeRegistry::clear() {
  while (!groups_.empty())
    release(groups_.begin()->first);
}

bool ResourceLifetimeRegistry::owns(const std::string_view owner,
                                    const std::string_view assetId) const {
  const auto group = groups_.find(std::string(owner));
  return group != groups_.end() &&
         group->second.contains(std::string(assetId));
}

bool ResourceLifetimeRegistry::isReferenced(
    const std::string_view assetId) const {
  return referenceCount(assetId) != 0;
}

std::size_t
ResourceLifetimeRegistry::referenceCount(const std::string_view assetId) const {
  return static_cast<std::size_t>(
      std::ranges::count_if(groups_, [&](const auto &group) {
        return group.second.contains(std::string(assetId));
      }));
}

std::size_t ResourceLifetimeRegistry::groupCount() const {
  return groups_.size();
}

} // namespace demi::runtime
