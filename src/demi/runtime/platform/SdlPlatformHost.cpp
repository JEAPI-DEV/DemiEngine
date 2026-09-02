#include "demi/runtime/platform/PlatformHost.h"

#include "demi/runtime/diagnostics/DeviceLog.h"
#include "demi/runtime/platform/PlatformInput.h"
#include "demi/runtime/platform/SdlNativeWindow.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace demi::runtime::platform {

namespace {

using Clock = std::chrono::steady_clock;

#if defined(__ANDROID__)
bool androidPermissionPermanentlyDenied(const char *permission) {
  auto *environment = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (environment == nullptr || activity == nullptr)
    return false;
  const jclass activityClass = environment->GetObjectClass(activity);
  const jmethodID method = environment->GetMethodID(
      activityClass, "shouldShowRequestPermissionRationale",
      "(Ljava/lang/String;)Z");
  const jstring name = environment->NewStringUTF(permission);
  const bool permanentlyDenied =
      method != nullptr && name != nullptr &&
      !environment->CallBooleanMethod(activity, method, name);
  if (environment->ExceptionCheck()) {
    environment->ExceptionClear();
    environment->DeleteLocalRef(activityClass);
    if (name != nullptr)
      environment->DeleteLocalRef(name);
    return false;
  }
  if (name != nullptr)
    environment->DeleteLocalRef(name);
  environment->DeleteLocalRef(activityClass);
  return permanentlyDenied;
}

bool requestAndroidFrameRate(const float framesPerSecond) {
  auto *environment = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (environment == nullptr || activity == nullptr)
    return false;
  const jclass activityClass = environment->GetObjectClass(activity);
  const jmethodID method = environment->GetMethodID(
      activityClass, "setDemiPreferredFrameRate", "(F)Z");
  const bool accepted =
      method != nullptr &&
      environment->CallBooleanMethod(activity, method, framesPerSecond);
  if (environment->ExceptionCheck()) {
    environment->ExceptionDescribe();
    environment->ExceptionClear();
    environment->DeleteLocalRef(activityClass);
    return false;
  }
  environment->DeleteLocalRef(activityClass);
  return accepted;
}

struct AndroidPermissionRequest {
  std::function<void(bool, bool)> result;
};

void SDLCALL permissionResult(void *userdata, const char *permission,
                              const bool granted) {
  std::unique_ptr<AndroidPermissionRequest> request(
      static_cast<AndroidPermissionRequest *>(userdata));
  request->result(granted,
                  !granted && androidPermissionPermanentlyDenied(permission));
}
#endif

std::string_view keyName(const SDL_Scancode key) {
  switch (key) {
  case SDL_SCANCODE_A:
    return "a";
  case SDL_SCANCODE_B:
    return "b";
  case SDL_SCANCODE_C:
    return "c";
  case SDL_SCANCODE_D:
    return "d";
  case SDL_SCANCODE_E:
    return "e";
  case SDL_SCANCODE_F:
    return "f";
  case SDL_SCANCODE_G:
    return "g";
  case SDL_SCANCODE_H:
    return "h";
  case SDL_SCANCODE_I:
    return "i";
  case SDL_SCANCODE_J:
    return "j";
  case SDL_SCANCODE_K:
    return "k";
  case SDL_SCANCODE_L:
    return "l";
  case SDL_SCANCODE_M:
    return "m";
  case SDL_SCANCODE_N:
    return "n";
  case SDL_SCANCODE_O:
    return "o";
  case SDL_SCANCODE_P:
    return "p";
  case SDL_SCANCODE_Q:
    return "q";
  case SDL_SCANCODE_R:
    return "r";
  case SDL_SCANCODE_S:
    return "s";
  case SDL_SCANCODE_T:
    return "t";
  case SDL_SCANCODE_U:
    return "u";
  case SDL_SCANCODE_V:
    return "v";
  case SDL_SCANCODE_W:
    return "w";
  case SDL_SCANCODE_X:
    return "x";
  case SDL_SCANCODE_Y:
    return "y";
  case SDL_SCANCODE_Z:
    return "z";
  case SDL_SCANCODE_0:
    return "0";
  case SDL_SCANCODE_1:
    return "1";
  case SDL_SCANCODE_2:
    return "2";
  case SDL_SCANCODE_3:
    return "3";
  case SDL_SCANCODE_4:
    return "4";
  case SDL_SCANCODE_5:
    return "5";
  case SDL_SCANCODE_6:
    return "6";
  case SDL_SCANCODE_7:
    return "7";
  case SDL_SCANCODE_8:
    return "8";
  case SDL_SCANCODE_9:
    return "9";
  case SDL_SCANCODE_SPACE:
    return "space";
  case SDL_SCANCODE_RETURN:
    return "return";
  case SDL_SCANCODE_ESCAPE:
    return "escape";
  case SDL_SCANCODE_AC_BACK:
    return "back";
  case SDL_SCANCODE_TAB:
    return "tab";
  case SDL_SCANCODE_BACKSPACE:
    return "backspace";
  case SDL_SCANCODE_DELETE:
    return "delete";
  case SDL_SCANCODE_HOME:
    return "home";
  case SDL_SCANCODE_END:
    return "end";
  case SDL_SCANCODE_UP:
    return "up";
  case SDL_SCANCODE_DOWN:
    return "down";
  case SDL_SCANCODE_LEFT:
    return "left";
  case SDL_SCANCODE_RIGHT:
    return "right";
  case SDL_SCANCODE_LSHIFT:
    return "left shift";
  case SDL_SCANCODE_RSHIFT:
    return "right shift";
  case SDL_SCANCODE_LCTRL:
    return "left ctrl";
  case SDL_SCANCODE_RCTRL:
    return "right ctrl";
  case SDL_SCANCODE_LALT:
    return "left alt";
  case SDL_SCANCODE_RALT:
    return "right alt";
  case SDL_SCANCODE_F1:
    return "f1";
  case SDL_SCANCODE_F2:
    return "f2";
  case SDL_SCANCODE_F3:
    return "f3";
  case SDL_SCANCODE_F4:
    return "f4";
  case SDL_SCANCODE_F5:
    return "f5";
  case SDL_SCANCODE_F6:
    return "f6";
  case SDL_SCANCODE_F7:
    return "f7";
  case SDL_SCANCODE_F8:
    return "f8";
  case SDL_SCANCODE_F9:
    return "f9";
  case SDL_SCANCODE_F10:
    return "f10";
  case SDL_SCANCODE_F11:
    return "f11";
  case SDL_SCANCODE_F12:
    return "f12";
  default:
    return {};
  }
}

std::string_view mouseButtonName(const std::uint8_t button) {
  switch (button) {
  case SDL_BUTTON_LEFT:
    return "left";
  case SDL_BUTTON_RIGHT:
    return "right";
  case SDL_BUTTON_MIDDLE:
    return "middle";
  default:
    return {};
  }
}

std::string_view gamepadButtonName(const SDL_GamepadButton button) {
  switch (button) {
  case SDL_GAMEPAD_BUTTON_SOUTH:
    return "south";
  case SDL_GAMEPAD_BUTTON_EAST:
    return "east";
  case SDL_GAMEPAD_BUTTON_WEST:
    return "west";
  case SDL_GAMEPAD_BUTTON_NORTH:
    return "north";
  case SDL_GAMEPAD_BUTTON_BACK:
    return "select";
  case SDL_GAMEPAD_BUTTON_GUIDE:
    return "guide";
  case SDL_GAMEPAD_BUTTON_START:
    return "start";
  case SDL_GAMEPAD_BUTTON_LEFT_STICK:
    return "left_stick";
  case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
    return "right_stick";
  case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
    return "left_bumper";
  case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
    return "right_bumper";
  case SDL_GAMEPAD_BUTTON_DPAD_UP:
    return "dpad_up";
  case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    return "dpad_down";
  case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    return "dpad_left";
  case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
    return "dpad_right";
  default:
    return {};
  }
}

std::string_view gamepadAxisName(const SDL_GamepadAxis axis) {
  switch (axis) {
  case SDL_GAMEPAD_AXIS_LEFTX:
    return "left_x";
  case SDL_GAMEPAD_AXIS_LEFTY:
    return "left_y";
  case SDL_GAMEPAD_AXIS_RIGHTX:
    return "right_x";
  case SDL_GAMEPAD_AXIS_RIGHTY:
    return "right_y";
  case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
    return "left_trigger";
  case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
    return "right_trigger";
  default:
    return {};
  }
}

float normalizedAxis(const std::int16_t value) {
  return value < 0 ? static_cast<float>(value) / 32768.0F
                   : static_cast<float>(value) / 32767.0F;
}

class SdlPlatformHost final : public PlatformHost {
public:
  ~SdlPlatformHost() override { shutdown(); }

