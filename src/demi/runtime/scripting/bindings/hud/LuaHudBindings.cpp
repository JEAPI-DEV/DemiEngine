#include "demi/runtime/scripting/bindings/hud/LuaHudBindings.h"
#include "demi/runtime/ui/RichTextParser.h"
#include "demi/runtime/ui/TextLayoutEngine.h"
#include "demi/runtime/ui/UiAccessibilityTree.h"
#include "demi/runtime/ui/UiVirtualCollection.h"
#include <memory>
#include <sol/sol.hpp>
#include <tuple>
#include <vector>
namespace demi::runtime {
void LuaHudBindingModule::install(LuaScriptHost &host, lua_State *state) const {
  sol::state_view lua(state);
  lua.new_usertype<ui::UiNodeHandle>("HudNodeHandle", sol::no_constructor, "id",
                                     &ui::UiNodeHandle::id, "generation",
                                     &ui::UiNodeHandle::generation);
  lua.new_usertype<ui::UiVirtualLayout>(
      "HudVirtualLayout", sol::no_constructor, "item_count",
      &ui::UiVirtualLayout::itemCount, "total_extent",
      &ui::UiVirtualLayout::totalExtent, "visible_range",
      [](const ui::UiVirtualLayout &layout, const float offset,
         const float viewport, const sol::optional<std::size_t> overscan) {
        const ui::UiVirtualRange range =
            layout.visibleRange(offset, viewport, overscan.value_or(2));
        return std::tuple{range.first + 1, range.count,
                          layout.itemOffset(range.first), layout.totalExtent()};
      },
      "set_extent",
      [](ui::UiVirtualLayout &layout, const std::size_t oneBasedIndex,
         const float extent) {
        std::string error;
        const bool changed =
            oneBasedIndex > 0 &&
            layout.setItemExtent(oneBasedIndex - 1, extent, error);
        if (oneBasedIndex == 0)
          error = "Virtual item indices are one-based.";
        return std::tuple{changed, error};
      },
      "item_offset",
      [](const ui::UiVirtualLayout &layout, const std::size_t oneBasedIndex) {
        if (oneBasedIndex == 0 || oneBasedIndex > layout.itemCount())
          return std::tuple{0.0F,
                            std::string{"Virtual item index is out of range."}};
        return std::tuple{layout.itemOffset(oneBasedIndex - 1), std::string{}};
      });
  sol::table hud = lua.create_named_table("Hud");
  hud.set_function("find", [&host](const std::string &id) {
    return host.hudNodeHandle(id);
  });
  hud.set_function("create", [&host](const std::string &parent,
                                     const sol::table &definition) {
    ui::UiNode node;
    node.id = definition.get_or("id", std::string{});
    node.type = definition.get_or("type", std::string{"container"});
    node.text = definition.get_or("text", std::string{});
    node.action = definition.get_or("action", std::string{});
    node.style = definition.get_or("style", std::string{});
    node.texture = definition.get_or("texture", std::string{});
    node.accessibilityLabel =
        definition.get_or("accessibility_label", std::string{});
    node.accessibilityDescription =
        definition.get_or("accessibility_description", std::string{});
    node.accessibilityHidden = definition.get_or("accessibility_hidden", false);
    node.visible = definition.get_or("visible", true);
    node.disabled = definition.get_or("disabled", false);
    node.focusable = definition.get_or(
        "focusable", node.type == "button" || node.type == "toggle" ||
                         node.type == "slider" || node.type == "text_input");
    node.fontSize = definition.get_or("font_size", 20.0F);
    node.layout.position = {definition.get_or("x", 0.0F),
                            definition.get_or("y", 0.0F)};
    node.layout.size = {definition.get_or("width", 0.0F),
                        definition.get_or("height", 0.0F)};
    std::string error;
    auto handle = host.createHudNode(parent, std::move(node), error);
    return std::tuple{std::move(handle), error};
  });
  hud.set_function("clone", [&host](const ui::UiNodeHandle &source,
                                    const std::string &newRootId,
                                    sol::optional<std::string> parent) {
    std::string error;
    auto handle = host.cloneHudNode(source, newRootId,
                                    parent.value_or(std::string{}), error);
    return std::tuple{std::move(handle), error};
  });
  hud.set_function("remove", [&host](const ui::UiNodeHandle &node) {
    std::string error;
    return std::tuple{host.removeHudNode(node, error), error};
  });
  hud.set_function("reparent", [&host](const ui::UiNodeHandle &node,
                                       const std::string &parent) {
    std::string error;
    return std::tuple{host.reparentHudNode(node, parent, error), error};
  });
  hud.set_function("clear_children", [&host](const std::string &parent) {
    std::string error;
    return std::tuple{host.clearHudChildren(parent, error), error};
  });
  hud.set_function("children", [&host](const std::string &parent) {
    return sol::as_table(host.hudChildren(parent));
  });
  hud.set_function("accessibility_snapshot", [&host, state]() {
    sol::state_view lua(state);
    const auto snapshot = host.hudAccessibilitySnapshot();
    sol::table result = lua.create_table(static_cast<int>(snapshot.size()), 0);
    for (std::size_t index = 0; index < snapshot.size(); ++index) {
      const ui::UiAccessibilityNode &node = snapshot[index];
      sol::table value = lua.create_table();
      value["id"] = node.id;
      value["parent"] = node.parent;
      value["role"] = std::string(ui::uiAccessibilityRoleName(node.role));
      value["label"] = node.label;
      value["description"] = node.description;
      value["value_text"] = node.valueText;
      value["x"] = node.bounds.x;
      value["y"] = node.bounds.y;
      value["width"] = node.bounds.width;
      value["height"] = node.bounds.height;
      value["value"] = node.value;
      value["minimum"] = node.minimum;
      value["maximum"] = node.maximum;
      value["focused"] = node.focused;
      value["disabled"] = node.disabled;
      value["checked"] = node.checked;
      value["focusable"] = node.focusable;
      value["offscreen"] = node.offscreen;
      result[index + 1] = std::move(value);
    }
    return result;
  });
  hud.set_function("tween", [&host](const ui::UiNodeHandle &node,
                                    const std::string &property, float target,
                                    float duration) {
    std::string error;
    const auto handle =
        host.startHudTween(node, property, target, duration, error);
    return std::tuple{handle, error};
  });
  hud.set_function("cancel_tween", [&host](std::uint64_t handle) {
    return host.cancelHudTween(handle);
  });
  hud.set_function("set_reduced_motion", [&host](bool enabled) {
    host.setHudReducedMotion(enabled);
  });
  hud.set_function("set_locale", [&host](const std::string &locale) {
    std::string error;
    const bool changed = host.setHudLocale(locale, error);
    return std::tuple{changed, error};
  });
  hud.set_function("set_pseudo_locale",
                   [&host](bool enabled) { host.setHudPseudoLocale(enabled); });
  hud.set_function("visible_range", [](std::size_t count, float itemExtent,
                                       float offset, float viewport,
                                       sol::optional<std::size_t> overscan) {
    const auto range = ui::UiVirtualCollection::visibleRange(
        count, itemExtent, offset, viewport, overscan.value_or(2));
    return std::tuple{range.first + 1, range.count};
  });
  hud.set_function("virtual_layout", [](const sol::table &extents) {
    std::vector<float> values;
    values.reserve(extents.size());
    for (std::size_t index = 1; index <= extents.size(); ++index) {
      const sol::optional<float> value =
          extents.get<sol::optional<float>>(index);
      if (!value)
        return std::tuple{
            std::shared_ptr<ui::UiVirtualLayout>{},
            std::string{"Virtual item extents must be a dense numeric array."}};
      values.push_back(*value);
    }
    auto layout = std::make_shared<ui::UiVirtualLayout>();
    std::string error;
    if (!layout->reset(values, error))
      layout.reset();
    return std::tuple{std::move(layout), error};
  });
  hud.set_function(
      "recycle_rows",
      [&host, lua](const std::string &collectionId,
                   const ui::UiNodeHandle &rowTemplate,
                   const sol::table &keys, const sol::table &extents,
                   const float offset, const float viewport,
                   const sol::optional<std::size_t> overscan) mutable {
        std::vector<std::string> stableKeys;
        std::vector<float> rowExtents;
        stableKeys.reserve(keys.size());
        rowExtents.reserve(extents.size());
        for (std::size_t index = 1; index <= keys.size(); ++index) {
          const auto key = keys.get<sol::optional<std::string>>(index);
          if (!key) {
            sol::table empty = lua.create_table();
            return std::tuple{empty, std::string{
                                         "Virtual row keys must be a dense "
                                         "string array."}};
          }
          stableKeys.push_back(*key);
        }
        for (std::size_t index = 1; index <= extents.size(); ++index) {
          const auto extent = extents.get<sol::optional<float>>(index);
          if (!extent) {
            sol::table empty = lua.create_table();
            return std::tuple{empty, std::string{
                                         "Virtual row extents must be a dense "
                                         "number array."}};
          }
          rowExtents.push_back(*extent);
        }
        const auto result = host.reconcileHudRows(
            collectionId, rowTemplate, stableKeys, rowExtents, offset,
            viewport, overscan.value_or(2));
        sol::table rows = lua.create_table(static_cast<int>(result.rows.size()), 0);
        for (std::size_t index = 0; index < result.rows.size(); ++index) {
          const auto &binding = result.rows[index];
          sol::table row = lua.create_table();
          row["key"] = binding.key;
          row["index"] = binding.index + 1;
          row["node"] = binding.node;
          row["offset"] = binding.offset;
          row["extent"] = binding.extent;
          row["rebound"] = binding.rebound;
          rows[index + 1] = std::move(row);
        }
        return std::tuple{rows, result.error};
      });
  hud.set_function("clear_recycled_rows",
                   [&host](const std::string &collectionId) {
                     return host.clearHudRows(collectionId);
                   });
  hud.set_function("set_text",
                   [&host](const std::string &id, const std::string &text) {
                     return host.setHudText(id, text);
                   });
  hud.set_function("set_font_size",
                   [&host](const std::string &id, float fontSize) {
                     return host.setHudFontSize(id, fontSize);
                   });
  hud.set_function("set_rect", [&host](const std::string &id, float x, float y,
                                       float width, float height) {
    return host.setHudRect(id, x, y, width, height);
  });
  hud.set_function("set_image",
                   [&host](const std::string &id, const std::string &texture,
                           float x, float y, float width, float height) {
                     return host.setHudImage(id, texture, x, y, width, height);
                   });
  hud.set_function(
      "set_image_animation_frame",
      [&host](const std::string &id, const std::string &animation, int frame) {
        return host.setHudImageAnimationFrame(id, animation, frame);
      });
  hud.set_function("set_position",
                   [&host](const std::string &id, float x, float y) {
                     return host.setHudPosition(id, x, y);
                   });
  hud.set_function("set_size",
                   [&host](const std::string &id, float width, float height) {
                     return host.setHudSize(id, width, height);
                   });
  hud.set_function("set_color", [&host](const std::string &id, float r, float g,
                                        float b, sol::optional<float> a) {
    return host.setHudColor(id, {r, g, b, a.value_or(1.0F)});
  });
  hud.set_function("set_background_color",
                   [&host](const std::string &id, float r, float g, float b,
                           sol::optional<float> a) {
                     return host.setHudBackgroundColor(
                         id, {r, g, b, a.value_or(1.0F)});
                   });
  hud.set_function("set_opacity",
                   [&host](const std::string &id, float opacity) {
                     return host.setHudOpacity(id, opacity);
                   });
  hud.set_function("set_visible", [&host](const std::string &id, bool visible) {
    return host.setHudVisible(id, visible);
  });
  hud.set_function("set_value", [&host](const std::string &id, float value) {
    return host.setHudValue(id, value);
  });
  hud.set_function("set_checked", [&host](const std::string &id, bool checked) {
    return host.setHudChecked(id, checked);
  });
  hud.set_function("set_disabled",
                   [&host](const std::string &id, bool disabled) {
                     return host.setHudDisabled(id, disabled);
                   });
  hud.set_function("focus_next", [&host](sol::optional<bool> reverse) {
    return host.focusNextHudControl(reverse.value_or(false));
  });
  hud.set_function("focused", [&host]() { return host.focusedHudControl(); });
  hud.set_function("canvas_size", [&host]() {
    const Vec2 size = host.hudCanvasSize();
    return std::tuple{size.x, size.y};
  });
  hud.set_function("get_text",
                   [&host](const std::string &id) { return host.hudText(id); });

  sol::table textApi = lua.create_named_table("Text");
  textApi.set_function("grapheme_count", [](const std::string &text) {
    return ui::TextLayoutEngine::graphemeCount(text);
  });
  textApi.set_function("grapheme_slice", [](const std::string &text,
                                            std::size_t first,
                                            std::size_t count) {
    // Lua's public index is one-based; zero still means the first grapheme to
    // keep accidental underflow deterministic.
    return ui::TextLayoutEngine::graphemeSlice(text, first > 0 ? first - 1 : 0,
                                               count);
  });
  textApi.set_function(
      "layout", [lua](const std::string &text, float width, float fontSize,
                      sol::optional<std::size_t> maxLines) mutable {
        const auto result =
            ui::TextLayoutEngine{}.layout({.text = text,
                                           .width = width,
                                           .fontSize = fontSize,
                                           .maxLines = maxLines.value_or(0),
                                           .locale = {}});
        sol::table out = lua.create_table();
        out["width"] = result.width;
        out["height"] = result.height;
        out["grapheme_count"] = result.graphemeCount;
        out["truncated"] = result.truncated;
        out["valid_utf8"] = result.validUtf8;
        out["shaping_complete"] = result.shapingComplete;
        sol::table lines = lua.create_table();
        for (std::size_t index = 0; index < result.lines.size(); ++index) {
          sol::table line = lua.create_table();
          line["text"] = result.lines[index].text;
          line["x"] = result.lines[index].x;
          line["y"] = result.lines[index].y;
          line["width"] = result.lines[index].width;
          lines[index + 1] = line;
        }
        out["lines"] = lines;
        return out;
      });
  textApi.set_function("parse_rich", [lua](const std::string &markup,
                                           sol::optional<bool> strict) mutable {
    const auto parsed =
        ui::RichTextParser{}.parse(markup, strict.value_or(true));
    sol::table out = lua.create_table();
    out["text"] = parsed.text;
    out["diagnostics"] = sol::as_table(parsed.diagnostics);
    sol::table spans = lua.create_table();
    for (std::size_t index = 0; index < parsed.spans.size(); ++index) {
      sol::table span = lua.create_table();
      span["begin"] = parsed.spans[index].begin + 1;
      span["length"] = parsed.spans[index].length;
      span["style"] = parsed.spans[index].style;
      span["value"] = parsed.spans[index].value;
      spans[index + 1] = span;
    }
    out["spans"] = spans;
    return out;
  });
}
} // namespace demi::runtime
