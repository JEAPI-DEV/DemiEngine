#include "editor/EditorPlaySession.h"

#include "demi/runtime/app/EmbeddedRuntimeSession.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"

#if defined(__linux__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <utility>

namespace demi::editor {

std::string_view editorPlayStateLabel(const EditorPlayState state) {
  switch (state) {
  case EditorPlayState::Stopped:
    return "Stopped";
  case EditorPlayState::Starting:
    return "Starting";
  case EditorPlayState::Running:
    return "Running";
  case EditorPlayState::Paused:
    return "Paused";
  case EditorPlayState::Failed:
    return "Failed";
  }
  return "Unknown";
}

EditorPlaySession::EditorPlaySession() = default;

EditorPlaySession::~EditorPlaySession() { stop(); }

bool EditorPlaySession::startEmbedded(const std::filesystem::path &project,
                                      std::string &error) {
  stop();
  state_ = EditorPlayState::Starting;
  mode_ = EditorPlayMode::Embedded;
  failure_.clear();
  auto session = std::make_unique<runtime::EmbeddedRuntimeSession>();
  runtime::RuntimeProfiler::setEnabled(true);
  runtime::RuntimeProfiler::resetSession();
  if (!session->start(project, error)) {
    runtime::RuntimeProfiler::setEnabled(false);
    reportFailure(error);
    return false;
  }
  embedded_ = std::move(session);
  state_ = EditorPlayState::Running;
  return true;
}

bool EditorPlaySession::startExternal(const std::filesystem::path &project,
                                      std::string &error) {
  stop();
  state_ = EditorPlayState::Starting;
  mode_ = EditorPlayMode::External;
  failure_.clear();
#if defined(__linux__)
  std::error_code filesystemError;
  const std::filesystem::path editor =
      std::filesystem::read_symlink("/proc/self/exe", filesystemError);
  if (filesystemError) {
    error = "Could not locate the editor executable.";
    reportFailure(error);
    return false;
  }
  const std::filesystem::path runtime = editor.parent_path() / "demi-runtime";
  if (!std::filesystem::is_regular_file(runtime)) {
    error = "Could not find demi-runtime beside the editor.";
    reportFailure(error);
    return false;
  }
  const pid_t child = fork();
  if (child < 0) {
    error = "Could not create the external play process.";
    reportFailure(error);
    return false;
  }
  if (child == 0) {
    const std::string executable = runtime.string();
    const std::string projectPath = project.string();
    execl(executable.c_str(), executable.c_str(), "--project",
          projectPath.c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }
  processId_ = static_cast<int>(child);
  state_ = EditorPlayState::Running;
  return true;
#else
  (void)project;
  error = "External play sessions are currently available on Linux only.";
  reportFailure(error);
  return false;
#endif
}

bool EditorPlaySession::togglePause(std::string &error) {
  poll();
  if (!isRunning()) {
    error = "No play session is running.";
    return false;
  }
  const bool pause = state_ == EditorPlayState::Running;
  if (mode_ == EditorPlayMode::Embedded) {
    embedded_->setPaused(pause);
  } else {
#if defined(__linux__)
    const int signal = pause ? SIGSTOP : SIGCONT;
    if (kill(static_cast<pid_t>(processId_), signal) != 0) {
      error = "Could not change the external play-session state.";
      poll();
      return false;
    }
#else
    error = "Pause is unavailable on this platform.";
    return false;
#endif
  }
  state_ = pause ? EditorPlayState::Paused : EditorPlayState::Running;
  return true;
}

bool EditorPlaySession::step(runtime::InputState input,
                             const std::uint16_t width,
                             const std::uint16_t height, std::string &error) {
  if (mode_ != EditorPlayMode::Embedded || !isPaused()) {
    error = "Step requires a paused embedded play session.";
    return false;
  }
  if (!embedded_->step(std::move(input), width, height, error)) {
    reportFailure(error);
    return false;
  }
  return true;
}

bool EditorPlaySession::update(runtime::InputState input,
                               const float deltaSeconds,
                               const std::uint16_t width,
                               const std::uint16_t height, std::string &error) {
  poll();
  if (state_ != EditorPlayState::Running || mode_ != EditorPlayMode::Embedded)
    return true;
  if (!embedded_->update(std::move(input), deltaSeconds, width, height,
                         error)) {
    reportFailure(error);
    return false;
  }
  if (embedded_->quitRequested())
    stop();
  return true;
}

void EditorPlaySession::stop() {
  if (embedded_ != nullptr) {
    embedded_->stop();
    embedded_.reset();
    runtime::RuntimeProfiler::setEnabled(false);
  }
#if defined(__linux__)
  if (processId_ > 0) {
    if (state_ == EditorPlayState::Paused)
      (void)kill(static_cast<pid_t>(processId_), SIGCONT);
    (void)kill(static_cast<pid_t>(processId_), SIGTERM);
    (void)waitpid(static_cast<pid_t>(processId_), nullptr, 0);
  }
#endif
  processId_ = 0;
  state_ = EditorPlayState::Stopped;
}

void EditorPlaySession::poll() {
  if (mode_ != EditorPlayMode::External || processId_ <= 0)
    return;
#if defined(__linux__)
  int status = 0;
  const pid_t result =
      waitpid(static_cast<pid_t>(processId_), &status, WNOHANG);
  if (result == static_cast<pid_t>(processId_)) {
    processId_ = 0;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
      state_ = EditorPlayState::Stopped;
    else
      reportFailure("The external play process exited unexpectedly.");
  } else if (result < 0) {
    processId_ = 0;
    reportFailure("The external play process could not be observed.");
  }
#endif
}

const runtime::World *EditorPlaySession::runtimeWorld() const {
  return embedded_ == nullptr ? nullptr : embedded_->world();
}

std::uint64_t EditorPlaySession::fixedTickCount() const {
  return embedded_ == nullptr ? 0 : embedded_->fixedTickCount();
}

float EditorPlaySession::interpolationAlpha() const {
  return embedded_ == nullptr ? 1.0F : embedded_->interpolationAlpha();
}

EditorProfilerSnapshot EditorPlaySession::profilerSnapshot() const {
  return buildEditorProfilerSnapshot(isEmbedded(), isPaused(),
                                     runtime::RuntimeProfiler::sessionEntries(),
                                     runtime::RuntimeProfiler::frameCount());
}

runtime::RuntimeDebugSnapshot EditorPlaySession::debugSnapshot() const {
  return embedded_ == nullptr ? runtime::RuntimeDebugSnapshot{}
                              : embedded_->debugSnapshot();
}

void EditorPlaySession::setDebugOverlays(
    const runtime::DebugOverlayConfig overlays) {
  if (embedded_ != nullptr)
    embedded_->setDebugOverlays(overlays);
}

void EditorPlaySession::setDebugFocus(std::string entityId) {
  if (embedded_ != nullptr)
    embedded_->setDebugFocus(std::move(entityId));
}

void EditorPlaySession::reportFailure(std::string message) {
  if (embedded_ != nullptr) {
    embedded_->stop();
    embedded_.reset();
  }
  failure_ = std::move(message);
  state_ = EditorPlayState::Failed;
}

} // namespace demi::editor
