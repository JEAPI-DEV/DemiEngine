#include "demi/runtime/app/EmbeddedRuntimeSession.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/animation/AnimationCollision2DSystem.h"
#include "demi/runtime/animation/AnimationStateMachineSystem.h"
#include "demi/runtime/animation/SpriteAnimationSystem.h"
#include "demi/runtime/assets/RuntimeAssetBootstrap.h"
#include "demi/runtime/assets/RuntimeAssetService.h"
#include "demi/runtime/audio/AudioSceneSystem.h"
#include "demi/runtime/audio/AudioSystem.h"
#include "demi/runtime/camera/Camera2DSystem.h"
#include "demi/runtime/media/MediaSystem.h"
#include "demi/runtime/network/NetworkSystem.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/Physics3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/tilemap/TilemapCollisionGenerator.h"
#include "demi/runtime/ui/UiAccessibilityBridge.h"
#include "demi/runtime/ui/UiLayoutEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace demi::runtime {
namespace {

std::atomic_size_t LiveSessions = 0;

std::string firstError(const Diagnostics &diagnostics,
                       const std::string_view fallback) {
  const auto found = std::ranges::find_if(diagnostics, [](const auto &entry) {
    return entry.severity == Severity::Error;
  });
  return found == diagnostics.end() ? std::string(fallback) : found->message;
}

} // namespace

class EmbeddedRuntimeSession::State {
public:
  LoadedProject loaded;
  AssetRegistry assetRegistry;
  RuntimeAssetService assets;
  AudioSystem audio;
  MediaSystem media;
  NetworkSystem network;
  InputState input;
  ui::BufferedUiAccessibilityBridge accessibilityBridge;
  ui::UiAccessibilityBridgeController accessibility{accessibilityBridge};
  LuaScriptHost lua;
  double fixedAccumulator = 0.0;
  std::uint64_t fixedTicks = 0;
  bool running = false;
  bool paused = false;
  bool audioInitialized = false;
};

EmbeddedRuntimeSession::EmbeddedRuntimeSession() = default;

EmbeddedRuntimeSession::~EmbeddedRuntimeSession() { stop(); }

bool EmbeddedRuntimeSession::start(const std::filesystem::path &projectPath,
                                   std::string &error) {
  error.clear();
  if (state_ != nullptr) {
    error = "An embedded runtime session is already active.";
    return false;
  }
  auto state = std::make_unique<State>();
  auto loaded = loadProject(projectPath, error);
  if (!loaded)
    return false;
  state->loaded = std::move(*loaded);
  state->assetRegistry =
      loadAssetRegistry(state->loaded.project.projectDirectory);
  if (hasErrors(state->assetRegistry.diagnostics)) {
    error = firstError(state->assetRegistry.diagnostics,
                       "The runtime asset registry is invalid.");
    return false;
  }
  generateTilemapColliders(state->loaded.world, state->assetRegistry);
  state->audioInitialized = state->audio.initialize();
  state->media.loadVideoAssets(state->assetRegistry);
  if (!state->network.initialize()) {
    error = "The embedded runtime network service could not initialize.";
    return false;
  }
  Diagnostics assetDiagnostics;
  if (!state->assets.configure(state->loaded.project, state->assetRegistry,
                               &assetDiagnostics)) {
    error = firstError(assetDiagnostics,
                       "The embedded runtime assets could not initialize.");
    return false;
  }
  if (state->audioInitialized)
    state->assets.registerLoader(
        state->audio.createAssetLoader(state->assetRegistry));
  state->lua.setMediaSystem(&state->media);
  state->lua.setNetworkSystem(&state->network);
  state->lua.setRuntimeAssetService(&state->assets);
  if (!state->lua.initialize(state->loaded.world, state->input, &state->audio,
                             error))
    return false;
  state->lua.setAssetRegistry(&state->assetRegistry);
  state->lua.seedRandom(state->loaded.project.simulation.randomSeed);
  if (!state->lua.loadWorldScripts(state->loaded.project, state->loaded.world,
                                   error))
    return false;
  const SceneAssetBootstrapResult prepared = prepareInitialSceneAssets(
      state->assets, state->loaded.world.activeSceneId,
      state->loaded.project.preloadedAssets, &assetDiagnostics);
  if (!prepared.success) {
    error = firstError(assetDiagnostics,
                       "The initial runtime scene assets could not load.");
    return false;
  }
  if (prepared.hasResidentGroup)
    state->lua.adoptActiveSceneAssetGroup(state->loaded.world.activeSceneId);
  state->lua.start();
  state->lua.appendRuntimeLog({.severity = RuntimeLogSeverity::Info,
                               .channel = "runtime",
                               .message = "Embedded Play started",
                               .source = projectPath.string(),
                               .line = 0,
                               .entityId = {},
                               .component = {},
                               .field = {}});
  state->running = true;
  state_ = std::move(state);
  ++LiveSessions;
  return true;
}

