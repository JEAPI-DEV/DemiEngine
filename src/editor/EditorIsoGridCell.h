#pragma once

#include "editor/EditorSceneView2DState.h"
#include "editor/EditorSelection.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {
struct World;
}

namespace demi::editor {

[[nodiscard]] std::string isoGridCellKey(int x, int y);
[[nodiscard]] std::vector<EditorIsoGridCell>
paintedIsoGridCells(const runtime::World &world, std::string_view gridEntityId);
[[nodiscard]] std::optional<EditorIsoGridCell> pickPaintedIsoGridCell(
    const runtime::World &world, const EditorSceneView2DCamera &camera,
    runtime::Vec2 viewportPosition, runtime::Vec2 viewportSize);
[[nodiscard]] std::optional<runtime::Vec2>
isoGridCellWorldPosition(const runtime::World &world,
                         const EditorIsoGridCell &cell);

} // namespace demi::editor
