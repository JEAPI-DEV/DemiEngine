#include "editor/EditorScenePreview.h"
#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

namespace demi::editor {

bool EditorWorkspace::addComponent(const std::string_view id,
                                   const std::string_view componentName,
                                   std::string &error) {
  const auto *descriptor =
      runtime::scene_loading::findComponentDescriptor(componentName);
  if (descriptor == nullptr) {
    error = "Unknown component: " + std::string(componentName);
    return false;
  }
  auto initialValues = runtime::scene_loading::componentDefaults(*descriptor);
  for (const auto &field : descriptor->fields) {
    if (!field.required)
      initialValues.erase(std::string(field.name));
  }
  return addComponent(id, componentName, std::move(initialValues), error);
}

bool EditorWorkspace::addComponent(const std::string_view id,
                                   const std::string_view componentName,
                                   nlohmann::json initialValues,
                                   std::string &error) {
  const auto target = resolveSceneTarget(
      {.entityId = std::string(id), .component = std::string(componentName)});
  if (target.isPrefabOverride()) {
    const auto *entity = runtime::findEntity(project_->world, std::string(id));
    if (entity == nullptr) {
      error = "The entity no longer exists.";
      return false;
    }
    const auto effective = editorPreviewEntityJson(*entity);
    if (effective.value("components", nlohmann::json::object())
            .contains(componentName)) {
      error = "The entity already has a " + std::string(componentName) +
              " component.";
      return false;
    }
    if (!initialValues.is_object()) {
      error = "Initial component values must be an object.";
      return false;
    }
    return mutateAndRebuild(
        [target, initialValues = std::move(initialValues)](
            EditorSceneDocument &document, std::string &mutationError) {
          return document.setPrefabComponentOverride(target, initialValues,
                                                     mutationError);
        },
        error);
  }
  return mutateAndRebuild(
      [id = std::string(id), componentName = std::string(componentName),
       initialValues = std::move(initialValues)](
          EditorSceneDocument &document, std::string &mutationError) mutable {
        return document.addComponent(id, componentName,
                                     std::move(initialValues), mutationError);
      },
      error);
}

bool EditorWorkspace::addScriptComponent(
    const std::string_view id, const EditorLuaComponentMetadata &metadata,
    std::string &error) {
  if (!metadata.module.starts_with("script://") ||
      !metadata.defaultProperties.is_object()) {
    error =
        "Script components require a script:// module and object properties.";
    return false;
  }
  return addComponent(
      id, "LuaScript",
      {{"module", metadata.module}, {"properties", metadata.defaultProperties}},
      error);
}

bool EditorWorkspace::removeComponent(const std::string_view id,
                                      const std::string_view componentName,
                                      std::string &error) {
  const auto target = resolveSceneTarget(
      {.entityId = std::string(id), .component = std::string(componentName)});
  if (target.isPrefabOverride()) {
    return mutateAndRebuild(
        [target](EditorSceneDocument &document, std::string &mutationError) {
          const auto inherited =
              document.prefabInheritsComponent(target, mutationError);
          if (!inherited)
            return false;
          const std::optional<nlohmann::json> removal =
              *inherited ? std::optional<nlohmann::json>(nullptr)
                         : std::nullopt;
          return document.setPrefabComponentOverride(target, removal,
                                                     mutationError);
        },
        error);
  }
  return mutateAndRebuild(
      [id = std::string(id), componentName = std::string(componentName)](
          EditorSceneDocument &document, std::string &mutationError) {
        return document.removeComponent(id, componentName, mutationError);
      },
      error);
}

bool EditorWorkspace::revertComponentOverride(
    const std::string_view id, const std::string_view componentName,
    std::string &error) {
  const auto target = resolveSceneTarget(
      {.entityId = std::string(id), .component = std::string(componentName)});
  return mutateAndRebuild(
      [target](EditorSceneDocument &document, std::string &mutationError) {
        return document.setPrefabComponentOverride(target, std::nullopt,
                                                   mutationError);
      },
      error);
}

std::optional<std::filesystem::path>
EditorWorkspace::prefabSourcePath(const std::string_view id) const {
  const auto target = resolveSceneTarget({.entityId = std::string(id)});
  const auto *instance =
      findPrefabInstance(sceneDocument_.json(), target.prefabInstanceId);
  if (instance == nullptr)
    return std::nullopt;
  return runtime::composition::resolvePrefabReference(
      sceneDocument_.path(), instance->at("prefab").get<std::string>());
}

std::vector<std::string>
EditorWorkspace::removedComponentOverrides(const std::string_view id) const {
  std::vector<std::string> result;
  const auto target = resolveSceneTarget({.entityId = std::string(id)});
  const auto *instance =
      findPrefabInstance(sceneDocument_.json(), target.prefabInstanceId);
  if (instance == nullptr || !instance->contains("overrides"))
    return result;
  const auto &overrides = instance->at("overrides");
  const auto local = overrides.find(target.prefabEntityId);
  if (local == overrides.end() || !local->is_object() ||
      !local->contains("components"))
    return result;
  const auto &components = local->at("components");
  if (!components.is_object())
    return result;
  for (const auto &[name, value] : components.items()) {
    if (value.is_null())
      result.push_back(name);
  }
  return result;
}

} // namespace demi::editor
