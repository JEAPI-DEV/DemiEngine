#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/LuaScriptHostInternal.h"
#include "demi/runtime/ui/UiActionController.h"
#include "demi/runtime/ui/UiInteractionController.h"
#include "demi/runtime/ui/UiLayoutEngine.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

namespace demi::runtime {

std::string normalizedKey(std::string key) {
  std::ranges::transform(key, key.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return key;
}

void LuaScriptHost::setViewport(const int width, const int height) {
  viewportWidth_ = std::max(width, 1);
  viewportHeight_ = std::max(height, 1);
}

void LuaScriptHost::requestQuit() { quitRequested_ = true; }

bool LuaScriptHost::quitRequested() const { return quitRequested_; }

void LuaScriptHost::setWindowMode(std::string mode) {
  mode = normalizedKey(std::move(mode));
  if (mode != "windowed" && mode != "borderless" && mode != "fullscreen") {
    return;
  }
  if (windowMode_ == mode) {
    return;
  }
  windowMode_ = std::move(mode);
  windowModeDirty_ = true;
}

const std::string &LuaScriptHost::windowMode() const { return windowMode_; }

bool LuaScriptHost::windowModeDirty() const { return windowModeDirty_; }

void LuaScriptHost::clearWindowModeDirty() { windowModeDirty_ = false; }

void LuaScriptHost::setMaxFps(const int maxFps) {
  if (maxFps <= 0) {
    maxFps_ = 0;
    return;
  }
  maxFps_ = std::clamp(maxFps, 15, 1000);
}

int LuaScriptHost::maxFps() const { return maxFps_; }

void LuaScriptHost::setMouseCaptured(const bool captured) {
  if (mouseCaptured_ == captured) {
    return;
  }
  mouseCaptured_ = captured;
  mouseCapturedDirty_ = true;
}

bool LuaScriptHost::mouseCaptured() const { return mouseCaptured_; }

bool LuaScriptHost::mouseCapturedDirty() const { return mouseCapturedDirty_; }

void LuaScriptHost::clearMouseCapturedDirty() { mouseCapturedDirty_ = false; }

void LuaScriptHost::setPhysicsEnabled(const bool enabled) {
  physicsEnabled_ = enabled;
}

bool LuaScriptHost::physicsEnabled() const { return physicsEnabled_; }

std::uint64_t LuaScriptHost::addTimer(const float seconds, const bool repeating,
                                      const int callbackRef) {
  if (state_ == nullptr || seconds < 0.0F || callbackRef == LUA_NOREF) {
    return 0;
  }
  const std::uint64_t id = nextTimerId_++;
  timers_.push_back(TimerInstance{.id = id,
                                  .remaining = seconds,
                                  .interval = std::max(seconds, 0.0F),
                                  .repeating = repeating,
                                  .callbackRef = callbackRef});
  return id;
}

bool LuaScriptHost::cancelTimer(const std::uint64_t timerId) {
  for (TimerInstance &timer : timers_) {
    if (timer.id == timerId && !timer.cancelled) {
      timer.cancelled = true;
      return true;
    }
  }
  return false;
}

std::uint64_t LuaScriptHost::addEventSubscription(std::string eventName,
                                                  const int callbackRef) {
  if (state_ == nullptr || eventName.empty() || callbackRef == LUA_NOREF) {
    return 0;
  }
  const std::uint64_t id = nextEventSubscriptionId_++;
  eventSubscriptions_.push_back(EventSubscription{
      .id = id, .eventName = std::move(eventName), .callbackRef = callbackRef});
  return id;
}

bool LuaScriptHost::removeEventSubscription(
    const std::uint64_t subscriptionId) {
  for (EventSubscription &subscription : eventSubscriptions_) {
    if (subscription.id == subscriptionId && !subscription.cancelled) {
      subscription.cancelled = true;
      return true;
    }
  }
  return false;
}

int LuaScriptHost::emitEvent(const std::string &eventName,
                             const int payloadIndex) {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr || eventName.empty()) {
    return 0;
  }
  int delivered = 0;
  for (EventSubscription &subscription : eventSubscriptions_) {
    if (subscription.cancelled || subscription.eventName != eventName) {
      continue;
    }
    lua_rawgeti(state, LUA_REGISTRYINDEX, subscription.callbackRef);
    payloadIndex > 0 ? lua_pushvalue(state, payloadIndex) : lua_newtable(state);
    std::string error;
    if (!luaCall(state, 1, 0, error)) {
      luaReportCallbackError("Events.emit", {}, eventName, error);
    }
    ++delivered;
  }
  for (const ScriptInstance &script : scripts_) {
    for (const LuaEventHandler &handler : script.eventHandlers) {
      if (handler.eventName == eventName) {
        luaCallScriptEvent(state, script.tableRef, handler.functionName,
                           payloadIndex, script.path, eventName);
        ++delivered;
      }
    }
  }
  for (const ModuleActionHandler &module : moduleActionHandlers_) {
    for (const LuaEventHandler &handler : module.eventHandlers) {
      if (handler.eventName == eventName) {
        luaCallModuleEvent(state, module.module, handler.functionName,
                           payloadIndex, module.path, eventName);
        ++delivered;
      }
    }
  }
  return delivered;
}

void LuaScriptHost::dispatchHudEvents() {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr || world_ == nullptr || input_ == nullptr) {
    return;
  }
  const Vec2 mouse{
      .x = input_->mousePosition.x * std::max(world_->hudCanvasSize.x, 1.0F) /
           static_cast<float>(std::max(viewportWidth_, 1)),
      .y = input_->mousePosition.y * std::max(world_->hudCanvasSize.y, 1.0F) /
           static_cast<float>(std::max(viewportHeight_, 1)),
  };
  const bool mouseDown = isMouseDown("left");
  std::optional<std::string> clickedButtonId;
  if (!world_->ui.nodes.empty()) {
    ui::UiInteractionController interaction;
    const auto mappedAction = [&](const std::string &name,
                                  const std::string &fallback) {
      const auto found = world_->ui.actionMap.find(name);
      return found == world_->ui.actionMap.end() ? fallback : found->second;
    };
    const std::string nextAction = mappedAction("next", "ui_next");
    const std::string previousAction = mappedAction("previous", "ui_previous");
    const std::string submitAction = mappedAction("submit", "ui_accept");
    const bool reverse = input_->keysDown.contains("left shift") ||
                         input_->keysDown.contains("right shift");
    if (input_->keysPressed.contains("tab") ||
        input_->keysPressed.contains("down") ||
        input_->keysPressed.contains(nextAction))
      interaction.focusNext(world_->ui, reverse);
    else if (input_->keysPressed.contains("up") ||
             input_->keysPressed.contains(previousAction))
      interaction.focusNext(world_->ui, true);

    if (mouseDown && !previousUiMouseDown_ &&
        interaction.capturePointer(world_->ui, mouse)) {
      clickedButtonId = world_->ui.pointerCaptureId;
      if (const auto action = interaction.activateFocused(world_->ui))
        (void)ui::UiActionController{}.apply(world_->ui, *action);
      ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
    } else if (!mouseDown && previousUiMouseDown_) {
      interaction.releasePointer(world_->ui);
    }
    if (mouseDown && interaction.updatePointer(world_->ui, mouse))
      ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);

