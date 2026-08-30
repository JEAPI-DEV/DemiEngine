#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace demi::runtime {

enum class RuntimeLogSeverity { Info, Warning, Error };

struct RuntimeLogEntry {
  std::uint64_t sequence = 0;
  double elapsedSeconds = 0.0;
  RuntimeLogSeverity severity = RuntimeLogSeverity::Info;
  std::string channel;
  std::string message;
  std::string source;
  int line = 0;
  std::string entityId;
  std::string component;
  std::string field;
};

// Per-runtime bounded log ownership. Snapshots are immutable copies so editor
// presentation never observes a Lua/network callback mutating the buffer.
class RuntimeLogBuffer {
public:
  explicit RuntimeLogBuffer(std::size_t capacity = 512);

  void append(RuntimeLogEntry entry);
  [[nodiscard]] std::vector<RuntimeLogEntry> entries() const;
  void clear();
  [[nodiscard]] std::size_t capacity() const { return capacity_; }

private:
  const std::size_t capacity_;
  const std::chrono::steady_clock::time_point started_ =
      std::chrono::steady_clock::now();
  mutable std::mutex mutex_;
  std::deque<RuntimeLogEntry> entries_;
  std::uint64_t nextSequence_ = 1;
};

} // namespace demi::runtime
