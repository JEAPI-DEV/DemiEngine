#include "demi/runtime/app/RuntimeApp.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetCooker.h"
#include "demi/core/Version.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/animation/AnimationCollision2DSystem.h"
#include "demi/runtime/animation/AnimationStateMachineSystem.h"
#include "demi/runtime/animation/SpriteAnimationSystem.h"
#include "demi/runtime/app/Bgfx2DAppHost.h"
#include "demi/runtime/app/Bgfx3DAppHost.h"
#include "demi/runtime/app/ReloadCoordinator.h"
#include "demi/runtime/audio/AudioSceneSystem.h"
#include "demi/runtime/audio/AudioSystem.h"
#include "demi/runtime/camera/Camera2DSystem.h"
#include "demi/runtime/input/replay/InputReplay.h"
#include "demi/runtime/media/MediaSystem.h"
#include "demi/runtime/network/NetworkSystem.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/platform/ProjectFileWatcher.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/tilemap/TilemapCollisionGenerator.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

namespace demi::runtime {

namespace {

constexpr int RuntimeFailure = 3;

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

void reportReload(const ReloadResult &result) {
  if (result.generation == 0)
    return;
  if (!result.diagnostics.empty())
    printDiagnosticsText(std::cerr, result.diagnostics);
  if (result.applied)
    std::cout << "Watch reload applied generation " << result.generation
              << ".\n";
  else if (hasErrors(result.diagnostics))
    std::cerr << "Watch reload rejected generation " << result.generation
              << "; retaining the last good state.\n";
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
#if DEMI_ENABLE_GRAPHICS_RUNTIME
  const bool hasAuthoredShaders =
      std::ranges::any_of(assetRegistry.assets, [](const AssetManifest &asset) {
        return asset.type == "Shader";
      });
  if (!isHeadless() && !options.serve && hasAuthoredShaders &&
      !std::filesystem::is_regular_file(assetRegistry.projectDirectory /
                                        "cook.manifest.json")) {
    const std::filesystem::path cookedDirectory =
        loaded.project.projectDirectory / "generated/runtime-cook/linux";
    const Diagnostics diagnostics = assets::cookProject(
        {.projectFile = options.projectPath,
         .outputDirectory = cookedDirectory,
         .platform = "linux",
         .shaderCompiler = {},
         .shaderIncludeDirectory = {}});
    if (hasErrors(diagnostics)) {
      std::cerr << "Runtime shader cooking failed.\n";
      printDiagnosticsText(std::cerr, diagnostics);
      return RuntimeFailure;
    }
    assetRegistry = loadAssetRegistry(cookedDirectory);
  }
#endif
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
  luaHost.setHotReloadEnabled(options.watch || luaHost.hotReloadEnabled());

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
    platform::ProjectFileWatcher watcher;
    watcher.reset(loaded.project.projectDirectory);
    ReloadCoordinator reloadCoordinator(
        options.projectPath,
        {.reloadScene = [&](std::string &reloadError) {
           if (luaHost.requestSceneReload())
             return true;
           reloadError = "The active scene could not begin reloading.";
           return false;
         },
         .cancelSceneReload = [&] { (void)luaHost.cancelScenePreparation(); },
         .reloadAssets = [&](std::string &reloadError) {
           AssetRegistry candidate =
               loadAssetRegistry(loaded.project.projectDirectory);
           if (hasErrors(candidate.diagnostics)) {
             reloadError = "The changed asset registry is invalid.";
             return false;
           }
           assetRegistry = std::move(candidate);
           luaHost.setAssetRegistry(&assetRegistry);
           return true;
         }});
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
      if (options.watch)
        reportReload(reloadCoordinator.process(watcher.poll()));
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

#if !DEMI_ENABLE_GRAPHICS_RUNTIME
  std::cerr << "Runtime windowing is unavailable in the server-only build.\n";
  luaHost.destroy();
  networkSystem.shutdown();
  mediaSystem.shutdown();
  audioSystem.shutdown();
  return RuntimeFailure;
#else
  const bool use3D = sceneIs3D(loaded.world);
  const Camera2DComponent fallbackCamera2D;
  const Camera3DComponent fallbackCamera3D;

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

    platform::ProjectFileWatcher watcher;
    watcher.reset(loaded.project.projectDirectory);
    ReloadCoordinator reloadCoordinator(
        options.projectPath,
        {.reloadScene = [&](std::string &reloadError) {
           if (luaHost.requestSceneReload())
             return true;
           reloadError = "The active scene could not begin reloading.";
           return false;
         },
         .cancelSceneReload = [&] { (void)luaHost.cancelScenePreparation(); },
         .reloadAssets = [&](std::string &reloadError) {
           AssetRegistry candidate =
               loadAssetRegistry(loaded.project.projectDirectory);
           if (hasErrors(candidate.diagnostics)) {
             reloadError = "The changed asset registry is invalid.";
             return false;
           }
           std::vector<std::string> diagnostics;
           if (!appHost.reloadAssets(candidate, diagnostics, reloadError)) {
             for (const auto &diagnostic : diagnostics)
               std::cerr << "2D watch asset load failed: " << diagnostic << '\n';
             return false;
           }
           assetRegistry = std::move(candidate);
           luaHost.setAssetRegistry(&assetRegistry);
           return true;
         }});

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
      if (options.watch)
        reportReload(reloadCoordinator.process(watcher.poll()));

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

  {
    Bgfx3DAppHost appHost;
    std::vector<std::string> renderDiagnostics;
    const std::string title = std::string(EngineName) + " - " +
                              loaded.project.name + " - " + loaded.world.name;
    if (!appHost.initialize(
            Bgfx3DAppHostConfig{.title = title,
                                .width = 960,
                                .height = 540,
                                .graphicsApi = configuredGraphicsApi(),
                                .vsync = true,
                                .debugGraphics = false},
            assetRegistry, renderDiagnostics, error)) {
      std::cerr << "3D renderer initialization failed: " << error << '\n';
      luaHost.destroy();
      networkSystem.shutdown();
      mediaSystem.shutdown();
      audioSystem.shutdown();
      return RuntimeFailure;
    }
    for (const std::string &diagnostic : renderDiagnostics)
      std::cerr << "3D asset load failed: " << diagnostic << '\n';

    platform::ProjectFileWatcher watcher;
    watcher.reset(loaded.project.projectDirectory);
    ReloadCoordinator reloadCoordinator(
        options.projectPath,
        {.reloadScene = [&](std::string &reloadError) {
           if (luaHost.requestSceneReload())
             return true;
           reloadError = "The active scene could not begin reloading.";
           return false;
         },
         .cancelSceneReload = [&] { (void)luaHost.cancelScenePreparation(); },
         .reloadAssets = [&](std::string &reloadError) {
           AssetRegistry candidate =
               loadAssetRegistry(loaded.project.projectDirectory);
           if (hasErrors(candidate.diagnostics)) {
             reloadError = "The changed asset registry is invalid.";
             return false;
           }
           std::vector<std::string> diagnostics;
           if (!appHost.reloadAssets(candidate, diagnostics, reloadError)) {
             for (const auto &diagnostic : diagnostics)
               std::cerr << "3D watch asset load failed: " << diagnostic << '\n';
             return false;
           }
           assetRegistry = std::move(candidate);
           luaHost.setAssetRegistry(&assetRegistry);
           return true;
         }});

    luaHost.applicationServices().setClipboardHandlers(
        [&appHost] { return appHost.clipboard(); },
        [&appHost](const std::string &text) {
          std::string clipboardError;
          if (!appHost.setClipboard(text, clipboardError))
            std::cerr << "Clipboard update failed: " << clipboardError << '\n';
        });
    std::cout << "Using bgfx " << appHost.rendererName()
              << " through the SDL3 3D platform host.\n";

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
      if (options.watch)
        reportReload(reloadCoordinator.process(watcher.poll()));

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

      std::vector<render::BgfxCameraFrame3D> cameraFrames;
      const auto cameraEntities = renderCameras3D(loaded.world);
      if (cameraEntities.empty()) {
        cameraFrames.push_back({.cameraId = "fallback",
                                .camera = fallbackCamera3D,
                                .postProcess = std::nullopt,
                                .position = {},
                                .forward = {0.0F, 0.0F, 1.0F},
                                .up = {0.0F, 1.0F, 0.0F},
                                .viewportX = 0,
                                .viewportY = 0,
                                .viewportWidth = 0,
                                .viewportHeight = 0,
                                .viewId = 0});
      } else {
        cameraFrames.reserve(cameraEntities.size());
        for (std::size_t index = 0; index < cameraEntities.size(); ++index) {
          const Entity &cameraEntity = *cameraEntities[index];
          const Camera3DComponent camera =
              *cameraEntity.component<Camera3DComponent>();
          render::BgfxCameraFrame3D cameraFrame;
          cameraFrame.cameraId = cameraEntity.id;
          cameraFrame.camera = camera;
          if (const auto *postProcess =
                  cameraEntity.component<PostProcessStackComponent>())
            cameraFrame.postProcess = *postProcess;
          cameraFrame.viewportX = static_cast<std::uint16_t>(
              std::clamp(camera.viewportX * frameState.width, 0.0F,
                         static_cast<float>(UINT16_MAX)));
          cameraFrame.viewportY = static_cast<std::uint16_t>(
              std::clamp(camera.viewportY * frameState.height, 0.0F,
                         static_cast<float>(UINT16_MAX)));
          cameraFrame.viewportWidth = static_cast<std::uint16_t>(
              std::clamp(camera.viewportWidth * frameState.width, 1.0F,
                         static_cast<float>(UINT16_MAX)));
          cameraFrame.viewportHeight = static_cast<std::uint16_t>(
              std::clamp(camera.viewportHeight * frameState.height, 1.0F,
                         static_cast<float>(UINT16_MAX)));
          cameraFrame.viewId = static_cast<std::uint16_t>(index * 4U);
          if (const auto transform =
                  resolveWorldTransform3D(loaded.world, cameraEntity)) {
            cameraFrame.position = transform->position;
            cameraFrame.forward =
                transformDirection3D(*transform, camera.targetOffset);
            cameraFrame.up =
                transformDirection3D(*transform, {0.0F, camera.upAxis, 0.0F});
          }
          cameraFrames.push_back(cameraFrame);
        }
      }
      const auto renderStart = std::chrono::steady_clock::now();
      std::string renderError;
      {
        ProfileScope renderScope("Render.frame");
        if (!appHost.renderFrames(loaded.world, cameraFrames, dt,
                                  renderError)) {
          std::cerr << "3D rendering failed: " << renderError << '\n';
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
      if (running && maxFps > 0)
        std::this_thread::sleep_until(
            frameStart + std::chrono::duration<double>(1.0 / maxFps));
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

#endif
}

} // namespace demi::runtime
