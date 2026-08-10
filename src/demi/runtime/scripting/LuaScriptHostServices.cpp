#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/LuaScriptHostInternal.h"
#include "demi/runtime/ui/TextEditingEngine.h"
#include "demi/runtime/ui/TextLayoutEngine.h"
#include "demi/runtime/ui/UiActionController.h"
#include "demi/runtime/ui/UiEventQueue.h"
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
  if (world_ != nullptr) {
    const platform::SafeAreaInsets safe = applicationServices_.safeArea();
    const float scaleX = std::max(world_->hudCanvasSize.x, 1.0F) /
                         static_cast<float>(viewportWidth_);
    const float scaleY = std::max(world_->hudCanvasSize.y, 1.0F) /
                         static_cast<float>(viewportHeight_);
    world_->ui.safeArea = {.left = safe.left * scaleX,
                           .top = safe.top * scaleY,
                           .right = safe.right * scaleX,
                           .bottom = safe.bottom * scaleY};
    ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
  }
}

platform::ApplicationServices &LuaScriptHost::applicationServices() {
  return applicationServices_;
}
const platform::ApplicationServices &
LuaScriptHost::applicationServices() const {
  return applicationServices_;
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

void LuaScriptHost::beginFrame(const float unscaledDeltaTime) {
  unscaledDeltaTime_ = std::max(unscaledDeltaTime, 0.0F);
  deltaTime_ = paused_ ? 0.0F : unscaledDeltaTime_ * timeScale_;
  gameTime_ += deltaTime_;
  ++frameCount_;
  gestureEvents_ =
      input_ != nullptr
          ? touchGestureRecognizer_.update(input_->touches, unscaledDeltaTime_)
          : std::vector<input::GestureEvent>{};
  if (input_ != nullptr) {
    input_->virtualButtonsPressed.clear();
    input_->virtualButtonsReleased.clear();
  }
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr)
    return;
  lua_getglobal(state, "Time");
  if (lua_istable(state, -1)) {
    lua_pushnumber(state, deltaTime_);
    lua_setfield(state, -2, "delta_time");
    lua_pushnumber(state, unscaledDeltaTime_);
    lua_setfield(state, -2, "unscaled_delta_time");
    lua_pushnumber(state, gameTime_);
    lua_setfield(state, -2, "time");
    lua_pushnumber(state, fixedTime_);
    lua_setfield(state, -2, "fixed_time");
    lua_pushinteger(state, static_cast<lua_Integer>(frameCount_));
    lua_setfield(state, -2, "frame_count");
    lua_pushnumber(state, timeScale_);
    lua_setfield(state, -2, "time_scale");
    lua_pushboolean(state, paused_);
    lua_setfield(state, -2, "paused");
  }
  lua_pop(state, 1);
}

void LuaScriptHost::advanceFixedTime(const float fixedDeltaTime) {
  fixedTime_ += std::max(fixedDeltaTime, 0.0F);
}

void LuaScriptHost::setPaused(const bool paused) { paused_ = paused; }
bool LuaScriptHost::paused() const { return paused_; }

void LuaScriptHost::setTimeScale(const float scale) {
  timeScale_ = std::clamp(scale, 0.0F, 100.0F);
}
float LuaScriptHost::timeScale() const { return timeScale_; }
float LuaScriptHost::deltaTime() const { return deltaTime_; }
float LuaScriptHost::unscaledDeltaTime() const { return unscaledDeltaTime_; }
double LuaScriptHost::gameTime() const { return gameTime_; }
double LuaScriptHost::fixedTime() const { return fixedTime_; }
std::uint64_t LuaScriptHost::frameCount() const { return frameCount_; }

void LuaScriptHost::setApplicationFocused(const bool focused) {
  applicationServices_.setFocused(focused);
  if (applicationFocused_ == focused)
    return;
  applicationFocused_ = focused;
  (void)emitEvent(focused ? "application_focus" : "application_blur", 0);
}
bool LuaScriptHost::applicationFocused() const { return applicationFocused_; }

void LuaScriptHost::setApplicationMinimized(const bool minimized) {
  applicationServices_.setMinimized(minimized);
  if (applicationMinimized_ == minimized)
    return;
  applicationMinimized_ = minimized;
  (void)emitEvent(minimized ? "application_minimize" : "application_restore",
                  0);
}

void LuaScriptHost::setApplicationSuspended(const bool suspended) {
  applicationServices_.setSuspended(suspended);
  if (applicationSuspended_ == suspended)
    return;
  applicationSuspended_ = suspended;
  (void)emitEvent(suspended ? "application_suspend" : "application_resume", 0);
}
bool LuaScriptHost::applicationSuspended() const {
  return applicationSuspended_;
}

