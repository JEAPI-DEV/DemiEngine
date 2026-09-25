#pragma once
#include "editor/EditorStructuredValue.h"

#include <array>
#include <string>
#include <filesystem>
#include <optional>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

struct EditorInspectorPanelState {
  StructuredValueState structuredValues;
  std::array<char, 128> componentSearch{};
  std::array<char, 128> propertySearch{};
  std::optional<std::filesystem::path> openRequest;
  std::string pendingComponentEntity;
  std::string pendingComponent;
  std::string pendingReferenceField;
};

void drawInspectorPanel(EditorWorkspace &workspace, ImVec2 position,
                        ImVec2 size, EditorInspectorPanelState &state,
                        std::string &notice, bool *open = nullptr);

} // namespace demi::editor
