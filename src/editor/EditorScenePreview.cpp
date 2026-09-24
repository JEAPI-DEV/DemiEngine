#include "editor/EditorScenePreview.h"

#include "demi/runtime/scene/SceneEntityParser.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/PrefabPlacements3D.h"
#include "demi/runtime/scene/components/3dcomponents/PrefabPlacement3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include <functional>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace demi::editor {

void updateEditorMeshRevision(runtime::Entity &entity) {
  auto *mesh = entity.component<runtime::MeshRendererComponent>();
  const auto source = entity.serializedComponents.find("MeshRenderer");
  if (mesh && source != entity.serializedComponents.end())
    mesh->revision = std::hash<std::string>{}(source->second);
}

std::string editorPlacementOwner(const runtime::World &world, std::string_view entityId) {
  const auto *entity = runtime::findEntity(world, std::string(entityId));
  std::string masonryOwner;
  std::unordered_set<std::string> visited;
  while (entity && visited.insert(entity->id).second) {
    if (entity->hasComponent<runtime::PrefabPlacement3DComponent>()) return entity->id;
    const auto *transform = entity->component<runtime::Transform3DComponent>();
    if (transform && entity->id.starts_with(transform->parent + "/__masonry_preview/"))
      masonryOwner = transform->parent;
    entity = transform && !transform->parent.empty()
        ? runtime::findEntity(world, transform->parent) : nullptr;
  }
  return masonryOwner;
}

void updateEditorPlacementVisibility(runtime::World &world) {
  std::unordered_set<std::string> enabled;
  for (const auto &placement : runtime::collectPrefabPlacements3D(world))
    enabled.insert(placement.id);
  for (auto &entity : world.entities) {
    const auto owner = editorPlacementOwner(world, entity.id);
    if (!owner.empty() && entity.id.starts_with(owner + "/__preview/") && !enabled.contains(owner))
      entity.enabled = false;
  }
}

nlohmann::json editorPreviewEntityJson(const runtime::Entity &entity) {
  nlohmann::json result{{"id", entity.id},
                        {"name", entity.name},
                        {"enabled", entity.enabled},
                        {"layer", entity.layer},
                        {"persistent", entity.persistent},
                        {"components", nlohmann::json::object()}};
  if (!entity.tags.empty())
    result["tags"] = entity.tags;
  for (const auto &[name, serialized] : entity.serializedComponents) {
    try {
      result["components"][name] = nlohmann::json::parse(serialized);
    } catch (const nlohmann::json::parse_error &) {
      result["components"][name] = nlohmann::json::object();
    }
  }
  return result;
}

bool applyEditorPreviewValue(runtime::World &world,
                             const SceneValueTarget &target,
                             const nlohmann::json &value, std::string &error) {
  runtime::Entity *current = runtime::findEntity(world, target.entityId);
  if (current == nullptr) {
    error = "The edited prefab entity is no longer in the preview world.";
    return false;
  }

  nlohmann::json effective = editorPreviewEntityJson(*current);
  nlohmann::json *container = &effective;
  if (!target.component.empty()) {
    auto component = effective["components"].find(target.component);
    if (component == effective["components"].end() || !component->is_object()) {
      error = "The edited prefab component is no longer in the preview world.";
      return false;
    }
    container = &*component;
  }
  (*container)[target.field] = value;

  runtime::Entity replacement =
      runtime::scene_loading::parseSceneEntity(effective);
  updateEditorMeshRevision(replacement);
  replacement.sceneOwner = current->sceneOwner;
  replacement.prefabInstance = current->prefabInstance;
  replacement.prefabLocalId = current->prefabLocalId;

  runtime::Entity previous = std::move(*current);
  *current = std::move(replacement);
  if (const auto issues = runtime::validateTransform3DHierarchy(world);
      !issues.empty()) {
    const auto &issue = issues.front();
    error =
        issue.kind == runtime::Transform3DHierarchyIssueKind::Cycle
            ? "Transform3D hierarchy cycle includes entity: " + issue.entityId
            : "Transform3D parent was not found for " + issue.entityId + ": " +
                  issue.parentId;
    *current = std::move(previous);
    return false;
  }
  return true;
}

} // namespace demi::editor
