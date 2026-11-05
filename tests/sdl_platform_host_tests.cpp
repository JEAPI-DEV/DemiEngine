#include "demi/runtime/app/FramePacing.h"
#include "demi/runtime/platform/PlatformHost.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"

#include <SDL3/SDL.h>

#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using demi::runtime::InputState;
using demi::runtime::platform::createSdlPlatformHost;
using demi::runtime::platform::PlatformHostConfig;
using demi::runtime::platform::WindowMode;
using demi::runtime::render::BgfxGraphicsDevice;
using demi::runtime::render::GraphicsApi;
using demi::runtime::render::GraphicsDeviceConfig;

namespace {

void invalidConfigurationIsRejected() {
  auto host = createSdlPlatformHost();
  std::string error;
  assert(!host->initialize(
      PlatformHostConfig{.title = "invalid", .width = 0, .height = 1}, error));
  assert(!error.empty());
  host->shutdown();
}

void framePacingPolicyHandlesHighRefreshDisplays() {
  using demi::runtime::compositorSatisfiesFrameCap;
  assert(compositorSatisfiesFrameCap(true, false, 0, 90.0F, false));
  assert(!compositorSatisfiesFrameCap(true, false, 60, 90.0F, false));
  assert(compositorSatisfiesFrameCap(true, false, 60, 60.0F, false));
  assert(compositorSatisfiesFrameCap(true, false, 60, 90.0F, true));
  assert(!compositorSatisfiesFrameCap(false, false, 60, 60.0F, true));
  assert(!compositorSatisfiesFrameCap(true, true, 60, 60.0F, true));
}

void lifecycleAndEventTranslationWorkWithoutAGpu() {
  assert(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
  auto host = createSdlPlatformHost();
  std::string error;
  assert(host->initialize(PlatformHostConfig{.title = "Demi platform test",
                                             .width = 320,
                                             .height = 180},
                          error));
  assert(host->frameState().width == 320);
  assert(host->frameState().height == 180);
  assert(!host->initialize(PlatformHostConfig{}, error));
  assert(!host->requestFrameRate(60.0F));

  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.type = SDL_EVENT_KEY_DOWN;
  event.key.scancode = SDL_SCANCODE_W;
  event.key.down = true;
  assert(SDL_PushEvent(&event));

  assert(host->waitForEvent(50));
  InputState input;
  host->poll(input);
  assert(input.keysDown.contains("w"));
  assert(input.keysPressed.contains("w"));

  event = {};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.type = SDL_EVENT_KEY_DOWN;
  event.key.scancode = SDL_SCANCODE_LALT;
  event.key.down = true;
  assert(SDL_PushEvent(&event));
  host->poll(input);
  assert(input.keysDown.contains("left alt"));

  event = {};
  event.type = SDL_EVENT_KEY_UP;
  event.key.type = SDL_EVENT_KEY_UP;
  event.key.scancode = SDL_SCANCODE_W;
  event.key.down = false;
  assert(SDL_PushEvent(&event));
  host->poll(input);
  assert(!input.keysDown.contains("w"));
  assert(input.keysReleased.contains("w"));

  event = {};
  event.type = SDL_EVENT_DROP_FILE;
  event.drop.type = SDL_EVENT_DROP_FILE;
  event.drop.data = "/tmp/demi-dropped-asset.png";
  assert(SDL_PushEvent(&event));
  host->poll(input);
  const auto dropped = host->takeDroppedFiles();
  assert(dropped ==
         std::vector<std::filesystem::path>{"/tmp/demi-dropped-asset.png"});
  assert(host->takeDroppedFiles().empty());

  BgfxGraphicsDevice graphics;
  assert(graphics.initialize(GraphicsDeviceConfig{.api = GraphicsApi::Noop,
                                                  .width = 320,
                                                  .height = 180,
                                                  .vsync = false},
                             error));
  graphics.beginFrame(0x112233ff);
  const std::uint32_t submittedFrame = graphics.endFrame();
  (void)submittedFrame;
  graphics.shutdown();

  assert(host->setWindowMode(WindowMode::Windowed, error));
  host->shutdown();
  host->shutdown();
}

void everyScancodePreservesTheEngineKeyContract() {
  assert(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
  auto host = createSdlPlatformHost();
  std::string error;
  assert(host->initialize(
      {.title = "Keyboard names", .width = 320, .height = 180}, error));
  const std::array letters{
      SDL_SCANCODE_A, SDL_SCANCODE_B, SDL_SCANCODE_C, SDL_SCANCODE_D,
      SDL_SCANCODE_E, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
      SDL_SCANCODE_I, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L,
      SDL_SCANCODE_M, SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_P,
      SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_S, SDL_SCANCODE_T,
      SDL_SCANCODE_U, SDL_SCANCODE_V, SDL_SCANCODE_W, SDL_SCANCODE_X,
      SDL_SCANCODE_Y, SDL_SCANCODE_Z};
  const std::array digits{SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2,
                          SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
                          SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
                          SDL_SCANCODE_9};
  const std::array functions{
      SDL_SCANCODE_F1, SDL_SCANCODE_F2,  SDL_SCANCODE_F3,  SDL_SCANCODE_F4,
      SDL_SCANCODE_F5, SDL_SCANCODE_F6,  SDL_SCANCODE_F7,  SDL_SCANCODE_F8,
      SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_F11, SDL_SCANCODE_F12};
  std::map<int, std::string> expected{{SDL_SCANCODE_SPACE, "space"},
                                      {SDL_SCANCODE_RETURN, "return"},
                                      {SDL_SCANCODE_ESCAPE, "escape"},
                                      {SDL_SCANCODE_AC_BACK, "back"},
                                      {SDL_SCANCODE_TAB, "tab"},
                                      {SDL_SCANCODE_BACKSPACE, "backspace"},
                                      {SDL_SCANCODE_DELETE, "delete"},
                                      {SDL_SCANCODE_HOME, "home"},
                                      {SDL_SCANCODE_END, "end"},
                                      {SDL_SCANCODE_UP, "up"},
                                      {SDL_SCANCODE_DOWN, "down"},
                                      {SDL_SCANCODE_LEFT, "left"},
                                      {SDL_SCANCODE_RIGHT, "right"},
                                      {SDL_SCANCODE_LSHIFT, "left shift"},
                                      {SDL_SCANCODE_RSHIFT, "right shift"},
                                      {SDL_SCANCODE_LCTRL, "left ctrl"},
                                      {SDL_SCANCODE_RCTRL, "right ctrl"},
                                      {SDL_SCANCODE_LALT, "left alt"},
                                      {SDL_SCANCODE_RALT, "right alt"}};
  for (std::size_t index = 0; index < letters.size(); ++index) {
    expected[letters[index]] = std::string(1, static_cast<char>('a' + index));
  }
  for (std::size_t index = 0; index < digits.size(); ++index) {
    expected[digits[index]] = std::to_string(index);
  }
  for (std::size_t index = 0; index < functions.size(); ++index) {
    expected[functions[index]] = "f" + std::to_string(index + 1);
  }
  for (int key = -1; key <= static_cast<int>(SDL_SCANCODE_COUNT); ++key) {
    InputState input;
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = static_cast<SDL_Scancode>(key);
    event.key.down = true;
    assert(SDL_PushEvent(&event));
    host->poll(input);
    const auto name = expected.find(key);
    if (name == expected.end()) {
      assert(input.keysDown.empty() && input.keysPressed.empty());
    } else {
      assert(input.keysDown.size() == 1 &&
             input.keysDown.contains(name->second));
      assert(input.keysPressed.size() == 1 &&
             input.keysPressed.contains(name->second));
    }
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    assert(SDL_PushEvent(&event));
    host->poll(input);
    assert(input.keysDown.empty() && input.keysPressed.empty());
    assert(input.keysReleased.size() == (name == expected.end() ? 0U : 1U));
    if (name != expected.end()) {
      assert(input.keysReleased.contains(name->second));
    }
  }
  host->shutdown();
}

void maximizedConfigurationKeepsHeadlessLifecycleValid() {
  assert(!PlatformHostConfig{}.maximized);
  assert(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
  auto host = createSdlPlatformHost();
  std::string error;
  assert(host->initialize({.title = "Maximized platform startup",
                           .width = 800,
                           .height = 600,
                           .resizable = true,
                           .maximized = true},
                          error));
  int count = 0;
  SDL_Window **windows = SDL_GetWindows(&count);
  assert(windows && count == 1);
  // SDL's dummy driver has no window manager and cannot maximize. Verify the
  // request remains lifecycle-safe here; native desktop captures qualify size.
  assert(host->frameState().width > 0 && host->frameState().height > 0);
  SDL_free(windows);
  host->shutdown();
}

void authoredStartupModesAndSceneTitlesReachTheNativeWindow() {
  assert(PlatformHostConfig{}.mode == WindowMode::Windowed);
  assert(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
  for (const auto mode :
       {WindowMode::Windowed, WindowMode::Borderless, WindowMode::Fullscreen}) {
    auto host = createSdlPlatformHost();
    std::string error;
    assert(host->initialize(
        {.title = "Preparing scene", .width = 640, .height = 360, .mode = mode},
        error));
    int count = 0;
    SDL_Window **windows = SDL_GetWindows(&count);
    assert(windows != nullptr && count == 1);
    const auto flags = SDL_GetWindowFlags(windows[0]);
    assert(bool(flags & SDL_WINDOW_FULLSCREEN) ==
           (mode == WindowMode::Fullscreen));
    if (mode != WindowMode::Fullscreen) {
      assert(bool(flags & SDL_WINDOW_BORDERLESS) ==
             (mode == WindowMode::Borderless));
      assert(host->frameState().width == 640);
      assert(host->frameState().height == 360);
    }
    assert(host->setWindowTitle("Activated scene", error));
    assert(std::string(SDL_GetWindowTitle(windows[0])) == "Activated scene");
    SDL_free(windows);
    host->shutdown();
  }
}

} // namespace

int main() {
  framePacingPolicyHandlesHighRefreshDisplays();
  invalidConfigurationIsRejected();
  lifecycleAndEventTranslationWorkWithoutAGpu();
  everyScancodePreservesTheEngineKeyContract();
  maximizedConfigurationKeepsHeadlessLifecycleValid();
  authoredStartupModesAndSceneTitlesReachTheNativeWindow();
  return 0;
}