void EmbeddedRuntimeSession::stop() {
  if (state_ == nullptr)
    return;
  state_->lua.destroy();
  state_->assets.shutdown();
  state_->network.shutdown();
  state_->media.shutdown();
  state_->audio.shutdown();
  state_.reset();
  --LiveSessions;
}

void EmbeddedRuntimeSession::setPaused(const bool paused) {
  if (state_ == nullptr)
    return;
  state_->paused = paused;
  state_->lua.setPaused(paused);
  state_->audio.setGamePaused(paused);
}

bool EmbeddedRuntimeSession::advance(State &state, InputState input,
                                     const float deltaSeconds,
                                     const std::uint16_t viewportWidth,
                                     const std::uint16_t viewportHeight,
                                     const bool oneFixedTick,
                                     std::string &error) {
  RuntimeProfiler::beginFrame();
  ProfileScope frameScope("Frame.update");
  error.clear();
  state.input = std::move(input);
  state.lua.setViewport(std::max<int>(viewportWidth, 1),
                        std::max<int>(viewportHeight, 1));
  const float fixedStep = state.loaded.project.simulation.fixedTimestep;
  const bool restorePause = state.paused && oneFixedTick;
  if (restorePause)
    state.lua.setPaused(false);
  const float frameDelta = oneFixedTick ? fixedStep : deltaSeconds;
  state.lua.beginFrame(frameDelta);
  const float scaledDelta = state.lua.deltaTime();
  state.fixedAccumulator += scaledDelta;
  const auto fixedUpdate = [&] {
    ProfileScope fixedScope("Frame.fixed_update");
    state.lua.fixedUpdate(fixedStep);
    if (state.lua.physicsEnabled()) {
      {
        ProfileScope physicsScope("Physics2D.step");
        stepPhysics2D(state.loaded.world, fixedStep);
      }
      {
        ProfileScope physicsScope("Physics3D.step");
        stepPhysics3D(state.loaded.world, fixedStep);
      }
    }
    state.lua.advanceFixedTime(fixedStep);
    ++state.fixedTicks;
  };
  if (oneFixedTick) {
    fixedUpdate();
    state.fixedAccumulator = 0.0;
  } else {
    while (state.fixedAccumulator >= fixedStep) {
      fixedUpdate();
      state.fixedAccumulator -= fixedStep;
    }
  }
  {
    ProfileScope networkScope("Network.update");
    state.network.update();
  }
  {
    ProfileScope animationScope("Animation.update");
    AnimationStateMachineSystem{}.update(state.loaded.world, scaledDelta,
                                         frameDelta);
    SpriteAnimationSystem{}.update(state.loaded.world, scaledDelta);
    AnimationCollision2DSystem{}.update(state.loaded.world);
  }
  state.lua.update(scaledDelta);
  if (state.loaded.world.uiTweens.activeCount() > 0) {
    state.loaded.world.uiTweens.update(state.loaded.world.ui, scaledDelta);
    ui::UiLayoutEngine{}.layout(state.loaded.world.ui,
                                state.loaded.world.ui.canvasSize);
  }
  Camera2DSystem{}.update(
      state.loaded.world, scaledDelta,
      fixedStep <= 0.0F
          ? 1.0F
          : std::clamp(static_cast<float>(state.fixedAccumulator / fixedStep),
                       0.0F, 1.0F));
  if (state.loaded.world.tilemapCollisionDirty)
    generateTilemapColliders(state.loaded.world, state.assetRegistry);
  if (state.lua.hasPendingSceneLoad()) {
    if (!state.lua.applyPendingSceneLoad(error)) {
      state.lua.appendRuntimeLog({.severity = RuntimeLogSeverity::Error,
                                  .channel = "runtime.scene",
                                  .message = error,
                                  .source = {},
                                  .line = 0,
                                  .entityId = {},
                                  .component = {},
                                  .field = {}});
      if (restorePause)
        state.lua.setPaused(true);
      return false;
    }
    generateTilemapColliders(state.loaded.world, state.assetRegistry);
  }
  state.audio.setGamePaused(restorePause || state.lua.paused());
  AudioSceneSystem{}.update(state.loaded.world, state.audio);
  state.audio.update(frameDelta);
  state.media.update(frameDelta);
  state.accessibility.update(state.loaded.world.ui);
  const assets::AssetMemoryReport memory = state.assets.memoryReport();
  RuntimeProfiler::setGauge(
      "World.entities",
      static_cast<double>(state.loaded.world.entities.size()));
  RuntimeProfiler::setGauge(
      "UI.nodes", static_cast<double>(state.loaded.world.ui.nodes.size()));
  RuntimeProfiler::setGauge("Assets.resident_count",
                            static_cast<double>(memory.assets.size()));
  RuntimeProfiler::setGauge("Assets.resident_bytes",
                            static_cast<double>(memory.residentBytes));
  RuntimeProfiler::setGauge("Input.keys_down",
                            static_cast<double>(state.input.keysDown.size()));
  RuntimeProfiler::setGauge("Input.gamepads",
                            static_cast<double>(state.input.gamepads.size()));
  RuntimeProfiler::setGauge("Network.connected",
                            state.network.isConnected() ? 1.0 : 0.0);
  RuntimeProfiler::setGauge("Network.latency_ms",
                            static_cast<double>(state.network.latencyMs()));
  if (restorePause)
    state.lua.setPaused(true);
  return true;
}

