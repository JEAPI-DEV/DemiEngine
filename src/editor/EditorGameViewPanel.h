#pragma once

#include "editor/EditorPlaySession.h"
#include "editor/EditorUiHost.h"

#include <string>

struct ImVec2;

namespace demi::runtime {
struct World;
}

namespace demi::editor {

void drawEditorGameView(const EditorPlaySession &session, ImVec2 position,
                        ImVec2 size, std::uint16_t textureIndex,
                        EditorViewportArea &area, bool &focused);
void drawRuntimeHierarchy(const runtime::World &world, ImVec2 position,
                          ImVec2 size, std::string &selectedEntityId);
void drawRuntimeInspector(const runtime::World &world, ImVec2 position,
                          ImVec2 size, const std::string &selectedEntityId);

} // namespace demi::editor
