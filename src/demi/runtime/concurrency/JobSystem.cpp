#include "demi/runtime/concurrency/JobSystem.h"

#include <algorithm>
#include <stdexcept>

namespace demi::runtime {

void JobHandle::wait() const {
  if (state_ == nullptr)
    return;
  std::unique_lock lock(state_->mutex);
  state_->completed.wait(lock, [this] { return state_->done; });
  if (state_->error)
    std::rethrow_exception(state_->error);
}

bool JobHandle::ready() const {
  if (state_ == nullptr)
    return true;
  std::scoped_lock lock(state_->mutex);
  return state_->done;
}

std::size_t JobSystem::defaultWorkerCount() {
  const unsigned available = std::thread::hardware_concurrency();
  return available > 1
             ? std::min<std::size_t>(static_cast<std::size_t>(available - 1),
                                     8)
             : 0;
}

JobSystem::JobSystem(const std::size_t workerCount) {
  workers_.reserve(workerCount);
  for (std::size_t index = 0; index < workerCount; ++index)
    workers_.emplace_back([this] { workerLoop(); });
}

JobSystem::~JobSystem() { shutdown(); }

JobHandle JobSystem::submit(std::function<void()> job) {
  if (!job)
    throw std::invalid_argument("A job must contain callable work.");
  auto state = std::make_shared<JobHandle::State>();
  WorkItem item{.job = std::move(job), .state = state};
  bool runInline = false;
  {
    std::scoped_lock lock(mutex_);
    if (stopping_)
      throw std::runtime_error("Cannot submit work to a stopped job system.");
    runInline = workers_.empty();
    if (!runInline)
      queue_.push_back(std::move(item));
  }
  if (runInline) {
    run(std::move(item));
    return JobHandle{std::move(state)};
  }
  available_.notify_one();
  return JobHandle{std::move(state)};
}

void JobSystem::shutdown() {
  {
    std::scoped_lock lock(mutex_);
    if (stopping_ && workers_.empty())
      return;
    stopping_ = true;
  }
  available_.notify_all();
  for (std::thread &worker : workers_)
    if (worker.joinable())
      worker.join();
  workers_.clear();
}

void JobSystem::workerLoop() {
  while (true) {
    WorkItem item;
    {
      std::unique_lock lock(mutex_);
      available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) {
        if (stopping_)
          return;
        continue;
      }
      item = std::move(queue_.front());
      queue_.pop_front();
    }
    run(std::move(item));
  }
}

void JobSystem::run(WorkItem item) {
  try {
    item.job();
  } catch (...) {
    std::scoped_lock lock(item.state->mutex);
    item.state->error = std::current_exception();
  }
  {
    std::scoped_lock lock(item.state->mutex);
    item.state->done = true;
  }
  item.state->completed.notify_all();
}

} // namespace demi::runtime
