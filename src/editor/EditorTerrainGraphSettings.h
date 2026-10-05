#pragma once

#include <string>
#include <string_view>

namespace demi::editor {
class EditorWorkspace;

// Presents the authored TerrainRecipe beside the graph canvas. Node parameters
// and these recipe fields share one transient authoring draft.
void drawTerrainGraphSettings(EditorWorkspace &workspace, std::string &notice);

// Node context exposes the same recipe fields, not node-local copies. Supported
// registry types are landform, biomes, scatter and output; other types draw no
// settings.
void drawTerrainNodeSettings(EditorWorkspace &workspace,
                             std::string_view nodeType, std::string &notice);
} // namespace demi::editor
