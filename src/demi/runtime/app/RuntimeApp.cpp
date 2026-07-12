#include "demi/runtime/app/RuntimeApp.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/core/Version.h"
#include "demi/runtime/audio/AudioSystem.h"
#include "demi/runtime/media/MediaSystem.h"
#include "demi/runtime/network/NetworkSystem.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if DEMI_HAS_RAYLIB
#include "demi/runtime/render/Renderer2D.h"
#include "demi/runtime/render/Renderer3D.h"
#include <raylib.h>
#endif

namespace demi::runtime {

namespace {

constexpr int RuntimeFailure = 3;

#if DEMI_HAS_RAYLIB
void configureRaylibLogging() {
  if (std::getenv("DEMI_RAYLIB_INFO") == nullptr) {
    SetTraceLogLevel(LOG_WARNING);
  }
}
#endif

struct RuntimeProfile {
  int frames = 0;
  double updateMs = 0.0;
  double renderMs = 0.0;
  double frameMs = 0.0;
  double maxUpdateMs = 0.0;
  double maxRenderMs = 0.0;
  double maxFrameMs = 0.0;
  std::vector<double> updateSamples;
  std::vector<double> renderSamples;
  std::vector<double> frameSamples;
};

#if DEMI_HAS_RAYLIB
struct KeyMapping {
  int key;
  std::string_view name;
};

constexpr std::array<KeyMapping, 64> KeyMap{{
    {KEY_A, "a"},
    {KEY_B, "b"},
    {KEY_C, "c"},
    {KEY_D, "d"},
    {KEY_E, "e"},
    {KEY_F, "f"},
    {KEY_G, "g"},
    {KEY_H, "h"},
    {KEY_I, "i"},
    {KEY_J, "j"},
    {KEY_K, "k"},
    {KEY_L, "l"},
    {KEY_M, "m"},
    {KEY_N, "n"},
    {KEY_O, "o"},
    {KEY_P, "p"},
    {KEY_Q, "q"},
    {KEY_R, "r"},
    {KEY_S, "s"},
    {KEY_T, "t"},
    {KEY_U, "u"},
    {KEY_V, "v"},
    {KEY_W, "w"},
    {KEY_X, "x"},
    {KEY_Y, "y"},
    {KEY_Z, "z"},
    {KEY_ZERO, "0"},
    {KEY_ONE, "1"},
    {KEY_TWO, "2"},
    {KEY_THREE, "3"},
    {KEY_FOUR, "4"},
    {KEY_FIVE, "5"},
    {KEY_SIX, "6"},
    {KEY_SEVEN, "7"},
    {KEY_EIGHT, "8"},
    {KEY_NINE, "9"},
    {KEY_SPACE, "space"},
    {KEY_ENTER, "return"},
    {KEY_ESCAPE, "escape"},
    {KEY_TAB, "tab"},
    {KEY_BACKSPACE, "backspace"},
    {KEY_UP, "up"},
    {KEY_DOWN, "down"},
    {KEY_LEFT, "left"},
    {KEY_RIGHT, "right"},
    {KEY_LEFT_SHIFT, "left shift"},
    {KEY_RIGHT_SHIFT, "right shift"},
    {KEY_LEFT_CONTROL, "left ctrl"},
    {KEY_RIGHT_CONTROL, "right ctrl"},
    {KEY_F1, "f1"},
    {KEY_F2, "f2"},
    {KEY_F3, "f3"},
    {KEY_F4, "f4"},
    {KEY_F5, "f5"},
    {KEY_F6, "f6"},
    {KEY_F7, "f7"},
    {KEY_F8, "f8"},
    {KEY_F9, "f9"},
    {KEY_F10, "f10"},
    {KEY_F11, "f11"},
    {KEY_F12, "f12"},
}};

void pollKeys(InputState &input) {
  const std::unordered_set<std::string> previousKeysDown = input.keysDown;
  input.keysDown.clear();
  input.keysPressed.clear();
  input.textEntered.clear();
  for (const KeyMapping &mapping : KeyMap) {
    if (IsKeyDown(mapping.key)) {
      input.keysDown.emplace(mapping.name);
      if (!previousKeysDown.contains(std::string(mapping.name))) {
        input.keysPressed.emplace(mapping.name);
      }
    }
  }
  if (IsGamepadAvailable(0)) {
    const std::array<KeyMapping, 3> gamepadUiMap{{
        {GAMEPAD_BUTTON_LEFT_FACE_DOWN, "ui_next"},
        {GAMEPAD_BUTTON_LEFT_FACE_UP, "ui_previous"},
        {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, "ui_accept"},
    }};
    for (const KeyMapping &mapping : gamepadUiMap) {
      if (IsGamepadButtonDown(0, mapping.key)) {
        input.keysDown.emplace(mapping.name);
        if (!previousKeysDown.contains(std::string(mapping.name))) {
          input.keysPressed.emplace(mapping.name);
        }
      }
    }
  }
  for (int character = GetCharPressed(); character > 0;
       character = GetCharPressed()) {
    if (character >= 32 && character <= 126) {
      input.textEntered.push_back(static_cast<char>(character));
    }
  }
}

void pollMouse(InputState &input) {
  input.mouseButtonsDown.clear();
#if defined(__ANDROID__)
  const int touchCount = GetTouchPointCount();
  if (touchCount > 0) {
    input.mouseButtonsDown.emplace("left");
    const Vector2 touch = GetTouchPosition(0);
    const Vec2 previous = input.mousePosition;
    input.mousePosition = Vec2{.x = touch.x, .y = touch.y};
    input.mouseDelta = Vec2{.x = input.mousePosition.x - previous.x,
                            .y = input.mousePosition.y - previous.y};
  } else {
    input.mouseDelta = Vec2{};
  }
  return;
#endif
  if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
    input.mouseButtonsDown.emplace("left");
  }
  if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
    input.mouseButtonsDown.emplace("right");
  }
  if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
    input.mouseButtonsDown.emplace("middle");
  }
  input.mousePosition = Vec2{.x = static_cast<float>(GetMouseX()),
                             .y = static_cast<float>(GetMouseY())};
  const Vector2 delta = GetMouseDelta();
  input.mouseDelta = Vec2{.x = delta.x, .y = delta.y};
}

