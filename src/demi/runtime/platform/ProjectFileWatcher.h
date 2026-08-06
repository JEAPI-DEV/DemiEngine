#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace demi::runtime::platform {

struct ProjectFileChangeBatch {
  std::uint64_t generation = 0;
  std::vector<std::filesystem::path> changed;
  std::vector<std::filesystem::path> removed;

  [[nodiscard]] bool empty() const {
    return changed.empty() && removed.empty();
  }
};

// Platform infrastructure only: observes files and assigns monotonically
// increasing generations. Runtime subsystems decide what a path means.
class ProjectFileWatcher {
public:
  void reset(const std::filesystem::path &projectDirectory);
  [[nodiscard]] ProjectFileChangeBatch poll();

private:
  struct Signature {
    std::filesystem::file_time_type writeTime{};
    std::uintmax_t size = 0;
    bool operator==(const Signature &) const = default;
  };

  [[nodiscard]] std::unordered_map<std::string, Signature> scan() const;

  std::filesystem::path projectDirectory_;
  std::unordered_map<std::string, Signature> snapshot_;
  std::uint64_t generation_ = 0;
};

} // namespace demi::runtime::platform
