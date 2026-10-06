#pragma once

#include "editor/EditorSceneView2DState.h"
#include "editor/EditorSceneViewState.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace demi::runtime {
struct World;
}

namespace demi::editor {

// Converts viewport-local drop points to the 2D view or the 3D ground plane.
[[nodiscard]] runtime::Vec2
prefabDropWorldPosition2D(const EditorSceneView2DCamera &camera,
                          runtime::Vec2 viewportPosition,
                          runtime::Vec2 viewportSize);
[[nodiscard]] std::optional<runtime::Vec3>
prefabDropWorldPosition3D(const EditorSceneViewCamera &camera,
                          runtime::Vec2 viewportPosition,
                          runtime::Vec2 viewportSize);
// Surface first for authored terrain, then the ground plane or a point ahead
// of the camera when its ray does not meet the plane.
[[nodiscard]] runtime::Vec3 sceneDropWorldPosition3D(
    const EditorSceneViewCamera &camera, const runtime::World &world,
    runtime::Vec2 viewportPosition, runtime::Vec2 viewportSize);

// Builds the complete initial override object for a positioned prefab
// insertion. Root pivots retain their authored offsets above the hit surface;
// placement does not keep them attached to later terrain edits. Isometric
// roots snap through the active scene grid.
[[nodiscard]] std::optional<nlohmann::json>
prefabPlacementOverrides(const std::filesystem::path &path,
                         const runtime::World &world,
                         runtime::Vec2 worldPosition, std::string &error);
[[nodiscard]] std::optional<nlohmann::json>
prefabPlacementOverrides(const std::filesystem::path &path,
                         runtime::Vec3 groundPosition, std::string &error);

} // namespace demi::editor
