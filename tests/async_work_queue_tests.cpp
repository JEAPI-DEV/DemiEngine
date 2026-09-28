#include "demi/runtime/concurrency/AsyncCompletion.h"
#include "demi/runtime/concurrency/AsyncWorkQueue.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

using demi::runtime::AsyncCompletion;
using demi::runtime::AsyncWaitResult;
using demi::runtime::AsyncWorkQueue;
using namespace std::chrono_literals;

namespace {

template <typename T> void finishesSoon(const std::future<T> &future) {
  assert(future.wait_for(2s) == std::future_status::ready);
}

void completionSubscriptions() {
  AsyncCompletion completion;
  std::atomic<int> calls = 0;
  auto retained = completion.subscribe([&] { ++calls; });
  {
    auto removed = completion.subscribe([&] { ++calls; });
  }
  auto throwing = completion.subscribe([] { throw std::runtime_error("callback"); });
  completion.complete();
  completion.complete();
  assert(completion.ready());
  assert(calls == 1);
  auto late = completion.subscribe([&] { ++calls; });
  assert(calls == 2);
  retained.unsubscribe();
  throwing.unsubscribe();
  late.unsubscribe();

  AsyncCompletion::Subscription orphan;
  {
    auto temporary = std::make_shared<AsyncCompletion>();
    orphan = temporary->subscribe([] {});
  }
  orphan.unsubscribe();

  AsyncCompletion reentrant;
  auto reentrantSubscription = reentrant.subscribe([&] {
    assert(reentrant.ready());
    reentrant.complete();
  });
  reentrant.complete();
}

void subscribeCompleteRace() {
  for (int iteration = 0; iteration < 200; ++iteration) {
    AsyncCompletion completion;
    std::atomic<int> calls = 0;
    std::atomic<bool> go = false;
    std::thread finisher([&] {
      while (!go.load())
        std::this_thread::yield();
      completion.complete();
    });
    go = true;
    auto subscription = completion.subscribe([&] { ++calls; });
    finisher.join();
    assert(calls == 1);
    assert(completion.ready());
  }
}

void completionWaits() {
  AsyncCompletion completion;
  assert(completion.wait({}, 0ms) == AsyncWaitResult::Timeout);
  assert(completion.wait({}, -1ms) == AsyncWaitResult::Timeout);
  assert(completion.wait({}, std::chrono::milliseconds::min()) ==
         AsyncWaitResult::Timeout);
  assert(completion.wait({}, 10ms) == AsyncWaitResult::Timeout);
  std::stop_source stopped;
  stopped.request_stop();
  assert(completion.wait(stopped.get_token()) == AsyncWaitResult::Cancelled);
  assert(!completion.ready());
  completion.complete();
  assert(completion.wait() == AsyncWaitResult::Ready);
  assert(completion.wait(stopped.get_token(), 0ms) == AsyncWaitResult::Ready);

  for (int iteration = 0; iteration < 50; ++iteration) {
    AsyncCompletion pending;
    std::stop_source stop;
    auto cancelled = std::async(std::launch::async,
                                [&] { return pending.wait(stop.get_token()); });
    auto timedCancelled = std::async(std::launch::async, [&] {
      return pending.wait(stop.get_token(), 30s);
    });
    auto ready = std::async(std::launch::async, [&] { return pending.wait(); });
    stop.request_stop();
    finishesSoon(cancelled);
    finishesSoon(timedCancelled);
    assert(cancelled.get() == AsyncWaitResult::Cancelled);
    assert(timedCancelled.get() == AsyncWaitResult::Cancelled);
    assert(!pending.ready());
    pending.complete();
    finishesSoon(ready);
    assert(ready.get() == AsyncWaitResult::Ready);
  }

  AsyncCompletion pending;
  std::vector<std::future<AsyncWaitResult>> waiters;
  for (int index = 0; index < 8; ++index) {
    waiters.push_back(
        std::async(std::launch::async, [&] { return pending.wait({}, 30s); }));
  }
  // Completion must wake waiters before running potentially slow subscribers.
  auto subscription = pending.subscribe([&] {
    for (auto &waiter : waiters) {
      finishesSoon(waiter);
      assert(waiter.get() == AsyncWaitResult::Ready);
    }
  });
  pending.complete();
}

void hugeCompletionTimeouts() {
  for (const bool cancel : {false, true}) {
    AsyncCompletion completion;
    std::stop_source stop;
    std::promise<void> started;
    auto startedFuture = started.get_future();
    auto waiter = std::async(std::launch::async, [&] {
      started.set_value();
      return completion.wait(stop.get_token(), std::chrono::milliseconds::max());
    });
    finishesSoon(startedFuture);
    // Overflow must not turn a huge timeout into an already-expired deadline.
    assert(waiter.wait_for(20ms) == std::future_status::timeout);
    if (cancel)
      stop.request_stop();
    else
      completion.complete();
    finishesSoon(waiter);
    assert(waiter.get() ==
           (cancel ? AsyncWaitResult::Cancelled : AsyncWaitResult::Ready));
  }
}

void resultsAndWorkerReuse() {
  bool rejectedZeroWorkers = false;
  try {
    AsyncWorkQueue invalid(0);
  } catch (const std::invalid_argument &) {
    rejectedZeroWorkers = true;
  }
  assert(rejectedZeroWorkers);

  AsyncWorkQueue queue;
  std::mutex threadsMutex;
  std::set<std::thread::id> threads;
  std::vector<std::shared_ptr<demi::runtime::AsyncWorkOperation>> operations;
  for (int index = 0; index < 64; ++index) {
    operations.push_back(queue.submit([&, index](const std::atomic<bool> &) {
      std::scoped_lock lock(threadsMutex);
      threads.insert(std::this_thread::get_id());
      return nlohmann::json(index);
    }));
  }
  std::vector<AsyncCompletion::Subscription> subscriptions;
  std::vector<std::future<void>> done;
  for (const auto &operation : operations) {
    auto promise = std::make_shared<std::promise<void>>();
    done.push_back(promise->get_future());
    subscriptions.push_back(
        operation->completion()->subscribe([promise] { promise->set_value(); }));
  }
  for (int index = 0; index < 64; ++index) {
    finishesSoon(done[index]);
    assert(operations[index]->result() == nlohmann::json(index));
    assert(operations[index]->error().empty());
  }
  assert(!threads.empty() && threads.size() <= 2);
  queue.shutdown();
  queue.shutdown();
  bool rejected = false;
  try {
    static_cast<void>(queue.submit([](const std::atomic<bool> &) {
      return nlohmann::json();
    }));
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
}

void cancellationAndExceptions() {
  AsyncWorkQueue queue(1);
  auto started = std::make_shared<std::promise<void>>();
  auto startedFuture = started->get_future();
  auto active = queue.submit([started](const std::atomic<bool> &cancelled) {
    started->set_value();
    while (!cancelled.load())
      std::this_thread::yield();
    return nlohmann::json("ignored");
  });
  finishesSoon(startedFuture);
  std::atomic<bool> queuedRan = false;
  auto queued = queue.submit([&](const std::atomic<bool> &) {
    queuedRan = true;
    return nlohmann::json(9);
  });
  active->cancel();
  queued->cancel();
  assert(active->completion()->ready() && queued->completion()->ready());
  assert(active->cancelled() && queued->cancelled());
  assert(!active->result() && !queued->result());
  assert(active->error() == "cancelled" && queued->error() == "cancelled");
  queue.shutdown();
  assert(!queuedRan);

  AsyncWorkQueue failures(1);
  auto failed = failures.submit([](const std::atomic<bool> &) -> nlohmann::json {
    throw std::runtime_error("native failure");
  });
  auto promise = std::make_shared<std::promise<void>>();
  auto future = promise->get_future();
  auto subscription =
      failed->completion()->subscribe([promise] { promise->set_value(); });
  finishesSoon(future);
  assert(failed->error() == "native failure");
  assert(!failed->result());

  auto unknownFailure = failures.submit([](const std::atomic<bool> &)
                                            -> nlohmann::json { throw 7; });
  auto unknownPromise = std::make_shared<std::promise<void>>();
  auto unknownFuture = unknownPromise->get_future();
  auto unknownSubscription = unknownFailure->completion()->subscribe(
      [unknownPromise] { unknownPromise->set_value(); });
  finishesSoon(unknownFuture);
  assert(unknownFailure->error() == "native job failed");
}

void shutdownAndHandleLifetime() {
  std::shared_ptr<demi::runtime::AsyncWorkOperation> handle;
  {
    AsyncWorkQueue queue(1);
    auto started = std::make_shared<std::promise<void>>();
    auto startedFuture = started->get_future();
    handle = queue.submit([started](const std::atomic<bool> &cancelled) {
      started->set_value();
      while (!cancelled.load())
        std::this_thread::yield();
      return nlohmann::json(42);
    });
    finishesSoon(startedFuture);
  }
  assert(handle->completion()->ready());
  assert(handle->cancelled());
  assert(handle->error() == "cancelled");
  handle->cancel();
  assert(!handle->result());
}

} // namespace

int main() {
  completionSubscriptions();
  subscribeCompleteRace();
  completionWaits();
  hugeCompletionTimeouts();
  resultsAndWorkerReuse();
  cancellationAndExceptions();
  shutdownAndHandleLifetime();
  return 0;
}