    const auto focused = std::ranges::find(
        world_->ui.nodes, world_->ui.focusedId, &ui::UiNode::id);
    if (focused != world_->ui.nodes.end() && focused->type == "text_input" &&
        !focused->disabled) {
      bool changed = false;
      if (!input_->textEntered.empty()) {
        focused->text += input_->textEntered;
        changed = true;
      }
      if (input_->keysPressed.contains("backspace") && !focused->text.empty()) {
        focused->text.pop_back();
        changed = true;
      }
      if (changed)
        ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
    }
    if (input_->keysPressed.contains("return") ||
        input_->keysPressed.contains(submitAction)) {
      if (const auto action = interaction.activateFocused(world_->ui)) {
        clickedButtonId = world_->ui.focusedId;
        (void)ui::UiActionController{}.apply(world_->ui, *action);
        ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
      }
    }
  }
  for (ui::UiNode &node : world_->ui.nodes) {
    if (node.type != "button" && node.type != "toggle" &&
        node.type != "text_input")
      continue;
    if (!node.visible) {
      node.hovered = false;
      continue;
    }
    const float hitX = node.resolved.width > 0.0F ? node.resolved.x
                                                  : node.layout.position.x;
    const float hitY = node.resolved.height > 0.0F ? node.resolved.y
                                                   : node.layout.position.y;
    const float hitW = node.resolved.width > 0.0F ? node.resolved.width
                                                  : node.layout.size.x;
    const float hitH = node.resolved.height > 0.0F ? node.resolved.height
                                                   : node.layout.size.y;
    node.hovered = mouse.x >= hitX && mouse.x <= hitX + hitW &&
                    mouse.y >= hitY && mouse.y <= hitY + hitH;
    if (!clickedButtonId.has_value() && node.hovered && mouseDown &&
        !previousUiMouseDown_) {
      clickedButtonId = node.id;
    }
  }

  for (ui::UiNode &node : world_->ui.nodes) {
    if (node.type != "button" && node.type != "toggle" &&
        node.type != "text_input")
      continue;
    const bool clicked =
        clickedButtonId.has_value() && *clickedButtonId == node.id;
    if (!node.visible || (!node.hovered && !clicked)) {
      continue;
    }
    for (const ScriptInstance &script : scripts_) {
      if (script.entityId != node.id) {
        continue;
      }
      if (node.hovered)
        luaCallUiEvent(state, script.tableRef, "on_ui_hover", node, mouse,
                       script.path);
      if (clicked) {
        luaCallUiEvent(state, script.tableRef, "on_ui_click", node, mouse,
                       script.path);
      }
    }
    if (clicked && !node.action.empty()) {
      for (const ScriptInstance &script : scripts_) {
        for (const LuaActionHandler &handler : script.actionHandlers) {
          if (handler.action == node.action) {
            luaCallActionEvent(state, script.tableRef, handler.functionName,
                               node, mouse, script.path);
          }
        }
      }
      for (const ModuleActionHandler &module : moduleActionHandlers_) {
        for (const LuaActionHandler &handler : module.actionHandlers) {
          if (handler.action == node.action) {
            luaCallModuleActionEvent(state, module.module, handler.functionName,
                                     node, mouse, module.path);
          }
        }
      }
      lua_newtable(state);
      lua_pushstring(state, node.id.c_str());
      lua_setfield(state, -2, "id");
      lua_pushstring(state, node.text.c_str());
      lua_setfield(state, -2, "label");
      lua_pushstring(state, node.action.c_str());
      lua_setfield(state, -2, "action");
      lua_pushnumber(state, mouse.x);
      lua_setfield(state, -2, "mouse_x");
      lua_pushnumber(state, mouse.y);
      lua_setfield(state, -2, "mouse_y");
      const int payloadIndex = lua_gettop(state);
      (void)emitEvent("hud_action", payloadIndex);
      lua_pop(state, 1);
    }
  }
  previousUiMouseDown_ = mouseDown;
}

