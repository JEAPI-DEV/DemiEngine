#include "editor/EditorTerrainGraphPanel.h"
#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <imgui.h>

namespace demi::editor {
namespace {
bool validEndpoint(const nlohmann::json &endpoint) {
  return endpoint.is_object() && endpoint.contains("node") &&
         endpoint["node"].is_string() && endpoint.contains("port") &&
         endpoint["port"].is_string();
}
} // namespace

void EditorTerrainGraphPanel::drawInputSource(
    EditorWorkspace &workspace, std::string_view nodeId,
    const runtime::TerrainGraphPort &port, std::string &notice) {
  const auto &graph = workspace.terrainAuthoring().draft().at("graph");
  std::string sourceNode;
  std::string sourcePort;
  for (const auto &link : graph.at("links")) {
    if (!link.is_object() || !link.contains("from") || !link.contains("to") ||
        !validEndpoint(link["from"]) || !validEndpoint(link["to"]))
      continue;
    if (link.at("to").at("node") == nodeId &&
        link.at("to").at("port") == port.name) {
      sourceNode = link.at("from").at("node").get<std::string>();
      sourcePort = link.at("from").at("port").get<std::string>();
      break;
    }
  }
  const std::string preview =
      sourceNode.empty()
          ? (port.required ? "Choose source (required)" : "None (optional)")
          : sourceNode + " / " + sourcePort;
  ImGui::PushID(port.name.c_str());
  ImGui::SetNextItemWidth(192.0F * canvasView_.zoom());
  if (ImGui::BeginCombo("##input-source", preview.c_str())) {
    // Selecting a source replaces the graph. Snapshot choices only while the
    // popup is open so iteration remains valid without copying every frame.
    const auto candidates = graph.at("nodes");
    const auto connect = [&](std::string_view fromNode,
                             std::string_view fromPort) {
      std::string error;
      if (!document_.setInputSource(workspace.terrainAuthoring().draft(),
                                    nodeId, port.name, fromNode, fromPort,
                                    error))
        notice = std::move(error);
    };
    if (ImGui::Selectable("None", sourceNode.empty()))
      connect({}, {});
    for (const auto &candidate : candidates) {
      if (!candidate.is_object() || !candidate.contains("id") ||
          !candidate["id"].is_string() || !candidate.contains("type") ||
          !candidate["type"].is_string())
        continue;
      const auto id = candidate.at("id").get<std::string>();
      if (id == nodeId)
        continue;
      const auto *definition = runtime::terrainGraphNodeDefinition(
          candidate.at("type").get<std::string>());
      if (!definition)
        continue;
      for (const auto &output : definition->outputs) {
        if (output.kind != port.kind)
          continue;
        const std::string label = id + " / " + output.label;
        ImGui::PushID(id.c_str());
        ImGui::PushID(output.name.c_str());
        if (ImGui::Selectable(label.c_str(),
                              sourceNode == id && sourcePort == output.name))
          connect(id, output.name);
        ImGui::PopID();
        ImGui::PopID();
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "%s input. Choose a matching output or drag a wire to this "
        "pin.\n%s\nChanging the source is one undoable edit.",
        runtime::terrainGraphValueKindName(port.kind).data(),
        port.required ? "A connection is required to generate."
                      : "Unconnected: the node uses its default behavior.");
  }
  ImGui::PopID();
}

} // namespace demi::editor
