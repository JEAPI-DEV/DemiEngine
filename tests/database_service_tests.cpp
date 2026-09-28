#include "demi/runtime/concurrency/AsyncWorkQueue.h"
#include "demi/runtime/database/DBDriver.h"
#include "demi/runtime/database/DatabaseService.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using demi::runtime::AsyncWorkOperation;
using demi::runtime::DatabaseService;
using Json = nlohmann::json;

Json value(const std::shared_ptr<AsyncWorkOperation> &operation) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!operation->completion()->ready() &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(operation->completion()->ready());
  if (!operation->error().empty())
    throw std::runtime_error(operation->error());
  assert(operation->result());
  return *operation->result();
}

void expectFailure(const std::shared_ptr<AsyncWorkOperation> &operation) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!operation->completion()->ready() &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(operation->completion()->ready());
  assert(!operation->error().empty());
}

void sqliteRoundTripAndOrder() {
  DatabaseService service;
  auto db = service.connect("sqlite", ":memory:");
  value(db->connectOperation());
  value(db->execute("CREATE TABLE records (id INTEGER PRIMARY KEY, name TEXT, "
                    "flag INTEGER, data BLOB)"));
  auto first =
      db->execute("INSERT INTO records (name, flag, data) VALUES (?, ?, ?)",
                  Json::array({"one", true, Json::binary({0, 1, 255})}));
  auto second =
      db->execute("INSERT INTO records (name, flag, data) VALUES (?, ?, ?)",
                  Json::array({nullptr, false, Json::binary({})}));
  assert(value(first)["last_insert_id"] == 1);
  assert(value(second)["last_insert_id"] == 2);
  const auto selected =
      value(db->query("SELECT name AS duplicate, flag AS duplicate, data FROM "
                      "records ORDER BY id"));
  assert(selected["columns"] ==
         Json::array({"duplicate", "duplicate", "data"}));
  assert(selected["rows"].size() == 2);
  assert(selected["rows"][0][0] == "one");
  assert(selected["rows"][0][1] == 1);
  assert(selected["rows"][0][2] == Json::binary({0, 1, 255}));
  assert(selected["rows"][1][0].is_null());
  assert(selected["rows"][1][2] == Json::binary({}));
  expectFailure(db->query("SELECT 1; SELECT 2"));
  expectFailure(db->query(std::string("SELECT 1\0SELECT 2", 17)));
  expectFailure(db->query("SELECT ?", Json::array()));
  expectFailure(db->query("SELECT ?", Json::array({Json::object()})));
  expectFailure(db->query(
      "SELECT ?", Json::array({std::numeric_limits<std::uint64_t>::max()})));
  assert(value(db->query(
             "SELECT ?",
             Json::array(
                 {std::numeric_limits<std::int64_t>::max()})))["rows"][0][0] ==
         std::numeric_limits<std::int64_t>::max());
  expectFailure(db->execute("ATTACH DATABASE '/tmp/other.sqlite' AS other"));
  value(db->close());
  bool rejected = false;
  try {
    (void)db->query("SELECT 1");
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
}

void transactionRollsBack() {
  DatabaseService service;
  auto db = service.connect("sqlite", ":memory:");
  value(db->connectOperation());
  value(db->execute("CREATE TABLE items (id INTEGER PRIMARY KEY)"));
  const Json batch = Json::array({Json{{"sql", "INSERT INTO items VALUES (?)"},
                                       {"parameters", Json::array({1})}},
                                  Json{{"sql", "INSERT INTO items VALUES (?)"},
                                       {"parameters", Json::array({1})}}});
  expectFailure(db->transaction(batch));
  assert(value(db->query("SELECT count(*) FROM items"))["rows"][0][0] == 0);
  expectFailure(db->transaction(
      Json::array({Json{{"sql", "INSERT INTO items VALUES (1)"}},
                   Json{{"sql", "COMMIT"}}})));
  assert(value(db->query("SELECT count(*) FROM items"))["rows"][0][0] == 0);
  const auto result = value(db->transaction(
      Json::array({Json{{"sql", "INSERT INTO items VALUES (?)"},
                        {"parameters", Json::array({2})}},
                   Json{{"sql", "SELECT id FROM items"}, {"query", true}}})));
  assert(result[0]["changes"] == 1);
  assert(result[1]["rows"][0][0] == 2);
}

void cancellationAndLifetime() {
  std::shared_ptr<demi::runtime::DatabaseConnection> db;
  {
    DatabaseService service;
    db = service.connect("sqlite", ":memory:");
    value(db->connectOperation());
    value(db->execute("CREATE TABLE items (id INTEGER)"));
    auto batch = db->transaction(
        Json::array({Json{{"sql", "INSERT INTO items VALUES (1)"}},
                     Json{{"sql", "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL "
                                  "SELECT x+1 FROM n WHERE x<1000) "
                                  "SELECT count(*) FROM n a, n b, n c"},
                          {"query", true}}}));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    batch->cancel();
    expectFailure(batch);
    assert(value(db->query("SELECT count(*) FROM items"))["rows"][0][0] == 0);
  }
  bool rejected = false;
  try {
    (void)db->query("SELECT 1");
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
}

void fileReopen() {
  const auto path =
      std::filesystem::temp_directory_path() /
      ("demi-db-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".sqlite");
  {
    DatabaseService service;
    auto db =
        service.connect("sqlite", path.string(), {{"busy_timeout_ms", 100}});
    value(db->connectOperation());
    value(db->execute("CREATE TABLE saved (value TEXT)"));
    value(db->execute("INSERT INTO saved VALUES (?)",
                      Json::array({"persisted"})));
    value(db->close());
  }
  {
    DatabaseService service;
    auto db = service.connect("sqlite", path.string());
    value(db->connectOperation());
    assert(value(db->query("SELECT value FROM saved"))["rows"][0][0] ==
           "persisted");
    value(db->close());
  }
  std::filesystem::remove(path);
}

class FakeConnection final : public demi::runtime::DBConnection {
public:
  Json query(const std::string &sql, const Json &,
             const std::atomic<bool> &) override {
    return Json{{"columns", Json::array({"sql"})},
                {"rows", Json::array({Json::array({sql})})}};
  }
  Json execute(const std::string &, const Json &,
               const std::atomic<bool> &) override {
    return Json{{"changes", 0}, {"last_insert_id", 0}};
  }
  Json transaction(const Json &, const std::atomic<bool> &) override {
    return Json::array();
  }
  void close() override {}
};

class FakeDriver final : public demi::runtime::DBDriver {
public:
  std::unique_ptr<demi::runtime::DBConnection>
  connect(const std::string &, const Json &,
          const std::atomic<bool> &) override {
    return std::make_unique<FakeConnection>();
  }
};

void driverRegistry() {
  DatabaseService service;
  bool rejected = false;
  try {
    (void)service.connect("missing", ":memory:");
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  service.registerDriver("fake", [] { return std::make_unique<FakeDriver>(); });
  auto db = service.connect("fake", "unused");
  value(db->connectOperation());
  assert(value(db->query("hello"))["rows"][0][0] == "hello");
}

void sharedPoolAndShutdown() {
  struct Threads {
    std::mutex mutex;
    std::set<std::thread::id> ids;
  } threads;
  class TrackingDriver final : public demi::runtime::DBDriver {
  public:
    explicit TrackingDriver(Threads &threads) : threads_(threads) {}
    std::unique_ptr<demi::runtime::DBConnection>
    connect(const std::string &, const Json &,
            const std::atomic<bool> &) override {
      std::scoped_lock lock(threads_.mutex);
      threads_.ids.insert(std::this_thread::get_id());
      return std::make_unique<FakeConnection>();
    }

  private:
    Threads &threads_;
  };
  DatabaseService service(2);
  service.registerDriver(
      "tracking", [&] { return std::make_unique<TrackingDriver>(threads); });
  std::vector<std::shared_ptr<demi::runtime::DatabaseConnection>> handles;
  for (int index = 0; index < 8; ++index)
    handles.push_back(service.connect("tracking", "unused"));
  for (const auto &handle : handles)
    value(handle->connectOperation());
  assert(threads.ids.size() <= 2);
  assert(!threads.ids.empty());
  service.shutdown();
  bool rejected = false;
  try {
    (void)handles.front()->query("SELECT 1");
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
  rejected = false;
  try {
    (void)service.connect("tracking", "unused");
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  assert(rejected);
}

void droppedHandleClosesOnWorker() {
  struct Flags {
    std::atomic<bool> started = false;
    std::atomic<bool> release = false;
    std::atomic<bool> closed = false;
    std::thread::id closedThread;
  } flags;
  class DelayedConnection final : public demi::runtime::DBConnection {
  public:
    explicit DelayedConnection(Flags &flags) : flags_(flags) {}
    ~DelayedConnection() override { close(); }
    Json query(const std::string &, const Json &,
               const std::atomic<bool> &cancelled) override {
      flags_.started = true;
      while (!flags_.release && !cancelled)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return Json{{"columns", Json::array()}, {"rows", Json::array()}};
    }
    Json execute(const std::string &, const Json &,
                 const std::atomic<bool> &) override {
      return Json::object();
    }
    Json transaction(const Json &, const std::atomic<bool> &) override {
      return Json::array();
    }
    void close() override {
      flags_.closedThread = std::this_thread::get_id();
      flags_.closed = true;
    }

  private:
    Flags &flags_;
  };
  class DelayedDriver final : public demi::runtime::DBDriver {
  public:
    explicit DelayedDriver(Flags &flags) : flags_(flags) {}
    std::unique_ptr<demi::runtime::DBConnection>
    connect(const std::string &, const Json &,
            const std::atomic<bool> &) override {
      return std::make_unique<DelayedConnection>(flags_);
    }

  private:
    Flags &flags_;
  };
  DatabaseService service(2);
  service.registerDriver(
      "delayed", [&] { return std::make_unique<DelayedDriver>(flags); });
  auto db = service.connect("delayed", "unused");
  value(db->connectOperation());
  auto query = db->query("wait");
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!flags.started && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(flags.started);
  std::jthread watchdog([&] {
    const auto timeout =
        std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!flags.release && std::chrono::steady_clock::now() < timeout)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    flags.release = true;
  });
  const auto before = std::chrono::steady_clock::now();
  db.reset();
  assert(std::chrono::steady_clock::now() - before <
         std::chrono::milliseconds(750));
  flags.release = true;
  value(query);
  while (!flags.closed && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(flags.closed);
  assert(flags.closedThread != std::this_thread::get_id());
}

void failedConnectPropagates() {
  DatabaseService service;
  auto db = service.connect("sqlite", ":memory:", {{"busy_timeout_ms", -1}});
  auto query = db->query("SELECT 1");
  expectFailure(db->connectOperation());
  expectFailure(query);
  assert(query->error().find("busy_timeout_ms") != std::string::npos);
  for (const Json &options :
       {Json{{"busy_timeout_ms", 1.5}},
        Json{{"busy_timeout_ms", std::numeric_limits<std::uint64_t>::max()}},
        Json{{"busy_timeout_ms",
              static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 1}},
        Json{{"busy_timeout_ms", true}}, Json{{"read_only", true}}}) {
    auto invalid = service.connect("sqlite", ":memory:", options);
    expectFailure(invalid->connectOperation());
  }
}
} // namespace

int main() {
  sqliteRoundTripAndOrder();
  transactionRollsBack();
  cancellationAndLifetime();
  fileReopen();
  driverRegistry();
  sharedPoolAndShutdown();
  droppedHandleClosesOnWorker();
  failedConnectPropagates();
}
