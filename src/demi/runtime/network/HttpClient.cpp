#include "demi/runtime/network/HttpClient.h"
#include "demi/runtime/network/HttpTransfer.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

#if !defined(_WIN32)
#include <pthread.h>
#include <signal.h>
#endif

namespace demi::runtime {
namespace {

struct WorkerWakeup {
  std::mutex mutex;
  CURLM* multi = nullptr;

  void wake() {
    // Serialize wakeup against cleanup, including handles outliving the client.
    std::lock_guard lock(mutex);
    if (multi) {
      (void)curl_multi_wakeup(multi);
    }
  }
};

HttpResponse cancelledResponse() {
  return http::failure("cancelled", "HTTP request cancelled");
}

} // namespace

struct HttpOperation::Impl {
  mutable std::mutex mutex;
  std::optional<HttpResponse> result;
  std::atomic<bool> cancelled = false;
  std::weak_ptr<WorkerWakeup> wakeup;

  void complete(HttpResponse response) {
    std::lock_guard lock(mutex);
    if (!result) {
      result = std::move(response);
    }
  }
};

HttpOperation::HttpOperation(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
HttpOperation::~HttpOperation() = default;

bool HttpOperation::done() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->result.has_value();
}

std::optional<HttpResponse> HttpOperation::response() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->result;
}

void HttpOperation::cancel() {
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->result) {
      return;
    }
    impl_->cancelled.store(true, std::memory_order_relaxed);
    impl_->result = cancelledResponse();
  }
  if (const auto wakeup = impl_->wakeup.lock()) {
    wakeup->wake();
  }
}

struct HttpClient::Impl {
  struct Pending {
    HttpRequest request;
    std::shared_ptr<HttpOperation::Impl> operation;
  };
  struct Active {
    std::shared_ptr<HttpOperation::Impl> operation;
    std::unique_ptr<http::Transfer> transfer;
  };

  std::shared_ptr<const http::CurlRuntime> runtime;
  std::shared_ptr<WorkerWakeup> wakeup = std::make_shared<WorkerWakeup>();
  std::mutex mutex;
  std::mutex shutdownMutex;
  std::deque<Pending> pending;
  std::atomic<bool> stopping = false;
  std::thread worker;
  CURLM* multi = nullptr;
  // Worker-owned after startup. The caller only publishes pending requests.
  std::unordered_map<CURL*, Active> active;

  bool start(std::string& error) {
    if (worker.joinable()) {
      return true;
    }
    multi = curl_multi_init();
    if (!multi) {
      error = "HTTP worker initialization failed";
      return false;
    }
    {
      std::lock_guard lock(wakeup->mutex);
      wakeup->multi = multi;
    }
    try {
      worker = std::thread([this] {
        run();
      });
      return true;
    } catch (...) {
      cleanupMulti();
      error = "HTTP worker could not start";
      return false;
    }
  }

  void cleanupMulti() {
    std::lock_guard lock(wakeup->mutex);
    wakeup->multi = nullptr;
    if (multi) {
      curl_multi_cleanup(multi);
    }
    multi = nullptr;
  }

  void admitPending() {
    // Pop individually so shutdown can interrupt even a large submission queue.
    // Snapshot its length so continuous submissions cannot starve active work.
    std::size_t count;
    {
      std::lock_guard lock(mutex);
      count = pending.size();
    }
    while (count-- && !stopping.load(std::memory_order_relaxed)) {
      Pending item;
      {
        std::lock_guard lock(mutex);
        if (pending.empty()) {
          break;
        }
        item = std::move(pending.front());
        pending.pop_front();
      }
      if (item.operation->cancelled.load(std::memory_order_relaxed)) {
        continue;
      }
      // Retain operation ownership through allocation/configuration failures.
      try {
        auto transfer = std::make_unique<http::Transfer>(std::move(item.request), item.operation->cancelled, stopping);
        const auto result = transfer->configure();
        if (result != CURLE_OK) {
          item.operation->complete(transfer->finish(result));
          continue;
        }
        auto* easy = transfer->handle();
        active.emplace(easy, Active{item.operation, std::move(transfer)});
        if (curl_multi_add_handle(multi, easy) != CURLM_OK) {
          item.operation->complete(http::failure("internal", "HTTP transfer could not start"));
          active.erase(easy);
        }
      } catch (...) {
        item.operation->complete(http::failure("internal", "HTTP transfer storage failed"));
      }
    }
  }

