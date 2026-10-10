#include "editor/EditorHudNodeInspector.h"

#include "editor/EditorColorControl.h"
#include "editor/EditorHudColorEncoding.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorWorkspace.h"
#include "demi/filesystem/ProjectPaths.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>

namespace demi::editor {
namespace {

using Json = nlohmann::json;
constexpr float CompactInspectorWidth = 340.0F;

template <std::size_t Size>
void syncBuffer(std::array<char, Size> &buffer, std::string &synced,
                const std::string_view value) {
  if (synced == value)
    return;
  synced = value;
  const std::size_t count = std::min(value.size(), buffer.size() - 1);
  std::memcpy(buffer.data(), value.data(), count);
  buffer[count] = '\0';
}

bool setField(EditorWorkspace &workspace, const runtime::ui::UiNode &node,
              const char *field, Json value, std::string message,
              std::string &notice, bool continuous = false) {
  std::string error;
  if (!workspace.setHudNodeField(node.id, field, std::move(value), error,
                                 continuous)) {
    notice = std::move(error);
    return false;
  }
  notice = std::move(message);
  return true;
}

void setVec2(EditorWorkspace &workspace, const runtime::ui::UiNode &node,
             const char *field, const float values[2], std::string message,
             std::string &notice) {
  (void)setField(workspace, node, field, Json::array({values[0], values[1]}),
                 std::move(message), notice);
}

void setInsets(EditorWorkspace &workspace, const runtime::ui::UiNode &node,
               const char *field, const float values[4], std::string message,
               std::string &notice) {
  (void)setField(workspace, node, field,
                 Json::array({values[0], values[1], values[2], values[3]}),
                 std::move(message), notice);
}

template <std::size_t Size>
void editHudString(EditorWorkspace &workspace, const runtime::ui::UiNode &node,
                   const char *widgetId, const char *field,
                   std::array<char, Size> &buffer, const char *message,
                   std::string &notice, bool multiline = false) {
  // Keep ImGui editing state with its document and node across selection
  // changes.
  ImGui::PushID(&workspace);
  ImGui::PushID(node.id.c_str());
  const bool submitted =
      multiline ? ImGui::InputTextMultiline(
                      widgetId, buffer.data(), buffer.size(), {0.0F, 54.0F},
                      ImGuiInputTextFlags_EnterReturnsTrue |
                          ImGuiInputTextFlags_CtrlEnterForNewLine)
                : ImGui::InputText(widgetId, buffer.data(), buffer.size(),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
  if (ImGui::IsItemActivated())
    workspace.endHudContinuousEdit();
  if (ImGui::IsItemEdited())
    (void)setField(workspace, node, field,
                   buffer[0] == '\0' ? Json(nullptr) : Json(buffer.data()),
                   message, notice, true);
  if (submitted || ImGui::IsItemDeactivated())
    workspace.endHudContinuousEdit();
  ImGui::PopID();
  ImGui::PopID();
}

struct PropertyGrid {
  explicit PropertyGrid(const char *id) {
    table = ImGui::GetContentRegionAvail().x >= CompactInspectorWidth &&
            ImGui::BeginTable(id, 2,
                              ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_NoSavedSettings);
    if (table) {
      ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed,
                              116.0F);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    }
  }

  ~PropertyGrid() {
    if (table)
      ImGui::EndTable();
  }

  template <typename Draw>
  void row(const char *label, Draw &&draw, const char *help = nullptr) const {
    if (table) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%s", label);
      if (help != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", help);
      ImGui::TableSetColumnIndex(1);
    } else {
      ImGui::TextDisabled("%s", label);
      if (help != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", help);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::forward<Draw>(draw)();
  }

  bool table = false;
};

template <std::size_t Size>
void editColor(EditorWorkspace &workspace, const runtime::ui::UiNode &node,
               const char *id, const char *field, const runtime::Color &color,
               std::array<char, Size> &buffer, std::string &synced,
               const char *successMessage, std::string &notice) {
  syncBuffer(buffer, synced, editorHudColorHex(color));
  float swatch[4]{color.r, color.g, color.b, color.a};
  const float available = ImGui::GetContentRegionAvail().x;
  ImGui::SetNextItemWidth(std::max(available - 98.0F, 56.0F));
  const std::string colorId = std::string("##") + id + "-color";
  if (drawEditorColorControl(colorId.c_str(), swatch)) {
    const std::string hex =
        editorHudColorHex({swatch[0], swatch[1], swatch[2], swatch[3]});
    if (setField(workspace, node, field, hex, successMessage, notice)) {
      synced.clear();
      syncBuffer(buffer, synced, hex);
    }
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-FLT_MIN);
  const std::string hexId = std::string("##") + id + "-hex";
  if (ImGui::InputText(hexId.c_str(), buffer.data(), buffer.size(),
                       ImGuiInputTextFlags_EnterReturnsTrue |
                           ImGuiInputTextFlags_CharsUppercase))
    (void)setField(workspace, node, field, std::string(buffer.data()),
                   successMessage, notice);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Hex color: #RRGGBB or #RRGGBBAA");
}

std::string dockLabel(const runtime::ui::UiNode &node, const Json *authored) {
  if (authored != nullptr) {
    const auto dock = authored->find("dock");
    if (dock != authored->end() && dock->is_string())
      return dock->get<std::string>();
  }
  if (node.layout.anchorMin.x == 0.0F && node.layout.anchorMin.y == 0.0F &&
      node.layout.anchorMax.x == 1.0F && node.layout.anchorMax.y == 1.0F)
    return "Custom (fills parent)";
  return "Custom anchors";
}

bool isTextNode(const std::string_view type) {
  return type == "label" || type == "text" || type == "button" ||
         type == "toggle" || type == "text_input";
}

bool isInteractiveNode(const std::string_view type) {
  return type == "button" || type == "toggle" || type == "slider" ||
         type == "text_input" || type == "virtual_button" ||
         type == "virtual_stick";
}

bool laysOutChildren(const std::string_view type) {
  return type == "container" || type == "panel" || type == "scroll" ||
         type == "list" || type == "modal";
}

int layoutDirectionIndex(const runtime::ui::LayoutDirection direction) {
  switch (direction) {
  case runtime::ui::LayoutDirection::Row:
    return 1;
  case runtime::ui::LayoutDirection::Column:
    return 2;
  case runtime::ui::LayoutDirection::Grid:
    return 3;
  case runtime::ui::LayoutDirection::None:
    return 0;
  }
  return 0;
}

int alignmentIndex(const runtime::ui::Alignment alignment) {
  switch (alignment) {
  case runtime::ui::Alignment::Center:
    return 1;
  case runtime::ui::Alignment::End:
    return 2;
  case runtime::ui::Alignment::Stretch:
    return 3;
  case runtime::ui::Alignment::Start:
    return 0;
  }
  return 0;
}

int textWrapIndex(const runtime::ui::TextWrapMode wrap) {
  switch (wrap) {
  case runtime::ui::TextWrapMode::Word:
    return 1;
  case runtime::ui::TextWrapMode::Grapheme:
    return 2;
  case runtime::ui::TextWrapMode::None:
    return 0;
  }
  return 0;
}

int textOverflowIndex(const runtime::ui::TextOverflowMode overflow) {
  switch (overflow) {
  case runtime::ui::TextOverflowMode::Visible:
    return 0;
  case runtime::ui::TextOverflowMode::Clip:
    return 1;
  case runtime::ui::TextOverflowMode::Ellipsis:
    return 2;
  }
  return 1;
}

void drawDockPresets(EditorWorkspace &workspace,
                     const runtime::ui::UiNode &node,
                     const std::string_view current, std::string &notice) {
  struct Preset {
    const char *label;
    const char *value;
  };
  constexpr std::array<Preset, 6> presets{{
      {"Fill", "fill"},
      {"Top", "top"},
      {"Bottom", "bottom"},
      {"Left", "left"},
      {"Right", "right"},
      {"Center", "center"},
  }};
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float buttonWidth = std::max(
      (ImGui::GetContentRegionAvail().x - spacing * 2.0F) / 3.0F, 48.0F);
  for (std::size_t index = 0; index < presets.size(); ++index) {
    const bool selected = current == presets[index].value;
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_Header));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    }
    const std::string id =
        std::string(presets[index].label) + "##dock-" + presets[index].value;
    const bool pressed = ImGui::Button(id.c_str(), {buttonWidth, 0.0F});
    if (selected)
      ImGui::PopStyleColor(2);
    if (pressed)
      (void)setField(workspace, node, "dock", presets[index].value,
                     "HUD dock preset modified", notice);
    if (index % 3 != 2)
      ImGui::SameLine();
  }
}

void drawAnchorPresets(EditorWorkspace &workspace,
                       const runtime::ui::UiNode &node,
                       const bool hasDockPreset, std::string &notice) {
  struct Preset {
    const char *label;
    runtime::Vec2 anchor;
  };
  constexpr std::array<Preset, 9> presets{{
      {"TL", {0.0F, 0.0F}},
      {"T", {0.5F, 0.0F}},
      {"TR", {1.0F, 0.0F}},
      {"L", {0.0F, 0.5F}},
      {"C", {0.5F, 0.5F}},
      {"R", {1.0F, 0.5F}},
      {"BL", {0.0F, 1.0F}},
      {"B", {0.5F, 1.0F}},
      {"BR", {1.0F, 1.0F}},
  }};
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float buttonWidth = std::max(
      (ImGui::GetContentRegionAvail().x - spacing * 2.0F) / 3.0F, 34.0F);
  for (std::size_t index = 0; index < presets.size(); ++index) {
    const runtime::Vec2 anchor = presets[index].anchor;
    const bool selected = !hasDockPreset &&
                          node.layout.anchorMin.x == anchor.x &&
                          node.layout.anchorMin.y == anchor.y &&
                          node.layout.anchorMax.x == anchor.x &&
                          node.layout.anchorMax.y == anchor.y;
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_Header));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    }
    const std::string id =
        std::string(presets[index].label) + "##anchor-preset";
    const bool pressed = ImGui::Button(id.c_str(), {buttonWidth, 0.0F});
    if (selected)
      ImGui::PopStyleColor(2);
    if (pressed) {
      std::string error;
      notice = workspace.setHudNodeAnchors(node.id, anchor, anchor, error)
                   ? "HUD anchor preset modified"
                   : error;
    }
    if (index % 3 != 2)
      ImGui::SameLine();
  }
}

