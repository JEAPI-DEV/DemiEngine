#include "editor/EditorInspectorModel.h"

#include "editor/EditorSceneJson.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace demi::editor {
namespace {

using runtime::ComponentDomain;
using runtime::scene_loading::ComponentDescriptor;

bool hasDomain(const nlohmann::json &entity, const ComponentDomain domain) {
  const auto components = entity.find("components");
  if (components == entity.end() || !components->is_object())
    return false;
  return std::ranges::any_of(components->items(), [domain](const auto &item) {
    const ComponentDescriptor *descriptor =
        runtime::scene_loading::findComponentDescriptor(item.key());
    return descriptor != nullptr && descriptor->domain == domain;
  });
}

bool containsCaseInsensitive(const std::string_view value,
                             const std::string_view query) {
  if (query.empty())
    return true;
  return std::ranges::search(
             value, query,
             [](const char left, const char right) {
               return std::tolower(static_cast<unsigned char>(left)) ==
                      std::tolower(static_cast<unsigned char>(right));
             })
             .begin() != value.end();
}

std::string prefabReference(const std::filesystem::path &projectDirectory,
                            const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::path relative =
      std::filesystem::relative(path, projectDirectory / "prefabs", error);
  if (error || relative.empty() || relative.string().starts_with(".."))
    return {};
  std::string value = relative.generic_string();
  constexpr std::string_view Suffix = ".prefab.json";
  if (!value.ends_with(Suffix))
    return {};
  value.resize(value.size() - Suffix.size());
  return "prefab://" + value;
}

} // namespace

bool editorComponentMatchesSearch(const std::string_view query,
                                  const std::string_view internalName,
                                  const std::string_view displayName,
                                  const std::string_view category) {
  return containsCaseInsensitive(internalName, query) ||
         containsCaseInsensitive(displayName, query) ||
         containsCaseInsensitive(category, query);
}

std::vector<EditorReferenceChoice>
editorReferenceChoices(const runtime::ComponentReferenceKind kind,
                       const std::filesystem::path &projectDirectory,
                       const std::filesystem::path &scenePath,
                       const nlohmann::json &scene,
                       const std::span<const std::filesystem::path> sources) {
  std::vector<EditorReferenceChoice> choices;
  if (kind == runtime::ComponentReferenceKind::Asset) {
    for (const AssetManifest &asset :
         loadAssetRegistry(projectDirectory).assets)
      choices.push_back({.id = asset.id, .label = asset.id});
  } else if (kind == runtime::ComponentReferenceKind::Entity) {
    const nlohmann::json *entities = entitiesArray(scene);
    if (entities != nullptr) {
      for (const nlohmann::json &entity : *entities) {
        if (!entity.is_object())
          continue;
        const std::string id = entity.value("id", std::string{});
        if (!id.empty())
          choices.push_back(
              {.id = id, .label = entity.value("name", id) + " (" + id + ")"});
      }
    }
  } else if (kind == runtime::ComponentReferenceKind::Prefab) {
    for (const std::filesystem::path &source : sources) {
      if (!isPrefabFile(source))
        continue;
      const std::string id = prefabReference(projectDirectory, source);
      const auto resolved =
          runtime::composition::resolvePrefabReference(scenePath, id);
      if (!id.empty() && resolved.has_value() &&
          runtime::composition::inspectPrefab(*resolved).document.has_value())
        choices.push_back({.id = id, .label = id});
    }
  }
  std::ranges::sort(choices, {}, &EditorReferenceChoice::id);
  choices.erase(
      std::ranges::unique(choices, {}, &EditorReferenceChoice::id).begin(),
      choices.end());
  return choices;
}

std::vector<EditorComponentChoice>
editorComponentChoices(const nlohmann::json &entity) {
  const bool has2D = hasDomain(entity, ComponentDomain::TwoDimensional);
  const bool has3D = hasDomain(entity, ComponentDomain::ThreeDimensional);
  std::vector<EditorComponentChoice> choices;
  const auto components = entity.find("components");
  for (const ComponentDescriptor &descriptor :
       runtime::scene_loading::componentDescriptors()) {
    if (components != entity.end() && components->is_object() &&
        components->contains(descriptor.name))
      continue;
    EditorComponentChoice choice{.descriptor = &descriptor};
    if ((descriptor.domain == ComponentDomain::TwoDimensional && has3D) ||
        (descriptor.domain == ComponentDomain::ThreeDimensional && has2D)) {
      choice.compatible = false;
      choice.incompatibility =
          descriptor.domain == ComponentDomain::TwoDimensional
              ? "This entity already contains 3D components."
              : "This entity already contains 2D components.";
    }
    choices.push_back(std::move(choice));
  }
  std::ranges::sort(choices, [](const auto &left, const auto &right) {
    return std::tie(left.descriptor->editor.category,
                    left.descriptor->editor.displayName) <
           std::tie(right.descriptor->editor.category,
                    right.descriptor->editor.displayName);
  });
  return choices;
}

std::vector<EditorCommonField>
editorCommonFields(const nlohmann::json &scene,
                   const std::span<const std::string> entityIds) {
  std::vector<EditorCommonField> common;
  if (entityIds.empty())
    return common;
  const nlohmann::json *first = findEntity(scene, entityIds.front());
  if (first == nullptr)
    return common;
  const auto components = first->find("components");
  if (components == first->end() || !components->is_object())
    return common;

  for (const auto &[componentName, component] : components->items()) {
    const ComponentDescriptor *descriptor =
        runtime::scene_loading::findComponentDescriptor(componentName);
    if (descriptor == nullptr || !component.is_object())
      continue;
    for (const runtime::ComponentFieldDescriptor &field : descriptor->fields) {
      if (!field.editorVisible ||
          runtime::scene_loading::componentFieldEditorReadOnly(field) ||
          !component.contains(field.name))
        continue;
      EditorCommonField candidate{.component = descriptor,
                                  .field = &field,
                                  .value = component.at(field.name)};
      for (const std::string &entityId : entityIds) {
        const nlohmann::json *other = findEntity(scene, entityId);
        const nlohmann::json *otherComponent =
            other == nullptr ? nullptr : findComponent(*other, componentName);
        if (otherComponent == nullptr || !otherComponent->is_object() ||
            !otherComponent->contains(field.name)) {
          candidate.targets.clear();
          break;
        }
        candidate.targets.push_back({.entityId = entityId,
                                     .component = componentName,
                                     .field = std::string(field.name)});
        candidate.mixed = candidate.mixed ||
                          otherComponent->at(field.name) != candidate.value;
      }
      if (candidate.targets.size() == entityIds.size())
        common.push_back(std::move(candidate));
    }
  }
  return common;
}

} // namespace demi::editor
