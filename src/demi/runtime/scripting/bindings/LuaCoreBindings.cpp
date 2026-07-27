#include "demi/runtime/scripting/bindings/LuaCoreBindings.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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
  input.set_function("is_released", [&host](const std::string& key) { return host.isKeyReleased(key); });
  input.set_function("action_down", [&host](const std::string& action, sol::optional<int> player) { return host.isActionDown(action, player.value_or(-1)); });
  input.set_function("action_pressed", [&host](const std::string& action, sol::optional<int> player) { return host.isActionPressed(action, player.value_or(-1)); });
  input.set_function("action_released", [&host](const std::string& action, sol::optional<int> player) { return host.isActionReleased(action, player.value_or(-1)); });
  input.set_function("action_value", [&host](const std::string& action, sol::optional<int> player) { return host.actionValue(action, player.value_or(-1)); });
  input.set_function("action_vector", [&host](const std::string& action, sol::optional<int> player) {
      const Vec2 value = host.actionVector(action, player.value_or(-1));
      return std::tuple{value.x, value.y};
    });
  input.set_function("action_source", [&host](const std::string& action, sol::optional<int> player) { return host.actionSource(action, player.value_or(-1)); });
  input.set_function("enable_context", [&host](const std::string& context) { host.enableInputContext(context); });
  input.set_function("disable_context", [&host](const std::string& context) { host.disableInputContext(context); });
  input.set_function("context_enabled", [&host](const std::string& context) { return host.inputContextEnabled(context); });
  input.set_function("rebind", [&host](const std::string& action, int binding, const std::string& control, sol::optional<int> player) {
      std::string error;
      const bool success =
          binding > 0
              ? host.rebindInput(action, static_cast<std::size_t>(binding - 1),
                                 control, player.value_or(-1), error)
              : false;
      if (binding <= 0) error = "binding index must be one or greater";
      return std::tuple{success, error};
    });
  input.set_function("save_bindings", [&host](const std::string& path) {
      std::string error;
      const bool success = host.saveInputBindings(path, error);
      return std::tuple{success, error};
    });
  input.set_function("load_bindings", [&host](const std::string& path) {
      std::string error;
      const bool success = host.loadInputBindings(path, error);
      return std::tuple{success, error};
    });
  input.set_function("assign_gamepad", [&host](int device, int player) { return host.assignGamepad(device, player); });
  input.set_function("gamepad_count", [&host] {
      const InputState* state = host.inputState();
      return state == nullptr ? std::size_t{0} : state->gamepads.size();
    });
  input.set_function("touch_count", [&host] {
      const InputState* state = host.inputState();
      if (state == nullptr) return std::size_t{0};
      return static_cast<std::size_t>(std::ranges::count_if(state->touches, [](const TouchPoint& touch) {
        return touch.phase != TouchPhase::Ended && touch.phase != TouchPhase::Cancelled;
      }));
    });
  input.set_function("touches", [state, &host] {
      sol::state_view view(state);
      sol::table result = view.create_table();
      const InputState* snapshot = host.inputState();
      if (snapshot == nullptr) return result;
      int index = 1;
      for (const TouchPoint& touch : snapshot->touches) {
        sol::table value = view.create_table();
        value["id"] = touch.id;
        value["phase"] = touch.phase == TouchPhase::Began ? "began" :
                         touch.phase == TouchPhase::Moved ? "moved" :
                         touch.phase == TouchPhase::Ended ? "ended" :
                         touch.phase == TouchPhase::Cancelled ? "cancelled" : "stationary";
        value["x"] = touch.position.x;
        value["y"] = touch.position.y;
        value["dx"] = touch.delta.x;
        value["dy"] = touch.delta.y;
        value["pressure"] = touch.pressure;
        result[index++] = value;
      }
      return result;
    });
  input.set_function("gestures", [state, &host] {
      sol::state_view view(state);
      sol::table result = view.create_table();
      int index = 1;
      for (const demi::runtime::input::GestureEvent& event : host.gestures()) {
        sol::table value = view.create_table();
        value["type"] = event.type == demi::runtime::input::GestureType::Tap ? "tap" :
                        event.type == demi::runtime::input::GestureType::DoubleTap ? "double_tap" :
                        event.type == demi::runtime::input::GestureType::LongPress ? "long_press" :
                        event.type == demi::runtime::input::GestureType::Drag ? "drag" :
                        event.type == demi::runtime::input::GestureType::Pinch ? "pinch" : "rotate";
        value["pointer_id"] = event.pointerId;
        value["x"] = event.position.x;
        value["y"] = event.position.y;
        value["dx"] = event.delta.x;
        value["dy"] = event.delta.y;
        value["value"] = event.value;
        result[index++] = value;
      }
      return result;
    });
  input.set_function("text_entered", [&host] { return host.textEntered(); });
  input.set_function("set_text_input_active", [&host](const bool active) {
      host.applicationServices().setKeyboardVisible(active);
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
  input.set_function("ui_pointer_captured", [&host](sol::optional<std::int64_t> pointer) { return host.uiPointerCaptured(pointer.value_or(0)); });

  sol::table time = lua.create_named_table("Time");
  time["delta_time"] = 0.0F;
  time["unscaled_delta_time"] = 0.0F;
  time["time"] = 0.0;
  time["fixed_time"] = 0.0;
  time["frame_count"] = 0;
  time["time_scale"] = 1.0F;
  time["paused"] = false;
  time.set_function("set_paused", [&host](bool paused) { host.setPaused(paused); });
  time.set_function("is_paused", [&host] { return host.paused(); });
  time.set_function("set_scale", [&host](float scale) { host.setTimeScale(scale); });
  time.set_function("get_scale", [&host] { return host.timeScale(); });

  sol::table timer = lua.create_named_table("Timer");
  timer.set_function("delay", [state, &host](float seconds, const sol::function callback) { return luaAddTimer(state, host, seconds, false, callback); });
  timer.set_function("every", [state, &host](float seconds, const sol::function callback) { return luaAddTimer(state, host, seconds, true, callback); });
  timer.set_function("cancel", [&host](std::uint64_t id) { return host.cancelTimer(id); });

  sol::table events = lua.create_named_table("Events");
  events.set_function("subscribe", [state, &host](const std::string& eventName, const sol::function callback) { return luaAddEventSubscription(state, host, eventName, callback); });
  events.set_function("unsubscribe", [&host](std::uint64_t id) { return host.removeEventSubscription(id); });
  events.set_function("emit", [state, &host](const std::string& eventName, sol::optional<sol::object> payload) { return luaEmitEvent(state, host, eventName, payload.value_or(sol::nil)); });

  sol::table scene = lua.create_named_table("Scene");
  scene.set_function("load", [&host](const std::string& sceneId) { return host.requestSceneLoad(sceneId); });
  scene.set_function("reload", [&host] { return host.requestSceneReload(); });
  scene.set_function("prepare", [&host](const std::string& sceneId, sol::optional<bool> additive) { return host.prepareScene(sceneId, additive.value_or(false)); });
  scene.set_function("cancel", [&host] { return host.cancelScenePreparation(); });
  scene.set_function("progress", [&host] { return host.scenePreparationProgress(); });
  scene.set_function("is_prepared", [&host] { return host.scenePrepared(); });
  scene.set_function("activate", [&host] { return host.requestPreparedSceneActivation(); });
  scene.set_function("unload", [&host](const std::string& sceneId) { return host.requestSceneUnload(sceneId); });
  scene.set_function("set_persistent", [&host](const std::string& entityId, bool persistent) { return host.setEntityPersistent(entityId, persistent); });
  scene.set_function("active", [&host] { return host.activeSceneId(); });
  scene.set_function("error", [&host] { return host.sceneFlowError(); });

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
  runtime.set_function("is_focused", [&host] { return host.applicationFocused(); });
  runtime.set_function("is_suspended", [&host] { return host.applicationSuspended(); });

  sol::table application = lua.create_named_table("Application");
  application.set_function("safe_area", [&host] {
      const auto value = host.applicationServices().safeArea();
      return std::tuple{value.left, value.top, value.right, value.bottom};
    });
  application.set_function("logical_dpi", [&host] { return host.applicationServices().logicalDpi(); });
  application.set_function("ui_scale", [&host] { return host.applicationServices().uiScale(); });
  application.set_function("orientation", [&host] {
      return platform::orientationName(host.applicationServices().orientation());
    });
  application.set_function("request_orientation", [&host](const std::string& value) {
      std::string normalized = value;
      std::ranges::transform(normalized, normalized.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      const platform::Orientation orientation =
          normalized == "portrait" ? platform::Orientation::Portrait :
          normalized == "landscape" ? platform::Orientation::Landscape :
                                      platform::Orientation::Unspecified;
      host.applicationServices().requestOrientation(orientation);
      return normalized == "portrait" || normalized == "landscape" ||
             normalized == "unspecified";
    });
  application.set_function("keyboard_visible", [&host] { return host.applicationServices().keyboardVisible(); });
  application.set_function("clipboard", [&host] { return host.applicationServices().clipboard(); });
  application.set_function("set_clipboard", [&host](const std::string& value) { host.applicationServices().setClipboard(value); });
  application.set_function("focused", [&host] { return host.applicationServices().focused(); });
  application.set_function("minimized", [&host] { return host.applicationServices().minimized(); });
  application.set_function("suspended", [&host] { return host.applicationServices().suspended(); });
  application.set_function("low_memory_generation", [&host] { return host.applicationServices().lowMemoryGeneration(); });
  application.set_function("user_data_path", [&host] { return host.applicationServices().userDataPath().string(); });
  application.set_function("cache_path", [&host] { return host.applicationServices().cachePath().string(); });
}

} // namespace demi::runtime
