#pragma once

#include "editor/EditorSceneViewState.h"

#include <optional>
#include <string>

namespace demi::runtime {
struct World;
}

namespace demi::editor {

struct EditorViewportRay {
  runtime::Vec3 origin;
  runtime::Vec3 direction;
};

[[nodiscard]] EditorViewportRay
sceneViewportRay(const EditorSceneViewCamera &camera,
                 runtime::Vec2 viewportPosition, runtime::Vec2 viewportSize);

[[nodiscard]] std::optional<runtime::Vec2>
projectScenePoint3D(const EditorSceneViewCamera &camera,
                    runtime::Vec3 worldPoint, runtime::Vec2 viewportSize);

// Returns a screen-space direction (positive Y points down) using the same
// right-handed camera basis as the bgfx view matrix.
[[nodiscard]] runtime::Vec2
projectSceneDirection3D(const EditorSceneViewCamera &camera,
                        runtime::Vec3 worldDirection);

// World-space length of a screen pixel at the point's view depth. Useful for
// constant-screen-size editor handles without fixed world-distance floors.
[[nodiscard]] float sceneWorldUnitsPerPixel(const EditorSceneViewCamera &camera,
                                            runtime::Vec3 worldPoint,
                                            runtime::Vec2 viewportSize);

// Returns the cursor ray intersection with the 3D authoring ground plane
// (Y = 0), or nullopt when the ray is parallel or points away from it.
[[nodiscard]] std::optional<runtime::Vec3>
intersectSceneGroundPlane3D(const EditorSceneViewCamera &camera,
                            runtime::Vec2 viewportPosition,
                            runtime::Vec2 viewportSize);

[[nodiscard]] std::optional<std::string>
pickSceneEntity3D(const runtime::World &world,
                  const EditorSceneViewCamera &camera,
                  runtime::Vec2 viewportPosition, runtime::Vec2 viewportSize);

} // namespace demi::editor
