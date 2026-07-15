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
  using nlohmann::json;
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

  demi::runtime::ui::UiLayoutEngine{}.layout(document, {960.0F, 540.0F});
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
      document.pointerCaptureId != "play") {
    std::cerr << "UI pointer capture failed.\n";
    return 1;
  }
  interaction.releasePointer(document);
  if (!document.pointerCaptureId.empty()) {
    std::cerr << "UI pointer capture was not released.\n";
    return 1;
  }
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