  bool initialize(const PlatformHostConfig &config,
                  std::string &error) override {
    if (window_ != nullptr) {
      error = "The SDL platform host is already initialized.";
      return false;
    }
    if (config.width <= 0 || config.height <= 0) {
      error = "Platform window dimensions must be positive.";
      return false;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
      error = SDL_GetError();
      return false;
    }
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY |
                            (config.resizable ? SDL_WINDOW_RESIZABLE : 0);
    window_ = SDL_CreateWindow(config.title.c_str(), config.width,
                               config.height, flags);
    if (window_ == nullptr) {
      error = SDL_GetError();
      SDL_Quit();
      return false;
    }
    // Android pops the software keyboard whenever text input is active, so
    // only desktop platforms start text input eagerly; Android games opt in
    // through ApplicationServices::setKeyboardVisible instead.
#if !defined(__ANDROID__)
    SDL_StartTextInput(window_);
#endif
    SDL_AddEventWatch(&SdlPlatformHost::watchLifecycle, this);
    lastFrame_ = Clock::now();
    updateWindowState();
    return true;
  }

  void shutdown() override {
    if (window_ == nullptr)
      return;
    SDL_RemoveEventWatch(&SdlPlatformHost::watchLifecycle, this);
    for (auto &[id, gamepad] : gamepads_) {
      (void)id;
      SDL_CloseGamepad(gamepad);
    }
    gamepads_.clear();
    SDL_StopTextInput(window_);
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
    state_ = {};
  }

