#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace demi::runtime {

class RuntimeProfiler {
public:
  struct Entry {
    std::string name;
    double totalMilliseconds = 0.0;
    double maxMilliseconds = 0.0;
    int calls = 0;
    std::size_t bytes = 0;
  };

  static void setEnabled(bool enabled);
  [[nodiscard]] static bool enabled();
  static void resetSession();
  static void beginFrame();
  static void record(std::string name, double milliseconds);
  static void addBytes(std::string name, std::size_t bytes);
  [[nodiscard]] static std::string
  frameSummary(double minimumMilliseconds = 0.01);
  [[nodiscard]] static std::vector<Entry> sessionEntries();
  [[nodiscard]] static std::string sessionReport();
};

class ProfileScope {
public:
  explicit ProfileScope(std::string name);
  ~ProfileScope();

  ProfileScope(const ProfileScope &) = delete;
  ProfileScope &operator=(const ProfileScope &) = delete;

private:
  std::string name_;
  std::chrono::steady_clock::time_point start_;
  bool enabled_ = false;
};

} // namespace demi::runtime
