#pragma once
#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>

namespace demi::runtime {
struct ResidencySettings {
  std::size_t maximumBytes = 0;
  std::size_t maximumPendingJobs = 2;
  std::size_t maximumStartsPerUpdate = 1;
  std::size_t maximumPublicationsPerUpdate = 1;
};
struct ResidencyStatistics {
  std::size_t residentCells = 0, residentBytes = 0;
  std::size_t retiringJobs = 0, retiringBytes = 0;
  std::size_t pendingJobs = 0, pendingBytes = 0;
  std::size_t submitted = 0, published = 0, discarded = 0, failed = 0,
              budgetMisses = 0;
  double latestLoadMilliseconds = 0, latestPreparationMilliseconds = 0;
};

// Shared main-thread lifetime/admission owner. Loaders own private worker
// state; a completed payload remains resident until its key leaves demand. The
// byte estimate bounds admitted policy, not allocator overhead or process RSS.
template <class Key, class Value> class AsyncResidency {
public:
  struct Demand {
    Key key;
    std::size_t preparationBytes = 0;
  };
  struct Loaded {
    Value value;
    std::size_t bytes = 0;
    double loadMilliseconds = 0, preparationMilliseconds = 0;
  };
  using Load = std::function<Loaded(const Key &, std::stop_token)>;
  AsyncResidency(JobSystem &jobs, ResidencySettings settings, Load load)
      : jobs_(jobs), settings_(settings),
        load_(std::make_shared<const Load>(std::move(load))) {
    if (!*load_ || jobs.workerCount() == 0 || settings.maximumBytes == 0 ||
        settings.maximumPendingJobs == 0 ||
        settings.maximumStartsPerUpdate == 0 ||
        settings.maximumPublicationsPerUpdate == 0) {
      throw std::invalid_argument(
          "Residency requires a loader, workers and nonzero budgets");
    }
  }
  ~AsyncResidency() {
    for (auto &[key, entry] : entries_) {
      if (entry.pending) {
        entry.pending->stop.request_stop();
      }
    }
    for (auto &[key, entry] : entries_) {
      if (entry.pending) {
        join(entry.pending->job);
      }
    }
    for (const auto &retiring : retiring_) {
      join(retiring.job);
    }
  }
  AsyncResidency(const AsyncResidency &) = delete;
  AsyncResidency &operator=(const AsyncResidency &) = delete;

  void update(std::span<const Demand> demand) {
    std::set<Key> keys;
    for (const auto &request : demand) {
      if (request.preparationBytes == 0 || !keys.insert(request.key).second) {
        throw std::invalid_argument("Invalid or duplicate residency demand");
      }
    }
    for (auto &[key, entry] : entries_) {
      entry.demanded = keys.contains(key);
    }
    for (const auto &request : demand) {
      entries_[request.key].demanded = true;
    }
    for (auto &[key, entry] : entries_) {
      if (!entry.demanded) {
        retire(std::move(entry.ready), entry.bytes);
        entry.bytes = 0;
        if (entry.pending) {
          entry.pending->stop.request_stop();
        }
      }
    }
    collect();
    std::size_t started = 0;
    for (const auto &request : demand) {
      auto &entry = entries_.at(request.key);
      if (entry.ready || entry.pending || !entry.error.empty()) {
        continue;
      }
      if (started == settings_.maximumStartsPerUpdate) {
        break;
      }
      if (start(request, entry)) {
        ++started;
      }
    }
  }
  void clear() { update({}); }
  [[nodiscard]] const Value *find(const Key &key) const {
    const auto found = entries_.find(key);
    return found == entries_.end() ? nullptr : found->second.ready.get();
  }
  [[nodiscard]] std::string error(const Key &key) const {
    const auto found = entries_.find(key);
    return found == entries_.end() ? std::string{} : found->second.error;
  }
  [[nodiscard]] ResidencyStatistics statistics() const {
    auto result = counters_;
    for (const auto &[key, entry] : entries_) {
      if (entry.ready) {
        ++result.residentCells;
        result.residentBytes += entry.bytes;
      }
      if (entry.pending) {
        ++result.pendingJobs;
        result.pendingBytes += entry.pending->reservation;
      }
    }
    result.retiringJobs = retiring_.size();
    for (const auto &retiring : retiring_) {
      result.retiringBytes += retiring.bytes;
    }
    return result;
  }

private:
  struct Outcome {
    std::unique_ptr<Value> value;
    std::size_t bytes = 0;
    double loadMilliseconds = 0, preparationMilliseconds = 0;
    std::string error;
  };
  struct Pending {
    JobHandle job;
    std::stop_source stop;
    std::shared_ptr<Outcome> outcome;
    std::size_t reservation = 0;
  };
  struct Entry {
    std::unique_ptr<Value> ready;
    std::unique_ptr<Pending> pending;
    std::size_t bytes = 0;
    std::string error;
    bool demanded = false;
  };
  struct Retirement {
    JobHandle job;
    std::size_t bytes = 0;
  };
  static void join(const JobHandle &job) noexcept {
    try {
      job.wait();
    } catch (...) { /* Join every owned job during destruction. */
    }
  }
  void retire(std::unique_ptr<Value> value, std::size_t bytes) {
    if (!value) {
      return;
    }
    retiring_.emplace_back();
    try {
      auto payload = std::shared_ptr<Value>(std::move(value));
      auto job = jobs_.submit(
          [payload = std::move(payload)]() mutable { payload.reset(); });
      retiring_.back() = {std::move(job), bytes};
    } catch (...) {
      retiring_.pop_back();
      throw;
    }
  }
  void collect() {
    std::erase_if(retiring_, [](const auto &item) {
      if (!item.job.ready()) {
        return false;
      }
      item.job.wait();
      return true;
    });
    std::size_t published = 0;
    for (auto &[key, entry] : entries_) {
      if (!entry.pending || !entry.pending->job.ready()) {
        continue;
      }
      auto &pending = *entry.pending;
      pending.job.wait();
      auto &outcome = *pending.outcome;
      counters_.latestLoadMilliseconds = outcome.loadMilliseconds;
      counters_.latestPreparationMilliseconds = outcome.preparationMilliseconds;
      if (!entry.demanded || pending.stop.stop_requested()) {
        ++counters_.discarded;
        retire(std::move(outcome.value), outcome.bytes);
        entry.pending.reset();
      } else if (!outcome.error.empty()) {
        entry.error = std::move(outcome.error);
        ++counters_.failed;
        entry.pending.reset();
      } else if (published < settings_.maximumPublicationsPerUpdate) {
        entry.ready = std::move(outcome.value);
        entry.bytes = outcome.bytes;
        entry.pending.reset();
        ++published;
        ++counters_.published;
      }
    }
    std::erase_if(entries_, [](const auto &item) {
      return !item.second.demanded && !item.second.pending;
    });
  }
  bool start(const Demand &request, Entry &entry) {
    const auto stats = statistics();
    if (stats.pendingJobs + stats.retiringJobs >=
        settings_.maximumPendingJobs) {
      return false;
    }
    const auto used =
        stats.residentBytes + stats.pendingBytes + stats.retiringBytes;
    if (request.preparationBytes > settings_.maximumBytes - used) {
      ++counters_.budgetMisses;
      return false;
    }
    auto pending = std::make_unique<Pending>();
    pending->reservation = request.preparationBytes;
    pending->outcome = std::make_shared<Outcome>();
    auto outcome = pending->outcome;
    auto stop = pending->stop.get_token();
    pending->job = jobs_.submit([load = load_, key = request.key, stop, outcome,
                                 reservation = request.preparationBytes] {
      ScopedProfileSuppression suppress;
      try {
        if (stop.stop_requested()) {
          return;
        }
        auto loaded = (*load)(key, stop);
        outcome->loadMilliseconds = loaded.loadMilliseconds;
        outcome->preparationMilliseconds = loaded.preparationMilliseconds;
        if (stop.stop_requested()) {
          return;
        }
        if (loaded.bytes > reservation) {
          throw std::runtime_error(
              "Prepared cell output exceeds its preparation reservation");
        }
        outcome->value = std::make_unique<Value>(std::move(loaded.value));
        outcome->bytes = loaded.bytes;
      } catch (const std::exception &error) {
        outcome->error = error.what();
      } catch (...) {
        outcome->error = "Unknown cell preparation failure";
      }
    });
    entry.pending = std::move(pending);
    ++counters_.submitted;
    return true;
  }
  JobSystem &jobs_;
  ResidencySettings settings_;
  std::shared_ptr<const Load> load_;
  std::map<Key, Entry> entries_;
  std::vector<Retirement> retiring_;
  ResidencyStatistics counters_;
};
} // namespace demi::runtime
