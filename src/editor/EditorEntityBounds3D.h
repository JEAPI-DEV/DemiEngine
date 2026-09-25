#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <optional>
#include <vector>

namespace demi::runtime {
struct Entity;
struct World;
} // namespace demi::runtime

namespace demi::editor {

struct EditorBounds3D {
  runtime::Vec3 minimum;
  runtime::Vec3 maximum;
};

// An entity's pickable local box and every world pose that displays it.
// MeshInstances3D contributes one pose per render-only instance.
struct EditorEntityBounds3D {
  EditorBounds3D local;
  std::vector<runtime::WorldTransform3D> worldTransforms;
  bool isSceneGeometry = false;
};

[[nodiscard]] std::optional<EditorEntityBounds3D>
editorEntityBounds3D(const runtime::World &world,
                     const runtime::Entity &entity);

[[nodiscard]] std::optional<EditorBounds3D>
editorWorldBounds3D(const EditorEntityBounds3D &entityBounds);

// Includes enabled renderable/collider geometry and excludes transform-only
// placeholders such as cameras and lights.
[[nodiscard]] std::optional<EditorBounds3D>
editorSceneBounds3D(const runtime::World &world);

} // namespace demi::editor
