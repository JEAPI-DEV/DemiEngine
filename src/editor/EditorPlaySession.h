#pragma once

#include <filesystem>
#include <string>

namespace demi::editor {

class EditorPlaySession {
public:
  EditorPlaySession() = default;
  EditorPlaySession(const EditorPlaySession &) = delete;
  EditorPlaySession &operator=(const EditorPlaySession &) = delete;
  ~EditorPlaySession();

  [[nodiscard]] bool start(const std::filesystem::path &project,
                           std::string &error);
  [[nodiscard]] bool togglePause(std::string &error);
  void stop();
  void poll();

  [[nodiscard]] bool isRunning() const { return processId_ > 0; }
  [[nodiscard]] bool isPaused() const { return isPaused_; }

private:
  int processId_ = 0;
  bool isPaused_ = false;
};

} // namespace demi::editor
