#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace demi::runtime {

class AsyncWorkStrand;
class AsyncWorkQueue;
class AsyncWorkOperation;
class DBDriver;

class DatabaseConnection final {
public:
  ~DatabaseConnection();
  DatabaseConnection(const DatabaseConnection &) = delete;
  DatabaseConnection &operator=(const DatabaseConnection &) = delete;

  [[nodiscard]] std::shared_ptr<AsyncWorkOperation> connectOperation() const;
  [[nodiscard]] std::shared_ptr<AsyncWorkOperation>
  query(std::string sql, nlohmann::json parameters = nlohmann::json::array());
  [[nodiscard]] std::shared_ptr<AsyncWorkOperation>
  execute(std::string sql, nlohmann::json parameters = nlohmann::json::array());
  [[nodiscard]] std::shared_ptr<AsyncWorkOperation>
  transaction(nlohmann::json statements);
  [[nodiscard]] std::shared_ptr<AsyncWorkOperation> close();

private:
  friend class DatabaseService;
  struct State;
  struct Record;
  DatabaseConnection(std::shared_ptr<AsyncWorkQueue> queue,
                     std::unique_ptr<DBDriver> driver, std::string path,
                     nlohmann::json options);
  void stop();
  void releaseAfterShutdown();

  std::shared_ptr<State> state_;
  std::shared_ptr<Record> record_;
  std::shared_ptr<AsyncWorkOperation> connectOperation_;
  mutable std::mutex submissionMutex_;
  bool closeSubmitted_ = false;
  bool stopped_ = false;
};

// Registry and connection submission are thread-safe. Factories run on the
// submitting thread outside service locks and must support concurrent calls;
// each connection owns its driver instance. Join Lua workers before shutdown.
class DatabaseService final {
public:
  using DriverFactory = std::function<std::unique_ptr<DBDriver>()>;

  explicit DatabaseService(std::size_t workerCount = 2);
  ~DatabaseService();
  DatabaseService(const DatabaseService &) = delete;
  DatabaseService &operator=(const DatabaseService &) = delete;

  void registerDriver(std::string name, DriverFactory factory);
  // Cancels outstanding work and joins pool workers. Hosts should call this
  // explicitly during teardown; the destructor calls it if needed. It may
  // block until a driver observes cancellation or its busy timeout expires.
  void shutdown();
  [[nodiscard]] std::shared_ptr<DatabaseConnection>
  connect(const std::string &driverName, std::string path,
          nlohmann::json options = nlohmann::json::object());

private:
  std::mutex mutex_;
  std::mutex shutdownMutex_;
  std::shared_ptr<AsyncWorkQueue> queue_;
  std::map<std::string, DriverFactory> drivers_;
  std::vector<std::shared_ptr<DatabaseConnection::Record>> connections_;
  bool stopped_ = false;
};

} // namespace demi::runtime
