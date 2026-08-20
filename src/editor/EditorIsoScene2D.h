#pragma once

#include "editor/EditorSceneView2DState.h"

#include <optional>
#include <string>

namespace demi::runtime {
struct Entity;
struct World;
} // namespace demi::runtime

namespace demi::editor {

struct EditorIsoVisual2D {
  runtime::Vec2 worldAnchor;
  runtime::Vec2 screenMinimum;
  runtime::Vec2 screenMaximum;
  int sortingOrder = 0;
  std::string layer;
  float depth = 0.0F;
};

// Mirrors the isometric renderer's placement and sprite bounds so viewport
// picking and gizmos operate on what the author actually sees.
[[nodiscard]] std::optional<EditorIsoVisual2D>
editorIsoVisual2D(const runtime::World &world, const runtime::Entity &entity,
                  const EditorSceneView2DCamera &camera,
                  runtime::Vec2 viewportSize);

[[nodiscard]] bool editorIsoVisualContains(const runtime::Entity &entity,
                                           const EditorIsoVisual2D &visual,
                                           runtime::Vec2 viewportPoint);

} // namespace demi::editor
