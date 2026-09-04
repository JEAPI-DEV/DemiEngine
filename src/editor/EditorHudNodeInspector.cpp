#include "editor/EditorHudNodeInspector.h"

#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace demi::editor {
namespace {

void setVec2(EditorWorkspace &workspace, const runtime::ui::UiNode &node,
             const char *field, const float values[2], std::string message,
             std::string &notice) {
  std::string error;
  notice =
      workspace.setHudNodeField(
          node.id, field, nlohmann::json::array({values[0], values[1]}), error)
          ? std::move(message)
          : error;
}

void setOptionalString(EditorWorkspace &workspace,
                       const runtime::ui::UiNode &node, const char *field,
                       const std::string &value, std::string message,
                       std::string &notice) {
  std::string error;
  notice = workspace.setHudNodeField(node.id, field,
                                     value.empty() ? nlohmann::json(nullptr)
                                                   : nlohmann::json(value),
                                     error)
               ? std::move(message)
               : error;
}

// Hex forms accepted by the runtime parser ("#RRGGBB" / "#RRGGBBAA").
// Float arrays keep working; the inspector round-trips through hex so files
// stay terse and copy-paste friendly.
std::string colorToHex(const runtime::Color &color) {
  const auto byte = [](float v) {
    return static_cast<int>(
        std::round(std::clamp(v, 0.0F, 1.0F) * 255.0F));
  };
  char buffer[10];
  const int r = byte(color.r);
  const int g = byte(color.g);
  const int b = byte(color.b);
  const int a = byte(color.a);
  if (a == 255)
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", r, g, b);
  else
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", r, g, b, a);
  return {buffer};
}

void editColorHex(EditorWorkspace &workspace, const runtime::ui::UiNode &node,
                  const char *label, const char *field,
                  const runtime::Color &color, std::array<char, 16> &buffer,
                  bool &initialized, const char *successMessage,
                  std::string &notice) {
  if (!initialized) {
    const std::string hex = colorToHex(color);
    std::strncpy(buffer.data(), hex.c_str(), buffer.size() - 1);
    buffer[buffer.size() - 1] = '\0';
    initialized = true;
  }
  float swatch[4]{color.r, color.g, color.b, color.a};
  if (ImGui::ColorEdit4(label, swatch)) {
    std::string error;
    notice = workspace.setHudNodeField(
                 node.id, field,
                 nlohmann::json::array({swatch[0], swatch[1], swatch[2],
                                        swatch[3]}),
                 error)
                 ? successMessage
                 : error;
    if (notice == successMessage) {
      const std::string hex =
          colorToHex({swatch[0], swatch[1], swatch[2], swatch[3]});
      std::strncpy(buffer.data(), hex.c_str(), buffer.size() - 1);
      buffer[buffer.size() - 1] = '\0';
    }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.0F);
  char hexId[64];
  std::snprintf(hexId, sizeof(hexId), "##%s-hex", field);
  if (ImGui::InputText(hexId, buffer.data(), buffer.size(),
                       ImGuiInputTextFlags_EnterReturnsTrue |
                           ImGuiInputTextFlags_CharsUppercase)) {
    std::string error;
    notice = workspace
                 .setHudNodeField(node.id, field,
                                  nlohmann::json(std::string(buffer.data())),
                                  error)
                 ? successMessage
                 : error;
  }
}

const char *dockLabel(const runtime::ui::UiNode &node,
                      const nlohmann::json *authored) {
  if (authored != nullptr) {
    const auto dock = authored->find("dock");
    if (dock != authored->end() && dock->is_string())
      return dock->get<std::string>().c_str();
  }
  // Infer from resolved anchors when the author used explicit anchors.
  if (node.layout.anchorMin.x == 0.0F && node.layout.anchorMin.y == 0.0F &&
      node.layout.anchorMax.x == 1.0F && node.layout.anchorMax.y == 1.0F)
    return "fill (inferred)";
  return "custom";
}

} // namespace

