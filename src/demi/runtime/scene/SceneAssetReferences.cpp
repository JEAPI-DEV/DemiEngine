#include "demi/runtime/scene/SceneAssetReferences.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/ComponentRegistry.h"

#include <algorithm>
#include <set>

namespace demi::runtime {
namespace {

bool fieldMayReferenceAssets(const ComponentFieldDescriptor &field) {
  if (field.referenceKind == ComponentReferenceKind::Asset) {
    return true;
  }
  if (field.assetReferenceFree ||
      field.referenceKind == ComponentReferenceKind::Entity ||
      field.referenceKind == ComponentReferenceKind::Prefab) {
    return false;
  }
  // Open strings and structured payloads remain conservative. This includes
  // script properties, gameplay data, nested resource tables and custom values.
  switch (field.type) {
  case ComponentFieldType::Object:
  case ComponentFieldType::StringArray:
    return true;
  case ComponentFieldType::String:
    return true;
  default:
    return false;
  }
}

bool componentMayReferenceAssets(std::string_view name) {
  const auto *descriptor = scene_loading::findComponentDescriptor(name);
  if (descriptor == nullptr) {
    return true;
  }
  return std::ranges::any_of(descriptor->fields, fieldMayReferenceAssets);
}

void collectPayload(std::string_view name, std::string_view payload,
                    std::set<std::string> &references,
                    SceneAssetReferenceStatistics *statistics) {
  if (!componentMayReferenceAssets(name)) {
    if (statistics != nullptr) {
      ++statistics->skippedComponents;
    }
    return;
  }
  if (statistics != nullptr) {
    ++statistics->parsedPayloads;
    statistics->parsedPayloadBytes += payload.size();
  }
  const auto values = nlohmann::json::parse(payload, nullptr, false);
  if (!values.is_object()) {
    return;
  }
  const auto assets = extractAssetReferencesFromJson(values);
  references.insert(assets.begin(), assets.end());
}

} // namespace

std::vector<std::string>
collectEntityAssetReferences(const std::span<const Entity> entities,
                             const std::optional<std::string_view> sceneOwner,
                             SceneAssetReferenceStatistics *statistics) {
  std::set<std::string> references;
  for (const Entity &entity : entities) {
    if (sceneOwner && entity.sceneOwner != *sceneOwner) {
      continue;
    }
    // Both authored and patched/runtime payloads can own resource references.
    // Their reflected name is checked before reading or copying the JSON.
    for (const auto &authored : entity.authoredComponents) {
      if (authored != nullptr) {
        if (!componentMayReferenceAssets(authored->name())) {
          if (statistics != nullptr) {
            ++statistics->skippedComponents;
          }
          continue;
        }
        collectPayload(authored->name(), authored->json(), references,
                       statistics);
      }
    }
    for (const auto &[name, serialized] : entity.serializedComponents) {
      collectPayload(name, serialized, references, statistics);
    }
  }
  return {references.begin(), references.end()};
}

std::vector<std::string>
collectSceneAssetReferences(const World &world,
                            const std::string_view sceneOwner,
                            SceneAssetReferenceStatistics *statistics) {
  const auto entityAssets = collectEntityAssetReferences(
      world.entities,
      sceneOwner.empty() ? std::nullopt
                         : std::optional<std::string_view>(sceneOwner),
      statistics);
  std::set<std::string> references(entityAssets.begin(), entityAssets.end());
  for (const ui::UiNode &node : world.ui.nodes) {
    if (!sceneOwner.empty() && node.sceneOwner != sceneOwner) {
      continue;
    }
    for (const std::string *reference :
         {&node.font, &node.texture, &node.animation}) {
      if (reference->starts_with("asset://")) {
        references.insert(*reference);
      }
    }
  }
  return {references.begin(), references.end()};
}

} // namespace demi::runtime
