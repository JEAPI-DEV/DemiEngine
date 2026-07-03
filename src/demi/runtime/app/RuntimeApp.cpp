#include "demi/runtime/app/RuntimeApp.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/core/Version.h"
#include "demi/runtime/audio/AudioSystem.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/media/MediaSystem.h"
#include "demi/runtime/network/NetworkSystem.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/render/Renderer2D.h"
#include "demi/runtime/render/Renderer3D.h"
#include "demi/runtime/scene/SceneData.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#if DEMI_HAS_RAYLIB
#include <raylib.h>
#endif

namespace demi::runtime
{

  namespace
  {

    constexpr int RuntimeFailure = 3;

    struct KeyMapping
    {
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

#if DEMI_HAS_RAYLIB

    void pollKeys(InputState &input)
    {
      input.keysDown.clear();
      for (const KeyMapping &mapping : KeyMap)
      {
        if (IsKeyDown(mapping.key))
        {
          input.keysDown.emplace(mapping.name);
        }
      }
    }

    void pollMouse(InputState &input)
    {
      input.mouseButtonsDown.clear();
      if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
      {
        input.mouseButtonsDown.emplace("left");
      }
      if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
      {
        input.mouseButtonsDown.emplace("right");
      }
      if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
      {
        input.mouseButtonsDown.emplace("middle");
      }
      input.mousePosition = Vec2{.x = static_cast<float>(GetMouseX()), .y = static_cast<float>(GetMouseY())};
    }

    void applyWindowMode(const std::string &mode)
    {
      if (mode == "fullscreen")
      {
        if (!IsWindowFullscreen())
        {
          const int monitor = GetCurrentMonitor();
          SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
          ToggleFullscreen();
        }
        return;
      }

      if (IsWindowFullscreen())
      {
        ToggleFullscreen();
      }

      if (mode == "borderless")
      {
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        const int monitor = GetCurrentMonitor();
        SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
      }
      else
      {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
      }

      if (mode == "windowed")
      {
        SetWindowSize(960, 540);
      }
    }

#endif

    void stepSimulation(LoadedProject &loaded, LuaScriptHost &luaHost, InputState &input, AudioSystem &audioSystem,
                        MediaSystem &mediaSystem, NetworkSystem &networkSystem, const float dt, const float fixedStep,
                        double &fixedAccumulator, bool &running)
    {
      fixedAccumulator += dt;
      while (fixedAccumulator >= fixedStep)
      {
        luaHost.fixedUpdate(static_cast<float>(fixedStep));
        if (luaHost.physicsEnabled())
        {
          stepPhysics2D(loaded.world, static_cast<float>(fixedStep));
        }
        fixedAccumulator -= fixedStep;
      }

      networkSystem.update();
      luaHost.update(dt);
      if (luaHost.quitRequested())
      {
        running = false;
      }
      if (luaHost.hasPendingSceneLoad())
      {
        std::string sceneError;
        if (!luaHost.applyPendingSceneLoad(sceneError))
        {
          std::cerr << "Scene switch failed: " << sceneError << '\n';
        }
        else
        {
          std::cout << "Switched scene to " << loaded.world.id << " (" << loaded.world.name << ").\n";
        }
      }

      audioSystem.update();
      mediaSystem.update(dt);
      (void)input;
    }

    [[nodiscard]] bool isHeadless()
    {
      const char *value = std::getenv("DEMI_HEADLESS");
      return value != nullptr && std::string(value) != "0";
    }

  } // namespace

