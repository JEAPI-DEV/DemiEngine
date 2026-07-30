#include "demi/runtime/app/RuntimeApp.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/core/Version.h"
#include "demi/runtime/animation/AnimationCollision2DSystem.h"
#include "demi/runtime/animation/AnimationStateMachineSystem.h"
#include "demi/runtime/animation/SpriteAnimationSystem.h"
#include "demi/runtime/app/Bgfx2DAppHost.h"
#include "demi/runtime/audio/AudioSceneSystem.h"
#include "demi/runtime/audio/AudioSystem.h"
#include "demi/runtime/camera/Camera2DSystem.h"
#include "demi/runtime/camera/CameraRenderScheduler3D.h"
#include "demi/runtime/input/replay/InputReplay.h"
#include "demi/runtime/media/MediaSystem.h"
#include "demi/runtime/network/NetworkSystem.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/RaylibFileSystemBridge.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/tilemap/TilemapCollisionGenerator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
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
  input.keysReleased.clear();
  input.textEntered.clear();
  for (const KeyMapping &mapping : KeyMap) {
    if (IsKeyDown(mapping.key)) {
      input.keysDown.emplace(mapping.name);
      if (!previousKeysDown.contains(std::string(mapping.name))) {
        input.keysPressed.emplace(mapping.name);
      }
    }
  }
  for (const std::string &key : previousKeysDown)
    if (!input.keysDown.contains(key))
      input.keysReleased.insert(key);
  for (int character = GetCharPressed(); character > 0;
       character = GetCharPressed()) {
    if (character >= 32 && character <= 126) {
      input.textEntered.push_back(static_cast<char>(character));
    }
  }
}

void pollMouse(InputState &input) {
  const std::unordered_set<std::string> previous = input.mouseButtonsDown;
  input.mouseButtonsDown.clear();
  input.mouseButtonsPressed.clear();
  input.mouseButtonsReleased.clear();
#if !defined(__ANDROID__)
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
#endif
  for (const std::string &button : input.mouseButtonsDown)
    if (!previous.contains(button))
      input.mouseButtonsPressed.insert(button);
  for (const std::string &button : previous)
    if (!input.mouseButtonsDown.contains(button))
      input.mouseButtonsReleased.insert(button);
}