  void poll(InputState &inputState) override {
    PlatformInput input(inputState);
    input.beginFrame();
    // SDL mouse events use window points; bgfx renders to drawable pixels.
    updateWindowState();
    state_.lowMemorySignals = pendingLowMemorySignals_;
    pendingLowMemorySignals_ = 0;
    state_.backRequests = 0;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_EVENT_QUIT:
        state_.quitRequested = true;
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_AC_BACK)
          ++state_.backRequests;
        if (const std::string_view name = keyName(event.key.scancode);
            !name.empty())
          input.key(name, event.key.down, event.key.repeat);
        break;
      case SDL_EVENT_TEXT_INPUT:
        if (event.text.text != nullptr)
          input.text(event.text.text);
        break;
      case SDL_EVENT_TEXT_EDITING:
        if (event.edit.text != nullptr)
          input.composition(event.edit.text, event.edit.start,
                            event.edit.length);
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        input.composition({}, 0, 0);
        break;
      case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.which != SDL_TOUCH_MOUSEID) {
          const PointerMotion motion = pointerMotionInDrawablePixels(
              {.position = {event.motion.x, event.motion.y},
               .delta = {event.motion.xrel, event.motion.yrel}},
              {static_cast<float>(windowWidth_),
               static_cast<float>(windowHeight_)},
              {static_cast<float>(state_.width),
               static_cast<float>(state_.height)});
          input.pointerPosition(motion.position.x, motion.position.y,
                                motion.delta.x, motion.delta.y);
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.which != SDL_TOUCH_MOUSEID) {
          const PointerMotion motion = pointerMotionInDrawablePixels(
              {.position = {event.button.x, event.button.y}, .delta = {}},
              {static_cast<float>(windowWidth_),
               static_cast<float>(windowHeight_)},
              {static_cast<float>(state_.width),
               static_cast<float>(state_.height)});
          input.pointerPosition(motion.position.x, motion.position.y, 0.0F,
                                0.0F);
          const std::string_view name = mouseButtonName(event.button.button);
          if (!name.empty())
            input.pointerButton(name, event.button.down);
        }
        break;
      case SDL_EVENT_MOUSE_WHEEL: {
        const float direction =
            event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
        input.pointerScroll(event.wheel.x * direction,
                            event.wheel.y * direction);
        break;
      }
      case SDL_EVENT_DROP_FILE:
        if (event.drop.data != nullptr)
          droppedFiles_.emplace_back(event.drop.data);
        break;
      case SDL_EVENT_FINGER_DOWN:
      case SDL_EVENT_FINGER_MOTION:
      case SDL_EVENT_FINGER_UP:
      case SDL_EVENT_FINGER_CANCELED: {
        TouchPhase phase = TouchPhase::Moved;
        if (event.type == SDL_EVENT_FINGER_DOWN)
          phase = TouchPhase::Began;
        else if (event.type == SDL_EVENT_FINGER_UP)
          phase = TouchPhase::Ended;
        else if (event.type == SDL_EVENT_FINGER_CANCELED)
          phase = TouchPhase::Cancelled;
        const Vec2 position{event.tfinger.x * state_.width,
                            event.tfinger.y * state_.height};
        const Vec2 delta{event.tfinger.dx * state_.width,
                         event.tfinger.dy * state_.height};
        input.touch(event.tfinger.fingerID, phase, position, delta,
                    event.tfinger.pressure);
#if defined(__ANDROID__)
        input.pointerPosition(position.x, position.y, delta.x, delta.y);
        input.pointerButton("left", phase != TouchPhase::Ended &&
                                        phase != TouchPhase::Cancelled);
#endif
        break;
      }
      case SDL_EVENT_GAMEPAD_ADDED:
        openGamepad(event.gdevice.which, input);
        break;
      case SDL_EVENT_GAMEPAD_REMOVED:
        closeGamepad(event.gdevice.which, input);
        break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const auto button =
            static_cast<SDL_GamepadButton>(event.gbutton.button);
        const std::string_view name = gamepadButtonName(button);
        if (!name.empty())
          input.gamepadButton(static_cast<int>(event.gbutton.which), name,
                              event.gbutton.down);
        break;
      }
      case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const auto axis = static_cast<SDL_GamepadAxis>(event.gaxis.axis);
        const std::string_view name = gamepadAxisName(axis);
        if (!name.empty()) {
          float value = normalizedAxis(event.gaxis.value);
          if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
              axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            value = std::max(value, 0.0F);
          input.gamepadAxis(static_cast<int>(event.gaxis.which), name, value);
        }
        break;
      }
      default:
        break;
      }
    }
    updateWindowState();
    if (const SDL_DisplayMode *mode =
            SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
        mode != nullptr && mode->refresh_rate > 0) {
      state_.displayRefreshHz = mode->refresh_rate;
    }
    const Clock::time_point now = Clock::now();
    state_.deltaSeconds = std::clamp(
        std::chrono::duration<float>(now - lastFrame_).count(), 0.0F, 0.1F);
    lastFrame_ = now;
    // Opt-in determinism for headless replay tests: a fixed delta seconds
    // removes wall-clock sensitivity so scripted input sequences always
    // advance the simulation by the same amount per frame.
    if (const char *fixedDelta = std::getenv("DEMI_FIXED_DELTA_SECONDS");
        fixedDelta != nullptr && *fixedDelta != '\0') {
      try {
        state_.deltaSeconds = std::max(std::stof(fixedDelta), 0.0F);
      } catch (...) {
      }
    }
  }

  void clearQuitRequest() override { state_.quitRequested = false; }

  const PlatformFrameState &frameState() const override { return state_; }

  std::vector<std::filesystem::path> takeDroppedFiles() override {
    return std::exchange(droppedFiles_, {});
  }

  render::NativeWindowHandle nativeWindow() const override {
    return sdlNativeWindowHandle(window_);
  }

  bool setWindowMode(const WindowMode mode, std::string &error) override {
    bool ok = true;
    if (mode == WindowMode::Fullscreen) {
      ok = SDL_SetWindowFullscreen(window_, true);
    } else {
      ok = SDL_SetWindowFullscreen(window_, false);
      if (ok)
        ok = SDL_SetWindowBordered(window_, mode != WindowMode::Borderless);
    }
    if (!ok)
      error = SDL_GetError();
    return ok;
  }

  bool setMouseCaptured(const bool captured, std::string &error) override {
    if (SDL_SetWindowRelativeMouseMode(window_, captured))
      return true;
    error = SDL_GetError();
    return false;
  }

  bool requestFrameRate(const float framesPerSecond) override {
#if defined(__ANDROID__)
    return requestAndroidFrameRate(std::max(framesPerSecond, 0.0F));
#else
    (void)framesPerSecond;
    return false;
#endif
  }

  std::string clipboard() const override {
    const char *text = SDL_GetClipboardText();
    const std::string result = text != nullptr ? text : "";
    SDL_free(const_cast<char *>(text));
    return result;
  }

  bool setClipboard(const std::string &text, std::string &error) override {
    if (SDL_SetClipboardText(text.c_str()))
      return true;
    error = SDL_GetError();
    return false;
  }

  bool requestPermission(const std::string &permission,
                         std::function<void(bool, bool)> result,
                         std::string &error) override {
#if defined(__ANDROID__)
    auto request = std::make_unique<AndroidPermissionRequest>(
        AndroidPermissionRequest{.result = std::move(result)});
    if (!SDL_RequestAndroidPermission(permission.c_str(), permissionResult,
                                      request.get())) {
      error = SDL_GetError();
      return false;
    }
    (void)request.release();
    return true;
#else
    (void)permission;
    (void)error;
    result(true, false);
    return true;
#endif
  }

