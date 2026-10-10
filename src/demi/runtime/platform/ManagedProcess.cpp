#include "demi/runtime/platform/ManagedProcess.h"
#include <SDL3/SDL.h>
#include <array>

namespace demi::runtime::platform {
struct ManagedProcess::Impl {
  SDL_Process *process = nullptr;
  std::optional<int> exit;
  std::string output;
  void drain() {
    if (!process)
      return;
    auto *stream = SDL_GetProcessOutput(process);
    if (!stream)
      return;
    std::array<char, 8192> buffer{};
    // Bound work per UI tick while draining often enough to avoid pipe stalls.
    for (int chunk = 0; chunk < 32; ++chunk) {
      const auto count = SDL_ReadIO(stream, buffer.data(), buffer.size());
      if (!count)
        break;
      output.append(buffer.data(), count);
      constexpr std::size_t retainedBytes = 1024 * 1024;
      if (output.size() > retainedBytes)
        output.erase(0, output.size() - retainedBytes);
    }
  }
};
ManagedProcess::ManagedProcess() : impl_(std::make_unique<Impl>()) {}
ManagedProcess::~ManagedProcess() { stop(); }
bool ManagedProcess::start(const std::vector<std::string> &arguments,
                           std::string &error) {
  if (running()) {
    error = "A process is already running.";
    return false;
  }
  if (arguments.empty()) {
    error = "An executable is required.";
    return false;
  }
  impl_->exit.reset();
  impl_->output.clear();
  std::vector<const char *> argv;
  for (const auto &argument : arguments)
    argv.push_back(argument.c_str());
  argv.push_back(nullptr);
  const auto properties = SDL_CreateProperties();
  SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                         argv.data());
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                        SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                        SDL_PROCESS_STDIO_APP);
  SDL_SetBooleanProperty(
      properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
  impl_->process = SDL_CreateProcessWithProperties(properties);
  SDL_DestroyProperties(properties);
  if (!impl_->process) {
    error = SDL_GetError();
    return false;
  }
  return true;
}
void ManagedProcess::poll() {
  if (!impl_->process)
    return;
  impl_->drain();
  int exit = 0;
  if (SDL_WaitProcess(impl_->process, false, &exit)) {
    impl_->drain();
    impl_->exit = exit;
    SDL_DestroyProcess(impl_->process);
    impl_->process = nullptr;
  }
}
void ManagedProcess::stop() {
  if (!impl_->process)
    return;
  SDL_KillProcess(impl_->process, true);
  int exit = 0;
  SDL_WaitProcess(impl_->process, true, &exit);
  impl_->drain();
  impl_->exit = exit;
  SDL_DestroyProcess(impl_->process);
  impl_->process = nullptr;
}
bool ManagedProcess::running() const { return impl_->process != nullptr; }
std::optional<int> ManagedProcess::exitCode() const { return impl_->exit; }
const std::string &ManagedProcess::output() const { return impl_->output; }
std::filesystem::path applicationDirectory() {
  const char *path = SDL_GetBasePath();
  return path ? std::filesystem::path(path) : std::filesystem::path{};
}
} // namespace demi::runtime::platform
