#include "demi/runtime/platform/SdlNativeWindow.h"

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include <cstdint>

namespace demi::runtime::platform {

render::NativeWindowHandle sdlNativeWindowHandle(SDL_Window *window) {
  if (window == nullptr)
    return {};

  const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
  if (properties == 0)
    return {};

#if defined(__ANDROID__)
  return {
      .window = SDL_GetPointerProperty(
          properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr),
  };
#elif defined(__linux__)
  if (void *surface = SDL_GetPointerProperty(
          properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
      surface != nullptr) {
    return {
        .display = SDL_GetPointerProperty(
            properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr),
        .window = surface,
        .kind = render::NativeWindowKind::Wayland,
    };
  }

  const std::int64_t x11Window =
      SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
  if (x11Window != 0) {
    return {
        .display = SDL_GetPointerProperty(
            properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr),
        .window =
            reinterpret_cast<void *>(static_cast<std::uintptr_t>(x11Window)),
    };
  }
#endif
  return {};
}

} // namespace demi::runtime::platform
