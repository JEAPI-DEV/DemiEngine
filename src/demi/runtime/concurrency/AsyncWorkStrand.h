#pragma once

#include "demi/runtime/concurrency/AsyncWorkQueue.h"

#include <atomic>
#include <functional>
#include <memory>

namespace demi::runtime {

// Serializes one native request stream over a shared worker pool. A pending
// request occupies no pool worker until its predecessor has returned. The pool
// owner must outlive the strand and call pool.shutdown() at service teardown;
// strand.shutdown() only settles handles and never joins native jobs.
class AsyncWorkStrand {
public:
  explicit AsyncWorkStrand(std::shared_ptr<AsyncWorkQueue> queue);
  ~AsyncWorkStrand();
  AsyncWorkStrand(const AsyncWorkStrand &) = delete;
  AsyncWorkStrand &operator=(const AsyncWorkStrand &) = delete;

  [[nodiscard]] std::shared_ptr<AsyncWorkOperation>
  submit(std::function<nlohmann::json(const std::atomic<bool> &cancelled)> work);
  // Settles outstanding handles immediately. Already executing native work
  // must observe its cancellation token; the pool owner joins at teardown.
  void shutdown();

private:
  struct State;
  struct Dispatch;
  static void dispatchNext(const std::shared_ptr<State> &state);
  static void finish(const std::shared_ptr<State> &state,
                     const std::shared_ptr<Dispatch> &dispatch);
  static void cancelAll(const std::shared_ptr<State> &state);

  std::shared_ptr<State> state_;
  std::shared_ptr<AsyncWorkQueue> queue_;
};

} // namespace demi::runtime