void applyWindowMode(const std::string &mode) {
  if (mode == "fullscreen") {
    if (!IsWindowFullscreen()) {
      const int monitor = GetCurrentMonitor();
      SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
      ToggleFullscreen();
    }
    return;
  }

  if (IsWindowFullscreen()) {
    ToggleFullscreen();
  }

  if (mode == "borderless") {
    SetWindowState(FLAG_WINDOW_UNDECORATED);
    const int monitor = GetCurrentMonitor();
    SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
  } else {
    ClearWindowState(FLAG_WINDOW_UNDECORATED);
  }

  if (mode == "windowed") {
    SetWindowSize(960, 540);
  }
}

#endif

void stepSimulation(LoadedProject &loaded, LuaScriptHost &luaHost,
                    InputState &input, AudioSystem &audioSystem,
                    MediaSystem &mediaSystem, NetworkSystem &networkSystem,
                    const float dt, const float fixedStep,
                    double &fixedAccumulator, bool &running) {
  ProfileScope stepScope("Runtime.step_simulation");
  fixedAccumulator += dt;
  while (fixedAccumulator >= fixedStep) {
    {
      ProfileScope scope("Lua.fixed_update");
      luaHost.fixedUpdate(static_cast<float>(fixedStep));
    }
    if (luaHost.physicsEnabled()) {
      ProfileScope scope("Physics2D.step");
      stepPhysics2D(loaded.world, static_cast<float>(fixedStep));
    }
    fixedAccumulator -= fixedStep;
  }

  {
    ProfileScope scope("Network.update");
    networkSystem.update();
  }
  {
    ProfileScope scope("Lua.update");
    luaHost.update(dt);
  }
  if (luaHost.quitRequested()) {
    running = false;
  }
  if (luaHost.hasPendingSceneLoad()) {
    std::string sceneError;
    if (!luaHost.applyPendingSceneLoad(sceneError)) {
      std::cerr << "Scene switch failed: " << sceneError << '\n';
    } else {
      std::cout << "Switched scene to " << loaded.world.id << " ("
                << loaded.world.name << ").\n";
    }
  }

  {
    ProfileScope scope("Audio.update");
    audioSystem.update();
  }
  {
    ProfileScope scope("Media.update");
    mediaSystem.update(dt);
  }
  (void)input;
}

