#include "demi/runtime/concurrency/AsyncWorkQueue.h"

#include <exception>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace demi::runtime {

struct AsyncWorkQueue::Outstanding {
  struct Entry {
    std::shared_ptr<AsyncWorkOperation> operation;
    AsyncCompletion::Subscription subscription;
  };

  std::mutex mutex;
  std::unordered_map<std::size_t, Entry> entries;
};

AsyncWorkOperation::AsyncWorkOperation()
    : completion_(std::make_shared<AsyncCompletion>()) {}

std::shared_ptr<AsyncCompletion> AsyncWorkOperation::completion() const {
  return completion_;
}

std::optional<nlohmann::json> AsyncWorkOperation::result() const {
  std::scoped_lock lock(mutex_);
  return result_;
}

std::string AsyncWorkOperation::error() const {
  std::scoped_lock lock(mutex_);
  return error_;
}

bool AsyncWorkOperation::cancelled() const noexcept { return cancelled_.load(); }

void AsyncWorkOperation::cancel() {
  {
    std::scoped_lock lock(mutex_);
    if (settled_)
      return;
    cancelled_.store(true);
    settled_ = true;
    error_ = "cancelled";
  }
  completion_->complete();
}

void AsyncWorkOperation::succeed(nlohmann::json value) {
  {
    std::scoped_lock lock(mutex_);
    if (settled_)
      return;
    result_ = std::move(value);
    settled_ = true;
  }
  completion_->complete();
}

void AsyncWorkOperation::fail(std::string message) {
  {
    std::scoped_lock lock(mutex_);
    if (settled_)
      return;
    error_ = std::move(message);
    settled_ = true;
  }
  completion_->complete();
}

AsyncWorkQueue::AsyncWorkQueue(const std::size_t workerCount)
    : workerCount_(workerCount), outstanding_(std::make_shared<Outstanding>()) {
  if (workerCount == 0)
    throw std::invalid_argument("An asynchronous work queue needs a worker.");
}

AsyncWorkQueue::~AsyncWorkQueue() { shutdown(); }

std::shared_ptr<AsyncWorkOperation> AsyncWorkQueue::submit(
    std::function<nlohmann::json(const std::atomic<bool> &cancelled)> work) {
  if (!work)
    throw std::invalid_argument("An asynchronous job must be callable.");
  auto operation = std::shared_ptr<AsyncWorkOperation>(new AsyncWorkOperation());
  {
    std::scoped_lock lock(mutex_);
    if (stopping_)
      throw std::runtime_error("Cannot submit work to a stopped queue.");
    if (!jobs_)
      jobs_ = std::make_unique<JobSystem>(workerCount_);
    const std::size_t id = nextOperationId_++;
    auto subscription = operation->completion()->subscribe(
        [registry = std::weak_ptr<Outstanding>(outstanding_), id] {
          if (const auto entries = registry.lock()) {
            std::scoped_lock lock(entries->mutex);
            entries->entries.erase(id);
          }
        });
    {
      std::scoped_lock entriesLock(outstanding_->mutex);
      outstanding_->entries.emplace(
          id, Outstanding::Entry{operation, std::move(subscription)});
    }
    try {
      static_cast<void>(jobs_->submit([operation, work = std::move(work)] {
        if (operation->cancelled())
          return;
        try {
          operation->succeed(work(operation->cancelled_));
        } catch (const std::exception &exception) {
          operation->fail(exception.what());
        } catch (...) {
          operation->fail("native job failed");
        }
      }));
    } catch (...) {
      std::scoped_lock entriesLock(outstanding_->mutex);
      outstanding_->entries.erase(id);
      throw;
    }
  }
  return operation;
}

void AsyncWorkQueue::shutdown() {
  std::unique_ptr<JobSystem> jobs;
  std::unordered_map<std::size_t, Outstanding::Entry> operations;
  {
    std::scoped_lock lock(mutex_);
    if (stopping_)
      return;
    stopping_ = true;
    jobs = std::move(jobs_);
    std::scoped_lock entriesLock(outstanding_->mutex);
    operations.swap(outstanding_->entries);
  }
  for (const auto &[id, entry] : operations) {
    static_cast<void>(id);
    entry.operation->cancel();
  }
  if (jobs)
    jobs->shutdown();
}

} // namespace demi::runtime
