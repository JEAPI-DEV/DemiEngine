#pragma once

#include "demi/runtime/render/backend/GraphicsDevice.h"

struct SDL_Window;

namespace demi::runtime::platform {

[[nodiscard]] render::NativeWindowHandle
sdlNativeWindowHandle(SDL_Window *window);

} // namespace demi::runtime::platform