void drawEditorHudNodeInspector(EditorWorkspace &workspace,
                                const ImVec2 position, const ImVec2 size,
                                EditorHudInspectorState &state,
                                std::string &notice) {
  beginEditorPanel("HudNodeInspector", position, size);
  if (ImGui::BeginTabBar("hud-inspector-tabs")) {
    if (ImGui::BeginTabItem("Inspector"))
      ImGui::EndTabItem();
    ImGui::EndTabBar();
  }

  const runtime::ui::UiNode *selectedNode = workspace.selectedHudNode();
  if (selectedNode == nullptr) {
    ImGui::TextDisabled("The selected HUD element no longer exists.");
    ImGui::End();
    return;
  }
  const runtime::ui::UiNode node = *selectedNode;
  const EditorHudDocument *document = workspace.hudDocument();
  const bool authored =
      document != nullptr && document->authoredNode(node.id) != nullptr;
  if (state.nodeId != node.id) {
    state = {};
    state.nodeId = node.id;
    std::strncpy(state.text.data(), node.text.c_str(), state.text.size() - 1);
    std::strncpy(state.texture.data(), node.texture.c_str(),
                 state.texture.size() - 1);
    const std::string bgHex = colorToHex(node.backgroundColor);
    std::strncpy(state.backgroundHex.data(), bgHex.c_str(),
                 state.backgroundHex.size() - 1);
    const std::string textHex = colorToHex(node.textColor);
    std::strncpy(state.textHex.data(), textHex.c_str(),
                 state.textHex.size() - 1);
    state.backgroundHexInitialized = true;
    state.textHexInitialized = true;
  }

  ImGui::TextUnformatted(node.id.c_str());
  ImGui::TextDisabled("HUD element · %s", node.type.c_str());
  if (!authored)
    ImGui::TextColored({0.91F, 0.68F, 0.30F, 1.0F}, "Generated by a UI prefab");
  ImGui::Separator();

  ImGui::CollapsingHeader("UI Element", ImGuiTreeNodeFlags_DefaultOpen);
  ImGui::TextDisabled("Parent");
  ImGui::SameLine(105.0F);
  ImGui::TextUnformatted(node.parent.empty() ? "HUD root"
                                             : node.parent.c_str());
  ImGui::BeginDisabled(!authored);
  bool visible = node.visible;
  if (ImGui::Checkbox("Visible", &visible)) {
    std::string error;
    notice = workspace.setHudNodeField(node.id, "visible", visible, error)
                 ? "HUD visibility modified"
                 : error;
  }
  bool respectsSafeArea = node.respectsSafeArea;
  if (ImGui::Checkbox("Respect Safe Area", &respectsSafeArea)) {
    std::string error;
    notice = workspace.setHudNodeField(node.id, "respect_safe_area",
                                       respectsSafeArea, error)
                 ? "HUD safe-area behavior modified"
                 : error;
  }
  if (!respectsSafeArea)
    ImGui::TextDisabled("Layout uses the full screen behind cutouts.");
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!authored);
  if (node.type == "label" || node.type == "button" ||
      node.type == "text_input") {
    if (ImGui::InputText("Text", state.text.data(), state.text.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
      std::string error;
      notice =
          workspace.setHudNodeField(node.id, "text", state.text.data(), error)
              ? "HUD text modified"
              : error;
    }
    float fontSize = node.fontSize;
    if (ImGui::InputFloat("Font Size", &fontSize, 1.0F, 4.0F, "%.1f")) {
      std::string error;
      notice = workspace.setHudNodeField(node.id, "font_size",
                                         std::max(fontSize, 1.0F), error)
                   ? "HUD font size modified"
                   : error;
    }
  }
  if (node.type == "image" &&
      ImGui::InputText("Texture", state.texture.data(), state.texture.size(),
                       ImGuiInputTextFlags_EnterReturnsTrue)) {
    std::string error;
    notice = workspace.setHudNodeField(node.id, "texture", state.texture.data(),
                                       error)
                 ? "HUD texture modified"
                 : error;
  }
  float background[4]{node.backgroundColor.r, node.backgroundColor.g,
                      node.backgroundColor.b, node.backgroundColor.a};
  if (ImGui::ColorEdit4("Background", background)) {
    std::string error;
    notice = workspace.setHudNodeField(
                 node.id, "background_color",
                 nlohmann::json::array({background[0], background[1],
                                        background[2], background[3]}),
                 error)
                 ? "HUD background modified"
                 : error;
    if (notice == "HUD background modified") {
      const std::string hex = colorToHex(
          {background[0], background[1], background[2], background[3]});
      std::strncpy(state.backgroundHex.data(), hex.c_str(),
                   state.backgroundHex.size() - 1);
      state.backgroundHex[state.backgroundHex.size() - 1] = '\0';
    }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.0F);
  if (ImGui::InputText("##background-hex", state.backgroundHex.data(),
                       state.backgroundHex.size(),
                       ImGuiInputTextFlags_EnterReturnsTrue |
                           ImGuiInputTextFlags_CharsUppercase)) {
    std::string error;
    notice =
        workspace
            .setHudNodeField(node.id, "background_color",
                             nlohmann::json(std::string(state.backgroundHex.data())),
                             error)
            ? "HUD background modified"
            : error;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Hex color, e.g. #RRGGBB or #RRGGBBAA");
  if (node.type == "label" || node.type == "button" ||
      node.type == "text_input" || node.type == "text") {
    editColorHex(workspace, node, "Text Color", "text_color", node.textColor,
                 state.textHex, state.textHexInitialized,
                 "HUD text color modified", notice);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Hex color, e.g. #RRGGBB or #RRGGBBAA");
  }
  ImGui::EndDisabled();

  ImGui::CollapsingHeader("Rect Transform", ImGuiTreeNodeFlags_DefaultOpen);
  ImGui::BeginDisabled(!authored);
  // Dock preset: writes dock + clears explicit anchors so the file stays
  // terse. Custom anchor edits below clear dock back to explicit anchors.
  const nlohmann::json *authoredNode =
      document != nullptr ? document->authoredNode(node.id) : nullptr;
  ImGui::TextDisabled("Dock");
  ImGui::SameLine(105.0F);
  ImGui::TextUnformatted(dockLabel(node, authoredNode));
  if (authored) {
    const char *docks[] = {"fill", "top", "bottom", "left",
                           "right", "center", "custom"};
    int current = 6;
    if (authoredNode != nullptr) {
      const auto dock = authoredNode->find("dock");
      if (dock != authoredNode->end() && dock->is_string()) {
        const std::string name = dock->get<std::string>();
        for (int i = 0; i < 6; ++i)
          if (name == docks[i])
            current = i;
      }
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::Combo("##dock", &current, docks, 7)) {
      std::string error;
      if (current < 6) {
        // Write dock preset; explicit anchors are removed by the parser
        // expansion, so drop them from the authored JSON to keep it terse.
        nlohmann::json patch = authoredNode != nullptr
                                   ? *authoredNode
                                   : nlohmann::json::object();
        patch["dock"] = docks[current];
        patch.erase("anchor_min");
        patch.erase("anchor_max");
        notice = workspace.setHudNodeField(node.id, "dock",
                                           nlohmann::json(docks[current]),
                                           error)
                     ? "HUD dock preset modified"
                     : error;
        if (notice == "HUD dock preset modified") {
          std::string anchorError;
          (void)workspace.setHudNodeField(node.id, "anchor_min",
                                          nlohmann::json(nullptr), anchorError);
          (void)workspace.setHudNodeField(node.id, "anchor_max",
                                          nlohmann::json(nullptr), anchorError);
        }
      } else {
        notice = workspace.setHudNodeField(node.id, "dock",
                                           nlohmann::json(nullptr), error)
                     ? "HUD dock cleared to custom anchors"
                     : error;
      }
    }
    // Stack shorthand for flow containers: writes layout + gap alias.
    if (node.type == "container") {
      const char *stacks[] = {"none", "row", "column", "grid"};
      int stackCurrent = 0;
      if (authoredNode != nullptr) {
        const auto stack = authoredNode->find("stack");
        const auto layout = authoredNode->find("layout");
        const std::string name =
            (stack != authoredNode->end() && stack->is_string())
                ? stack->get<std::string>()
                : (layout != authoredNode->end() && layout->is_string())
                      ? layout->get<std::string>()
                      : "";
        for (int i = 1; i < 4; ++i)
          if (name == stacks[i])
            stackCurrent = i;
      }
      ImGui::TextDisabled("Stack");
      ImGui::SameLine(105.0F);
      ImGui::SetNextItemWidth(-1.0F);
      if (ImGui::Combo("##stack", &stackCurrent, stacks, 4)) {
        std::string error;
        notice = workspace.setHudNodeField(
                     node.id, "stack",
                     stackCurrent == 0 ? nlohmann::json(nullptr)
                                       : nlohmann::json(stacks[stackCurrent]),
                     error)
                     ? "HUD stack layout modified"
                     : error;
      }
    }
  }
  float nodePosition[2]{node.layout.position.x, node.layout.position.y};
  if (ImGui::InputFloat2("Position", nodePosition, "%.1f"))
    setVec2(workspace, node, "position", nodePosition, "HUD position modified",
            notice);
  float nodeSize[2]{node.layout.size.x, node.layout.size.y};
  if (ImGui::InputFloat2("Size", nodeSize, "%.1f")) {
    nodeSize[0] = std::max(nodeSize[0], 0.0F);
    nodeSize[1] = std::max(nodeSize[1], 0.0F);
    setVec2(workspace, node, "size", nodeSize, "HUD size modified", notice);
  }
  float anchorMin[2]{node.layout.anchorMin.x, node.layout.anchorMin.y};
  if (ImGui::InputFloat2("Anchor Min", anchorMin, "%.2f")) {
    anchorMin[0] = std::clamp(anchorMin[0], 0.0F, 1.0F);
    anchorMin[1] = std::clamp(anchorMin[1], 0.0F, 1.0F);
    setVec2(workspace, node, "anchor_min", anchorMin, "HUD anchors modified",
            notice);
  }
  float anchorMax[2]{node.layout.anchorMax.x, node.layout.anchorMax.y};
  if (ImGui::InputFloat2("Anchor Max", anchorMax, "%.2f")) {
    anchorMax[0] = std::clamp(anchorMax[0], 0.0F, 1.0F);
    anchorMax[1] = std::clamp(anchorMax[1], 0.0F, 1.0F);
    setVec2(workspace, node, "anchor_max", anchorMax, "HUD anchors modified",
            notice);
  }
  // Uniform pad: writes the terse pad alias (scalar) and clears an explicit
  // padding array so the file does not carry both. Per-side edits still go
  // through padding directly.
  if (!state.uniformPadInitialized) {
    const auto &pad = node.layout.padding;
    if (pad.left == pad.top && pad.left == pad.right &&
        pad.left == pad.bottom)
      state.uniformPad = pad.left;
    state.uniformPadInitialized = true;
  }
  float uniformPad = state.uniformPad;
  if (ImGui::InputFloat("Pad", &uniformPad, 1.0F, 4.0F, "%.1f")) {
    uniformPad = std::max(uniformPad, 0.0F);
    state.uniformPad = uniformPad;
    std::string error;
    notice =
        workspace.setHudNodeField(node.id, "pad", uniformPad, error)
            ? "HUD padding modified"
            : error;
    if (notice == "HUD padding modified") {
      std::string clearError;
      (void)workspace.setHudNodeField(node.id, "padding",
                                      nlohmann::json(nullptr), clearError);
    }
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Uniform padding on all sides (pad alias)");
  float gap = node.layout.gap;
  if (ImGui::InputFloat("Gap", &gap, 1.0F, 4.0F, "%.1f")) {
    std::string error;
    notice = workspace.setHudNodeField(node.id, "gap", std::max(gap, 0.0F),
                                       error)
                 ? "HUD gap modified"
                 : error;
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("Resolved: %.1f, %.1f  %.1f x %.1f", node.resolved.x,
                      node.resolved.y, node.resolved.width,
                      node.resolved.height);

  ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), size.y - 78.0F));
  ImGui::BeginDisabled(!authored);
  if (ImGui::Button("Delete UI Element", {-1.0F, 28.0F})) {
    std::string error;
    notice =
        workspace.deleteSelectedHudNode(error) ? "HUD element deleted" : error;
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(document == nullptr || !document->isDirty());
  if (ImGui::Button("Save HUD", {-1.0F, 28.0F})) {
    std::string error;
    notice = workspace.saveHud(error) ? "HUD saved" : error;
  }
  ImGui::EndDisabled();
  ImGui::End();
}

} // namespace demi::editor
