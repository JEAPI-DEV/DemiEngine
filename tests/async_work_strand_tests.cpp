#include "demi/runtime/concurrency/AsyncWorkStrand.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using demi::runtime::AsyncCompletion;
using demi::runtime::AsyncWorkOperation;
using demi::runtime::AsyncWorkQueue;
using demi::runtime::AsyncWorkStrand;
using namespace std::chrono_literals;

namespace {

void completesSoon(const std::shared_ptr<AsyncWorkOperation> &operation) {
  auto signal = std::make_shared<std::promise<void>>();
  auto future = signal->get_future();
  auto subscription =
      operation->completion()->subscribe([signal] { signal->set_value(); });
  assert(future.wait_for(2s) == std::future_status::ready);
}

void fifoAndIndependentStrands() {
  auto pool = std::make_shared<AsyncWorkQueue>(2);
  AsyncWorkStrand first(pool);
  AsyncWorkStrand second(pool);
  auto started = std::make_shared<std::promise<void>>();
  auto startedFuture = started->get_future();
  auto release = std::make_shared<std::promise<void>>();
  auto releaseFuture = release->get_future().share();
  std::mutex orderMutex;
  std::vector<int> order;
  auto leading = first.submit([&, started, releaseFuture](const std::atomic<bool> &) {
    {
      std::scoped_lock lock(orderMutex);
      order.push_back(1);
    }
    started->set_value();
    releaseFuture.wait();
    return nlohmann::json(1);
  });
  assert(startedFuture.wait_for(2s) == std::future_status::ready);

  auto cancelled = first.submit([&](const std::atomic<bool> &) {
    std::scoped_lock lock(orderMutex);
    order.push_back(-1);
    return nlohmann::json(-1);
  });
  auto middle = first.submit([&](const std::atomic<bool> &) {
    std::scoped_lock lock(orderMutex);
    order.push_back(2);
    return nlohmann::json(2);
  });
  auto last = first.submit([&](const std::atomic<bool> &) {
    std::scoped_lock lock(orderMutex);
    order.push_back(3);
    return nlohmann::json(3);
  });
  cancelled->cancel();
  assert(cancelled->completion()->ready());
  assert(!middle->completion()->ready());

  // A backlog on the first strand must leave the other pool worker free.
  auto independent = second.submit([](const std::atomic<bool> &) {
    return nlohmann::json("other strand");
  });
  completesSoon(independent);
  assert(independent->result() == nlohmann::json("other strand"));

  release->set_value();
  completesSoon(leading);
  completesSoon(middle);
  completesSoon(last);
  assert((order == std::vector<int>{1, 2, 3}));
  assert(cancelled->error() == "cancelled");
  pool->shutdown();
}

void failureDoesNotBlockFollowingWork() {
  auto pool = std::make_shared<AsyncWorkQueue>(1);
  AsyncWorkStrand strand(pool);
  auto failure = strand.submit([](const std::atomic<bool> &)
                                   -> nlohmann::json {
    throw std::runtime_error("database error");
  });
  auto following = strand.submit([](const std::atomic<bool> &) {
    return nlohmann::json(17);
  });
  completesSoon(failure);
  completesSoon(following);
  assert(failure->error() == "database error");
  assert(following->result() == nlohmann::json(17));
  pool->shutdown();
}

void shutdownIsLogicalAndHandleOutlivesStrand() {
  auto pool = std::make_shared<AsyncWorkQueue>(1);
  auto started = std::make_shared<std::promise<void>>();
  auto startedFuture = started->get_future();
  auto release = std::make_shared<std::promise<void>>();
  auto releaseFuture = release->get_future().share();
  std::shared_ptr<AsyncWorkOperation> active;
  std::shared_ptr<AsyncWorkOperation> pending;
  {
    AsyncWorkStrand strand(pool);
    active = strand.submit([started, releaseFuture](const std::atomic<bool> &) {
      started->set_value();
      releaseFuture.wait();
      return nlohmann::json("late result");
    });
    assert(startedFuture.wait_for(2s) == std::future_status::ready);
    std::atomic<bool> pendingRan = false;
    pending = strand.submit([&](const std::atomic<bool> &) {
      pendingRan = true;
      return nlohmann::json();
    });
    strand.shutdown();
    strand.shutdown();
    assert(active->completion()->ready());
    assert(pending->completion()->ready());
    assert(!pendingRan);
    bool rejected = false;
    try {
      static_cast<void>(strand.submit([](const std::atomic<bool> &) {
        return nlohmann::json();
      }));
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    assert(rejected);
  }
  assert(active->cancelled() && pending->cancelled());
  assert(active->error() == "cancelled");
  release->set_value();
  pool->shutdown();
  assert(!active->result());
}

void stoppedPoolSettlesDispatchFailure() {
  auto pool = std::make_shared<AsyncWorkQueue>(1);
  pool->shutdown();
  AsyncWorkStrand strand(pool);
  auto rejected = strand.submit([](const std::atomic<bool> &) {
    return nlohmann::json(1);
  });
  completesSoon(rejected);
  assert(!rejected->error().empty());
  assert(!rejected->result());
}

void poolShutdownSettlesStrandWithoutStrandShutdown() {
  auto pool = std::make_shared<AsyncWorkQueue>(1);
  AsyncWorkStrand strand(pool);
  auto started = std::make_shared<std::promise<void>>();
  auto startedFuture = started->get_future();
  auto release = std::make_shared<std::promise<void>>();
  auto releaseFuture = release->get_future().share();
  auto active = strand.submit([started, releaseFuture](const std::atomic<bool> &) {
    started->set_value();
    releaseFuture.wait();
    return nlohmann::json("too late");
  });
  assert(startedFuture.wait_for(2s) == std::future_status::ready);
  std::atomic<bool> pendingRan = false;
  auto pending = strand.submit([&](const std::atomic<bool> &) {
    pendingRan = true;
    return nlohmann::json(2);
  });

  std::thread shuttingDown([pool] { pool->shutdown(); });
  // The pool cancels its operation before joining the blocked native worker.
  completesSoon(active);
  completesSoon(pending);
  assert(active->cancelled() && pending->cancelled());
  assert(active->error() == "cancelled");
  assert(!pendingRan);
  release->set_value();
  shuttingDown.join();
  assert(!active->result());
}

} // namespace

int main() {
  fifoAndIndependentStrands();
  failureDoesNotBlockFollowingWork();
  shutdownIsLogicalAndHandleOutlivesStrand();
  stoppedPoolSettlesDispatchFailure();
  poolShutdownSettlesStrandWithoutStrandShutdown();
  return 0;
}
