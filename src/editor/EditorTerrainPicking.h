#pragma once
#include "editor/EditorTerrainAuthoring.h"
namespace demi::editor {
std::optional<runtime::Vec3>
pickEditorTerrain(const runtime::World &world, std::string_view entityId,
                  EditorTerrainSurfacePtr surface,
                  const EditorSceneViewCamera &camera,
                  const EditorViewportToolInput &input);
std::vector<std::optional<runtime::Vec2>> projectEditorTerrainBrush(
    const runtime::World &world, const EditorTerrainAuthoring &authoring,
    const EditorSceneViewCamera &camera, runtime::Vec2 viewportSize);
} // namespace demi::editor
