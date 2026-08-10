#include "demi/runtime/render/HudTextMetrics.h"
#include "demi/runtime/ui/UiActionController.h"
#include "demi/runtime/ui/UiDocumentParser.h"
#include "demi/runtime/ui/UiInteractionController.h"
#include "demi/runtime/ui/UiLayoutEngine.h"
#include "demi/runtime/ui/UiPresentation.h"

#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

bool near(float left, float right) { return std::abs(left - right) < 0.01F; }

bool verifyAspect(demi::runtime::ui::UiDocument document,
                  demi::runtime::Vec2 viewport) {
  demi::runtime::ui::UiLayoutEngine{}.layout(document, viewport);
  const auto &root = document.nodes[0];
  const auto &first = document.nodes[1];
  const auto &second = document.nodes[2];
  return near(root.resolved.width, viewport.x) &&
         near(root.resolved.height, viewport.y) && first.resolved.x >= 0.0F &&
         first.resolved.x + first.resolved.width <= viewport.x &&
         second.resolved.y > first.resolved.y;
}

} // namespace

int main() {
  const demi::runtime::HudTextMetrics nativeMetrics =
      demi::runtime::hudTextMetrics(28.0F, 1.0F);
  const demi::runtime::HudTextMetrics compactMetrics =
      demi::runtime::hudTextMetrics(28.0F, 0.2F);
  if (!near(nativeMetrics.fontSize, 28.0F) ||
      !near(nativeMetrics.letterSpacing, 5.0F) ||
      !near(compactMetrics.fontSize, 10.0F) ||
      compactMetrics.letterSpacing < 1.0F ||
      compactMetrics.letterSpacing >= nativeMetrics.letterSpacing) {
    std::cerr << "HUD text metrics did not preserve readable compact text.\n";
    return 1;
  }

  using nlohmann::json;
  const demi::runtime::ui::UiNode defaultContainer{.type = "container"};
  const demi::runtime::ui::UiNode defaultPanel{.type = "panel"};
  const demi::runtime::ui::UiNode coloredContainer{
      .type = "container",
      .backgroundColor = {0.1F, 0.2F, 0.3F, 0.75F},
  };
  if (!near(demi::runtime::ui::uiPanelFillColor(defaultContainer).a, 0.0F) ||
      !near(demi::runtime::ui::uiPanelFillColor(defaultPanel).a, 0.0F) ||
      !near(demi::runtime::ui::uiPanelFillColor(coloredContainer).a, 0.75F)) {
    std::cerr
        << "Layout containers did not preserve transparent backgrounds.\n";
    return 1;
  }

  auto document = demi::runtime::ui::parseUiDocument(json::parse(R"({
    "format_version": 1,
    "canvas_size": [960, 540],
    "styles": {
      "menu": {"background_color": [0.1, 0.2, 0.3, 1], "padding": 24, "gap": 12}
    },
    "localization": {"menu.play": "Play"},
    "action_map": {"submit": "ui_accept"},
    "action_effects": {
      "open_music": {"hide": ["play"], "show": ["locked"], "focus": "music"}
    },
    "root": {
      "id": "menu", "type": "container", "style": "menu",
      "anchor_min": [0, 0], "anchor_max": [1, 1],
      "layout": "column", "alignment": "center",
      "children": [
        {"id": "play", "type": "button", "localization_key": "menu.play", "action": "play", "size": [240, 48]},
        {"id": "locked", "type": "button", "text": "Locked", "disabled": true, "size": [240, 48]},
        {"id": "music", "type": "toggle", "text": "Music", "action": "toggle_music", "size": [240, 48]},
        {"id": "volume", "type": "slider", "minimum": 0, "maximum": 100, "value": 25, "size": [240, 24]},
        {"id": "search", "type": "text_input", "placeholder": "Search", "size": [240, 40]}
      ]
    }
  })"));

  if (document.nodes.size() != 6 || document.nodes[1].text != "Play" ||
      document.nodes[5].placeholder != "Search" ||
      document.actionMap["submit"] != "ui_accept" ||
      !verifyAspect(document, {1920.0F, 1080.0F}) ||
      !verifyAspect(document, {1024.0F, 768.0F}) ||
      !verifyAspect(document, {720.0F, 1280.0F})) {
    std::cerr << "UI parsing or responsive layout failed.\n";
    return 1;
  }

  auto panelDocument = demi::runtime::ui::parseUiDocument(json::parse(R"({
    "format_version": 1,
    "canvas_size": [400, 300],
    "root": {
      "id": "root", "type": "container",
      "anchor_min": [0, 0], "anchor_max": [1, 1],
      "children": [{
        "id": "panel", "type": "panel",
        "anchor_min": [0.25, 0.25], "anchor_max": [0.75, 0.75],
        "padding": [10, 20, 30, 40],
        "children": [{
          "id": "anchored_button", "type": "button",
          "action": "nested_action",
          "anchor_min": [1, 1], "anchor_max": [1, 1],
          "position": [-60, -30], "size": [50, 20]
        }]
      }]
    }
  })"));
  demi::runtime::ui::UiLayoutEngine{}.layout(panelDocument, {400.0F, 300.0F});
  if (panelDocument.nodes.size() != 3 ||
      panelDocument.nodes[2].parent != "panel" ||
      !near(panelDocument.nodes[1].resolved.x, 100.0F) ||
      !near(panelDocument.nodes[1].resolved.y, 75.0F) ||
      !near(panelDocument.nodes[1].resolved.width, 200.0F) ||
      !near(panelDocument.nodes[1].resolved.height, 150.0F) ||
      !near(panelDocument.nodes[2].resolved.x, 210.0F) ||
      !near(panelDocument.nodes[2].resolved.y, 155.0F)) {
    std::cerr << "Panel children did not resolve anchors against the panel's "
                 "padded content rectangle.\n";
    return 1;
  }
  demi::runtime::ui::UiInteractionController panelInteraction;
  const auto &nestedButton = panelDocument.nodes[2].resolved;
  if (!panelInteraction.capturePointer(
          panelDocument,
          {nestedButton.x + nestedButton.width * 0.5F,
           nestedButton.y + nestedButton.height * 0.5F}) ||
      panelDocument.pointerCaptures[0] != "anchored_button" ||
      panelInteraction.activateFocused(panelDocument) != "nested_action") {
    std::cerr << "Nested panel button did not receive pointer focus.\n";
    return 1;
  }
  panelInteraction.releasePointer(panelDocument);

  demi::runtime::ui::UiLayoutEngine{}.layout(document, {960.0F, 540.0F});
  demi::runtime::ui::UiDocument safeAreaDocument;
  safeAreaDocument.safeArea = {.left = 10.0F,
                               .top = 20.0F,
                               .right = 30.0F,
                               .bottom = 40.0F};
  safeAreaDocument.nodes.push_back(
      {.id = "safe_root",
       .type = "container",
       .layout = {.anchorMin = {0.0F, 0.0F},
                  .anchorMax = {1.0F, 1.0F}}});
  demi::runtime::ui::UiLayoutEngine{}.layout(safeAreaDocument,
                                             {200.0F, 150.0F});
  if (!near(safeAreaDocument.nodes[0].resolved.x, 10.0F) ||
      !near(safeAreaDocument.nodes[0].resolved.y, 20.0F) ||
      !near(safeAreaDocument.nodes[0].resolved.width, 160.0F) ||
      !near(safeAreaDocument.nodes[0].resolved.height, 90.0F)) {
    std::cerr << "UI roots did not respect application safe-area insets.\n";
    return 1;
  }
  demi::runtime::ui::UiInteractionController interaction;
  if (!interaction.focusNext(document) || document.focusedId != "play" ||
      !interaction.activateFocused(document).has_value() ||
      interaction.activateFocused(document) != "play") {
    std::cerr << "UI focus skipped or activated the wrong control.\n";
    return 1;
  }
  if (!interaction.capturePointer(document,
                                  {document.nodes[1].resolved.x + 1.0F,
                                   document.nodes[1].resolved.y + 1.0F}) ||
      document.pointerCaptures[0] != "play") {
    std::cerr << "UI pointer capture failed.\n";
    return 1;
  }
  interaction.releasePointer(document);
  if (document.pointerCaptures.contains(0)) {
    std::cerr << "UI pointer capture was not released.\n";
    return 1;
  }
  const demi::runtime::Vec2 playPoint{
      document.nodes[1].resolved.x + 1.0F,
      document.nodes[1].resolved.y + 1.0F};
  const demi::runtime::Vec2 musicPoint{
      document.nodes[3].resolved.x + 1.0F,
      document.nodes[3].resolved.y + 1.0F};
  if (!interaction.capturePointer(document, 11, playPoint) ||
      !interaction.capturePointer(document, 12, musicPoint) ||
      !interaction.pointerCaptured(document, 11) ||
      !interaction.pointerCaptured(document, 12)) {
    std::cerr << "UI did not preserve capture independently per pointer.\n";
    return 1;
  }
  interaction.releasePointer(document, 11);
  if (interaction.pointerCaptured(document, 11) ||
      !interaction.pointerCaptured(document, 12)) {
    std::cerr << "Releasing one UI pointer disturbed another capture.\n";
    return 1;
  }
  interaction.releasePointer(document, 12);
  document.focusedId = "play";
  if (!interaction.focusNext(document) || document.focusedId != "music" ||
      interaction.activateFocused(document) != "toggle_music" ||
      !document.nodes[3].checked) {
    std::cerr << "UI disabled-state skipping or toggle activation failed.\n";
    return 1;
  }
  const auto &slider = document.nodes[4].resolved;
  if (!interaction.capturePointer(
          document, {slider.x + slider.width * 0.75F, slider.y + 1.0F}) ||
      !interaction.updatePointer(
          document, {slider.x + slider.width * 0.75F, slider.y + 1.0F}) ||
      !near(document.nodes[4].value, 75.0F)) {
    std::cerr << "UI slider pointer state failed.\n";
    return 1;
  }
  if (!demi::runtime::ui::UiActionController{}.apply(document, "open_music") ||
      document.nodes[1].visible || document.focusedId != "music") {
    std::cerr << "UI declarative action effects failed.\n";
    return 1;
  }

  demi::runtime::ui::UiDocument layered;
  layered.nodes = {
      {.id = "child", .parent = "root", .type = "label", .layer = 7},
      {.id = "front", .type = "label", .layer = 100},
      {.id = "root", .type = "panel", .layer = -20, .visible = false},
      {.id = "orphan", .parent = "missing", .type = "label", .layer = -100},
  };
  auto presentation = demi::runtime::ui::buildUiPresentation(layered);
  if (presentation.size() != 4 || presentation[0].node->id != "orphan" ||
      presentation[1].node->id != "root" ||
      presentation[2].node->id != "child" || presentation[2].visible ||
      presentation[2].effectiveLayer != -13 ||
      presentation[3].node->id != "front" || !presentation[3].visible) {
    std::cerr << "UI presentation did not resolve inherited visibility and "
                 "effective layers deterministically.\n";
    return 1;
  }
  return 0;
}
