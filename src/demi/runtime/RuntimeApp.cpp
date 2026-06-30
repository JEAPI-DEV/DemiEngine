#include "demi/runtime/RuntimeApp.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/core/Version.h"
#include "demi/runtime/AudioSystem.h"
#include "demi/runtime/LuaScriptHost.h"
#include "demi/runtime/Physics2D.h"
#include "demi/runtime/Renderer2D.h"
#include "demi/runtime/SceneData.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <string>

#if DEMI_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace demi::runtime {

namespace {

constexpr int RuntimeFailure = 3;

#if DEMI_HAS_SDL3

std::string normalizedKeyName(const SDL_Keycode key) {
  std::string name = SDL_GetKeyName(key);
  std::ranges::transform(name, name.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return name;
}

void setKeyState(InputState& input, const SDL_Keycode key, const bool pressed) {
  const std::string name = normalizedKeyName(key);
  if (name.empty()) {
    return;
  }
  if (pressed) {
    input.keysDown.insert(name);
  } else {
    input.keysDown.erase(name);
  }
}

std::string mouseButtonName(const Uint8 button) {
  switch (button) {
  case SDL_BUTTON_LEFT:
    return "left";
  case SDL_BUTTON_RIGHT:
    return "right";
  case SDL_BUTTON_MIDDLE:
    return "middle";
  default:
    return "button" + std::to_string(static_cast<int>(button));
  }
}

void setMouseButtonState(InputState& input, const Uint8 button, const bool pressed) {
  const std::string name = mouseButtonName(button);
  if (pressed) {
    input.mouseButtonsDown.insert(name);
  } else {
    input.mouseButtonsDown.erase(name);
  }
}

void applyWindowMode(SDL_Window* window, const std::string& mode) {
  if (mode == "fullscreen") {
    SDL_SetWindowBordered(window, true);
    SDL_SetWindowFullscreen(window, true);
    return;
  }

  SDL_SetWindowFullscreen(window, false);
  SDL_SetWindowBordered(window, mode != "borderless");
  if (mode == "windowed") {
    SDL_SetWindowSize(window, 960, 540);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

#endif

} // namespace

int runProject(const RuntimeOptions& options) {
  std::string error;
  const std::optional<LoadedProject> loadedProject = loadProject(options.projectPath, error);
  if (!loadedProject.has_value()) {
    std::cerr << "Runtime load failed: " << error << '\n';
    return 1;
  }
  LoadedProject loaded = *loadedProject;

#if !DEMI_HAS_SDL3
  std::cerr << "Runtime windowing is unavailable because SDL3 was not found at configure time.\n";
  return RuntimeFailure;
#else
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return RuntimeFailure;
  }

  constexpr int windowWidth = 960;
  constexpr int windowHeight = 540;
  const std::string title = std::string(EngineName) + " - " + loaded.project.name + " - " + loaded.world.name;
  SDL_Window* window = SDL_CreateWindow(title.c_str(), windowWidth, windowHeight, SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    SDL_Quit();
    return RuntimeFailure;
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer == nullptr) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return RuntimeFailure;
  }

  Renderer2D renderer2D(renderer);
  const AssetRegistry assetRegistry = loadAssetRegistry(loaded.project.projectDirectory);
  renderer2D.loadTextureAssets(assetRegistry);
  AudioSystem audioSystem;
  if (audioSystem.initialize()) {
    audioSystem.loadAudioAssets(assetRegistry);
  }
  const Camera2DComponent fallbackCamera;
  InputState input;

  LuaScriptHost luaHost;
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

  std::cout << "Running " << loaded.project.name << " scene " << loaded.world.id << " with " << renderableEntityCount(loaded.world) << " renderable entity/entities.\n";
  std::cout << "Game scripts now own controls. Close the window or press Escape to stop.\n";

  bool running = true;
  int frameCount = 0;
  Uint64 previousTicks = SDL_GetTicksNS();
  double fixedAccumulator = 0.0;
  constexpr double fixedStep = 1.0 / 60.0;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
      if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        running = false;
      }
      if (event.type == SDL_EVENT_KEY_DOWN) {
        setKeyState(input, event.key.key, true);
      }
      if (event.type == SDL_EVENT_KEY_UP) {
        setKeyState(input, event.key.key, false);
      }
      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        input.mousePosition = Vec2{.x = event.motion.x, .y = event.motion.y};
      }
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        input.mousePosition = Vec2{.x = event.button.x, .y = event.button.y};
        setMouseButtonState(input, event.button.button, true);
      }
      if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        input.mousePosition = Vec2{.x = event.button.x, .y = event.button.y};
        setMouseButtonState(input, event.button.button, false);
      }
    }

    const Uint64 currentTicks = SDL_GetTicksNS();
    const float dt = std::min(static_cast<float>(static_cast<double>(currentTicks - previousTicks) / 1000000000.0), 0.1F);
    previousTicks = currentTicks;
    int width = windowWidth;
    int height = windowHeight;
    SDL_GetWindowSize(window, &width, &height);
    luaHost.setViewport(width, height);

    fixedAccumulator += dt;
    while (fixedAccumulator >= fixedStep) {
      luaHost.fixedUpdate(static_cast<float>(fixedStep));
      if (luaHost.physicsEnabled()) {
        stepPhysics2D(loaded.world, static_cast<float>(fixedStep));
      }
      fixedAccumulator -= fixedStep;
    }

    luaHost.update(dt);
    if (luaHost.quitRequested()) {
      running = false;
    }
    if (luaHost.windowModeDirty()) {
      applyWindowMode(window, luaHost.windowMode());
      luaHost.clearWindowModeDirty();
    }

    if (luaHost.hasPendingSceneLoad()) {
      std::string sceneError;
      if (!luaHost.applyPendingSceneLoad(sceneError)) {
        std::cerr << "Scene switch failed: " << sceneError << '\n';
      } else {
        std::cout << "Switched scene to " << loaded.world.id << " (" << loaded.world.name << ").\n";
      }
    }

    audioSystem.update();

    const Camera2DComponent* camera = activeCamera(loaded.world);
    renderer2D.beginFrame(camera != nullptr ? *camera : fallbackCamera, activeCameraPosition(loaded.world), width, height);
    renderer2D.drawWorld(loaded.world);
    renderer2D.drawHud(loaded.world);
    renderer2D.endFrame();

    ++frameCount;
    if (options.maxFrames > 0 && frameCount >= options.maxFrames) {
      running = false;
    }
  }

  luaHost.destroy();
  audioSystem.shutdown();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
#endif
}

} // namespace demi::runtime
