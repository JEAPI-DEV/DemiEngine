#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace demi::runtime {

class RuntimeProfiler {
public:
  static void setEnabled(bool enabled);
  [[nodiscard]] static bool enabled();
  static void beginFrame();
  static void record(std::string name, double milliseconds);
  static void addBytes(std::string name, std::size_t bytes);
  [[nodiscard]] static std::string frameSummary(double minimumMilliseconds = 0.01);
};

class ProfileScope {
public:
  explicit ProfileScope(std::string name);
  ~ProfileScope();

  ProfileScope(const ProfileScope&) = delete;
  ProfileScope& operator=(const ProfileScope&) = delete;

private:
  std::string name_;
  std::chrono::steady_clock::time_point start_;
  bool enabled_ = false;
};

} // namespace demi::runtime
