#include "demi/runtime/ui/UiLegacyProjection.h"

#include "demi/runtime/ui/UiLayoutEngine.h"

#include <algorithm>
#include <unordered_map>

namespace demi::runtime::ui {
namespace {

Vec2 position(const UiNode &node) { return {node.resolved.x, node.resolved.y}; }

Vec2 size(const UiNode &node) {
  return {node.resolved.width, node.resolved.height};
}

void addPanel(World &world, const UiNode &node, const bool visible,
              const bool modalLayer) {
  world.hudPanels.push_back({.id = node.id,
                             .group = {},
                             .layer = modalLayer ? 20 : 1,
                             .position = position(node),
                             .size = size(node),
                             .color = node.backgroundColor,
                             .visible = visible});
}

void addButton(World &world, const UiNode &node, std::string label,
               const bool visible, const bool modalLayer) {
  world.hudButtons.push_back({.id = node.id,
                              .group = {},
                              .layer = modalLayer ? 21 : 2,
                              .label = std::move(label),
                              .position = position(node),
                              .size = size(node),
                              .fontSize = node.fontSize,
                              .color = node.backgroundColor,
                              .textColor = node.color,
                              .script = {},
                              .action = node.action,
                              .visible = visible && !node.disabled});
}

void addProgress(World &world, const UiNode &node, const bool visible,
                 const bool modalLayer) {
  world.hudRects.push_back({.id = node.id + ".track",
                            .group = {},
                            .layer = modalLayer ? 21 : 1,
                            .position = position(node),
                            .size = size(node),
                            .color = node.backgroundColor,
                            .visible = visible});
  const float range = std::max(node.maximum - node.minimum, 0.0001F);
  const float fraction =
      std::clamp((node.value - node.minimum) / range, 0.0F, 1.0F);
  world.hudRects.push_back(
      {.id = node.id + ".value",
       .group = {},
       .layer = modalLayer ? 22 : 2,
       .position = position(node),
       .size = {node.resolved.width * fraction, node.resolved.height},
       .color = node.color,
       .visible = visible});
}

} // namespace

void projectUiDocument(World &world) {
  UiLayoutEngine{}.layout(world.ui, world.ui.canvasSize);
  world.hudRects.clear();
  world.hudPanels.clear();
  world.hudCircles.clear();
  world.hudImages.clear();
  world.hudButtons.clear();
  world.hudText.clear();

  std::unordered_map<std::string, bool> visibleById;
  std::unordered_map<std::string, bool> modalById;
  for (const UiNode &node : world.ui.nodes) {
    const bool parentVisible = node.parent.empty() || visibleById[node.parent];
    const bool visible = node.visible && parentVisible;
    const bool modalLayer = node.type == "modal" || modalById[node.parent];
    visibleById[node.id] = visible;
    modalById[node.id] = modalLayer;
    if (node.type == "container" || node.type == "panel" ||
        node.type == "scroll" || node.type == "list" || node.type == "modal") {
      if (node.backgroundColor.a > 0.0F)
        addPanel(world, node, visible, modalLayer);
    } else if (node.type == "label" || node.type == "text") {
      world.hudText.push_back({.id = node.id,
                               .group = {},
                               .layer = modalLayer ? 22 : 3,
                               .text = node.text,
                               .position = position(node),
                               .fontSize = node.fontSize,
                               .color = node.color,
                               .visible = visible});
    } else if (node.type == "image") {
      world.hudImages.push_back({.id = node.id,
                                 .group = {},
                                 .layer = modalLayer ? 21 : 1,
                                 .texture = node.texture,
                                 .animation = {},
                                 .position = position(node),
                                 .size = size(node),
                                 .sourcePosition = {},
                                 .sourceSize = {},
                                 .color = node.color,
                                 .visible = visible});
    } else if (node.type == "button") {
      addButton(world, node, node.text, visible, modalLayer);
    } else if (node.type == "toggle") {
      addButton(world, node,
                std::string(node.checked ? "[x] " : "[ ] ") + node.text,
                visible, modalLayer);
    } else if (node.type == "text_input") {
      addButton(world, node, node.text.empty() ? node.placeholder : node.text,
                visible, modalLayer);
    } else if (node.type == "slider" || node.type == "progress") {
      addProgress(world, node, visible, modalLayer);
    }
  }
}

} // namespace demi::runtime::ui