bool EmbeddedRuntimeSession::update(InputState input, const float deltaSeconds,
                                    const std::uint16_t viewportWidth,
                                    const std::uint16_t viewportHeight,
                                    std::string &error) {
  if (state_ == nullptr || !state_->running) {
    error = "No embedded runtime session is running.";
    return false;
  }
  if (state_->paused)
    return true;
  return advance(*state_, std::move(input), std::max(deltaSeconds, 0.0F),
                 viewportWidth, viewportHeight, false, error);
}

bool EmbeddedRuntimeSession::step(InputState input,
                                  const std::uint16_t viewportWidth,
                                  const std::uint16_t viewportHeight,
                                  std::string &error) {
  if (state_ == nullptr || !state_->running || !state_->paused) {
    error = "Step requires a paused embedded runtime session.";
    return false;
  }
  return advance(*state_, std::move(input), fixedTimestep(), viewportWidth,
                 viewportHeight, true, error);
}

bool EmbeddedRuntimeSession::isRunning() const {
  return state_ != nullptr && state_->running;
}

bool EmbeddedRuntimeSession::isPaused() const {
  return state_ != nullptr && state_->paused;
}

bool EmbeddedRuntimeSession::quitRequested() const {
  return state_ != nullptr && state_->lua.quitRequested();
}

const World *EmbeddedRuntimeSession::world() const {
  return state_ == nullptr ? nullptr : &state_->loaded.world;
}

World *EmbeddedRuntimeSession::world() {
  return state_ == nullptr ? nullptr : &state_->loaded.world;
}

float EmbeddedRuntimeSession::fixedTimestep() const {
  return state_ == nullptr ? 0.0F
                           : state_->loaded.project.simulation.fixedTimestep;
}

float EmbeddedRuntimeSession::interpolationAlpha() const {
  if (state_ == nullptr || fixedTimestep() <= 0.0F)
    return 1.0F;
  return std::clamp(
      static_cast<float>(state_->fixedAccumulator / fixedTimestep()), 0.0F,
      1.0F);
}