[[nodiscard]] bool isHeadless() {
  const char *value = std::getenv("DEMI_HEADLESS");
  return value != nullptr && std::string(value) != "0";
}

[[nodiscard]] bool profilingEnabled() {
  const char *value = std::getenv("DEMI_PROFILE");
  return value != nullptr && std::string(value) != "0";
}

[[nodiscard]] double profileSlowThresholdMs() {
  const char *value = std::getenv("DEMI_PROFILE_SLOW_MS");
  if (value == nullptr || std::string(value).empty()) {
    return 16.667;
  }
  try {
    return std::max(0.0, std::stod(value));
  } catch (...) {
    return 16.667;
  }
}

[[nodiscard]] double
millisecondsSince(const std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - start)
      .count();
}

double percentile(std::vector<double> samples, const double fraction) {
  if (samples.empty()) {
    return 0.0;
  }
  std::ranges::sort(samples);
  const std::size_t index = std::min<std::size_t>(
      samples.size() - 1, static_cast<std::size_t>(std::ceil(
                              static_cast<double>(samples.size()) * fraction)) -
                              1);
  return samples[index];
}

void printProfile(const RuntimeProfile &profile) {
  if (profile.frames <= 0) {
    return;
  }
  const double frames = static_cast<double>(profile.frames);
  std::cout << std::fixed << std::setprecision(3)
            << "Profile: frames=" << profile.frames
            << " avg_update_ms=" << profile.updateMs / frames
            << " avg_render_ms=" << profile.renderMs / frames
            << " avg_frame_ms=" << profile.frameMs / frames
            << " max_update_ms=" << profile.maxUpdateMs
            << " max_render_ms=" << profile.maxRenderMs
            << " max_frame_ms=" << profile.maxFrameMs
            << " p95_update_ms=" << percentile(profile.updateSamples, 0.95)
            << " p95_render_ms=" << percentile(profile.renderSamples, 0.95)
            << " p95_frame_ms=" << percentile(profile.frameSamples, 0.95)
            << " p99_update_ms=" << percentile(profile.updateSamples, 0.99)
            << " p99_render_ms=" << percentile(profile.renderSamples, 0.99)
            << " p99_frame_ms=" << percentile(profile.frameSamples, 0.99)
            << '\n';
}

void logSlowProfileFrame(const int frame, const double updateMs,
                         const double renderMs, const double frameMs,
                         const double thresholdMs) {
  if (frameMs < thresholdMs && updateMs < thresholdMs &&
      renderMs < thresholdMs) {
    return;
  }
  const std::string scopes = RuntimeProfiler::frameSummary();
  std::cout << std::fixed << std::setprecision(3)
            << "Profile slow frame: frame=" << frame
            << " update_ms=" << updateMs << " render_ms=" << renderMs
            << " frame_ms=" << frameMs << " threshold_ms=" << thresholdMs;
  if (!scopes.empty()) {
    std::cout << " scopes=[" << scopes << ']';
  }
  std::cout << '\n';
}

} // namespace