void LuaScriptHost::updateTimers(const float dt) {
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr) {
    return;
  }
  for (TimerInstance &timer : timers_) {
    if (timer.cancelled) {
      continue;
    }
    timer.remaining -= std::max(dt, 0.0F);
    if (timer.remaining > 0.0F) {
      continue;
    }
    lua_rawgeti(state, LUA_REGISTRYINDEX, timer.callbackRef);
    lua_pushinteger(state, static_cast<lua_Integer>(timer.id));
    std::string error;
    if (!luaCall(state, 1, 0, error)) {
      luaReportCallbackError("Timer", {}, std::to_string(timer.id), error);
    }
    if (timer.repeating && !timer.cancelled) {
      timer.remaining += std::max(timer.interval, 0.0001F);
    } else {
      timer.cancelled = true;
    }
  }
  std::erase_if(timers_, [&](const TimerInstance &timer) {
    if (!timer.cancelled) {
      return false;
    }
    luaL_unref(state, LUA_REGISTRYINDEX, timer.callbackRef);
    return true;
  });
}

void LuaScriptHost::clearTimersAndEvents() {
  auto *state = static_cast<lua_State *>(state_);
  if (state != nullptr) {
    for (const TimerInstance &timer : timers_) {
      luaL_unref(state, LUA_REGISTRYINDEX, timer.callbackRef);
    }
    for (const EventSubscription &subscription : eventSubscriptions_) {
      luaL_unref(state, LUA_REGISTRYINDEX, subscription.callbackRef);
    }
  }
  timers_.clear();
  eventSubscriptions_.clear();
}

void LuaScriptHost::clearSaveMigrationHooks() {
  auto *state = static_cast<lua_State *>(state_);
  if (state != nullptr) {
    for (const SaveMigrationHook &hook : saveMigrationHooks_) {
      luaL_unref(state, LUA_REGISTRYINDEX, hook.callbackRef);
    }
  }
  saveMigrationHooks_.clear();
}

} // namespace demi::runtime