std::uint64_t EmbeddedRuntimeSession::fixedTickCount() const {
  return state_ == nullptr ? 0 : state_->fixedTicks;
}

RuntimeDebugSnapshot EmbeddedRuntimeSession::debugSnapshot() const {
  if (state_ == nullptr)
    return {};
  RuntimeDebugSnapshot snapshot;
  const World &world = state_->loaded.world;
  snapshot.entities = world.entities.size();
  snapshot.focusedEntityId = world.debugFocusedEntityId;
  snapshot.input.keysDown.assign(state_->input.keysDown.begin(),
                                 state_->input.keysDown.end());
  snapshot.input.mouseButtonsDown.assign(state_->input.mouseButtonsDown.begin(),
                                         state_->input.mouseButtonsDown.end());
  std::ranges::sort(snapshot.input.keysDown);
  std::ranges::sort(snapshot.input.mouseButtonsDown);
  snapshot.input.gamepads = state_->input.gamepads.size();
  snapshot.input.touches = state_->input.touches.size();
  snapshot.input.mousePosition = state_->input.mousePosition;
  snapshot.input.mouseDelta = state_->input.mouseDelta;
  snapshot.input.mouseScroll = state_->input.mouseScroll;
  snapshot.input.textEntered = state_->input.textEntered;
  for (const Entity &entity : world.entities) {
    for (const auto &[type, unused] : entity.serializedComponents) {
      static_cast<void>(unused);
      if (type == "Rigidbody2D")
        ++snapshot.physics.rigidbodies2D;
      else if (type == "Rigidbody3D")
        ++snapshot.physics.rigidbodies3D;
      if (type.ends_with("Collider2D"))
        ++snapshot.physics.colliders2D;
      else if (type.ends_with("Collider3D"))
        ++snapshot.physics.colliders3D;
    }
  }
  snapshot.physics.contacts2D = world.physicsContacts.size();
  snapshot.physics.contacts3D = world.physicsContacts3D.size();
  const navigation::NavigationGrid2D &navigation =
      state_->lua.navigationGrid2D();
  snapshot.navigation = {.available = navigation.available(),
                         .width = navigation.width(),
                         .height = navigation.height(),
                         .cellSize = navigation.cellSize(),
                         .blockers = navigation.blockerCount(),
                         .weightedCells = navigation.costCount()};
  snapshot.network = {.available = state_->network.available(),
                      .mode = state_->network.mode(),
                      .connected = state_->network.isConnected(),
                      .secure = state_->network.isSecure(),
                      .latencyMilliseconds = state_->network.latencyMs(),
                      .securityError = state_->network.securityError()};
  snapshot.assets = state_->assets.memoryReport();
  snapshot.overlays = world.debug;
  return snapshot;
}

std::vector<RuntimeLogEntry> EmbeddedRuntimeSession::runtimeLogs() const {
  return state_ == nullptr ? std::vector<RuntimeLogEntry>{}
                           : state_->lua.runtimeLogs();
}

LuaScriptHost::ConsoleResult
EmbeddedRuntimeSession::executeLuaConsole(const std::string_view command) {
  if (state_ == nullptr)
    return {.succeeded = false,
            .values = {},
            .error = "No embedded runtime session is running."};
  return state_->lua.executeConsole(command);
}

void EmbeddedRuntimeSession::setDebugOverlays(DebugOverlayConfig overlays) {
  if (state_ != nullptr)
    state_->loaded.world.debug = overlays;
}

void EmbeddedRuntimeSession::setDebugFocus(std::string entityId) {
  if (state_ != nullptr) {
    state_->loaded.world.debugFocusRequired = true;
    // The editor Inspector already owns stable-ID presentation.
    state_->loaded.world.debug.entityIds = false;
    state_->loaded.world.debugFocusedEntityId = std::move(entityId);
  }
}

std::size_t EmbeddedRuntimeSession::liveSessionCount() {
  return LiveSessions.load();
}

} // namespace demi::runtime
