#include "demi/runtime/concurrency/AsyncWorkStrand.h"

#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace demi::runtime {

struct AsyncWorkStrand::State {
  struct Item {
    std::shared_ptr<AsyncWorkOperation> operation;
    std::function<nlohmann::json(const std::atomic<bool> &)> work;
  };

  explicit State(const std::shared_ptr<AsyncWorkQueue> &sharedQueue)
      : queue(sharedQueue) {}

  std::mutex mutex;
  std::weak_ptr<AsyncWorkQueue> queue;
  std::deque<Item> pending;
  std::shared_ptr<Dispatch> active;
  bool stopping = false;
};

struct AsyncWorkStrand::Dispatch {
  explicit Dispatch(State::Item next) : item(std::move(next)) {}

  State::Item item;
  AsyncCompletion::Subscription poolCompletion;
};

AsyncWorkStrand::AsyncWorkStrand(std::shared_ptr<AsyncWorkQueue> queue) {
  if (!queue)
    throw std::invalid_argument("An asynchronous strand needs a work queue.");
  state_ = std::make_shared<State>(queue);
  queue_ = std::move(queue);
}

AsyncWorkStrand::~AsyncWorkStrand() { shutdown(); }

std::shared_ptr<AsyncWorkOperation> AsyncWorkStrand::submit(
    std::function<nlohmann::json(const std::atomic<bool> &cancelled)> work) {
  if (!work)
    throw std::invalid_argument("An asynchronous job must be callable.");
  auto operation = std::shared_ptr<AsyncWorkOperation>(new AsyncWorkOperation());
  {
    std::scoped_lock lock(state_->mutex);
    if (state_->stopping)
      throw std::runtime_error("Cannot submit work to a stopped strand.");
    state_->pending.push_back({operation, std::move(work)});
  }
  dispatchNext(state_);
  return operation;
}

void AsyncWorkStrand::dispatchNext(const std::shared_ptr<State> &state) {
  while (true) {
    std::shared_ptr<Dispatch> dispatch;
    {
      std::scoped_lock lock(state->mutex);
      if (state->stopping || state->active)
        return;
      while (!state->pending.empty() &&
             state->pending.front().operation->cancelled())
        state->pending.pop_front();
      if (state->pending.empty())
        return;
      dispatch = std::make_shared<Dispatch>(std::move(state->pending.front()));
      state->pending.pop_front();
      state->active = dispatch;
    }

    try {
      const auto queue = state->queue.lock();
      if (!queue)
        throw std::runtime_error("The asynchronous work pool is unavailable.");
      const auto poolOperation = queue->submit(
          [state, dispatch](const std::atomic<bool> &) {
            const auto &operation = dispatch->item.operation;
            if (!operation->cancelled()) {
              try {
                operation->succeed(dispatch->item.work(operation->cancelled_));
              } catch (const std::exception &exception) {
                operation->fail(exception.what());
              } catch (...) {
                operation->fail("native job failed");
              }
            }
            finish(state, dispatch);
            return nlohmann::json();
          });
      dispatch->poolCompletion = poolOperation->completion()->subscribe(
          [weakState = std::weak_ptr<State>(state),
           weakOperation = std::weak_ptr<AsyncWorkOperation>(poolOperation)] {
            const auto poolOperation = weakOperation.lock();
            if (poolOperation && poolOperation->cancelled())
              if (const auto state = weakState.lock())
                cancelAll(state);
          });
      return;
    } catch (const std::exception &exception) {
      // Pool rejection must settle this handle and preserve FIFO progress.
      dispatch->item.operation->fail(exception.what());
    } catch (...) {
      dispatch->item.operation->fail("native job dispatch failed");
    }
    {
      std::scoped_lock lock(state->mutex);
      if (state->active == dispatch)
        state->active.reset();
    }
  }
}

void AsyncWorkStrand::finish(const std::shared_ptr<State> &state,
                             const std::shared_ptr<Dispatch> &dispatch) {
  {
    std::scoped_lock lock(state->mutex);
    if (state->active == dispatch)
      state->active.reset();
  }
  dispatchNext(state);
}

void AsyncWorkStrand::cancelAll(const std::shared_ptr<State> &state) {
  std::deque<State::Item> pending;
  std::shared_ptr<AsyncWorkOperation> active;
  {
    std::scoped_lock lock(state->mutex);
    if (state->stopping)
      return;
    state->stopping = true;
    pending = std::move(state->pending);
    if (state->active)
      active = state->active->item.operation;
  }
  for (const auto &item : pending)
    item.operation->cancel();
  if (active)
    active->cancel();
}

void AsyncWorkStrand::shutdown() { cancelAll(state_); }

} // namespace demi::runtime
