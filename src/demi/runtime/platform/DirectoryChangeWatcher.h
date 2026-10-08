#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace demi::runtime::platform {
struct DirectoryChanges {
  std::vector<std::filesystem::path> paths;
  bool rescan = false;
};

// Nonblocking native notifications; callers own traversal and filtering policy.
class DirectoryChangeWatcher {
public:
  ~DirectoryChangeWatcher();
  DirectoryChangeWatcher() = default;
  DirectoryChangeWatcher(const DirectoryChangeWatcher &) = delete;
  DirectoryChangeWatcher &operator=(const DirectoryChangeWatcher &) = delete;
  void reset();
  void watch(const std::filesystem::path &directory);
  DirectoryChanges poll();
  const std::string &error() const { return error_; }

private:
  int descriptor_ = -1;
  std::map<int, std::filesystem::path> directories_;
  std::string error_;
};
} // namespace demi::runtime::platform
