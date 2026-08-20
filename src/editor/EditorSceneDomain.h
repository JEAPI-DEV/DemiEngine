#pragma once

namespace demi::runtime {
struct World;
}

namespace demi::editor {

enum class EditorSceneDomain { Empty, TwoDimensional, ThreeDimensional, Mixed };
enum class EditorSceneViewDimension { TwoDimensional, ThreeDimensional };

[[nodiscard]] EditorSceneDomain
detectEditorSceneDomain(const runtime::World &world);
[[nodiscard]] EditorSceneViewDimension
defaultEditorSceneViewDimension(EditorSceneDomain domain);

} // namespace demi::editor