void pollGamepads(InputState &input) {
  const std::vector<GamepadState> previous = input.gamepads;
  input.gamepads.clear();
  struct ButtonMapping {
    int button;
    std::string_view name;
  };
  constexpr std::array<ButtonMapping, 17> buttons{{
      {GAMEPAD_BUTTON_LEFT_FACE_UP, "dpad_up"},
      {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, "dpad_right"},
      {GAMEPAD_BUTTON_LEFT_FACE_DOWN, "dpad_down"},
      {GAMEPAD_BUTTON_LEFT_FACE_LEFT, "dpad_left"},
      {GAMEPAD_BUTTON_RIGHT_FACE_UP, "north"},
      {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, "east"},
      {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, "south"},
      {GAMEPAD_BUTTON_RIGHT_FACE_LEFT, "west"},
      {GAMEPAD_BUTTON_LEFT_TRIGGER_1, "left_bumper"},
      {GAMEPAD_BUTTON_LEFT_TRIGGER_2, "left_trigger"},
      {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, "right_bumper"},
      {GAMEPAD_BUTTON_RIGHT_TRIGGER_2, "right_trigger"},
      {GAMEPAD_BUTTON_MIDDLE_LEFT, "select"},
      {GAMEPAD_BUTTON_MIDDLE, "guide"},
      {GAMEPAD_BUTTON_MIDDLE_RIGHT, "start"},
      {GAMEPAD_BUTTON_LEFT_THUMB, "left_stick"},
      {GAMEPAD_BUTTON_RIGHT_THUMB, "right_stick"},
  }};
  for (int device = 0; device < 4; ++device) {
    if (!IsGamepadAvailable(device))
      continue;
    GamepadState state;
    state.deviceId = device;
    state.player = input.gamepadAssignments.contains(device)
                       ? input.gamepadAssignments.at(device)
                       : device;
    state.name =
        GetGamepadName(device) != nullptr ? GetGamepadName(device) : "";
    const auto old =
        std::ranges::find(previous, device, &GamepadState::deviceId);
    for (const ButtonMapping &mapping : buttons) {
      const std::string name(mapping.name);
      if (IsGamepadButtonDown(device, mapping.button)) {
        state.buttonsDown.insert(name);
        if (old == previous.end() || !old->buttonsDown.contains(name))
          state.buttonsPressed.insert(name);
      } else if (old != previous.end() && old->buttonsDown.contains(name)) {
        state.buttonsReleased.insert(name);
      }
    }
    state.axes = {
        {"left_x", GetGamepadAxisMovement(device, GAMEPAD_AXIS_LEFT_X)},
        {"left_y", GetGamepadAxisMovement(device, GAMEPAD_AXIS_LEFT_Y)},
        {"right_x", GetGamepadAxisMovement(device, GAMEPAD_AXIS_RIGHT_X)},
        {"right_y", GetGamepadAxisMovement(device, GAMEPAD_AXIS_RIGHT_Y)},
        {"left_trigger",
         (GetGamepadAxisMovement(device, GAMEPAD_AXIS_LEFT_TRIGGER) + 1.0F) *
             0.5F},
        {"right_trigger",
         (GetGamepadAxisMovement(device, GAMEPAD_AXIS_RIGHT_TRIGGER) + 1.0F) *
             0.5F},
    };
    const auto mirrorUiButton = [&](const std::string &button,
                                    const std::string &key) {
      if (state.buttonsDown.contains(button)) {
        input.keysDown.insert(key);
        input.keysReleased.erase(key);
      }
      if (state.buttonsPressed.contains(button))
        input.keysPressed.insert(key);
      if (state.buttonsReleased.contains(button))
        input.keysReleased.insert(key);
    };
    mirrorUiButton("dpad_down", "ui_next");
    mirrorUiButton("dpad_up", "ui_previous");
    mirrorUiButton("south", "ui_accept");
    input.gamepads.push_back(std::move(state));
  }
}

