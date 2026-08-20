#include "editor/EditorIsoGridCellDocument.h"

#include "editor/EditorIsoGridCell.h"

#include <nlohmann/json.hpp>

namespace demi::editor {
namespace {

std::optional<nlohmann::json> authoredCells(const nlohmann::json &component,
                                            std::string &error) {
  const auto cells = component.find("cell_textures");
  if (cells == component.end() || !cells->is_object()) {
    error = "The isometric grid has no authored cell texture map.";
    return std::nullopt;
  }
  return *cells;
}

} // namespace

std::optional<std::string>
authoredIsoGridCellTexture(const nlohmann::json &component,
                           const EditorIsoGridCell &cell) {
  const auto cells = component.find("cell_textures");
  if (cells == component.end() || !cells->is_object())
    return std::nullopt;
  const auto texture = cells->find(isoGridCellKey(cell.x, cell.y));
  if (texture == cells->end() || !texture->is_string())
    return std::nullopt;
  return texture->get<std::string>();
}

std::optional<nlohmann::json>
moveAuthoredIsoGridCell(const nlohmann::json &component,
                        const EditorIsoGridCell &from, const int x, const int y,
                        const int width, const int height, std::string &error) {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    error = "The target cell is outside the isometric grid.";
    return std::nullopt;
  }
  auto cells = authoredCells(component, error);
  if (!cells)
    return std::nullopt;
  const std::string oldKey = isoGridCellKey(from.x, from.y);
  const std::string newKey = isoGridCellKey(x, y);
  const auto texture = cells->find(oldKey);
  if (texture == cells->end() || !texture->is_string()) {
    error = "The selected painted cell no longer exists.";
    return std::nullopt;
  }
  if (oldKey != newKey && cells->contains(newKey)) {
    error = "The target cell already has an authored texture.";
    return std::nullopt;
  }
  const nlohmann::json value = *texture;
  cells->erase(oldKey);
  (*cells)[newKey] = value;
  return cells;
}

std::optional<nlohmann::json>
setAuthoredIsoGridCellTexture(const nlohmann::json &component,
                              const EditorIsoGridCell &cell,
                              std::string texture, std::string &error) {
  auto cells = authoredCells(component, error);
  if (!cells)
    return std::nullopt;
  const std::string key = isoGridCellKey(cell.x, cell.y);
  if (!cells->contains(key)) {
    error = "The selected painted cell no longer exists.";
    return std::nullopt;
  }
  (*cells)[key] = std::move(texture);
  return cells;
}

std::optional<nlohmann::json>
clearAuthoredIsoGridCell(const nlohmann::json &component,
                         const EditorIsoGridCell &cell, std::string &error) {
  auto cells = authoredCells(component, error);
  if (!cells)
    return std::nullopt;
  if (cells->erase(isoGridCellKey(cell.x, cell.y)) == 0) {
    error = "The selected painted cell no longer exists.";
    return std::nullopt;
  }
  return cells;
}

} // namespace demi::editor
