#include "demi/runtime/RuntimeApp.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/core/Version.h"
#include "demi/runtime/LuaScriptHost.h"
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
  renderer2D.loadTextureAssets(loadAssetRegistry(loaded.project.projectDirectory));
  const Camera2DComponent fallbackCamera;
  InputState input;

  LuaScriptHost luaHost;
  std::string luaError;
  if (luaHost.initialize(loaded.world, input, luaError)) {
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
    }

    const Uint64 currentTicks = SDL_GetTicksNS();
    const float dt = std::min(static_cast<float>(static_cast<double>(currentTicks - previousTicks) / 1000000000.0), 0.1F);
    previousTicks = currentTicks;
    fixedAccumulator += dt;
    while (fixedAccumulator >= fixedStep) {
      luaHost.fixedUpdate(static_cast<float>(fixedStep));
      fixedAccumulator -= fixedStep;
    }

    luaHost.update(dt);

    int width = windowWidth;
    int height = windowHeight;
    SDL_GetWindowSize(window, &width, &height);

    const Camera2DComponent* camera = activeCamera(loaded.world);
    renderer2D.beginFrame(camera != nullptr ? *camera : fallbackCamera, width, height);
    renderer2D.drawWorld(loaded.world);
    renderer2D.endFrame();

    ++frameCount;
    if (options.maxFrames > 0 && frameCount >= options.maxFrames) {
      running = false;
    }
  }

  luaHost.destroy();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
#endif
}

} // namespace demi::runtime
