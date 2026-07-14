#include "demi/runtime/scripting/bindings/LuaCoreBindings.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <sol/sol.hpp>

#include <cmath>
#include <iostream>
#include <tuple>

#if defined(__ANDROID__)
#include <android/native_activity.h>
extern "C" ANativeActivity* DemiGetNativeActivity(void);
#endif

namespace demi::runtime {

void LuaCoreBindingModule::install(LuaScriptHost& host, lua_State* state) const {
  sol::state_view lua(state);

  sol::table debug = lua.create_named_table("Debug");
  debug.set_function("log", [](const std::string& message) { std::cout << "[lua] " << message << '\n'; });
  debug.set_function("line", [&host](float x1, float y1, float x2, float y2, sol::optional<float> r, sol::optional<float> g, sol::optional<float> b, sol::optional<float> a, sol::optional<float> width) {
      host.addDebugLine(x1, y1, x2, y2, r.value_or(1.0F), g.value_or(1.0F), b.value_or(1.0F), a.value_or(1.0F), width.value_or(1.0F));
    });
  debug.set_function("clear_lines", [&host] { host.clearDebugLines(); });

  sol::table profile = lua.create_named_table("Profile");
  profile.set_function("enabled", [] { return RuntimeProfiler::enabled(); });
  profile.set_function("scope", [](const std::string& name, const sol::protected_function& callback) {
    ProfileScope scope(name);
    const sol::protected_function_result result = callback();
    if (!result.valid()) {
      const sol::error error = result;
      std::cerr << "[lua] Profile.scope callback failed: " << error.what() << '\n';
      return false;
    }
    return true;
  });

  sol::table input = lua.create_named_table("Input");
  input.set_function("is_down", [&host](const std::string& key) { return host.isKeyDown(key); });
  input.set_function("is_pressed", [&host](const std::string& key) { return host.isKeyPressed(key); });
  input.set_function("action_down", [&host](const std::string& action) { return host.isActionDown(action); });
  input.set_function("action_pressed", [&host](const std::string& action) { return host.isActionPressed(action); });
  input.set_function("action_value", [&host](const std::string& action) { return host.actionValue(action); });
  input.set_function("text_entered", [&host] { return host.textEntered(); });
  input.set_function("set_text_input_active", [](const bool active) {
#if defined(__ANDROID__)
      ANativeActivity* activity = DemiGetNativeActivity();
      if (activity != nullptr) {
        if (active) ANativeActivity_showSoftInput(activity, ANATIVEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
        else ANativeActivity_hideSoftInput(activity, ANATIVEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS);
      }
#else
      (void)active;
#endif
    });
  input.set_function("axis", [&host](const std::string& negative, const std::string& positive) { return (host.isKeyDown(positive) ? 1.0F : 0.0F) - (host.isKeyDown(negative) ? 1.0F : 0.0F); });
  input.set_function("vector", [&host](const std::string& left, const std::string& right, const std::string& down, const std::string& up) {
      float x = (host.isKeyDown(right) ? 1.0F : 0.0F) - (host.isKeyDown(left) ? 1.0F : 0.0F);
      float y = (host.isKeyDown(up) ? 1.0F : 0.0F) - (host.isKeyDown(down) ? 1.0F : 0.0F);
      const float length = std::sqrt(x * x + y * y);
      if (length > 0.0F) {
        x /= length;
        y /= length;
      }
      return std::tuple{x, y};
    });
  input.set_function("mouse_down", [&host](const std::string& button) { return host.isMouseDown(button); });
  input.set_function("mouse_position", [&host] { const Vec2 value = host.mousePosition(); return std::tuple{value.x, value.y}; });
  input.set_function("mouse_delta", [&host] { const Vec2 value = host.mouseDelta(); return std::tuple{value.x, value.y}; });
  input.set_function("mouse_world_position", [&host] { const Vec2 value = host.mouseWorldPosition(); return std::tuple{value.x, value.y}; });
  input.set_function("viewport_size", [&host] { const Vec2 value = host.viewportSize(); return std::tuple{value.x, value.y}; });

  sol::table time = lua.create_named_table("Time");
  time["delta_time"] = 0.0F;

  sol::table timer = lua.create_named_table("Timer");
  timer.set_function("delay", [state, &host](float seconds, const sol::function callback) { return luaAddTimer(state, host, seconds, false, callback); });
  timer.set_function("every", [state, &host](float seconds, const sol::function callback) { return luaAddTimer(state, host, seconds, true, callback); });
  timer.set_function("cancel", [&host](std::uint64_t id) { return host.cancelTimer(id); });

  sol::table events = lua.create_named_table("Events");
  events.set_function("subscribe", [state, &host](const std::string& eventName, const sol::function callback) { return luaAddEventSubscription(state, host, eventName, callback); });
  events.set_function("unsubscribe", [&host](std::uint64_t id) { return host.removeEventSubscription(id); });
  events.set_function("emit", [state, &host](const std::string& eventName, sol::optional<sol::object> payload) { return luaEmitEvent(state, host, eventName, payload.value_or(sol::nil)); });

  sol::table scene = lua.create_named_table("Scene");
  scene.set_function("load", [&host](const std::string& sceneId) { host.requestSceneLoad(sceneId); return true; });

  sol::table runtime = lua.create_named_table("Runtime");
  runtime.set_function("quit", [&host] { host.requestQuit(); });
#if defined(__ANDROID__)
  runtime.set_function("platform", [] { return "android"; });
#elif defined(_WIN32)
  runtime.set_function("platform", [] { return "windows"; });
#elif defined(__APPLE__)
  runtime.set_function("platform", [] { return "macos"; });
#elif defined(__linux__)
  runtime.set_function("platform", [] { return "linux"; });
#else
  runtime.set_function("platform", [] { return "unknown"; });
#endif
  runtime.set_function("set_physics_enabled", [&host](bool enabled) { host.setPhysicsEnabled(enabled); });
  runtime.set_function("set_window_mode", [&host](const std::string& mode) { host.setWindowMode(mode); });
  runtime.set_function("get_window_mode", [&host] { return host.windowMode(); });
  runtime.set_function("set_max_fps", [&host](double maxFps) { host.setMaxFps(static_cast<int>(std::round(maxFps))); });
  runtime.set_function("get_max_fps", [&host] { return host.maxFps(); });
  runtime.set_function("set_mouse_captured", [&host](bool captured) { host.setMouseCaptured(captured); });
  runtime.set_function("get_mouse_captured", [&host] { return host.mouseCaptured(); });
}

} // namespace demi::runtime
