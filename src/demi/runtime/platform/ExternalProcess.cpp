#include "demi/runtime/platform/ExternalProcess.h"
#include <SDL3/SDL.h>
namespace demi::runtime::platform {
bool launchExternalProcess(const std::vector<std::string> &arguments,
                           std::string &error) {
  if (arguments.empty() || arguments.front().empty()) {
    error = "Choose a code editor executable in Editor Settings.";
    return false;
  }
  std::vector<const char *> argv;
  for (const auto &arg : arguments)
    argv.push_back(arg.c_str());
  argv.push_back(nullptr);
  const auto properties = SDL_CreateProperties();
  SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                         argv.data());
  SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN,
                         true);
  auto *process = SDL_CreateProcessWithProperties(properties);
  SDL_DestroyProperties(properties);
  if (!process) {
    error = SDL_GetError();
    return false;
  }
  SDL_DestroyProcess(process);
  return true;
}
} // namespace demi::runtime::platform