private:
#if defined(__ANDROID__)
  static constexpr std::chrono::milliseconds kSurfaceSettleDelay{250};
#endif
  static bool SDLCALL watchLifecycle(void *userdata, SDL_Event *event) {
    auto &host = *static_cast<SdlPlatformHost *>(userdata);
    switch (event->type) {
    case SDL_EVENT_LOW_MEMORY:
      ++host.pendingLowMemorySignals_;
      break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
      host.state_.suspended = true;
      break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND:
      host.state_.suspended = false;
      break;
    case SDL_EVENT_TERMINATING:
      host.state_.quitRequested = true;
      break;
    default:
      break;
    }
    return true;
  }

  void updateWindowState() {
    SDL_GetWindowSize(window_, &windowWidth_, &windowHeight_);
    SDL_GetWindowSizeInPixels(window_, &state_.width, &state_.height);
    const float scale = SDL_GetWindowDisplayScale(window_);
    state_.logicalDpi = 96.0F * (scale > 0.0F ? scale : 1.0F);
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
    state_.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    state_.minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
#if defined(__ANDROID__)
    const void *nativeWindow = sdlNativeWindowHandle(window_).window;
    const bool previousDrawable = state_.drawableAvailable;
    state_.drawableAvailable = nativeWindow != nullptr &&
                               (flags & SDL_WINDOW_HIDDEN) == 0 &&
                               !state_.minimized;
    if (nativeWindow != nativeWindow_) {
      const void *previousWindow = nativeWindow_;
      nativeWindow_ = nativeWindow;
      ++state_.surfaceGeneration;
      lastSurfaceChange_ = Clock::now();
      if (nativeWindow != nullptr)
        deviceLog(deviceLogMessage(
            "surface", "Native window " + devicePointerText(previousWindow) +
                           " -> " + devicePointerText(nativeWindow) +
                           ", surface generation " +
                           std::to_string(state_.surfaceGeneration) + "."));
    }
    state_.surfaceSettled =
        Clock::now() - lastSurfaceChange_ >= kSurfaceSettleDelay;
    if (state_.drawableAvailable != previousDrawable)
      deviceLog(deviceLogMessage(
          "surface",
          state_.drawableAvailable
              ? "Drawable available."
              : "Drawable unavailable (native window " +
                    devicePointerText(nativeWindow) + ", hidden " +
                    std::to_string((flags & SDL_WINDOW_HIDDEN) != 0) +
                    ", minimized " + std::to_string(state_.minimized) + ")."));
#else
    state_.drawableAvailable = true;
#endif
#if !defined(__ANDROID__)
    state_.suspended = state_.minimized;
#endif
  }

  void openGamepad(const SDL_JoystickID id, PlatformInput &input) {
    if (gamepads_.contains(id))
      return;
    SDL_Gamepad *gamepad = SDL_OpenGamepad(id);
    if (gamepad == nullptr)
      return;
    gamepads_[id] = gamepad;
    const char *name = SDL_GetGamepadName(gamepad);
    input.connectGamepad(static_cast<int>(id), name != nullptr ? name : "");
  }

  void closeGamepad(const SDL_JoystickID id, PlatformInput &input) {
    const auto gamepad = gamepads_.find(id);
    if (gamepad != gamepads_.end()) {
      SDL_CloseGamepad(gamepad->second);
      gamepads_.erase(gamepad);
    }
    input.disconnectGamepad(static_cast<int>(id));
  }

  SDL_Window *window_ = nullptr;
  std::unordered_map<SDL_JoystickID, SDL_Gamepad *> gamepads_;
  std::vector<std::filesystem::path> droppedFiles_;
  PlatformFrameState state_;
  int windowWidth_ = 1;
  int windowHeight_ = 1;
  Clock::time_point lastFrame_;
  unsigned pendingLowMemorySignals_ = 0;
#if defined(__ANDROID__)
  const void *nativeWindow_ = nullptr;
  Clock::time_point lastSurfaceChange_{};
#endif
};

} // namespace

std::unique_ptr<PlatformHost> createSdlPlatformHost() {
  return std::make_unique<SdlPlatformHost>();
}

} // namespace demi::runtime::platform
