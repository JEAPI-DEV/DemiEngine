#pragma once

#include <condition_variable>
#include <algorithm>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace demi::runtime {

// A small, engine-owned worker pool for CPU-only work. Jobs must not call Lua,
// mutate the World, or submit GPU commands. Store results by stable index and
// apply them on the main thread after wait() to keep gameplay deterministic.
class JobHandle {
public:
  JobHandle() = default;

  void wait() const;
  [[nodiscard]] bool ready() const;
  [[nodiscard]] explicit operator bool() const { return state_ != nullptr; }

private:
  struct State {
    mutable std::mutex mutex;
    std::condition_variable completed;
    std::exception_ptr error;
    bool done = false;
  };

  explicit JobHandle(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
  friend class JobSystem;
};

class JobSystem {
public:
  // Leaves one hardware thread for the main/render thread by default.
  explicit JobSystem(std::size_t workerCount = defaultWorkerCount());
  ~JobSystem();

  JobSystem(const JobSystem &) = delete;
  JobSystem &operator=(const JobSystem &) = delete;

  [[nodiscard]] JobHandle submit(std::function<void()> job);
  void shutdown();

  [[nodiscard]] std::size_t workerCount() const { return workers_.size(); }
  [[nodiscard]] static std::size_t defaultWorkerCount();

  // Divides an indexed range into bounded chunks. The caller participates in
  // the work and waits before returning, so indexed output remains ordered.
  template <typename Function>
  void parallelFor(const std::size_t count, const std::size_t minimumGrain,
                   Function &&function) {
    if (count == 0)
      return;
    const std::size_t grain = minimumGrain == 0 ? 1 : minimumGrain;
    const std::size_t chunkCount =
        std::min((count + grain - 1) / grain, workers_.size() + 1);
    if (chunkCount <= 1) {
      for (std::size_t index = 0; index < count; ++index)
        std::invoke(function, index);
      return;
    }

    const std::size_t chunkSize = (count + chunkCount - 1) / chunkCount;
    std::vector<JobHandle> handles;
    handles.reserve(chunkCount - 1);
    for (std::size_t chunk = 1; chunk < chunkCount; ++chunk) {
      const std::size_t begin = chunk * chunkSize;
      const std::size_t end = std::min(begin + chunkSize, count);
      if (begin >= end)
        break;
      handles.push_back(submit([begin, end, &function] {
        for (std::size_t index = begin; index < end; ++index)
          std::invoke(function, index);
      }));
    }

    std::exception_ptr callerError;
    try {
      const std::size_t callerEnd = std::min(chunkSize, count);
      for (std::size_t index = 0; index < callerEnd; ++index)
        std::invoke(function, index);
    } catch (...) {
      callerError = std::current_exception();
    }
    std::exception_ptr workerError;
    for (const JobHandle &handle : handles) {
      try {
        handle.wait();
      } catch (...) {
        if (!workerError)
          workerError = std::current_exception();
      }
    }
    if (callerError)
      std::rethrow_exception(callerError);
    if (workerError)
      std::rethrow_exception(workerError);
  }

private:
  struct WorkItem {
    std::function<void()> job;
    std::shared_ptr<JobHandle::State> state;
  };

  void workerLoop();
  static void run(WorkItem item);

  std::mutex mutex_;
  std::condition_variable available_;
  std::deque<WorkItem> queue_;
  std::vector<std::thread> workers_;
  bool stopping_ = false;
};

} // namespace demi::runtime
