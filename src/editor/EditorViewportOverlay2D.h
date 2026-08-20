#pragma once

#include "editor/EditorSceneView2DState.h"

#include "demi/runtime/tilemap/TilemapAsset.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace demi::editor {

struct EditorOverlayLine2D {
  runtime::Vec2 start;
  runtime::Vec2 end;
  std::uint32_t rgba = 0xffffffffU;
  float width = 1.0F;
};

struct EditorViewportOverlay2DRequest {
  bool grid = true;
  bool bounds = true;
  bool cameras = true;
};

// Extracts editor guides from the same resolved world transforms and tilemap
// assets used by the runtime renderers. Returned coordinates are
// viewport-local.
[[nodiscard]] std::vector<EditorOverlayLine2D> buildEditorViewportOverlays2D(
    const runtime::World &world, const EditorSceneView2DCamera &camera,
    runtime::Vec2 viewportSize,
    const std::unordered_map<std::string, runtime::TilemapAsset2D> &tilemaps,
    const EditorViewportOverlay2DRequest &request);

} // namespace demi::editor
