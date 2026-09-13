#include "demi/runtime/app/FramePacing.h"
#include "demi/runtime/platform/PlatformHost.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>
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

} // namespace

int main() {
  framePacingPolicyHandlesHighRefreshDisplays();
  invalidConfigurationIsRejected();
  lifecycleAndEventTranslationWorkWithoutAGpu();
  return 0;
}
