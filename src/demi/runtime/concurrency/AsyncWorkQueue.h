#pragma once

#include "demi/runtime/concurrency/AsyncCompletion.h"
#include "demi/runtime/concurrency/JobSystem.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace demi::runtime {

class AsyncWorkOperation {
public:
  [[nodiscard]] std::shared_ptr<AsyncCompletion> completion() const;
  [[nodiscard]] std::optional<nlohmann::json> result() const;
  [[nodiscard]] std::string error() const;
  void cancel();
  [[nodiscard]] bool cancelled() const noexcept;

private:
  friend class AsyncWorkQueue;
  friend class AsyncWorkStrand;
  AsyncWorkOperation();
  void succeed(nlohmann::json value);
  void fail(std::string message);

  mutable std::mutex mutex_;
  std::shared_ptr<AsyncCompletion> completion_;
  std::optional<nlohmann::json> result_;
  std::string error_;
  std::atomic<bool> cancelled_ = false;
  bool settled_ = false;
};

class AsyncWorkQueue {
public:
  explicit AsyncWorkQueue(std::size_t workerCount = 2);
  ~AsyncWorkQueue();
  AsyncWorkQueue(const AsyncWorkQueue &) = delete;
  AsyncWorkQueue &operator=(const AsyncWorkQueue &) = delete;

  [[nodiscard]] std::shared_ptr<AsyncWorkOperation>
  submit(std::function<nlohmann::json(const std::atomic<bool> &cancelled)> work);
  // Cancels outstanding handles immediately, then joins workers. A native job
  // that ignores its cancellation token can make joining take arbitrarily long.
  void shutdown();

private:
  struct Outstanding;
  std::mutex mutex_;
  std::size_t workerCount_;
  std::unique_ptr<JobSystem> jobs_;
  std::shared_ptr<Outstanding> outstanding_;
  std::size_t nextOperationId_ = 1;
  bool stopping_ = false;
};

} // namespace demi::runtime