void drawLayoutSection(EditorWorkspace &workspace,
                       const runtime::ui::UiNode &node,
                       const Json *authoredNode, std::string &notice) {
  if (!ImGui::CollapsingHeader("Layout", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  PropertyGrid grid("hud-layout-properties");
  const std::string currentDock =
      authoredNode != nullptr && authoredNode->contains("dock") &&
              (*authoredNode)["dock"].is_string()
          ? (*authoredNode)["dock"].get<std::string>()
          : std::string{};
  grid.row(
      "Dock preset",
      [&] { drawDockPresets(workspace, node, currentDock, notice); },
      "Pins or stretches the element against its parent.");
  grid.row(
      "Anchor preset",
      [&] { drawAnchorPresets(workspace, node, !currentDock.empty(), notice); },
      "Pins the element to one point without stretching it.");
  grid.row("Anchor mode", [&] {
    ImGui::TextUnformatted(dockLabel(node, authoredNode).c_str());
    if (!currentDock.empty()) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Make custom##dock-custom"))
        (void)setField(workspace, node, "dock", nullptr,
                       "HUD dock converted to custom anchors", notice);
    }
  });

  float anchorMin[2]{node.layout.anchorMin.x, node.layout.anchorMin.y};
  grid.row("Anchor min", [&] {
    if (ImGui::InputFloat2("##anchor-min", anchorMin, "%.2f")) {
      anchorMin[0] = std::clamp(anchorMin[0], 0.0F, 1.0F);
      anchorMin[1] = std::clamp(anchorMin[1], 0.0F, 1.0F);
      setVec2(workspace, node, "anchor_min", anchorMin, "HUD anchors modified",
              notice);
    }
  });
  float anchorMax[2]{node.layout.anchorMax.x, node.layout.anchorMax.y};
  grid.row("Anchor max", [&] {
    if (ImGui::InputFloat2("##anchor-max", anchorMax, "%.2f")) {
      anchorMax[0] = std::clamp(anchorMax[0], 0.0F, 1.0F);
      anchorMax[1] = std::clamp(anchorMax[1], 0.0F, 1.0F);
      setVec2(workspace, node, "anchor_max", anchorMax, "HUD anchors modified",
              notice);
    }
  });
  float position[2]{node.layout.position.x, node.layout.position.y};
  grid.row("Position", [&] {
    if (ImGui::InputFloat2("##position", position, "%.1f"))
      setVec2(workspace, node, "position", position, "HUD position modified",
              notice);
  });
  float size[2]{node.layout.size.x, node.layout.size.y};
  grid.row("Size", [&] {
    if (ImGui::InputFloat2("##size", size, "%.1f")) {
      size[0] = std::max(size[0], 0.0F);
      size[1] = std::max(size[1], 0.0F);
      setVec2(workspace, node, "size", size, "HUD size modified", notice);
    }
  });
  float minimumSize[2]{node.layout.minSize.x, node.layout.minSize.y};
  grid.row("Minimum size", [&] {
    if (ImGui::InputFloat2("##minimum-size", minimumSize, "%.1f")) {
      minimumSize[0] = std::max(minimumSize[0], 0.0F);
      minimumSize[1] = std::max(minimumSize[1], 0.0F);
      setVec2(workspace, node, "min_size", minimumSize,
              "HUD minimum size modified", notice);
    }
  });
  float maximumSize[2]{node.layout.maxSize.x, node.layout.maxSize.y};
  grid.row("Maximum size", [&] {
    if (ImGui::InputFloat2("##maximum-size", maximumSize, "%.1f")) {
      maximumSize[0] = std::max(maximumSize[0], 0.0F);
      maximumSize[1] = std::max(maximumSize[1], 0.0F);
      setVec2(workspace, node, "max_size", maximumSize,
              "HUD maximum size modified", notice);
    }
  });
  float margin[4]{node.layout.margin.left, node.layout.margin.top,
                  node.layout.margin.right, node.layout.margin.bottom};
  grid.row("Margin L T R B", [&] {
    if (ImGui::InputFloat4("##margin", margin, "%.1f"))
      setInsets(workspace, node, "margin", margin, "HUD margin modified",
                notice);
  });
  float padding[4]{node.layout.padding.left, node.layout.padding.top,
                   node.layout.padding.right, node.layout.padding.bottom};
  grid.row("Padding L T R B", [&] {
    if (ImGui::InputFloat4("##padding", padding, "%.1f")) {
      for (float &value : padding)
        value = std::max(value, 0.0F);
      setInsets(workspace, node, "padding", padding, "HUD padding modified",
                notice);
    }
  });

  if (laysOutChildren(node.type)) {
    constexpr const char *directions[] = {"Free", "Row", "Column", "Grid"};
    constexpr const char *directionValues[] = {"", "row", "column", "grid"};
    int direction = layoutDirectionIndex(node.layout.direction);
    grid.row(
        "Child layout",
        [&] {
          if (ImGui::Combo("##child-layout", &direction, directions, 4))
            (void)setField(workspace, node, "stack",
                           direction == 0 ? Json(nullptr)
                                          : Json(directionValues[direction]),
                           "HUD child layout modified", notice);
        },
        "Free keeps authored child positions. Row, column, and grid arrange "
        "children automatically.");
    constexpr const char *alignments[] = {"Start", "Center", "End", "Stretch"};
    constexpr const char *alignmentValues[] = {"start", "center", "end",
                                               "stretch"};
    int alignment = alignmentIndex(node.layout.alignment);
    grid.row("Child alignment", [&] {
      if (ImGui::Combo("##child-alignment", &alignment, alignments, 4))
        (void)setField(workspace, node, "alignment", alignmentValues[alignment],
                       "HUD child alignment modified", notice);
    });
    float gap = node.layout.gap;
    grid.row("Gap", [&] {
      if (ImGui::InputFloat("##gap", &gap, 1.0F, 4.0F, "%.1f"))
        (void)setField(workspace, node, "gap", std::max(gap, 0.0F),
                       "HUD gap modified", notice);
    });
    if (node.layout.direction == runtime::ui::LayoutDirection::Grid) {
      int columns = node.layout.columns;
      grid.row("Grid columns", [&] {
        if (ImGui::InputInt("##grid-columns", &columns))
          (void)setField(workspace, node, "columns", std::max(columns, 1),
                         "HUD grid columns modified", notice);
      });
    }
  }
  grid.row(
      "Resolved",
      [&] {
        ImGui::Text("%.1f, %.1f   %.1f x %.1f", node.resolved.x,
                    node.resolved.y, node.resolved.width, node.resolved.height);
      },
      "Final canvas-space rectangle after anchors and parent layout.");
}

void drawContentSection(EditorWorkspace &workspace,
                        const runtime::ui::UiNode &node,
                        EditorHudInspectorState &state, std::string &notice) {
  if (!ImGui::CollapsingHeader("Content", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  PropertyGrid grid("hud-content-properties");
  if (isTextNode(node.type)) {
    grid.row("Text", [&] {
      editHudString(workspace, node, "##text", "text", state.text,
                    "HUD text modified", notice, true);
    });
    grid.row(
        "Font asset",
        [&] {
          editHudString(workspace, node, "##font", "font", state.font,
                        "HUD font modified", notice);
        },
        "Optional asset:// Font2D ID. Empty uses the engine default.");
    float fontSize = node.fontSize;
    grid.row("Font size", [&] {
      if (ImGui::InputFloat("##font-size", &fontSize, 1.0F, 4.0F, "%.1f"))
        (void)setField(workspace, node, "font_size", std::max(fontSize, 1.0F),
                       "HUD font size modified", notice);
    });
    float lineSpacing = node.lineSpacing;
    grid.row("Line spacing", [&] {
      if (ImGui::InputFloat("##line-spacing", &lineSpacing, 1.0F, 4.0F, "%.1f"))
        (void)setField(workspace, node, "line_spacing", lineSpacing,
                       "HUD line spacing modified", notice);
    });
    int maxLines = static_cast<int>(node.maxLines);
    grid.row(
        "Maximum lines",
        [&] {
          if (ImGui::InputInt("##max-lines", &maxLines))
            (void)setField(workspace, node, "max_lines", std::max(maxLines, 0),
                           "HUD maximum lines modified", notice);
        },
        "Zero allows any number of lines.");
    constexpr const char *wrapLabels[] = {"No wrap", "Words", "Graphemes"};
    constexpr const char *wrapValues[] = {"none", "word", "grapheme"};
    int wrap = textWrapIndex(node.textWrap);
    grid.row("Wrapping", [&] {
      if (ImGui::Combo("##text-wrap", &wrap, wrapLabels, 3))
        (void)setField(workspace, node, "text_wrap", wrapValues[wrap],
                       "HUD text wrapping modified", notice);
    });
    constexpr const char *overflowLabels[] = {"Visible", "Clip", "Ellipsis"};
    constexpr const char *overflowValues[] = {"visible", "clip", "ellipsis"};
    int overflow = textOverflowIndex(node.textOverflow);
    grid.row("Overflow", [&] {
      if (ImGui::Combo("##text-overflow", &overflow, overflowLabels, 3))
        (void)setField(workspace, node, "text_overflow",
                       overflowValues[overflow], "HUD text overflow modified",
                       notice);
    });
    constexpr const char *textAlignments[] = {"Start", "Center", "End"};
    constexpr const char *textAlignmentValues[] = {"start", "center", "end"};
    int horizontal = std::min(alignmentIndex(node.textHorizontalAlignment), 2);
    grid.row("Horizontal align", [&] {
      if (ImGui::Combo("##text-horizontal", &horizontal, textAlignments, 3))
        (void)setField(workspace, node, "text_alignment",
                       textAlignmentValues[horizontal],
                       "HUD text alignment modified", notice);
    });
    int vertical = std::min(alignmentIndex(node.textVerticalAlignment), 2);
    grid.row("Vertical align", [&] {
      if (ImGui::Combo("##text-vertical", &vertical, textAlignments, 3))
        (void)setField(workspace, node, "text_vertical_alignment",
                       textAlignmentValues[vertical],
                       "HUD text alignment modified", notice);
    });
  }
  if (node.type == "text_input") {
    grid.row("Placeholder", [&] {
      editHudString(workspace, node, "##placeholder", "placeholder",
                    state.placeholder, "HUD placeholder modified", notice);
    });
  }
  if (node.type == "image") {
    grid.row(
        "Texture asset",
        [&] {
          editHudString(workspace, node, "##texture", "texture", state.texture,
                        "HUD texture modified", notice);
        },
        "asset:// texture ID");
  }
  if (!isTextNode(node.type) && node.type != "image")
    ImGui::TextDisabled("This element has no direct text or image content.");
}

void drawAppearanceSection(EditorWorkspace &workspace,
                           const runtime::ui::UiNode &node,
                           EditorHudInspectorState &state,
                           std::string &notice) {
  if (!ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  PropertyGrid grid("hud-appearance-properties");
  grid.row(
      "Style",
      [&] {
        editHudString(workspace, node, "##style", "style", state.style,
                      "HUD style modified", notice);
      },
      "Optional named style from the HUD theme.");
  grid.row("Tint", [&] {
    editColor(workspace, node, "tint", "color", node.color, state.tintHex,
              state.syncedTintHex, "HUD tint modified", notice);
  });
  grid.row("Background", [&] {
    editColor(workspace, node, "background", "background_color",
              node.backgroundColor, state.backgroundHex,
              state.syncedBackgroundHex, "HUD background modified", notice);
  });
  grid.row("Border", [&] {
    editColor(workspace, node, "border", "border_color", node.borderColor,
              state.borderHex, state.syncedBorderHex,
              "HUD border color modified", notice);
  });
  if (isTextNode(node.type)) {
    grid.row("Text color", [&] {
      editColor(workspace, node, "text", "text_color", node.textColor,
                state.textHex, state.syncedTextHex, "HUD text color modified",
                notice);
    });
  }
  float borderWidth = node.borderWidth;
  grid.row("Border width", [&] {
    if (ImGui::InputFloat("##border-width", &borderWidth, 0.5F, 2.0F, "%.1f"))
      (void)setField(workspace, node, "border_width",
                     std::max(borderWidth, 0.0F), "HUD border width modified",
                     notice);
  });
  float cornerRadius = node.cornerRadius;
  grid.row("Corner radius", [&] {
    if (ImGui::InputFloat("##corner-radius", &cornerRadius, 1.0F, 4.0F, "%.1f"))
      (void)setField(workspace, node, "corner_radius",
                     std::max(cornerRadius, 0.0F), "HUD corner radius modified",
                     notice);
  });
  int layer = node.layer;
  grid.row("Draw layer", [&] {
    if (ImGui::InputInt("##draw-layer", &layer))
      (void)setField(workspace, node, "layer", layer, "HUD draw layer modified",
                     notice);
  });
}

void drawInteractionSection(EditorWorkspace &workspace,
                            const runtime::ui::UiNode &node,
                            EditorHudInspectorState &state,
                            std::string &notice) {
  if (!ImGui::CollapsingHeader("Interaction", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  PropertyGrid grid("hud-interaction-properties");
  bool visible = node.visible;
  grid.row("Visible", [&] {
    if (ImGui::Checkbox("##visible", &visible))
      (void)setField(workspace, node, "visible", visible,
                     "HUD visibility modified", notice);
  });
  bool blocksPointer = node.blocksPointer;
  grid.row(
      "Block pointer",
      [&] {
        if (ImGui::Checkbox("##blocks-pointer", &blocksPointer))
          (void)setField(workspace, node, "blocks_pointer", blocksPointer,
                         "HUD pointer blocking modified", notice);
      },
      "Capture pointer clicks over this node without adding keyboard focus.");
  bool respectsSafeArea = node.respectsSafeArea;
  grid.row(
      "Safe area",
      [&] {
        if (ImGui::Checkbox("Protect from cutouts##safe-area",
                            &respectsSafeArea))
          (void)setField(workspace, node, "respect_safe_area", respectsSafeArea,
                         "HUD safe-area behavior modified", notice);
      },
      "Disable only for intentional edge-to-edge decoration.");
  if (isInteractiveNode(node.type)) {
    bool disabled = node.disabled;
    grid.row("Disabled", [&] {
      if (ImGui::Checkbox("##disabled", &disabled))
        (void)setField(workspace, node, "disabled", disabled,
                       "HUD disabled state modified", notice);
    });
    bool focusable = node.focusable;
    grid.row("Focusable", [&] {
      if (ImGui::Checkbox("Keyboard / controller##focusable", &focusable))
        (void)setField(workspace, node, "focusable", focusable,
                       "HUD focus behavior modified", notice);
    });
    grid.row(
        "Action",
        [&] {
          editHudString(workspace, node, "##action", "action", state.action,
                        "HUD action modified", notice);
        },
        "Action name emitted when the control is activated.");
  }
  if (node.type == "toggle") {
    bool checked = node.checked;
    grid.row("Checked", [&] {
      if (ImGui::Checkbox("##checked", &checked))
        (void)setField(workspace, node, "checked", checked,
                       "HUD toggle state modified", notice);
    });
  }
  if (node.type == "slider" || node.type == "progress") {
    float minimum = node.minimum;
    grid.row("Minimum", [&] {
      if (ImGui::InputFloat("##minimum", &minimum, 0.05F, 0.25F, "%.3f"))
        (void)setField(workspace, node, "minimum", minimum,
                       "HUD minimum value modified", notice);
    });
    float maximum = node.maximum;
    grid.row("Maximum", [&] {
      if (ImGui::InputFloat("##maximum", &maximum, 0.05F, 0.25F, "%.3f"))
        (void)setField(workspace, node, "maximum", maximum,
                       "HUD maximum value modified", notice);
    });
    float value = node.value;
    grid.row("Value", [&] {
      if (ImGui::InputFloat("##value", &value, 0.05F, 0.25F, "%.3f"))
        (void)setField(workspace, node, "value", value, "HUD value modified",
                       notice);
    });
  }
  grid.row("Accessible label", [&] {
    editHudString(workspace, node, "##accessible-label", "accessibility_label",
                  state.accessibilityLabel, "HUD accessibility label modified",
                  notice);
  });
  grid.row("Description", [&] {
    editHudString(workspace, node, "##accessible-description",
                  "accessibility_description", state.accessibilityDescription,
                  "HUD accessibility description modified", notice);
  });
  if (ImGui::TreeNode("Local UI actions")) {
    const auto *source = workspace.hudDocument()->authoringProperties(node.id);
    Json value = source ? source->value("action_effects", Json::object())
                        : Json::object();
    const auto edit = drawStructuredValue(value, state.actions);
    if (edit.changed)
      (void)setField(workspace, node, "action_effects", std::move(value),
                     "UI action effects modified", notice, edit.continuous);
    if (edit.finished)
      workspace.endHudContinuousEdit();
    ImGui::TreePop();
  }
  bool accessibilityHidden = node.accessibilityHidden;
  grid.row("Accessibility", [&] {
    if (ImGui::Checkbox("Hide decorative subtree##accessible-hidden",
                        &accessibilityHidden))
      (void)setField(workspace, node, "accessibility_hidden",
                     accessibilityHidden,
                     "HUD accessibility visibility modified", notice);
  });
}

void drawPrefabArguments(EditorWorkspace &workspace,
                         const runtime::ui::UiNode &node,
                         const Json &authoredNode,
                         EditorHudInspectorState &state, std::string &notice) {
  if (!ImGui::CollapsingHeader("Prefab Parameters",
                               ImGuiTreeNodeFlags_DefaultOpen))
    return;
  std::string error;
  const auto defaults =
      workspace.hudDocument()->prefabArguments(node.id, error);
  if (!defaults) {
    ImGui::TextWrapped("%s", error.c_str());
    return;
  }
  if (defaults->empty()) {
    ImGui::TextDisabled("This UI prefab has no parameters.");
    return;
  }
  const Json explicitArguments =
      authoredNode.value("arguments", Json::object());
  PropertyGrid grid("hud-prefab-arguments");
  for (const auto &[name, value] : defaults->items()) {
    ImGui::PushID(name.c_str());
    grid.row(name.c_str(), [&] {
      Json candidate = value;
      auto &valueState = state.prefabValues[name];
      const auto edit = drawStructuredValue(candidate, valueState);
      if (edit.changed) {
        Json replacement = explicitArguments;
        replacement[name] = std::move(candidate);
        (void)setField(workspace, node, "arguments", std::move(replacement),
                       "UI prefab parameter overridden", notice,
                       edit.continuous);
      }
      if (edit.finished)
        workspace.endHudContinuousEdit();
      if (explicitArguments.contains(name)) {
        if (ImGui::SmallButton("Reset to default")) {
          Json replacement = explicitArguments;
          replacement.erase(name);
          (void)setField(workspace, node, "arguments", std::move(replacement),
                         "UI prefab parameter reset", notice);
        }
      } else
        ImGui::TextDisabled("Inherited default");
    });
    ImGui::PopID();
  }
}

void drawInstanceOverrides(EditorWorkspace &workspace,
                           const runtime::ui::UiNode &node,
                           std::string &notice) {
  const auto *document = workspace.hudDocument();
  const auto *patch = document ? document->nodeOverrides(node.id) : nullptr;
  if (!patch || patch->empty())
    return;
  const Json snapshot = *patch;
  if (!ImGui::CollapsingHeader("Instance overrides",
                               ImGuiTreeNodeFlags_DefaultOpen))
    return;
  for (const auto &[field, value] : snapshot.items()) {
    ImGui::PushID(field.c_str());
    ImGui::TextUnformatted(field.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
      std::string error;
      notice = workspace.resetHudNodeOverride(node.id, field, error)
                   ? "Property reset to prefab source"
                   : error;
    }
    ImGui::PopID();
  }
  if (ImGui::Button("Reset all node overrides")) {
    std::string error;
    notice = workspace.resetHudNodeOverride(node.id, {}, error)
                 ? "Node reset to prefab source"
                 : error;
  }
}

void syncInspectorState(EditorHudInspectorState &state,
                        const runtime::ui::UiNode &node) {
  if (state.nodeId != node.id) {
    state = {};
    state.nodeId = node.id;
  }
  syncBuffer(state.text, state.syncedText, node.textTemplate);
  syncBuffer(state.texture, state.syncedTexture, node.texture);
  syncBuffer(state.font, state.syncedFont, node.font);
  syncBuffer(state.style, state.syncedStyle, node.style);
  syncBuffer(state.placeholder, state.syncedPlaceholder,
             node.placeholderTemplate);
  syncBuffer(state.action, state.syncedAction, node.action);
  syncBuffer(state.accessibilityLabel, state.syncedAccessibilityLabel,
             node.accessibilityLabel);
  syncBuffer(state.accessibilityDescription,
             state.syncedAccessibilityDescription,
             node.accessibilityDescription);
}

} // namespace

void drawEditorHudNodeInspectorContents(EditorWorkspace &workspace,
                                        EditorHudInspectorState &state,
                                        std::string &notice) {
  const runtime::ui::UiNode *selectedNode = workspace.selectedHudNode();
  if (selectedNode == nullptr) {
    ImGui::TextDisabled("Select a HUD element to edit its properties.");
    return;
  }
  runtime::ui::UiNode node = *selectedNode;
  const EditorHudDocument *document = workspace.hudDocument();
  std::optional<Json> authoredNodeSnapshot;
  if (document != nullptr)
    if (const Json *authoredNode = document->authoredNode(node.id))
      authoredNodeSnapshot = *authoredNode;
  const Json *authoredNode =
      authoredNodeSnapshot ? &*authoredNodeSnapshot : nullptr;
  const bool implicitRoot = document != nullptr &&
                            document->hasImplicitRoot() && node.id == "ui_root";
  const bool prefabInstance =
      authoredNode != nullptr && authoredNode->contains("prefab");
  const auto *origin = document ? document->prefabOrigin(node.id) : nullptr;
  const bool editable =
      !implicitRoot && (authoredNode != nullptr || origin != nullptr);
  std::optional<Json> effectiveSnapshot;
  if (document)
    if (const auto *effective = document->authoringProperties(node.id))
      effectiveSnapshot = *effective;
  const Json *layoutSource =
      effectiveSnapshot ? &*effectiveSnapshot : authoredNode;
  if (effectiveSnapshot)
    node.visible = effectiveSnapshot->value("visible", true);
  if (document)
    if (const auto *properties = document->authoringProperties(node.id))
      node.action = properties->value("action", node.action);
  syncInspectorState(state, node);

  ImGui::TextUnformatted(node.id.c_str());
  ImGui::TextDisabled("%s  |  parent: %s", node.type.c_str(),
                      node.parent.empty() ? "HUD" : node.parent.c_str());
  if (implicitRoot)
    ImGui::TextColored({0.63F, 0.68F, 0.76F, 1.0F},
                       "Implicit fill root - select a child to edit");
  else if (prefabInstance)
    ImGui::TextColored({0.63F, 0.68F, 0.76F, 1.0F},
                       "UI prefab instance - changes override this instance");
  else if (origin)
    ImGui::TextColored({0.63F, 0.68F, 0.76F, 1.0F},
                       "Inherited prefab node - edits affect this instance");
  if (origin && origin->instanceId != node.id) {
    ImGui::TextDisabled("Instance: %s", origin->instanceId.c_str());
    if (ImGui::SmallButton("Select owning instance")) {
      workspace.selectHudNode(origin->instanceId);
      return;
    }
  }
  if ((prefabInstance || origin) &&
      ImGui::Button(prefabInstance ? "Unpack Prefab"
                                   : "Unpack owning prefab")) {
    std::string error;
    notice = workspace.unpackHudPrefab(node.id, error)
                 ? "Prefab unpacked; IDs and values preserved"
                 : error;
    return;
  }
  ImGui::Separator();

  if (document != nullptr && isHudFile(document->path()) && node.parent.empty()) {
    ImGui::SeparatorText("Canvas");
    const auto canvas = document->preview().canvasSize;
    float dimensions[]{canvas.x, canvas.y};
    ImGui::TextUnformatted("Design size (pixels)");
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::InputFloat2("##canvas-size", dimensions, "%.0f")) {
      std::string error;
      notice = workspace.setHudCanvasSize({dimensions[0], dimensions[1]}, error)
                   ? "Canvas size updated" : error;
    }
    ImGui::TextWrapped("Layout is authored in this coordinate space and scaled for the game view.");
  }

  if (document && document->hasPreviewState()) {
    ImGui::TextDisabled("Preview state; source visibility is unchanged");
    if (ImGui::Button("Reset preview state")) {
      std::string error;
      notice = workspace.resetHudPreviewState(error)
                   ? "Authored preview defaults restored"
                   : error;
      return;
    }
  }
  if (prefabInstance)
    drawPrefabArguments(workspace, node, *authoredNode, state, notice);
  {
    ImGui::BeginDisabled(!editable);
    drawLayoutSection(workspace, node, layoutSource, notice);
    drawAppearanceSection(workspace, node, state, notice);
    drawContentSection(workspace, node, state, notice);
    drawInteractionSection(workspace, node, state, notice);
    ImGui::EndDisabled();
  }

  if (prefabInstance && authoredNode->contains("overrides")) {
    for (const auto &[target, value] : authoredNode->at("overrides").items()) {
      if (!value.is_null())
        continue;
      ImGui::PushID(target.c_str());
      ImGui::Text("Removed: %s", target.c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Restore")) {
        std::string error;
        notice = workspace.resetHudPrefabTarget(node.id, target, error)
                     ? "Inherited node restored"
                     : error;
      }
      ImGui::PopID();
    }
  }
  drawInstanceOverrides(workspace, node, notice);
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::BeginDisabled(!editable);
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
}

void drawEditorHudNodeInspector(EditorWorkspace &workspace,
                                const ImVec2 position, const ImVec2 size,
                                EditorHudInspectorState &state,
                                std::string &notice, bool *open) {
  if (beginEditorPanel("Inspector", position, size, open))
    drawEditorHudNodeInspectorContents(workspace, state, notice);
  ImGui::End();
}

} // namespace demi::editor
