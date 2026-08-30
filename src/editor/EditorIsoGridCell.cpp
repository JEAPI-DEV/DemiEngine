#include "editor/EditorIsoGridCell.h"

#include "editor/EditorViewportProjection2D.h"

#include "demi/runtime/isometric/IsoGridMath.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"

#include <algorithm>
#include <charconv>
#include <ranges>
#include <tuple>

namespace demi::editor {
namespace {

std::optional<runtime::isometric::GridDefinition>
gridDefinition(const runtime::IsoGridComponent &grid) {
  if (grid.width <= 0 || grid.height <= 0)
    return std::nullopt;
  return runtime::isometric::GridDefinition{.width = grid.width,
                                            .height = grid.height,
                                            .cellWidth = grid.cellSize.x,
                                            .cellHeight = grid.cellSize.y};
}

std::optional<runtime::isometric::GridCell> parseCell(const std::string &key) {
  const std::size_t separator = key.find(',');
  if (separator == std::string::npos)
    return std::nullopt;
  runtime::isometric::GridCell cell;
  const char *begin = key.data();
  const char *end = begin + key.size();
  const auto x = std::from_chars(begin, begin + separator, cell.x);
  const auto y = std::from_chars(begin + separator + 1, end, cell.y);
  if (x.ec != std::errc{} || x.ptr != begin + separator ||
      y.ec != std::errc{} || y.ptr != end)
    return std::nullopt;
  return cell;
}

} // namespace

std::string isoGridCellKey(const int x, const int y) {
  return std::to_string(x) + "," + std::to_string(y);
}

std::vector<EditorIsoGridCell>
paintedIsoGridCells(const runtime::World &world,
                    const std::string_view gridEntityId) {
  std::vector<EditorIsoGridCell> result;
  const runtime::Entity *entity =
      runtime::findEntity(world, std::string(gridEntityId));
  const auto *grid = entity == nullptr
                         ? nullptr
                         : entity->component<runtime::IsoGridComponent>();
  if (grid == nullptr)
    return result;
  result.reserve(grid->cellTextures.size());
  for (const auto &[key, texture] : grid->cellTextures) {
    (void)texture;
    if (const auto cell = parseCell(key))
      result.push_back(
          {.gridEntityId = entity->id, .x = cell->x, .y = cell->y});
  }
  std::ranges::sort(result, [](const auto &left, const auto &right) {
    return std::tie(left.y, left.x) < std::tie(right.y, right.x);
  });
  return result;
}

std::optional<EditorIsoGridCell> pickPaintedIsoGridCell(
    const runtime::World &world, const EditorSceneView2DCamera &camera,
    const runtime::Vec2 viewportPosition, const runtime::Vec2 viewportSize) {
  const runtime::Vec2 worldPosition =
      unprojectScenePoint2D(camera, viewportPosition, viewportSize);
  for (const runtime::Entity &entity : world.entities) {
    const auto *grid = entity.component<runtime::IsoGridComponent>();
    if (!entity.enabled || grid == nullptr)
      continue;
    const auto definition = gridDefinition(*grid);
    if (!definition)
      continue;
    const runtime::isometric::GridCell cell =
        runtime::isometric::worldToTile(*definition, worldPosition);
    if (runtime::isometric::contains(*definition, cell) &&
        grid->cellTextures.contains(isoGridCellKey(cell.x, cell.y)))
      return EditorIsoGridCell{
          .gridEntityId = entity.id, .x = cell.x, .y = cell.y};
  }
  return std::nullopt;
}

std::optional<runtime::Vec2>
isoGridCellWorldPosition(const runtime::World &world,
                         const EditorIsoGridCell &cell) {
  const runtime::Entity *entity = runtime::findEntity(world, cell.gridEntityId);
  const auto *grid = entity == nullptr
                         ? nullptr
                         : entity->component<runtime::IsoGridComponent>();
  if (grid == nullptr)
    return std::nullopt;
  const auto definition = gridDefinition(*grid);
  if (!definition)
    return std::nullopt;
  const runtime::isometric::GridCell coordinate{.x = cell.x, .y = cell.y};
  if (!runtime::isometric::contains(*definition, coordinate))
    return std::nullopt;
  return runtime::isometric::tileToWorld(*definition, coordinate);
}

} // namespace demi::editor
