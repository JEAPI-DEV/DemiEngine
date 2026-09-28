#include "demi/runtime/concurrency/AsyncCompletion.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace demi::runtime {

struct AsyncCompletion::State {
  std::mutex mutex;
  std::condition_variable_any notification;
  std::unordered_map<std::size_t, std::function<void()>> callbacks;
  std::size_t nextId = 1;
  bool done = false;
};

namespace {
void notifySafely(const std::function<void()> &callback) noexcept {
  try {
    callback();
  } catch (...) {
    // One subscriber must not prevent notification of the others.
  }
}
} // namespace

AsyncCompletion::Subscription::Subscription(std::weak_ptr<State> state,
                                            const std::size_t id) noexcept
    : state_(std::move(state)), id_(id) {}

AsyncCompletion::Subscription::~Subscription() { unsubscribe(); }

AsyncCompletion::Subscription::Subscription(Subscription &&other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}

AsyncCompletion::Subscription &
AsyncCompletion::Subscription::operator=(Subscription &&other) noexcept {
  if (this != &other) {
    unsubscribe();
    state_ = std::move(other.state_);
    id_ = std::exchange(other.id_, 0);
  }
  return *this;
}

void AsyncCompletion::Subscription::unsubscribe() noexcept {
  if (id_ == 0)
    return;
  if (const auto state = state_.lock()) {
    std::scoped_lock lock(state->mutex);
    state->callbacks.erase(id_);
  }
  id_ = 0;
  state_.reset();
}

AsyncCompletion::AsyncCompletion() : state_(std::make_shared<State>()) {}

bool AsyncCompletion::ready() const {
  std::scoped_lock lock(state_->mutex);
  return state_->done;
}

AsyncWaitResult
AsyncCompletion::wait(std::stop_token stopToken,
                      std::optional<std::chrono::milliseconds> timeout) const {
  const auto state = state_;
  std::unique_lock lock(state->mutex);
  const auto ready = [&] { return state->done; };
  if (timeout && *timeout > std::chrono::milliseconds::zero()) {
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    const auto remaining =
        Clock::time_point::max() - std::max(now, Clock::time_point{});
    // Compare in milliseconds before converting to finer clock ticks. Clamp
    // both the conversion and addition, including a clock with a negative
    // epoch.
    const auto deadline =
        *timeout >
                std::chrono::duration_cast<std::chrono::milliseconds>(remaining)
            ? Clock::time_point::max()
            : now + std::chrono::duration_cast<Clock::duration>(*timeout);
    state->notification.wait_until(lock, stopToken, deadline, ready);
  } else if (!timeout) {
    state->notification.wait(lock, stopToken, ready);
  }
  if (state->done)
    return AsyncWaitResult::Ready;
  return stopToken.stop_requested() ? AsyncWaitResult::Cancelled
                                    : AsyncWaitResult::Timeout;
}

AsyncCompletion::Subscription
AsyncCompletion::subscribe(std::function<void()> callback) {
  if (!callback)
    throw std::invalid_argument("A completion callback must be callable.");
  std::size_t id = 0;
  {
    std::scoped_lock lock(state_->mutex);
    if (!state_->done) {
      id = state_->nextId++;
      state_->callbacks.emplace(id, std::move(callback));
    }
  }
  if (id == 0) {
    notifySafely(callback);
    return {};
  }
  return Subscription{state_, id};
}

void AsyncCompletion::complete() {
  std::unordered_map<std::size_t, std::function<void()>> callbacks;
  {
    std::scoped_lock lock(state_->mutex);
    if (state_->done)
      return;
    state_->done = true;
    callbacks.swap(state_->callbacks);
  }
  state_->notification.notify_all();
  for (const auto &[id, callback] : callbacks) {
    static_cast<void>(id);
    notifySafely(callback);
  }
}

} // namespace demi::runtime
