#include "demi/runtime/render/backend/GraphicsDevice.h"

namespace demi::runtime::render {

std::string_view graphicsApiName(const GraphicsApi api) {
  switch (api) {
  case GraphicsApi::Automatic:
    return "automatic";
  case GraphicsApi::Vulkan:
    return "vulkan";
  case GraphicsApi::OpenGL:
    return "opengl";
  case GraphicsApi::OpenGLES:
    return "opengles";
  case GraphicsApi::Noop:
    return "noop";
  }
  return "automatic";
}

bool parseGraphicsApi(const std::string_view name, GraphicsApi &api) {
  if (name == "automatic" || name == "auto") {
    api = GraphicsApi::Automatic;
    return true;
  }
  if (name == "vulkan") {
    api = GraphicsApi::Vulkan;
    return true;
  }
  if (name == "opengl") {
    api = GraphicsApi::OpenGL;
    return true;
  }
  if (name == "opengles" || name == "gles") {
    api = GraphicsApi::OpenGLES;
    return true;
  }
  if (name == "noop") {
    api = GraphicsApi::Noop;
    return true;
  }
  return false;
}

} // namespace demi::runtime::render
