#pragma once

#include "editor/EditorSceneJson.h"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>

namespace demi::runtime {
struct Entity;
struct World;
} // namespace demi::runtime

namespace demi::editor {

// Produces the effective entity document represented by the editor's preview
// world. Prefab source values and scene overrides are already merged here.
[[nodiscard]] nlohmann::json
editorPreviewEntityJson(const runtime::Entity &entity);

// Redirect transient placement meshes to their authored placement for picking.
[[nodiscard]] std::string
editorPlacementOwner(const runtime::World &world, std::string_view entityId);
// Keep the renderer's inline-mesh cache in sync after field edits and undo.
void updateEditorMeshRevision(runtime::Entity &entity);
void updateEditorPlacementVisibility(runtime::World &world);

// Applies one already-validated authored value to a single preview entity.
// This deliberately avoids scene expansion and asset/HUD reconstruction while
// a field or gizmo is being dragged.
[[nodiscard]] bool applyEditorPreviewValue(runtime::World &world,
                                           const SceneValueTarget &target,
                                           const nlohmann::json &value,
                                           std::string &error);

} // namespace demi::editor
