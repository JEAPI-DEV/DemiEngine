#include "editor/EditorPlaySession.h"

#if defined(__linux__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace demi::editor {

EditorPlaySession::~EditorPlaySession() { stop(); }

bool EditorPlaySession::start(const std::filesystem::path &project,
                              std::string &error) {
  poll();
  if (isRunning()) {
    error = "A play session is already running.";
    return false;
  }
#if defined(__linux__)
  std::error_code filesystemError;
  const std::filesystem::path editor =
      std::filesystem::read_symlink("/proc/self/exe", filesystemError);
  if (filesystemError) {
    error = "Could not locate the editor executable.";
    return false;
  }
  const std::filesystem::path runtime = editor.parent_path() / "demi-runtime";
  if (!std::filesystem::is_regular_file(runtime)) {
    error = "Could not find demi-runtime beside the editor.";
    return false;
  }
  const pid_t child = fork();
  if (child < 0) {
    error = "Could not create the play process.";
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
  isPaused_ = false;
  return true;
#else
  (void)project;
  error = "Play sessions are currently available on Linux only.";
  return false;
#endif
}

bool EditorPlaySession::togglePause(std::string &error) {
  poll();
  if (!isRunning()) {
    error = "No play session is running.";
    return false;
  }
#if defined(__linux__)
  const int signal = isPaused_ ? SIGCONT : SIGSTOP;
  if (kill(static_cast<pid_t>(processId_), signal) != 0) {
    error = "Could not change the play-session state.";
    poll();
    return false;
  }
  isPaused_ = !isPaused_;
  return true;
#else
  error = "Pause is unavailable on this platform.";
  return false;
#endif
}

void EditorPlaySession::stop() {
  if (!isRunning())
    return;
#if defined(__linux__)
  if (isPaused_)
    (void)kill(static_cast<pid_t>(processId_), SIGCONT);
  (void)kill(static_cast<pid_t>(processId_), SIGTERM);
  (void)waitpid(static_cast<pid_t>(processId_), nullptr, 0);
#endif
  processId_ = 0;
  isPaused_ = false;
}

void EditorPlaySession::poll() {
  if (!isRunning())
    return;
#if defined(__linux__)
  int status = 0;
  const pid_t result =
      waitpid(static_cast<pid_t>(processId_), &status, WNOHANG);
  if (result == static_cast<pid_t>(processId_) || result < 0) {
    processId_ = 0;
    isPaused_ = false;
  }
#endif
}

} // namespace demi::editor
