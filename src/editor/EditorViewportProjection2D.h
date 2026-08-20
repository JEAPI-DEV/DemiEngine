#pragma once

#include "editor/EditorSceneView2DState.h"

#include <optional>
#include <string>
#include <string_view>

namespace demi::runtime {
struct World;
}

namespace demi::editor {

[[nodiscard]] runtime::Vec2
projectScenePoint2D(const EditorSceneView2DCamera &camera,
                    runtime::Vec2 worldPoint, runtime::Vec2 viewportSize);
[[nodiscard]] runtime::Vec2
unprojectScenePoint2D(const EditorSceneView2DCamera &camera,
                      runtime::Vec2 viewportPoint, runtime::Vec2 viewportSize);
[[nodiscard]] std::optional<std::string>
pickSceneEntity2D(const runtime::World &world,
                  const EditorSceneView2DCamera &camera,
                  runtime::Vec2 viewportPosition, runtime::Vec2 viewportSize,
                  std::string_view cycleAfterEntityId = {});

} // namespace demi::editor