  void removeCancelled() {
    for (auto it = active.begin(); it != active.end();) {
      if (!it->second.operation->cancelled.load(std::memory_order_relaxed)) {
        ++it;
        continue;
      }
      (void)curl_multi_remove_handle(multi, it->first);
      it = active.erase(it);
    }
  }

  void collectCompleted() {
    int remaining = 0;
    while (auto* message = curl_multi_info_read(multi, &remaining)) {
      if (message->msg != CURLMSG_DONE) {
        continue;
      }
      const auto it = active.find(message->easy_handle);
      if (it == active.end()) {
        continue;
      }
      auto response = it->second.transfer->finish(message->data.result);
      (void)curl_multi_remove_handle(multi, message->easy_handle);
      it->second.operation->complete(std::move(response));
      active.erase(it);
    }
  }

  void finishPending(const bool failed) {
    std::deque<Pending> queued;
    {
      std::lock_guard lock(mutex);
      stopping.store(true, std::memory_order_relaxed);
      queued.swap(pending);
    }
    const auto response = [failed] {
      return failed ? http::failure("internal", "HTTP worker failed") : cancelledResponse();
    };
    for (auto& item : queued) {
      item.operation->complete(response());
    }
    for (auto& [easy, item] : active) {
      (void)curl_multi_remove_handle(multi, easy);
      item.operation->complete(response());
    }
    active.clear();
  }

  void run() {
    bool failed = false;
#if !defined(_WIN32)
    // NOSIGNAL avoids process-wide signal-handler races. Blocking SIGPIPE only
    // on this dedicated thread also covers TLS backend write corner cases.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    failed = pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0;
#endif
    try {
      while (!failed && !stopping.load(std::memory_order_relaxed)) {
        admitPending();
        removeCancelled();
        if (stopping.load(std::memory_order_relaxed)) {
          break;
        }
        int running = 0;
        if (curl_multi_perform(multi, &running) != CURLM_OK) {
          failed = true;
          break;
        }
        collectCompleted();
        // wakeup interrupts idle and active waits. The short fallback also
        // bounds responsiveness if the OS wakeup mechanism itself fails.
        if (curl_multi_poll(multi, nullptr, 0, 100, nullptr) != CURLM_OK) {
          failed = true;
        }
      }
    } catch (...) {
      failed = true;
    }
    finishPending(failed);
    cleanupMulti();
  }

  void shutdown() {
    // Concurrent callers may request shutdown, but only one can join.
    std::lock_guard shutdownLock(shutdownMutex);
    {
      std::lock_guard lock(mutex);
      stopping.store(true, std::memory_order_relaxed);
    }
    wakeup->wake();
    if (worker.joinable()) {
      worker.join();
    }
  }
};

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() {
  shutdown();
}

std::shared_ptr<HttpOperation> HttpClient::request(HttpRequest request, std::string& error) {
  error.clear();
  try {
    const auto runtime = http::CurlRuntime::acquire();
    if (!runtime->error().empty()) {
      error = runtime->error();
      return nullptr;
    }
    if (!http::validate(request, error)) {
      return nullptr;
    }
    auto state = std::make_shared<HttpOperation::Impl>();
    state->wakeup = impl_->wakeup;
    const std::shared_ptr<HttpOperation> operation(new HttpOperation(state));
    {
      std::lock_guard lock(impl_->mutex);
      if (impl_->stopping.load(std::memory_order_relaxed)) {
        error = "HTTP client is shut down";
        return nullptr;
      }
      impl_->runtime = runtime;
      if (!impl_->start(error)) {
        return nullptr;
      }
      impl_->pending.push_back({std::move(request), std::move(state)});
    }
    impl_->wakeup->wake();
    return operation;
  } catch (...) {
    error = "HTTP request submission failed";
    return nullptr;
  }
}

void HttpClient::shutdown() {
  impl_->shutdown();
}

} // namespace demi::runtime
