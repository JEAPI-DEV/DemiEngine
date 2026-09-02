#include "demi/runtime/app/RuntimeApp.h"

#include "demi/runtime/app/FramePacing.h"

#include "demi/assets/AssetCooker.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/core/Version.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/animation/AnimationCollision2DSystem.h"
#include "demi/runtime/animation/AnimationStateMachineSystem.h"
#include "demi/runtime/animation/SpriteAnimationSystem.h"
#include "demi/runtime/app/Bgfx2DAppHost.h"
#include "demi/runtime/app/Bgfx3DAppHost.h"
#include "demi/runtime/app/ReloadCoordinator.h"
#include "demi/runtime/assets/RuntimeAssetBootstrap.h"
#include "demi/runtime/assets/RuntimeAssetReload.h"
#include "demi/runtime/assets/RuntimeAssetService.h"
#include "demi/runtime/audio/AudioSceneSystem.h"
#include "demi/runtime/audio/AudioSystem.h"
#include "demi/runtime/camera/Camera2DSystem.h"
#include "demi/runtime/diagnostics/DeviceLog.h"
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
#include "demi/runtime/ui/UiAccessibilityBridge.h"
#include "demi/runtime/ui/UiLayoutEngine.h"

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

float fixedStepInterpolationAlpha(const double accumulator,
                                  const float fixedStep) {
  if (fixedStep <= 0.0F)
    return 1.0F;
  return std::clamp(static_cast<float>(accumulator / fixedStep), 0.0F, 1.0F);
}

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
                    bool &running,
                    ui::UiAccessibilityBridgeController &accessibility) {
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
    if (loaded.world.uiTweens.activeCount() > 0) {
      loaded.world.uiTweens.update(loaded.world.ui, scaledDt);
      ui::UiLayoutEngine{}.layout(loaded.world.ui, loaded.world.ui.canvasSize);
    }
  }
  {
    ProfileScope scope("Camera2D.update");
    Camera2DSystem{}.update(
        loaded.world, scaledDt,
        fixedStepInterpolationAlpha(fixedAccumulator, fixedStep));
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
      deviceLogError(
          deviceLogMessage("runtime", "Scene switch failed: " + sceneError));
    } else {
      generateTilemapColliders(loaded.world, assetRegistry);
      std::cout << "Switched scene to " << loaded.world.id << " ("
                << loaded.world.name << ").\n";
      deviceLog(deviceLogMessage(
          "runtime", "Switched scene to " + loaded.world.activeSceneId + "."));
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
  accessibility.update(loaded.world.ui);
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
    const Diagnostics diagnostics =
        assets::cookProject({.projectFile = options.projectPath,
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
  bool audioInitialized = false;
  if (!isHeadless() && !options.serve && options.maxFrames == 0 &&
      audioSystem.initialize())
    audioInitialized = true;
  MediaSystem mediaSystem;
  {
    ProfileScope scope("Asset.media_load");
    mediaSystem.loadVideoAssets(assetRegistry);
  }
  NetworkSystem networkSystem;
  (void)networkSystem.initialize();
  InputState input;
  auto &platformAccessibility = ui::platformUiAccessibilityBridge();
  platformAccessibility.clear();
  struct AccessibilityLifetime {
    ui::PlatformUiAccessibilityBridge &bridge;
    ~AccessibilityLifetime() { bridge.clear(); }
  } accessibilityLifetime{platformAccessibility};
  ui::UiAccessibilityBridgeController accessibility(platformAccessibility);

  RuntimeAssetService runtimeAssets;
  Diagnostics runtimeAssetDiagnostics;
  if (!runtimeAssets.configure(loaded.project, assetRegistry,
                               &runtimeAssetDiagnostics)) {
    std::cerr << "Runtime asset-group configuration failed.\n";
    printDiagnosticsText(std::cerr, runtimeAssetDiagnostics);
    return RuntimeFailure;
  }
  if (audioInitialized)
    runtimeAssets.registerLoader(audioSystem.createAssetLoader(assetRegistry));

  LuaScriptHost luaHost;
  luaHost.setMediaSystem(&mediaSystem);
  luaHost.setNetworkSystem(&networkSystem);
  luaHost.setRuntimeAssetService(&runtimeAssets);
  std::string luaError;
  bool luaScriptsLoaded = false;
  if (luaHost.initialize(loaded.world, input, &audioSystem, luaError)) {
    luaHost.setAssetRegistry(&assetRegistry);
    luaHost.seedRandom(loaded.project.simulation.randomSeed);
    if (!luaHost.loadWorldScripts(loaded.project, loaded.world, luaError)) {
      std::cerr << "Lua scripts skipped: " << luaError << '\n';
    } else
      luaScriptsLoaded = true;
  } else {
    std::cerr << "Lua unavailable: " << luaError << '\n';
  }
  luaHost.setHotReloadEnabled(options.watch || luaHost.hotReloadEnabled());

  const auto prepareStartupAssets = [&] {
    Diagnostics diagnostics;
    const SceneAssetBootstrapResult result =
        prepareInitialSceneAssets(runtimeAssets, loaded.world.activeSceneId,
                                  loaded.project.preloadedAssets, &diagnostics);
    if (!result.success) {
      std::cerr << "Initial scene asset preparation failed.\n";
      printDiagnosticsText(std::cerr, diagnostics);
      return false;
    }
    if (result.hasResidentGroup)
      luaHost.adoptActiveSceneAssetGroup(loaded.world.activeSceneId);
    if (luaScriptsLoaded)
      luaHost.start();
    return true;
  };

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
    if (!prepareStartupAssets()) {
      luaHost.destroy();
      runtimeAssets.shutdown();
      networkSystem.shutdown();
      mediaSystem.shutdown();
      audioSystem.shutdown();
      return RuntimeFailure;
    }
    platform::ProjectFileWatcher watcher;
    watcher.reset(loaded.project.projectDirectory);
    ReloadCoordinator reloadCoordinator(
        options.projectPath,
        {.reloadScene =
             [&](std::string &reloadError) {
               if (luaHost.requestSceneReload())
                 return true;
               reloadError = "The active scene could not begin reloading.";
               return false;
             },
         .cancelSceneReload = [&] { (void)luaHost.cancelScenePreparation(); },
         .reloadAssets =
             [&](std::string &reloadError) {
               AssetRegistry candidate =
                   loadAssetRegistry(loaded.project.projectDirectory);
               if (hasErrors(candidate.diagnostics)) {
                 reloadError = "The changed asset registry is invalid.";
                 return false;
               }
               Diagnostics diagnostics;
               if (applyWatchedAssetRegistry(runtimeAssets, assetRegistry,
                                             std::move(candidate),
                                             &diagnostics))
                 return true;
               reloadError = diagnostics.empty()
                                 ? "A resident asset could not be reloaded."
                                 : diagnostics.front().message;
               return false;
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
    const bool unboundedServer = options.serve && options.maxFrames == 0;
    while (running && (unboundedServer || frameCount < targetFrames)) {
      if (options.watch)
        reportReload(reloadCoordinator.process(watcher.poll()));
      if (inputReplay) {
        // A max-frame replay run commonly needs extra neutral frames for a
        // simulation to settle after its final recorded input.
        if (options.maxFrames <= 0 &&
            static_cast<std::size_t>(frameCount) >= inputReplay->frames.size())
          break;
        inputReplay->applyOrNeutral(static_cast<std::size_t>(frameCount),
                                    input);
      }
      const auto nextFrame = std::chrono::steady_clock::now() +
                             std::chrono::duration<double>(fixedStep);
      RuntimeProfiler::beginFrame();
      const auto frameStart = std::chrono::steady_clock::now();
      const auto updateStart = std::chrono::steady_clock::now();
      stepSimulation(loaded, luaHost, input, audioSystem, mediaSystem,
                     networkSystem, assetRegistry,
                     static_cast<float>(fixedStep),
                     static_cast<float>(fixedStep), fixedAccumulator, running,
                     accessibility);
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
    runtimeAssets.shutdown();
    networkSystem.shutdown();
    mediaSystem.shutdown();
    audioSystem.shutdown();
    return 0;
  }

#if !DEMI_ENABLE_GRAPHICS_RUNTIME
  std::cerr << "Runtime windowing is unavailable in the server-only build.\n";
  luaHost.destroy();
  runtimeAssets.shutdown();
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
                .vsync = loaded.project.display.vsync,
                .debugGraphics = false,
            },
            AssetRegistry{.projectDirectory = assetRegistry.projectDirectory,
                          .assets = {},
                          .diagnostics = {}},
            renderDiagnostics, error)) {
      std::cerr << "2D renderer initialization failed: " << error << '\n';
      deviceLogError(deviceLogMessage(
          "runtime", "2D renderer initialization failed: " + error));
      luaHost.destroy();
      networkSystem.shutdown();
      mediaSystem.shutdown();
      audioSystem.shutdown();
      return RuntimeFailure;
    }
    for (const std::string &diagnostic : renderDiagnostics)
      std::cerr << "2D asset load failed: " << diagnostic << '\n';
    runtimeAssets.registerLoader(appHost.createAssetLoader(assetRegistry));
    if (!prepareStartupAssets()) {
      luaHost.destroy();
      runtimeAssets.shutdown();
      networkSystem.shutdown();
      mediaSystem.shutdown();
      audioSystem.shutdown();
      appHost.shutdown();
      return RuntimeFailure;
    }

    platform::ProjectFileWatcher watcher;
    watcher.reset(loaded.project.projectDirectory);
    ReloadCoordinator reloadCoordinator(
        options.projectPath,
        {.reloadScene =
             [&](std::string &reloadError) {
               if (luaHost.requestSceneReload())
                 return true;
               reloadError = "The active scene could not begin reloading.";
               return false;
             },
         .cancelSceneReload = [&] { (void)luaHost.cancelScenePreparation(); },
         .reloadAssets =
             [&](std::string &reloadError) {
               AssetRegistry candidate =
                   loadAssetRegistry(loaded.project.projectDirectory);
               if (hasErrors(candidate.diagnostics)) {
                 reloadError = "The changed asset registry is invalid.";
                 return false;
               }
               Diagnostics diagnostics;
               if (applyWatchedAssetRegistry(runtimeAssets, assetRegistry,
                                             std::move(candidate),
                                             &diagnostics))
                 return true;
               reloadError = diagnostics.empty()
                                 ? "A resident 2D asset could not be reloaded."
                                 : diagnostics.front().message;
               return false;
             }});

    luaHost.applicationServices().setClipboardHandlers(
        [&appHost] { return appHost.clipboard(); },
        [&appHost](const std::string &text) {
          std::string clipboardError;
          if (!appHost.setClipboard(text, clipboardError))
            std::cerr << "Clipboard update failed: " << clipboardError << '\n';
        });
    luaHost.applicationServices().setPermissionRequester(
        [&appHost](const std::string &permission,
                   platform::PermissionResult result, std::string &error) {
          return appHost.requestPermission(permission, std::move(result),
                                           error);
        });

    std::cout << "Using bgfx " << appHost.rendererName()
              << " through the SDL3 platform host.\n";
    deviceLog(
        deviceLogMessage("render", std::string("Using bgfx ") +
                                       std::string(appHost.rendererName()) +
                                       " through the SDL3 platform host."));
    bool running = true;
    bool renderFailed = false;
    bool pausedState = false;
    const bool compositorPaced = loaded.project.display.vsync && !isHeadless();
    std::chrono::steady_clock::time_point nextFrameDeadline =
        std::chrono::steady_clock::now();
    int requestedFrameRate = -1;
    bool platformPacesRequestedRate = false;
    int frameCount = 0;
    while (running) {
      appHost.poll(input);
      const platform::PlatformFrameState &frameState = appHost.frameState();
      if (frameState.quitRequested)
        break;
      if (inputReplay) {
        if (options.maxFrames <= 0 &&
            static_cast<std::size_t>(frameCount) >= inputReplay->frames.size())
          break;
        inputReplay->applyOrNeutral(static_cast<std::size_t>(frameCount),
                                    input);
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
      for (unsigned signal = 0; signal < frameState.lowMemorySignals;
           ++signal) {
        runtimeAssets.handleLowMemory();
        luaHost.notifyApplicationLowMemory();
      }
      for (unsigned request = 0; request < frameState.backRequests; ++request)
        luaHost.applicationServices().notifyBackRequested();
      luaHost.applicationServices().updateDisplay(
          frameState.width, frameState.height, frameState.logicalDpi);
      luaHost.setViewport(frameState.width, frameState.height);
      const bool paused = frameState.suspended ||
                          !frameState.drawableAvailable ||
                          !frameState.surfaceSettled;
      if (paused != pausedState) {
        pausedState = paused;
        deviceLog(deviceLogMessage(
            "runtime", paused ? std::string("Frame loop paused (") +
                                    (frameState.suspended ? "suspended"
                                     : !frameState.drawableAvailable
                                         ? "drawable unavailable"
                                         : "surface settling") +
                                    ")."
                              : "Frame loop resumed."));
      }
      if (paused) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        continue;
      }

      RuntimeProfiler::beginFrame();
      const auto frameStart = std::chrono::steady_clock::now();
      const float dt = frameState.deltaSeconds;
      double updateMs = 0.0;
      const auto updateStart = std::chrono::steady_clock::now();
      stepSimulation(loaded, luaHost, input, audioSystem, mediaSystem,
                     networkSystem, assetRegistry, dt,
                     static_cast<float>(fixedStep), fixedAccumulator, running,
                     accessibility);
      if (frameCount == 0 || frameCount == 60)
        deviceLog(deviceLogMessage("runtime",
                                   "Frame " + std::to_string(frameCount + 1) +
                                       ", Lua frame " +
                                       std::to_string(luaHost.frameCount()) +
                                       ", scene " + loaded.world.activeSceneId +
                                       ", dt " + std::to_string(dt) + "."));
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
        if (!appHost.renderFrame(
                loaded.world, camera != nullptr ? *camera : fallbackCamera2D,
                activeCameraPosition(loaded.world), dt, navigation, renderError,
                fixedStepInterpolationAlpha(fixedAccumulator, fixedStep))) {
          std::cerr << "2D rendering failed: " << renderError << '\n';
          deviceLogError(deviceLogMessage("runtime", "2D rendering failed: " +
                                                         renderError));
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
      if (maxFps != requestedFrameRate) {
        platformPacesRequestedRate =
            appHost.requestFrameRate(static_cast<float>(maxFps));
        requestedFrameRate = maxFps;
      }
      const bool compositorPacesCap = compositorSatisfiesFrameCap(
          compositorPaced, isHeadless(), maxFps, frameState.displayRefreshHz,
          platformPacesRequestedRate);
      if (running && maxFps > 0 && !compositorPacesCap) {
        nextFrameDeadline +=
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0 / maxFps));
        if (nextFrameDeadline < std::chrono::steady_clock::now())
          nextFrameDeadline = std::chrono::steady_clock::now();
        std::this_thread::sleep_until(nextFrameDeadline);
      }
    }

    luaHost.destroy();
    runtimeAssets.shutdown();
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
                                .vsync = loaded.project.display.vsync,
                                .debugGraphics = false},
            AssetRegistry{.projectDirectory = assetRegistry.projectDirectory,
                          .assets = {},
                          .diagnostics = {}},
            renderDiagnostics, error)) {
      std::cerr << "3D renderer initialization failed: " << error << '\n';
      deviceLogError(deviceLogMessage(
          "runtime", "3D renderer initialization failed: " + error));
      luaHost.destroy();
      networkSystem.shutdown();
      mediaSystem.shutdown();
      audioSystem.shutdown();
      return RuntimeFailure;
    }
    for (const std::string &diagnostic : renderDiagnostics)
      std::cerr << "3D asset load failed: " << diagnostic << '\n';
    runtimeAssets.registerLoader(appHost.createAssetLoader(assetRegistry));
    if (!prepareStartupAssets()) {
      luaHost.destroy();
      runtimeAssets.shutdown();
      networkSystem.shutdown();
      mediaSystem.shutdown();
      audioSystem.shutdown();
      appHost.shutdown();
      return RuntimeFailure;
    }

    platform::ProjectFileWatcher watcher;
    watcher.reset(loaded.project.projectDirectory);
    ReloadCoordinator reloadCoordinator(
        options.projectPath,
        {.reloadScene =
             [&](std::string &reloadError) {
               if (luaHost.requestSceneReload())
                 return true;
               reloadError = "The active scene could not begin reloading.";
               return false;
             },
         .cancelSceneReload = [&] { (void)luaHost.cancelScenePreparation(); },
         .reloadAssets =
             [&](std::string &reloadError) {
               AssetRegistry candidate =
                   loadAssetRegistry(loaded.project.projectDirectory);
               if (hasErrors(candidate.diagnostics)) {
                 reloadError = "The changed asset registry is invalid.";
                 return false;
               }
               Diagnostics diagnostics;
               if (applyWatchedAssetRegistry(runtimeAssets, assetRegistry,
                                             std::move(candidate),
                                             &diagnostics))
                 return true;
               reloadError = diagnostics.empty()
                                 ? "A resident 3D asset could not be reloaded."
                                 : diagnostics.front().message;
               return false;
             }});

    luaHost.applicationServices().setClipboardHandlers(
        [&appHost] { return appHost.clipboard(); },
        [&appHost](const std::string &text) {
          std::string clipboardError;
          if (!appHost.setClipboard(text, clipboardError))
            std::cerr << "Clipboard update failed: " << clipboardError << '\n';
        });
    luaHost.applicationServices().setPermissionRequester(
        [&appHost](const std::string &permission,
                   platform::PermissionResult result, std::string &error) {
          return appHost.requestPermission(permission, std::move(result),
                                           error);
        });
    std::cout << "Using bgfx " << appHost.rendererName()
              << " through the SDL3 3D platform host.\n";

    bool running = true;
    bool renderFailed = false;
    bool pausedState = false;
    const bool compositorPaced = loaded.project.display.vsync && !isHeadless();
    std::chrono::steady_clock::time_point nextFrameDeadline =
        std::chrono::steady_clock::now();
    int requestedFrameRate = -1;
    bool platformPacesRequestedRate = false;
    int frameCount = 0;
    while (running) {
      appHost.poll(input);
      const platform::PlatformFrameState &frameState = appHost.frameState();
      if (frameState.quitRequested)
        break;
      if (inputReplay) {
        if (options.maxFrames <= 0 &&
            static_cast<std::size_t>(frameCount) >= inputReplay->frames.size())
          break;
        inputReplay->applyOrNeutral(static_cast<std::size_t>(frameCount),
                                    input);
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
      for (unsigned signal = 0; signal < frameState.lowMemorySignals;
           ++signal) {
        runtimeAssets.handleLowMemory();
        luaHost.notifyApplicationLowMemory();
      }
      for (unsigned request = 0; request < frameState.backRequests; ++request)
        luaHost.applicationServices().notifyBackRequested();
      luaHost.applicationServices().updateDisplay(
          frameState.width, frameState.height, frameState.logicalDpi);
      luaHost.setViewport(frameState.width, frameState.height);
      const bool paused = frameState.suspended ||
                          !frameState.drawableAvailable ||
                          !frameState.surfaceSettled;
      if (paused != pausedState) {
        pausedState = paused;
        deviceLog(deviceLogMessage(
            "runtime", paused ? std::string("Frame loop paused (") +
                                    (frameState.suspended ? "suspended"
                                     : !frameState.drawableAvailable
                                         ? "drawable unavailable"
                                         : "surface settling") +
                                    ")."
                              : "Frame loop resumed."));
      }
      if (paused) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        continue;
      }

      RuntimeProfiler::beginFrame();
      const auto frameStart = std::chrono::steady_clock::now();
      const float dt = frameState.deltaSeconds;
      double updateMs = 0.0;
      const auto updateStart = std::chrono::steady_clock::now();
      stepSimulation(loaded, luaHost, input, audioSystem, mediaSystem,
                     networkSystem, assetRegistry, dt,
                     static_cast<float>(fixedStep), fixedAccumulator, running,
                     accessibility);
      if (frameCount == 0 || frameCount == 60)
        deviceLog(deviceLogMessage("runtime",
                                   "Frame " + std::to_string(frameCount + 1) +
                                       ", Lua frame " +
                                       std::to_string(luaHost.frameCount()) +
                                       ", scene " + loaded.world.activeSceneId +
                                       ", dt " + std::to_string(dt) + "."));
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
                                .debugGeometry = {},
                                .position = {},
                                .forward = {0.0F, 0.0F, 1.0F},
                                .up = {0.0F, 1.0F, 0.0F},
                                .viewportX = 0,
                                .viewportY = 0,
                                .viewportWidth = 0,
                                .viewportHeight = 0,
                                .viewId = 0,
                                .frameBuffer = {}});
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
          deviceLogError(deviceLogMessage("runtime", "3D rendering failed: " +
                                                         renderError));
          renderFailed = true;
          running = false;
        }
      }
      if (profileRun) {
        RuntimeProfiler::record("Renderer3D.visibility_extract",
                                appHost.lastExtractionMilliseconds());
        const auto &renderStats = appHost.statistics();
        RuntimeProfiler::setGauge(
            "Renderer3D.meshes_considered",
            static_cast<double>(renderStats.consideredMeshes));
        RuntimeProfiler::setGauge(
            "Renderer3D.meshes_visible",
            static_cast<double>(renderStats.visibleMeshes));
        RuntimeProfiler::setGauge(
            "Renderer3D.meshes_culled",
            static_cast<double>(renderStats.culledMeshes));
        RuntimeProfiler::setGauge("Renderer3D.batches",
                                  static_cast<double>(renderStats.batches));
        RuntimeProfiler::setGauge("Renderer3D.triangles",
                                  static_cast<double>(renderStats.triangles));
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
      if (maxFps != requestedFrameRate) {
        platformPacesRequestedRate =
            appHost.requestFrameRate(static_cast<float>(maxFps));
        requestedFrameRate = maxFps;
      }
      const bool compositorPacesCap = compositorSatisfiesFrameCap(
          compositorPaced, isHeadless(), maxFps, frameState.displayRefreshHz,
          platformPacesRequestedRate);
      if (running && maxFps > 0 && !compositorPacesCap) {
        nextFrameDeadline +=
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0 / maxFps));
        if (nextFrameDeadline < std::chrono::steady_clock::now())
          nextFrameDeadline = std::chrono::steady_clock::now();
        std::this_thread::sleep_until(nextFrameDeadline);
      }
    }

    luaHost.destroy();
    runtimeAssets.shutdown();
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
