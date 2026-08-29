#include "demi/runtime/diagnostics/RuntimeLog.h"

#include <algorithm>

namespace demi::runtime {

RuntimeLogBuffer::RuntimeLogBuffer(const std::size_t capacity)
    : capacity_(std::max<std::size_t>(capacity, 1)) {}

void RuntimeLogBuffer::append(RuntimeLogEntry entry) {
  if (entry.message.empty())
    return;
  constexpr std::size_t MaximumMessageBytes = 16 * 1024;
  if (entry.message.size() > MaximumMessageBytes)
    entry.message.resize(MaximumMessageBytes);
  entry.elapsedSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started_)
          .count();
  std::scoped_lock lock(mutex_);
  entry.sequence = nextSequence_++;
  entries_.push_back(std::move(entry));
  while (entries_.size() > capacity_)
    entries_.pop_front();
}

std::vector<RuntimeLogEntry> RuntimeLogBuffer::entries() const {
  std::scoped_lock lock(mutex_);
  return {entries_.begin(), entries_.end()};
}

void RuntimeLogBuffer::clear() {
  std::scoped_lock lock(mutex_);
  entries_.clear();
}

} // namespace demi::runtime
