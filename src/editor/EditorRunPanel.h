#pragma once
#include "demi/runtime/platform/ManagedProcess.h"
#include <chrono>
#include <functional>
#include <string>

namespace demi::editor {
class EditorWorkspace;
class EditorRunPanel {
public:
  void open() { show_ = true; }
  bool running() const { return process_.running(); }
  void stop() { process_.stop(); }
  void draw(EditorWorkspace &workspace, bool embeddedRunning,
            const std::function<bool(std::string &)> &saveAll,
            std::string &notice);

private:
  runtime::platform::ManagedProcess process_;
  std::chrono::steady_clock::time_point started_;
  std::string executable_;
  std::string result_;
  int profile_ = -1;
  int maxFrames_ = 24000;
  int timeoutSeconds_ = 300;
  bool testing_ = false;
  bool show_ = false;
};
} // namespace demi::editor