int runProject(const RuntimeOptions &options) {
  std::string error;
  const std::optional<LoadedProject> loadedProject =
      loadProject(options.projectPath, error);
  if (!loadedProject.has_value()) {
    std::cerr << "Runtime load failed: " << error << '\n';
    return 1;
  }
  LoadedProject loaded = *loadedProject;

  const AssetRegistry assetRegistry =
      loadAssetRegistry(loaded.project.projectDirectory);
  AudioSystem audioSystem;
  if (!isHeadless() && !options.serve && options.maxFrames == 0 &&
      audioSystem.initialize()) {
    audioSystem.loadAudioAssets(assetRegistry);
  }
  MediaSystem mediaSystem;
  mediaSystem.loadVideoAssets(assetRegistry);
  NetworkSystem networkSystem;
  (void)networkSystem.initialize();
  InputState input;

  LuaScriptHost luaHost;
  luaHost.setMediaSystem(&mediaSystem);
  luaHost.setNetworkSystem(&networkSystem);
  std::string luaError;
  if (luaHost.initialize(loaded.world, input, &audioSystem, luaError)) {
    if (!luaHost.loadWorldScripts(loaded.project, loaded.world, luaError)) {
      std::cerr << "Lua scripts skipped: " << luaError << '\n';
    } else {
      luaHost.start();
    }
  } else {
    std::cerr << "Lua unavailable: " << luaError << '\n';
  }

  std::cout << "Running " << loaded.project.name << " scene " << loaded.world.id
            << " with " << renderableEntityCount(loaded.world)
            << " renderable entity/entities.\n";
  std::cout << "Game scripts now own controls. Close the window or use "
               "script-defined controls to stop.\n";

  double fixedAccumulator = 0.0;
  constexpr double fixedStep = 1.0 / 60.0;
  RuntimeProfile profile;
  const bool profileRun = profilingEnabled();
  RuntimeProfiler::setEnabled(profileRun);
  const double slowProfileThresholdMs = profileSlowThresholdMs();

  if (isHeadless() || options.serve) {
    int frameCount = 0;
    const int targetFrames = options.maxFrames > 0 ? options.maxFrames : 1;
    bool running = true;
    while (running && (options.serve || frameCount < targetFrames)) {
      const auto nextFrame = std::chrono::steady_clock::now() +
                             std::chrono::duration<double>(fixedStep);
      RuntimeProfiler::beginFrame();
      const auto frameStart = std::chrono::steady_clock::now();
      const auto updateStart = std::chrono::steady_clock::now();
      stepSimulation(loaded, luaHost, input, audioSystem, mediaSystem,
                     networkSystem, static_cast<float>(fixedStep),
                     static_cast<float>(fixedStep), fixedAccumulator, running);
      if (profileRun) {
        const double updateMs = millisecondsSince(updateStart);
        const double frameMs = millisecondsSince(frameStart);
        profile.updateMs += updateMs;
        profile.frameMs += frameMs;
        profile.maxUpdateMs = std::max(profile.maxUpdateMs, updateMs);
        profile.maxFrameMs = std::max(profile.maxFrameMs, frameMs);
        profile.updateSamples.push_back(updateMs);
        profile.frameSamples.push_back(frameMs);
        logSlowProfileFrame(frameCount, updateMs, 0.0, frameMs,
                            slowProfileThresholdMs);
        ++profile.frames;
      }
      ++frameCount;
      if (options.serve) {
        std::this_thread::sleep_until(nextFrame);
      }
    }
    if (profileRun) {
      printProfile(profile);
    }
    luaHost.destroy();
    networkSystem.shutdown();
    mediaSystem.shutdown();
    audioSystem.shutdown();
    return 0;
  }

#if !DEMI_HAS_RAYLIB
  std::cerr << "Runtime windowing is unavailable because raylib was not found "
               "at configure time.\n";
  luaHost.destroy();
  networkSystem.shutdown();
  mediaSystem.shutdown();
  audioSystem.shutdown();
  return RuntimeFailure;
#else
  const bool use3D = sceneIs3D(loaded.world);
  const Camera2DComponent fallbackCamera2D;
  const Camera3DComponent fallbackCamera3D;

  configureRaylibLogging();
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  constexpr int windowWidth = 960;
  constexpr int windowHeight = 540;
  const std::string title = std::string(EngineName) + " - " +
                            loaded.project.name + " - " + loaded.world.name;
  InitWindow(windowWidth, windowHeight, title.c_str());
  SetExitKey(KEY_NULL);

  Renderer2D renderer2D;
  Renderer3D renderer3D;
  if (use3D) {
    renderer3D.loadTextureAssets(assetRegistry);
  } else {
    renderer2D.loadTextureAssets(assetRegistry);
  }

  bool running = true;
  int frameCount = 0;
  int appliedMaxFps = -1;
  while (running && !WindowShouldClose()) {
    RuntimeProfiler::beginFrame();
    const auto frameStart = std::chrono::steady_clock::now();
    pollKeys(input);
    pollMouse(input);

    const float dt = std::min(GetFrameTime(), 0.1F);

    double updateMs = 0.0;
    const auto updateStart = std::chrono::steady_clock::now();
    stepSimulation(loaded, luaHost, input, audioSystem, mediaSystem,
                   networkSystem, dt, static_cast<float>(fixedStep),
                   fixedAccumulator, running);
    if (profileRun) {
      updateMs = millisecondsSince(updateStart);
      profile.updateMs += updateMs;
      profile.maxUpdateMs = std::max(profile.maxUpdateMs, updateMs);
      profile.updateSamples.push_back(updateMs);
    }

    if (luaHost.windowModeDirty()) {
      applyWindowMode(luaHost.windowMode());
      luaHost.clearWindowModeDirty();
    }
    if (luaHost.mouseCapturedDirty()) {
      if (luaHost.mouseCaptured()) {
        DisableCursor();
      } else {
        EnableCursor();
      }
      luaHost.clearMouseCapturedDirty();
    }

    const int width = GetRenderWidth();
    const int height = GetRenderHeight();

    luaHost.setViewport(width, height);

    const auto renderStart = std::chrono::steady_clock::now();
    if (use3D) {
      const Camera3DComponent *camera = activeCamera3D(loaded.world);
      renderer3D.beginFrame(camera != nullptr ? *camera : fallbackCamera3D,
                            activeCamera3DPosition(loaded.world),
                            activeCamera3DRotation(loaded.world), width,
                            height);
      renderer3D.drawWorld(loaded.world, dt);
      renderer3D.drawHud(loaded.world);
      renderer3D.endFrame();
    } else {
      const Camera2DComponent *camera = activeCamera(loaded.world);
      renderer2D.beginFrame(camera != nullptr ? *camera : fallbackCamera2D,
                            activeCameraPosition(loaded.world), width, height);
      renderer2D.drawWorld(loaded.world);
      renderer2D.drawHud(loaded.world);
      renderer2D.endFrame();
    }
    double renderMs = 0.0;
    if (profileRun) {
      renderMs = millisecondsSince(renderStart);
      const double frameMs = millisecondsSince(frameStart);
      profile.renderMs += renderMs;
      profile.frameMs += frameMs;
      profile.maxRenderMs = std::max(profile.maxRenderMs, renderMs);
      profile.maxFrameMs = std::max(profile.maxFrameMs, frameMs);
      profile.renderSamples.push_back(renderMs);
      profile.frameSamples.push_back(frameMs);
      logSlowProfileFrame(frameCount, updateMs, renderMs, frameMs,
                          slowProfileThresholdMs);
      ++profile.frames;
    }

    const int maxFps = luaHost.maxFps();
    if (maxFps != appliedMaxFps) {
      if (maxFps > 0) {
        SetTargetFPS(maxFps);
      } else {
        SetTargetFPS(0);
      }

      appliedMaxFps = maxFps;
    }

    ++frameCount;
    if (options.maxFrames > 0 && frameCount >= options.maxFrames) {
      running = false;
    }
  }

  luaHost.destroy();
  networkSystem.shutdown();
  mediaSystem.shutdown();
  audioSystem.shutdown();
  CloseWindow();
  if (profileRun) {
    printProfile(profile);
  }
  return 0;
#endif
}

} // namespace demi::runtime
