#pragma once

#include "editor/EditorSceneJson.h"

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace demi::runtime {
struct Entity;
struct World;
} // namespace demi::runtime

namespace demi::editor {

// Produces the effective entity document represented by the editor's preview
// world. Prefab source values and scene overrides are already merged here.
[[nodiscard]] nlohmann::json
editorPreviewEntityJson(const runtime::Entity &entity);

// Applies one already-validated authored value to a single preview entity.
// This deliberately avoids scene expansion and asset/HUD reconstruction while
// a field or gizmo is being dragged.
[[nodiscard]] bool applyEditorPreviewValue(runtime::World &world,
                                           const SceneValueTarget &target,
                                           const nlohmann::json &value,
                                           std::string &error);

} // namespace demi::editor