  int runProject(const RuntimeOptions &options)
  {
    std::string error;
    const std::optional<LoadedProject> loadedProject = loadProject(options.projectPath, error);
    if (!loadedProject.has_value())
    {
      std::cerr << "Runtime load failed: " << error << '\n';
      return 1;
    }
    LoadedProject loaded = *loadedProject;

    const AssetRegistry assetRegistry = loadAssetRegistry(loaded.project.projectDirectory);
    AudioSystem audioSystem;
    if (!isHeadless() && options.maxFrames == 0 && audioSystem.initialize())
    {
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
    if (luaHost.initialize(loaded.world, input, &audioSystem, luaError))
    {
      if (!luaHost.loadWorldScripts(loaded.project, loaded.world, luaError))
      {
        std::cerr << "Lua scripts skipped: " << luaError << '\n';
      }
      else
      {
        luaHost.start();
      }
    }
    else
    {
      std::cerr << "Lua unavailable: " << luaError << '\n';
    }

    std::cout << "Running " << loaded.project.name << " scene " << loaded.world.id << " with " << renderableEntityCount(loaded.world) << " renderable entity/entities.\n";
    std::cout << "Game scripts now own controls. Close the window or press Escape to stop.\n";

    const bool use3D = sceneIs3D(loaded.world);
    const Camera2DComponent fallbackCamera2D;
    const Camera3DComponent fallbackCamera3D;

    double fixedAccumulator = 0.0;
    constexpr double fixedStep = 1.0 / 60.0;

#if !DEMI_HAS_RAYLIB
    std::cerr << "Runtime windowing is unavailable because raylib was not found at configure time.\n";
    return RuntimeFailure;
#else
    if (isHeadless())
    {
      int frameCount = 0;
      const int targetFrames = options.maxFrames > 0 ? options.maxFrames : 1;
      bool running = true;
      while (running && frameCount < targetFrames)
      {
        stepSimulation(loaded, luaHost, input, audioSystem, mediaSystem, networkSystem,
                       static_cast<float>(fixedStep), static_cast<float>(fixedStep), fixedAccumulator, running);
        ++frameCount;
      }
      luaHost.destroy();
      networkSystem.shutdown();
      mediaSystem.shutdown();
      audioSystem.shutdown();
      return 0;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    constexpr int windowWidth = 960;
    constexpr int windowHeight = 540;
    const std::string title = std::string(EngineName) + " - " + loaded.project.name + " - " + loaded.world.name;
    InitWindow(windowWidth, windowHeight, title.c_str());

    Renderer2D renderer2D;
    Renderer3D renderer3D;
    renderer2D.loadTextureAssets(assetRegistry);
    renderer3D.loadTextureAssets(assetRegistry);

    bool running = true;
    int frameCount = 0;
    int appliedMaxFps = -1;
    while (running && !WindowShouldClose())
    {
      pollKeys(input);
      pollMouse(input);

      const float dt = std::min(GetFrameTime(), 0.1F);

      stepSimulation(
          loaded,
          luaHost,
          input,
          audioSystem,
          mediaSystem,
          networkSystem,
          dt,
          static_cast<float>(fixedStep),
          fixedAccumulator,
          running);

      if (luaHost.windowModeDirty())
      {
        applyWindowMode(luaHost.windowMode());
        luaHost.clearWindowModeDirty();
      }

      const int width = GetRenderWidth();
      const int height = GetRenderHeight();

      luaHost.setViewport(width, height);

      if (use3D)
      {
        const Camera3DComponent *camera = activeCamera3D(loaded.world);
        renderer3D.beginFrame(
            camera != nullptr ? *camera : fallbackCamera3D,
            activeCamera3DPosition(loaded.world),
            activeCamera3DRotation(loaded.world),
            width,
            height);
        renderer3D.drawWorld(loaded.world);
        renderer3D.drawHud(loaded.world);
        renderer3D.endFrame();
      }
      else
      {
        const Camera2DComponent *camera = activeCamera(loaded.world);
        renderer2D.beginFrame(
            camera != nullptr ? *camera : fallbackCamera2D,
            activeCameraPosition(loaded.world),
            width,
            height);
        renderer2D.drawWorld(loaded.world);
        renderer2D.drawHud(loaded.world);
        renderer2D.endFrame();
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
      if (options.maxFrames > 0 && frameCount >= options.maxFrames)
      {
        running = false;
      }
    }

    luaHost.destroy();
    networkSystem.shutdown();
    mediaSystem.shutdown();
    audioSystem.shutdown();
    CloseWindow();
    return 0;
#endif
  }

} // namespace demi::runtime