void LuaScriptHost::notifyApplicationLowMemory() {
  applicationServices_.notifyLowMemory();
  (void)emitEvent("application_low_memory", 0);
}

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
    const std::string cancelAction = mappedAction("cancel", "ui_cancel");
    const bool reverse = input_->keysDown.contains("left shift") ||
                         input_->keysDown.contains("right shift");
    if (input_->keysPressed.contains("tab") ||
        input_->keysPressed.contains("down") ||
        input_->keysPressed.contains(nextAction))
      interaction.focusNext(world_->ui, reverse);
    else if (input_->keysPressed.contains("up") ||
             input_->keysPressed.contains(previousAction))
      interaction.focusNext(world_->ui, true);

    if (!input_->touches.empty()) {
      for (const TouchPoint &touch : input_->touches) {
        const Vec2 position{
            .x = touch.position.x * std::max(world_->hudCanvasSize.x, 1.0F) /
                 static_cast<float>(std::max(viewportWidth_, 1)),
            .y = touch.position.y * std::max(world_->hudCanvasSize.y, 1.0F) /
                 static_cast<float>(std::max(viewportHeight_, 1)),
        };
        if (touch.phase == TouchPhase::Began &&
            interaction.capturePointer(world_->ui, touch.id, position,
                                       "touch")) {
          clickedButtonId = world_->ui.pointerCaptures[touch.id];
          const auto captured = std::ranges::find(
              world_->ui.nodes, *clickedButtonId, &ui::UiNode::id);
          if (captured != world_->ui.nodes.end() &&
              captured->type == "virtual_button" &&
              !captured->control.empty()) {
            input_->virtualButtonsDown.insert(captured->control);
            input_->virtualButtonsPressed.insert(captured->control);
          }
          if (const auto action =
                  interaction.activateFocused(world_->ui, "touch"))
            (void)ui::UiActionController{}.apply(world_->ui, *action);
          ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
        } else if (touch.phase == TouchPhase::Ended ||
                   touch.phase == TouchPhase::Cancelled) {
          const auto capturedId = world_->ui.pointerCaptures.find(touch.id);
          if (capturedId != world_->ui.pointerCaptures.end()) {
            const auto captured = std::ranges::find(
                world_->ui.nodes, capturedId->second, &ui::UiNode::id);
            if (captured != world_->ui.nodes.end() &&
                !captured->control.empty()) {
              if (captured->type == "virtual_button") {
                input_->virtualButtonsDown.erase(captured->control);
                input_->virtualButtonsReleased.insert(captured->control);
              } else if (captured->type == "virtual_stick") {
                input_->virtualAxes.erase(captured->control);
              }
            }
          }
          interaction.releasePointer(world_->ui, touch.id, position,
                                     touch.phase == TouchPhase::Cancelled,
                                     "touch");
        } else {
          const auto capturedId = world_->ui.pointerCaptures.find(touch.id);
          if (capturedId != world_->ui.pointerCaptures.end()) {
            const auto captured = std::ranges::find(
                world_->ui.nodes, capturedId->second, &ui::UiNode::id);
            if (captured != world_->ui.nodes.end() &&
                captured->type == "virtual_stick" &&
                !captured->control.empty()) {
              const Vec2 center{
                  captured->resolved.x + captured->resolved.width * 0.5F,
                  captured->resolved.y + captured->resolved.height * 0.5F};
              const float radius =
                  captured->radius > 0.0F
                      ? captured->radius
                      : std::max(std::min(captured->resolved.width,
                                          captured->resolved.height) *
                                     0.5F,
                                 1.0F);
              Vec2 axis{(position.x - center.x) / radius,
                        (position.y - center.y) / radius};
              const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y);
              if (length <= captured->deadzone) {
                axis = {};
              } else if (length > 1.0F) {
                axis.x /= length;
                axis.y /= length;
              }
              input_->virtualAxes[captured->control] = axis;
            }
          }
          bool changed = interaction.updatePointer(world_->ui, touch.id,
                                                   position, "touch");
          if (touch.phase == TouchPhase::Moved &&
              (touch.delta.x != 0.0F || touch.delta.y != 0.0F)) {
            const Vec2 canvasDelta{
                .x = -touch.delta.x *
                     std::max(world_->hudCanvasSize.x, 1.0F) /
                     static_cast<float>(std::max(viewportWidth_, 1)),
                .y = -touch.delta.y *
                     std::max(world_->hudCanvasSize.y, 1.0F) /
                     static_cast<float>(std::max(viewportHeight_, 1)),
            };
            changed = interaction.scrollPointer(world_->ui, touch.id,
                                                  position, canvasDelta,
                                                  "touch") ||
                      changed;
          }
          if (changed)
            ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
        }
      }
    } else {
      (void)interaction.movePointer(world_->ui, 0, mouse, "mouse");
      if (mouseDown && !previousUiMouseDown_ &&
          interaction.capturePointer(world_->ui, mouse)) {
        if (const auto captured = world_->ui.pointerCaptures.find(0);
            captured != world_->ui.pointerCaptures.end())
          clickedButtonId = captured->second;
        if (const auto action =
                interaction.activateFocused(world_->ui, "mouse"))
          (void)ui::UiActionController{}.apply(world_->ui, *action);
        ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
      } else if (!mouseDown && previousUiMouseDown_) {
        interaction.releasePointer(world_->ui, 0, mouse, false, "mouse");
      }
      if (mouseDown && interaction.updatePointer(world_->ui, mouse))
        ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
      if ((input_->mouseScroll.x != 0.0F || input_->mouseScroll.y != 0.0F) &&
          interaction.scrollPointer(world_->ui, 0, mouse, input_->mouseScroll,
                                    "mouse"))
        ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
    }

    const auto focused = std::ranges::find(
        world_->ui.nodes, world_->ui.focusedId, &ui::UiNode::id);
    if (focused != world_->ui.nodes.end() && focused->type == "text_input" &&
        !focused->disabled) {
      bool changed = false;
      bool presentationChanged = false;
      ui::TextEditingEngine::normalize(focused->text, focused->textEdit);
      if (input_->textCompositionChanged) {
        presentationChanged = ui::TextEditingEngine::setComposition(
            focused->textEdit, input_->textComposition,
            input_->textCompositionSelectionStart,
            input_->textCompositionSelectionLength);
      }
      if (!input_->textEntered.empty()) {
        changed = ui::TextEditingEngine::insert(
                      focused->text, focused->textEdit, input_->textEntered) ||
                  changed;
      }
      const bool extendSelection = input_->keysDown.contains("left shift") ||
                                   input_->keysDown.contains("right shift");
      const bool control = input_->keysDown.contains("left ctrl") ||
                           input_->keysDown.contains("right ctrl");
      if (control && input_->keysPressed.contains("a")) {
        ui::TextEditingEngine::selectAll(focused->textEdit, focused->text);
        presentationChanged = true;
      }
      if (input_->keysPressed.contains("left")) {
        ui::TextEditingEngine::move(focused->textEdit, focused->text, -1,
                                    extendSelection);
        presentationChanged = true;
      }
      if (input_->keysPressed.contains("right")) {
        ui::TextEditingEngine::move(focused->textEdit, focused->text, 1,
                                    extendSelection);
        presentationChanged = true;
      }
      if (input_->keysPressed.contains("home")) {
        ui::TextEditingEngine::moveTo(focused->textEdit, focused->text, 0,
                                      extendSelection);
        presentationChanged = true;
      }
      if (input_->keysPressed.contains("end")) {
        ui::TextEditingEngine::moveTo(
            focused->textEdit, focused->text,
            ui::TextLayoutEngine::graphemeCount(focused->text),
            extendSelection);
        presentationChanged = true;
      }
      if (input_->keysPressed.contains("backspace"))
        changed = ui::TextEditingEngine::backspace(focused->text,
                                                   focused->textEdit) ||
                  changed;
      if (input_->keysPressed.contains("delete"))
        changed = ui::TextEditingEngine::deleteForward(focused->text,
                                                       focused->textEdit) ||
                  changed;
      if (changed || presentationChanged)
        ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
      if (changed)
        ui::UiEventQueue::valueChanged(world_->ui, *focused, "keyboard");
    }
    for (ui::UiNode &node : world_->ui.nodes) {
      if (node.type == "text_input" && node.id != world_->ui.focusedId)
        ui::TextEditingEngine::clearComposition(node.textEdit);
    }
    if (input_->keysPressed.contains("return") ||
        input_->keysPressed.contains(submitAction)) {
      if (const auto action =
              interaction.activateFocused(world_->ui, "keyboard")) {
        clickedButtonId = world_->ui.focusedId;
        (void)ui::UiActionController{}.apply(world_->ui, *action);
        ui::UiLayoutEngine{}.layout(world_->ui, world_->ui.canvasSize);
      }
    }
    if (input_->keysPressed.contains("escape") ||
        input_->keysPressed.contains(cancelAction))
      interaction.cancel(world_->ui, "keyboard");
  }
  for (ui::UiNode &node : world_->ui.nodes) {
    if (node.type != "button" && node.type != "toggle" &&
        node.type != "text_input")
      continue;
    if (!node.visible) {
      node.hovered = false;
      continue;
    }
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
    }
  }
  for (const ui::UiEvent &event : ui::UiEventQueue::take(world_->ui)) {
    const std::string callback =
        "on_ui_" + std::string(ui::uiEventTypeName(event.type));
    for (const ScriptInstance &script : scripts_) {
      if (script.entityId != event.id)
        continue;
      luaCallTypedUiEvent(state, script.tableRef, "on_ui_event", event,
                          script.path);
      luaCallTypedUiEvent(state, script.tableRef, callback.c_str(), event,
                          script.path);
    }
    luaPushUiEvent(state, event);
    const int payloadIndex = lua_gettop(state);
    (void)emitEvent("ui_event", payloadIndex);
    (void)emitEvent("ui_" + std::string(ui::uiEventTypeName(event.type)),
                    payloadIndex);
    lua_pop(state, 1);
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
