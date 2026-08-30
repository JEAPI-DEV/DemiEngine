#pragma once

#include "editor/EditorSelection.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace demi::editor {

[[nodiscard]] std::optional<std::string>
authoredIsoGridCellTexture(const nlohmann::json &component,
                           const EditorIsoGridCell &cell);

[[nodiscard]] std::optional<nlohmann::json>
moveAuthoredIsoGridCell(const nlohmann::json &component,
                        const EditorIsoGridCell &from, int x, int y, int width,
                        int height, std::string &error);

[[nodiscard]] std::optional<nlohmann::json>
setAuthoredIsoGridCellTexture(const nlohmann::json &component,
                              const EditorIsoGridCell &cell,
                              std::string texture, std::string &error);

[[nodiscard]] std::optional<nlohmann::json>
clearAuthoredIsoGridCell(const nlohmann::json &component,
                         const EditorIsoGridCell &cell, std::string &error);

} // namespace demi::editor
