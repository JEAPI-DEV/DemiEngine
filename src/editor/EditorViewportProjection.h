#pragma once

#include "editor/EditorSceneViewState.h"

#include <optional>
#include <string>

namespace demi::runtime {
struct World;
}

namespace demi::editor {

[[nodiscard]] std::optional<runtime::Vec2>
projectScenePoint3D(const EditorSceneViewCamera &camera,
                    runtime::Vec3 worldPoint, runtime::Vec2 viewportSize);

[[nodiscard]] std::optional<std::string>
pickSceneEntity3D(const runtime::World &world,
                  const EditorSceneViewCamera &camera,
                  runtime::Vec2 viewportPosition, runtime::Vec2 viewportSize);

} // namespace demi::editor
