#include "demi/runtime/scripting/bindings/hud/LuaHudBindings.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaHudBindingModule::install(LuaScriptHost &host, lua_State *state) const {
  sol::table hud = sol::state_view(state).create_named_table("Hud");
  hud.set_function(
      "text", [&host](const std::string &id, const std::string &text, float x,
                      float y, sol::optional<float> scale,
                      sol::optional<float> r, sol::optional<float> g,
                      sol::optional<float> b, sol::optional<float> a) {
        return host.createHudText(id, text, x, y, scale.value_or(3.0F),
                                  {r.value_or(1.0F), g.value_or(1.0F),
                                   b.value_or(1.0F), a.value_or(1.0F)});
      });
  hud.set_function(
      "rect",
      [&host](const std::string &id, float x, float y, float width,
              float height, sol::optional<float> r, sol::optional<float> g,
              sol::optional<float> b, sol::optional<float> a) {
        return host.createHudRect(id, x, y, width, height,
                                  {r.value_or(1.0F), g.value_or(1.0F),
                                   b.value_or(1.0F), a.value_or(1.0F)});
      });
  hud.set_function("set_text",
                   [&host](const std::string &id, const std::string &text) {
                     return host.setHudText(id, text);
                   });
  hud.set_function("set_text_scale",
                   [&host](const std::string &id, float scale) {
                     return host.setHudTextScale(id, scale);
                   });
  hud.set_function("set_button_label",
                   [&host](const std::string &id, const std::string &label) {
                     return host.setHudButtonLabel(id, label);
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
  hud.set_function("set_opacity",
                   [&host](const std::string &id, float opacity) {
                     return host.setHudOpacity(id, opacity);
                   });
  hud.set_function("set_visible", [&host](const std::string &id, bool visible) {
    return host.setHudVisible(id, visible);
  });
  hud.set_function("set_group_visible",
                   [&host](const std::string &group, bool visible) {
                     return host.setHudGroupVisible(group, visible);
                   });
  hud.set_function("get_text",
                   [&host](const std::string &id) { return host.hudText(id); });
}
} // namespace demi::runtime
