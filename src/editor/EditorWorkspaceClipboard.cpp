#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/composition/EntityHierarchy.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "editor/EditorClipboard.h"

#include <algorithm>
#include <set>

namespace demi::editor {
namespace {

std::string entityParent(const runtime::Entity &entity) {
  if (const auto *transform = entity.component<runtime::Transform3DComponent>())
    return transform->parent;
  if (const auto *transform = entity.component<runtime::Transform2DComponent>())
    return transform->parent;
  if (const auto *transform =
          entity.component<runtime::IsoTransformComponent>())
    return transform->parent;
  return {};
}

std::optional<std::vector<std::string>>
authoredSelection(const EditorWorkspace &workspace, std::string &error) {
  if (workspace.activeDocument() != EditorWorkspaceDocument::Scene ||
      workspace.selectedIsoGridCell()) {
    error = "Select authored scene entities or prefab roots first.";
    return std::nullopt;
  }
  std::vector<std::string> result;
  std::set<std::string> seen;
  const auto &document = workspace.sceneDocument().json();
  const runtime::composition::PrefabOriginIndex origins(document);
  for (const auto &selected : workspace.selectedEntityIds()) {
    std::string id = selected;
    if (!workspace.sceneDocument().entity(id) &&
        !findPrefabInstance(document, id)) {
      auto target = workspace.authoredTarget({.entityId = id});
      if (!target.isPrefabOverride()) {
        if (const auto origin = origins.find(id)) {
          target.prefabInstanceId = origin->instanceId;
          target.prefabEntityId = origin->localEntityId;
        }
      }
      if (!target.isPrefabOverride()) {
        error = "Generated entity '" + id +
                "' cannot be copied or deleted as authored source.";
        return std::nullopt;
      }
      const auto &world = workspace.project().world;
      const auto entity =
          std::ranges::find(world.entities, id, &runtime::Entity::id);
      if (entity == world.entities.end()) {
        error = "The selected entity no longer exists.";
        return std::nullopt;
      }
      const auto parent = std::ranges::find(
          world.entities, entityParent(*entity), &runtime::Entity::id);
      if (parent != world.entities.end() &&
          parent->prefabInstance == entity->prefabInstance) {
        error = "Select the prefab instance root, or open its source to edit "
                "individual children.";
        return std::nullopt;
      }
      id = target.prefabInstanceId;
    }
    if (seen.insert(id).second)
      result.push_back(std::move(id));
  }
  if (result.empty()) {
    error = "Select authored scene entities first.";
    return std::nullopt;
  }
  return result;
}

std::vector<std::string>
visibleSceneSelection(const runtime::World &world,
                      const std::vector<std::string> &ids) {
  std::vector<std::string> result;
  for (const auto &id : ids) {
    const auto exact =
        std::ranges::find(world.entities, id, &runtime::Entity::id);
    if (exact != world.entities.end()) {
      result.push_back(id);
      continue;
    }
    const auto instance =
        std::ranges::find_if(world.entities, [&](const auto &entity) {
          if (entity.prefabInstance != id)
            return false;
          const auto parent = std::ranges::find(
              world.entities, entityParent(entity), &runtime::Entity::id);
          return parent == world.entities.end() || parent->prefabInstance != id;
        });
    if (instance != world.entities.end())
      result.push_back(instance->id);
  }
  return result;
}

} // namespace

std::optional<std::string>
EditorWorkspace::exportSelection(std::string &error) const {
  if (activeDocument_ == EditorWorkspaceDocument::Hud) {
    const auto *hud = hudDocument();
    if (!hud) {
      error = "Open a HUD or UI prefab first.";
      return std::nullopt;
    }
    const auto nodes = hud->exportNodes(selectedHudNodeIds_, error);
    if (!nodes)
      return std::nullopt;
    return encodeEditorClipboard(EditorClipboardKind::Hud, *nodes);
  }
  const auto ids = authoredSelection(*this, error);
  if (!ids)
    return std::nullopt;
  const auto entities = sceneDocument_.exportEntities(*ids, error);
  if (!entities)
    return std::nullopt;
  return encodeEditorClipboard(EditorClipboardKind::Entities, *entities);
}

bool EditorWorkspace::pasteSelection(const std::string_view text,
                                     std::string &error) {
  const auto payload = decodeEditorClipboard(text, error);
  if (!payload)
    return false;
  std::vector<std::string> created;
  if (activeDocument_ == EditorWorkspaceDocument::Hud &&
      payload->kind == EditorClipboardKind::Hud) {
    auto *hud = activeHudDocument();
    if (!hud) {
      error = "Open a HUD or UI prefab before pasting controls.";
      return false;
    }
    if (!hud->pasteNodes(payload->data, selectedHudNodeId_, created, error))
      return false;
    syncHudPreview();
    selectedHudNodeIds_ = std::move(created);
    selectedHudNodeId_ = selectedHudNodeIds_.back();
    return true;
  }
  if (activeDocument_ != EditorWorkspaceDocument::Scene ||
      payload->kind != EditorClipboardKind::Entities) {
    error =
        "This clipboard content does not match the active authoring document.";
    return false;
  }
  if (!mutateAndRebuild(
          [&](EditorSceneDocument &document, std::string &mutationError) {
            return document.pasteEntities(payload->data, created,
                                          mutationError);
          },
          error))
    return false;
  selectedEntityIds_ = visibleSceneSelection(project_->world, created);
  selectedHudNodeIds_.clear();
  selectedHudNodeId_.clear();
  selectedIsoGridCell_.reset();
  syncTerrainAuthoring();
  return true;
}

bool EditorWorkspace::duplicateSelection(std::string &error) {
  if (activeDocument_ == EditorWorkspaceDocument::Hud) {
    auto *hud = activeHudDocument();
    if (!hud) {
      error = "Open a HUD or UI prefab first.";
      return false;
    }
    std::vector<std::string> created;
    if (!hud->duplicateNodes(selectedHudNodeIds_, created, error))
      return false;
    syncHudPreview();
    selectedHudNodeIds_ = std::move(created);
    selectedHudNodeId_ = selectedHudNodeIds_.back();
    return true;
  }
  const auto ids = authoredSelection(*this, error);
  if (!ids)
    return false;
  std::vector<std::string> created;
  if (!mutateAndRebuild(
          [&](EditorSceneDocument &document, std::string &mutationError) {
            return document.duplicateEntities(*ids, created, mutationError);
          },
          error))
    return false;
  selectedEntityIds_ = visibleSceneSelection(project_->world, created);
  syncTerrainAuthoring();
  return true;
}

bool EditorWorkspace::deleteSelection(std::string &error) {
  if (activeDocument_ == EditorWorkspaceDocument::Hud) {
    auto *hud = activeHudDocument();
    if (!hud) {
      error = "Open a HUD or UI prefab first.";
      return false;
    }
    if (!hud->deleteNodes(selectedHudNodeIds_, error))
      return false;
    selectedHudNodeIds_.clear();
    selectedHudNodeId_.clear();
    syncHudPreview();
    return true;
  }
  const auto ids = authoredSelection(*this, error);
  if (!ids)
    return false;
  if (!mutateAndRebuild(
          [&](EditorSceneDocument &document, std::string &mutationError) {
            return document.deleteEntities(*ids, mutationError);
          },
          error))
    return false;
  selectedEntityIds_.clear();
  syncTerrainAuthoring();
  return true;
}

bool EditorWorkspace::selectAllAuthored(std::string &error) {
  if (activeDocument_ == EditorWorkspaceDocument::Hud) {
    const auto *hud = hudDocument();
    if (!hud) {
      error = "Open a HUD or UI prefab first.";
      return false;
    }
    selectedHudNodeIds_.clear();
    const auto &nodes = hud->preview().nodes;
    for (const auto &node : nodes)
      if (node.id != nodes.front().id && hud->authoredNode(node.id))
        selectedHudNodeIds_.push_back(node.id);
    selectedHudNodeId_ =
        selectedHudNodeIds_.empty() ? "" : selectedHudNodeIds_.back();
    selectedEntityIds_.clear();
    selectedIsoGridCell_.reset();
    syncTerrainAuthoring();
    return true;
  }
  if (activeDocument_ != EditorWorkspaceDocument::Scene || !project_) {
    error = "Select a scene, prefab, HUD or UI prefab authoring document.";
    return false;
  }
  const auto &source = sceneDocument_.json();
  std::vector<std::string> ids;
  const auto flat =
      runtime::composition::flattenEntityHierarchy(source.at("entities"));
  for (const auto &entity : flat)
    ids.push_back(entity.at("id").get<std::string>());
  if (const auto instances = source.find("instances");
      instances != source.end())
    for (const auto &instance : *instances)
      ids.push_back(instance.at("id").get<std::string>());
  selectedEntityIds_ = visibleSceneSelection(project_->world, ids);
  selectedHudNodeIds_.clear();
  selectedHudNodeId_.clear();
  selectedIsoGridCell_.reset();
  syncTerrainAuthoring();
  return true;
}

} // namespace demi::editor
