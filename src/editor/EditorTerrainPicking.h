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
struct EditorTerrainProjectedMaskSample {
  runtime::Vec2 position;
  float weight = 0;
};
std::vector<EditorTerrainProjectedMaskSample> projectEditorTerrainExclusions(
    const runtime::World &world, const EditorTerrainAuthoring &authoring,
    const EditorSceneViewCamera &camera, runtime::Vec2 viewportSize);
struct EditorTerrainProjectedRuleMaskSample {
  runtime::Vec2 position;
  float weight = 0;
  std::size_t biome = 0;
  runtime::Color color;
};
std::vector<EditorTerrainProjectedRuleMaskSample> projectEditorTerrainRuleMask(
    const runtime::World &world, const EditorTerrainAuthoring &authoring,
    const EditorSceneViewCamera &camera, runtime::Vec2 viewportSize);
} // namespace demi::editor
