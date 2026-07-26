#include "demi/runtime/scene/ResourceLifetimeRegistry.h"

#include "demi/runtime/scene/ComponentRegistry.h"

#include <nlohmann/json.hpp>

namespace demi::runtime {

void ResourceLifetimeRegistry::capture(const std::string owner,
                                       const std::span<const Entity> entities) {
  auto &assets = groups_[owner];
  assets.clear();
  for (const Entity &entity : entities) {
    if (entity.sceneOwner != owner)
      continue;
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
}

void ResourceLifetimeRegistry::release(const std::string_view owner) {
  groups_.erase(std::string(owner));
}

bool ResourceLifetimeRegistry::owns(const std::string_view owner,
                                    const std::string_view assetId) const {
  const auto group = groups_.find(std::string(owner));
  return group != groups_.end() &&
         group->second.contains(std::string(assetId));
}

std::size_t ResourceLifetimeRegistry::groupCount() const {
  return groups_.size();
}

} // namespace demi::runtime