void pollTouches(InputState &input) {
  const std::vector<TouchPoint> previous = input.touches;
  input.touches.clear();
  const int count = GetTouchPointCount();
  for (int index = 0; index < count; ++index) {
    const std::int64_t id = GetTouchPointId(index);
    const Vector2 raw = GetTouchPosition(index);
    const Vec2 position{raw.x, raw.y};
    const auto old = std::ranges::find(previous, id, &TouchPoint::id);
    input.touches.push_back(
        {.id = id,
         .phase = old == previous.end() ? TouchPhase::Began
                                        : (old->position.x == position.x &&
                                                   old->position.y == position.y
                                               ? TouchPhase::Stationary
                                               : TouchPhase::Moved),
         .position = position,
         .delta = old == previous.end() ? Vec2{}
                                        : Vec2{position.x - old->position.x,
                                               position.y - old->position.y}});
  }
  for (const TouchPoint &old : previous)
    if (old.phase != TouchPhase::Ended && old.phase != TouchPhase::Cancelled &&
        std::ranges::find(input.touches, old.id, &TouchPoint::id) ==
            input.touches.end())
      input.touches.push_back({.id = old.id,
                               .phase = TouchPhase::Ended,
                               .position = old.position,
                               .delta = {},
                               .pressure = old.pressure});
#if defined(__ANDROID__)
  const auto primary =
      std::ranges::find_if(input.touches, [](const TouchPoint &touch) {
        return touch.phase != TouchPhase::Ended &&
               touch.phase != TouchPhase::Cancelled;
      });
  if (primary != input.touches.end()) {
    input.mousePosition = primary->position;
    input.mouseDelta = primary->delta;
    input.mouseButtonsDown.insert("left");
    input.mouseButtonsReleased.erase("left");
    if (primary->phase == TouchPhase::Began)
      input.mouseButtonsPressed.insert("left");
  }
#endif
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
                    const AssetRegistry &assetRegistry, const float dt,
                    const float fixedStep, double &fixedAccumulator,
                    bool &running) {
  ProfileScope stepScope("Runtime.step_simulation");
  luaHost.beginFrame(dt);
  const float scaledDt = luaHost.deltaTime();
  fixedAccumulator += scaledDt;
  while (fixedAccumulator >= fixedStep) {
    {
      ProfileScope scope("Lua.fixed_update");
      luaHost.fixedUpdate(static_cast<float>(fixedStep));
    }
    if (luaHost.physicsEnabled()) {
      {
        ProfileScope scope("Physics2D.step");
        stepPhysics2D(loaded.world, static_cast<float>(fixedStep));
      }
      {
        ProfileScope scope("Physics3D.step");
        stepPhysics3D(loaded.world, static_cast<float>(fixedStep));
      }
    }
    luaHost.advanceFixedTime(fixedStep);
    fixedAccumulator -= fixedStep;
  }

  {
    ProfileScope scope("Network.update");
    networkSystem.update();
  }
  {
    ProfileScope scope("AnimationStateMachine.update");
    AnimationStateMachineSystem{}.update(loaded.world, scaledDt, dt);
  }
  {
    ProfileScope scope("SpriteAnimation2D.update");
    SpriteAnimationSystem{}.update(loaded.world, scaledDt);
  }
  {
    ProfileScope scope("AnimationCollision2D.update");
    AnimationCollision2DSystem{}.update(loaded.world);
  }
  {
    ProfileScope scope("Lua.update");
    luaHost.update(scaledDt);
  }
  {
    ProfileScope scope("Camera2D.update");
    Camera2DSystem{}.update(loaded.world, scaledDt);
  }
  if (loaded.world.tilemapCollisionDirty) {
    ProfileScope scope("Tilemap2D.rebuild_collision");
    generateTilemapColliders(loaded.world, assetRegistry);
  }
  if (luaHost.quitRequested()) {
    running = false;
  }
  if (luaHost.hasPendingSceneLoad()) {
    std::string sceneError;
    if (!luaHost.applyPendingSceneLoad(sceneError)) {
      std::cerr << "Scene switch failed: " << sceneError << '\n';
    } else {
      generateTilemapColliders(loaded.world, assetRegistry);
      std::cout << "Switched scene to " << loaded.world.id << " ("
                << loaded.world.name << ").\n";
    }
  }

  {
    ProfileScope scope("Audio.update");
    audioSystem.setGamePaused(luaHost.paused());
    AudioSceneSystem{}.update(loaded.world, audioSystem);
    audioSystem.update(dt);
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

void writeProfileReport(const std::filesystem::path &path) {
  if (path.empty())
    return;
  std::ofstream output(path);
  if (!output) {
    std::cerr << "Profile report write failed: " << path << '\n';
    return;
  }
  output << RuntimeProfiler::sessionReport();
  std::cout << "Profile report written to " << path << '\n';
}

void applyDebugOverlayFlags(DebugOverlayConfig &config,
                            const std::string &flags) {
  if (flags.empty())
    return;
  config = {};
  std::size_t start = 0;
  while (start <= flags.size()) {
    const std::size_t end = flags.find(',', start);
    const std::string flag = flags.substr(start, end - start);
    const bool all = flag == "all";
    config.colliders |= all || flag == "colliders";
    config.contacts |= all || flag == "contacts";
    config.grid |= all || flag == "grid" || flag == "navigation";
    config.entityIds |= all || flag == "entity_ids";
    config.drawOrder |= all || flag == "draw_order";
    config.uiBounds |= all || flag == "ui_bounds";
    config.profilerHud |= all || flag == "profiler";
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
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
  bool profileRun = options.profiler || profilingEnabled() ||
                    !options.profileReportPath.empty();
  RuntimeProfiler::setEnabled(profileRun);
  RuntimeProfiler::resetSession();
  std::string error;
  std::optional<LoadedProject> loadedProject =
      loadProject(options.projectPath, error);
  if (!loadedProject.has_value()) {
    std::cerr << "Runtime load failed: " << error << '\n';
    return 1;
  }
  LoadedProject &loaded = *loadedProject;
  applyDebugOverlayFlags(loaded.project.debug, options.debugOverlays);
  loaded.world.debug = loaded.project.debug;
  if (loaded.project.debug.profilerHud && !profileRun) {
    profileRun = true;
    RuntimeProfiler::setEnabled(true);
  }

  AssetRegistry assetRegistry;
  {
    ProfileScope scope("Asset.registry_load");
    assetRegistry = loadAssetRegistry(loaded.project.projectDirectory);
  }
  std::optional<input::InputReplay> inputReplay;
  if (!options.inputReplayPath.empty()) {
    std::string replayError;
    inputReplay = input::loadInputReplay(options.inputReplayPath, replayError);
    if (!inputReplay) {
      std::cerr << "Input replay load failed: " << replayError << '\n';
      return 1;
    }
    if (std::abs(inputReplay->fixedTimestep -
                 loaded.project.simulation.fixedTimestep) > 0.000001F) {
      std::cerr << "Input replay fixed_timestep does not match the project "
                   "simulation.fixed_timestep.\n";
      return 1;
    }
  }
  generateTilemapColliders(loaded.world, assetRegistry);
  AudioSystem audioSystem;
  if (!isHeadless() && !options.serve && options.maxFrames == 0 &&
      audioSystem.initialize()) {
    ProfileScope scope("Asset.audio_load");
    audioSystem.loadAudioAssets(assetRegistry);
  }
  MediaSystem mediaSystem;
  {
    ProfileScope scope("Asset.media_load");
    mediaSystem.loadVideoAssets(assetRegistry);
  }
  NetworkSystem networkSystem;
  (void)networkSystem.initialize();
  InputState input;

  LuaScriptHost luaHost;
  luaHost.setMediaSystem(&mediaSystem);
  luaHost.setNetworkSystem(&networkSystem);
  std::string luaError;
  if (luaHost.initialize(loaded.world, input, &audioSystem, luaError)) {
    luaHost.setAssetRegistry(&assetRegistry);
    luaHost.seedRandom(loaded.project.simulation.randomSeed);
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
  const double fixedStep = loaded.project.simulation.fixedTimestep;
  RuntimeProfile profile;
  const double slowProfileThresholdMs = profileSlowThresholdMs();

  if (isHeadless() || options.serve) {
    // A headless run has no native window to supply pointer coordinates.
    // Treat replay positions as logical HUD-canvas coordinates, matching the
    // 960x540 coordinates authored by examples and used by windowed playback.
    luaHost.setViewport(
        std::max(static_cast<int>(std::lround(loaded.world.hudCanvasSize.x)),
                 1),
        std::max(static_cast<int>(std::lround(loaded.world.hudCanvasSize.y)),
                 1));
    int frameCount = 0;
    const int targetFrames =
        options.maxFrames > 0
            ? options.maxFrames
            : (inputReplay ? static_cast<int>(inputReplay->frames.size()) : 1);
    bool running = true;
    while (running && (options.serve || frameCount < targetFrames)) {
      if (inputReplay &&
          !inputReplay->apply(static_cast<std::size_t>(frameCount), input))
        break;
      const auto nextFrame = std::chrono::steady_clock::now() +
                             std::chrono::duration<double>(fixedStep);
      RuntimeProfiler::beginFrame();
      const auto frameStart = std::chrono::steady_clock::now();
      const auto updateStart = std::chrono::steady_clock::now();
      stepSimulation(loaded, luaHost, input, audioSystem, mediaSystem,
                     networkSystem, assetRegistry,
                     static_cast<float>(fixedStep),
                     static_cast<float>(fixedStep), fixedAccumulator, running);
      if (profileRun) {
        const double updateMs = millisecondsSince(updateStart);
        const double frameMs = millisecondsSince(frameStart);
        RuntimeProfiler::record("Frame.update", updateMs);
        RuntimeProfiler::record("Frame.total", frameMs);
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
    if (options.profiler)
      std::cout << RuntimeProfiler::sessionReport();
    writeProfileReport(options.profileReportPath);
    luaHost.destroy();
    networkSystem.shutdown();
    mediaSystem.shutdown();
    audioSystem.shutdown();
    return 0;
  }

  const bool use3D = sceneIs3D(loaded.world);
  const Camera2DComponent fallbackCamera2D;
  const Camera3DComponent fallbackCamera3D;

#if !DEMI_HAS_RAYLIB
  std::cerr << "Runtime windowing is unavailable because raylib was not found "
               "at configure time.\n";
  luaHost.destroy();
  networkSystem.shutdown();
  mediaSystem.shutdown();
  audioSystem.shutdown();
  return RuntimeFailure;
#else
  if (!use3D) {
    Bgfx2DAppHost appHost;
    std::vector<std::string> renderDiagnostics;
    const std::string title = std::string(EngineName) + " - " +
                              loaded.project.name + " - " + loaded.world.name;
    if (!appHost.initialize(
            Bgfx2DAppHostConfig{
                .title = title,
                .width = 960,
                .height = 540,
                .graphicsApi = configuredGraphicsApi(),
                .vsync = true,
                .debugGraphics = false,
            },
            assetRegistry, renderDiagnostics, error)) {
      std::cerr << "2D renderer initialization failed: " << error << '\n';
      luaHost.destroy();
      networkSystem.shutdown();
      mediaSystem.shutdown();
      audioSystem.shutdown();
      return RuntimeFailure;
    }
    for (const std::string &diagnostic : renderDiagnostics)
      std::cerr << "2D asset load failed: " << diagnostic << '\n';

    luaHost.applicationServices().setClipboardHandlers(
        [&appHost] { return appHost.clipboard(); },
        [&appHost](const std::string &text) {
          std::string clipboardError;
          if (!appHost.setClipboard(text, clipboardError))
            std::cerr << "Clipboard update failed: " << clipboardError << '\n';
        });

    std::cout << "Using bgfx " << appHost.rendererName()
              << " through the SDL3 platform host.\n";
    bool running = true;
    bool renderFailed = false;
    int frameCount = 0;
    while (running) {
      appHost.poll(input);
      const platform::PlatformFrameState &frameState = appHost.frameState();
      if (frameState.quitRequested)
        break;
      if (inputReplay) {
        if (!inputReplay->apply(static_cast<std::size_t>(frameCount), input))
          break;
        const std::uint64_t frameSeed =
            loaded.project.simulation.randomSeed ^
            (static_cast<std::uint64_t>(frameCount) * 0x9E3779B97F4A7C15ULL);
        luaHost.seedRandom(frameSeed);
      }

      luaHost.setApplicationFocused(frameState.focused);
      luaHost.setApplicationMinimized(frameState.minimized);
      luaHost.setApplicationSuspended(frameState.suspended);
      audioSystem.setSuspended(frameState.suspended);
      for (unsigned signal = 0; signal < frameState.lowMemorySignals; ++signal)
        luaHost.notifyApplicationLowMemory();
      luaHost.applicationServices().updateDisplay(
          frameState.width, frameState.height, frameState.logicalDpi);
      luaHost.setViewport(frameState.width, frameState.height);

      RuntimeProfiler::beginFrame();
      const auto frameStart = std::chrono::steady_clock::now();
      const float dt = frameState.deltaSeconds;
      double updateMs = 0.0;
      const auto updateStart = std::chrono::steady_clock::now();
      stepSimulation(loaded, luaHost, input, audioSystem, mediaSystem,
                     networkSystem, assetRegistry, dt,
                     static_cast<float>(fixedStep), fixedAccumulator, running);
      if (profileRun) {
        updateMs = millisecondsSince(updateStart);
        profile.updateMs += updateMs;
        profile.maxUpdateMs = std::max(profile.maxUpdateMs, updateMs);
        profile.updateSamples.push_back(updateMs);
      }

      if (luaHost.windowModeDirty()) {
        std::string modeError;
        if (!appHost.setWindowMode(luaHost.windowMode(), modeError))
          std::cerr << "Window mode update failed: " << modeError << '\n';
        luaHost.clearWindowModeDirty();
      }
      if (luaHost.mouseCapturedDirty()) {
        std::string captureError;
        if (!appHost.setMouseCaptured(luaHost.mouseCaptured(), captureError))
          std::cerr << "Mouse capture update failed: " << captureError << '\n';
        luaHost.clearMouseCapturedDirty();
      }

      const auto renderStart = std::chrono::steady_clock::now();
      const Camera2DComponent *camera = activeCamera(loaded.world);
      const navigation::NavigationGrid2D *navigation =
          loaded.world.debug.grid ? &luaHost.navigationGrid2D() : nullptr;
      std::string renderError;
      {
        ProfileScope renderScope("Render.frame");
        if (!appHost.renderFrame(loaded.world,
                                 camera != nullptr ? *camera : fallbackCamera2D,
                                 activeCameraPosition(loaded.world), dt,
                                 navigation, renderError)) {
          std::cerr << "2D rendering failed: " << renderError << '\n';
          renderFailed = true;
          running = false;
        }
      }

      if (profileRun) {
        const double renderMs = millisecondsSince(renderStart);
        const double frameMs = millisecondsSince(frameStart);
        RuntimeProfiler::record("Frame.update", updateMs);
        RuntimeProfiler::record("Render.total", renderMs);
        RuntimeProfiler::record("Frame.total", frameMs);
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

      ++frameCount;
      if (options.maxFrames > 0 && frameCount >= options.maxFrames)
        running = false;
      const int maxFps = luaHost.maxFps();
      if (running && maxFps > 0) {
        std::this_thread::sleep_until(
            frameStart + std::chrono::duration<double>(1.0 / maxFps));
      }
    }

    luaHost.destroy();
    networkSystem.shutdown();
    mediaSystem.shutdown();
    audioSystem.shutdown();
    appHost.shutdown();
    if (profileRun)
      printProfile(profile);
    if (options.profiler)
      std::cout << RuntimeProfiler::sessionReport();
    writeProfileReport(options.profileReportPath);
    return renderFailed ? RuntimeFailure : 0;
  }

  configureRaylibLogging();
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  constexpr int windowWidth = 960;
  constexpr int windowHeight = 540;
  const std::string title = std::string(EngineName) + " - " +
                            loaded.project.name + " - " + loaded.world.name;
  InitWindow(windowWidth, windowHeight, title.c_str());
  SetExitKey(KEY_NULL);
  installRaylibFileSystemBridge();
  luaHost.applicationServices().setClipboardHandlers(
      [] {
        const char *text = GetClipboardText();
        return text != nullptr ? std::string(text) : std::string{};
      },
      [](const std::string &text) { SetClipboardText(text.c_str()); });

  Renderer2D renderer2D;
  Renderer3D renderer3D;
  CameraRenderScheduler3D cameraRenderScheduler3D;
  if (use3D) {
    ProfileScope scope("Asset.renderer_load");
    renderer3D.loadAssets(assetRegistry);
  } else {
    ProfileScope scope("Asset.renderer_load");
    renderer2D.loadAssets(assetRegistry);
  }

  bool running = true;
  int frameCount = 0;
  int appliedMaxFps = -1;
  while (running && !WindowShouldClose()) {
    luaHost.setApplicationFocused(IsWindowFocused());
    luaHost.setApplicationMinimized(IsWindowMinimized());
    const bool applicationSuspended = IsWindowMinimized();
    luaHost.setApplicationSuspended(applicationSuspended);
    audioSystem.setSuspended(applicationSuspended);
    const int displayWidth = GetRenderWidth();
    const int displayHeight = GetRenderHeight();
    const Vector2 displayScale = GetWindowScaleDPI();
    luaHost.applicationServices().updateDisplay(
        displayWidth, displayHeight,
        96.0F * std::max(displayScale.x, displayScale.y));
    luaHost.setViewport(displayWidth, displayHeight);
    RuntimeProfiler::beginFrame();
    const auto frameStart = std::chrono::steady_clock::now();
    if (inputReplay) {
      if (!inputReplay->apply(static_cast<std::size_t>(frameCount), input))
        break;
      // Deterministic RNG: re-seed per frame from initial seed + frame index
      // so replays produce identical random sequences across runs.
      const std::uint64_t frameSeed =
          loaded.project.simulation.randomSeed ^
          (static_cast<std::uint64_t>(frameCount) * 0x9E3779B97F4A7C15ULL);
      luaHost.seedRandom(frameSeed);
    } else {
      pollKeys(input);
      pollMouse(input);
      pollGamepads(input);
      pollTouches(input);
    }

    const float dt = std::min(GetFrameTime(), 0.1F);

    double updateMs = 0.0;
    const auto updateStart = std::chrono::steady_clock::now();
    stepSimulation(loaded, luaHost, input, audioSystem, mediaSystem,
                   networkSystem, assetRegistry, dt,
                   static_cast<float>(fixedStep), fixedAccumulator, running);
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
    {
      ProfileScope renderScope("Render.frame");
      if (use3D) {
        const auto cameras = renderCameras3D(loaded.world);
        const Camera3DComponent *active = activeCamera3D(loaded.world);
        renderer3D.beginFrame(width, height,
                              active != nullptr ? active->clearColor
                                                : fallbackCamera3D.clearColor);
        cameraRenderScheduler3D.beginFrame();
        bool renderHud = cameras.empty();
        if (cameras.empty()) {
          renderer3D.beginCamera("fallback", fallbackCamera3D, {},
                                 {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F});
          renderer3D.drawWorld(loaded.world, dt);
          renderer3D.endCamera();
        } else {
          for (const Entity *cameraEntity : cameras) {
            const auto &camera = *cameraEntity->component<Camera3DComponent>();
            const auto *postProcess =
                cameraEntity->component<PostProcessStackComponent>();
            if (!cameraRenderScheduler3D.shouldRender(
                    cameraEntity->id, camera.updateInterval, dt) &&
                renderer3D.canPresentCamera(cameraEntity->id, camera)) {
              renderer3D.presentCamera(cameraEntity->id, camera, postProcess);
              renderHud |= camera.renderHud;
              continue;
            }
            const auto transform =
                resolveWorldTransform3D(loaded.world, *cameraEntity);
            const Vec3 position = transform ? transform->position : Vec3{};
            const Vec3 forward =
                transform
                    ? transformDirection3D(*transform, camera.targetOffset)
                    : camera.targetOffset;
            const Vec3 localUp{0.0F, camera.upAxis, 0.0F};
            const Vec3 up =
                transform ? transformDirection3D(*transform, localUp) : localUp;
            renderer3D.beginCamera(cameraEntity->id, camera, position, forward,
                                   up);
            renderer3D.drawWorld(loaded.world, dt);
            renderer3D.endCamera(postProcess, camera.renderHudToTarget
                                                  ? &loaded.world
                                                  : nullptr);
            renderHud |= camera.renderHud;
          }
        }
        cameraRenderScheduler3D.endFrame();
        if (renderHud)
          renderer3D.drawHud(loaded.world);
        renderer3D.endFrame();
      } else {
        const Camera2DComponent *camera = activeCamera(loaded.world);
        renderer2D.beginFrame(camera != nullptr ? *camera : fallbackCamera2D,
                              activeCameraPosition(loaded.world), width,
                              height);
        renderer2D.drawWorld(loaded.world);
        if (loaded.world.debug.grid)
          renderer2D.drawNavigation(luaHost.navigationGrid2D());
        renderer2D.drawHud(loaded.world);
        renderer2D.endFrame();
      }
    }
    double renderMs = 0.0;
    if (profileRun) {
      renderMs = millisecondsSince(renderStart);
      const double frameMs = millisecondsSince(frameStart);
      RuntimeProfiler::record("Frame.update", updateMs);
      RuntimeProfiler::record("Render.total", renderMs);
      RuntimeProfiler::record("Frame.total", frameMs);
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
  if (options.profiler)
    std::cout << RuntimeProfiler::sessionReport();
  writeProfileReport(options.profileReportPath);
  return 0;
#endif
}

} // namespace demi::runtime
