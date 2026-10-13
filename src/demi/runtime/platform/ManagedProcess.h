#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime::platform {
// One directly launched child. SDL owns OS-specific handles and termination.
// Poll and stop on the owning thread; no shell or process-global environment
// edits.
class ManagedProcess {
public:
  ManagedProcess();
  ~ManagedProcess();
  ManagedProcess(const ManagedProcess &) = delete;
  ManagedProcess &operator=(const ManagedProcess &) = delete;
  bool start(const std::vector<std::string> &arguments, std::string &error);
  void poll();
  void stop();
  bool running() const;
  std::optional<int> exitCode() const;
  const std::string &output() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
std::filesystem::path applicationDirectory();
} // namespace demi::runtime::platform
