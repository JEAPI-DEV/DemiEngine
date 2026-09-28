#include "demi/runtime/database/DatabaseService.h"
#include "demi/runtime/concurrency/AsyncWorkStrand.h"
#include "demi/runtime/database/SQLiteDriver.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace demi::runtime {

struct DatabaseConnection::State {
  std::unique_ptr<DBDriver> driver;
  std::unique_ptr<DBConnection> connection;
  std::string path;
  nlohmann::json options;
  std::string connectError;
};

struct DatabaseConnection::Record {
  explicit Record(std::shared_ptr<AsyncWorkQueue> queue)
      : strand(std::make_shared<AsyncWorkStrand>(std::move(queue))) {}

  std::shared_ptr<AsyncWorkStrand> strand;
  std::weak_ptr<DatabaseConnection> handle;
  std::mutex cleanupMutex;
  std::shared_ptr<AsyncWorkOperation> cleanupOperation;
};

DatabaseConnection::DatabaseConnection(std::shared_ptr<AsyncWorkQueue> queue,
                                       std::unique_ptr<DBDriver> driver,
                                       std::string path, nlohmann::json options)
    : state_(std::make_shared<State>(std::move(driver), nullptr,
                                     std::move(path), std::move(options))),
      record_(std::make_shared<Record>(std::move(queue))) {
  connectOperation_ =
      record_->strand->submit([state = state_](const auto &cancelled) {
        if (cancelled.load())
          throw std::runtime_error("cancelled");
        try {
          state->connection =
              state->driver->connect(state->path, state->options, cancelled);
          if (!state->connection)
            throw std::runtime_error("database driver returned no connection");
        } catch (const std::exception &error) {
          state->connectError = error.what();
          throw;
        }
        return nlohmann::json{{"connected", true}};
      });
}

DatabaseConnection::~DatabaseConnection() {
  std::scoped_lock lock(submissionMutex_);
  if (closeSubmitted_ || stopped_)
    return;
  // The state stays alive in the strand until earlier operations finish.
  // Connection destruction only enqueues cleanup and never joins a worker.
  try {
    std::scoped_lock cleanupLock(record_->cleanupMutex);
    record_->cleanupOperation =
        record_->strand->submit([state = state_](const auto &) {
          state->connection.reset();
          return nlohmann::json::object();
        });
  } catch (...) {
    // The service may already be shutting down; it releases state after join.
  }
}

std::shared_ptr<AsyncWorkOperation>
DatabaseConnection::connectOperation() const {
  return connectOperation_;
}

std::shared_ptr<AsyncWorkOperation>
DatabaseConnection::query(std::string sql, nlohmann::json parameters) {
  std::scoped_lock lock(submissionMutex_);
  if (closeSubmitted_ || stopped_)
    throw std::runtime_error("database connection is closed");
  return record_->strand->submit(
      [state = state_, sql = std::move(sql),
       parameters = std::move(parameters)](const auto &cancelled) {
        if (!state->connection)
          throw std::runtime_error(state->connectError.empty()
                                       ? "database connection failed"
                                       : state->connectError);
        return state->connection->query(sql, parameters, cancelled);
      });
}

std::shared_ptr<AsyncWorkOperation>
DatabaseConnection::execute(std::string sql, nlohmann::json parameters) {
  std::scoped_lock lock(submissionMutex_);
  if (closeSubmitted_ || stopped_)
    throw std::runtime_error("database connection is closed");
  return record_->strand->submit(
      [state = state_, sql = std::move(sql),
       parameters = std::move(parameters)](const auto &cancelled) {
        if (!state->connection)
          throw std::runtime_error(state->connectError.empty()
                                       ? "database connection failed"
                                       : state->connectError);
        return state->connection->execute(sql, parameters, cancelled);
      });
}

std::shared_ptr<AsyncWorkOperation>
DatabaseConnection::transaction(nlohmann::json statements) {
  std::scoped_lock lock(submissionMutex_);
  if (closeSubmitted_ || stopped_)
    throw std::runtime_error("database connection is closed");
  return record_->strand->submit(
      [state = state_,
       statements = std::move(statements)](const auto &cancelled) {
        if (!state->connection)
          throw std::runtime_error(state->connectError.empty()
                                       ? "database connection failed"
                                       : state->connectError);
        return state->connection->transaction(statements, cancelled);
      });
}

std::shared_ptr<AsyncWorkOperation> DatabaseConnection::close() {
  std::scoped_lock lock(submissionMutex_);
  if (closeSubmitted_ || stopped_)
    throw std::runtime_error("database connection is closed");
  auto operation = record_->strand->submit([state = state_](const auto &) {
    if (state->connection) {
      state->connection->close();
      state->connection.reset();
    }
    return nlohmann::json{{"closed", true}};
  });
  // Cancellation can skip a queued close operation. This serial cleanup job
  // still releases the native handle before the connection becomes idle.
  std::scoped_lock cleanupLock(record_->cleanupMutex);
  record_->cleanupOperation =
      record_->strand->submit([state = state_](const auto &) {
        state->connection.reset();
        return nlohmann::json::object();
      });
  closeSubmitted_ = true;
  return operation;
}

void DatabaseConnection::stop() {
  std::scoped_lock lock(submissionMutex_);
  stopped_ = true;
  closeSubmitted_ = true;
}

void DatabaseConnection::releaseAfterShutdown() {
  // The service has joined all pool workers, so no job can access this state.
  state_->connection.reset();
}

DatabaseService::DatabaseService(std::size_t workerCount)
    : queue_(std::make_shared<AsyncWorkQueue>(workerCount)) {
  registerDriver("sqlite", [] { return std::make_unique<SQLiteDriver>(); });
}

DatabaseService::~DatabaseService() { shutdown(); }

void DatabaseService::shutdown() {
  std::scoped_lock shutdownLock(shutdownMutex_);
  std::vector<std::shared_ptr<DatabaseConnection::Record>> connections;
  {
    std::scoped_lock lock(mutex_);
    if (stopped_)
      return;
    stopped_ = true;
    connections.swap(connections_);
  }
  for (auto &record : connections)
    if (auto connection = record->handle.lock())
      connection->stop();
  for (auto &record : connections)
    record->strand->shutdown();
  queue_->shutdown();
  for (auto &record : connections)
    if (auto connection = record->handle.lock())
      connection->releaseAfterShutdown();
}

void DatabaseService::registerDriver(std::string name, DriverFactory factory) {
  if (name.empty() || !factory)
    throw std::invalid_argument(
        "database driver name and factory are required");
  std::scoped_lock lock(mutex_);
  if (stopped_)
    throw std::runtime_error("database service is stopped");
  if (!drivers_.emplace(std::move(name), std::move(factory)).second)
    throw std::invalid_argument("database driver is already registered");
}

std::shared_ptr<DatabaseConnection>
DatabaseService::connect(const std::string &driverName, std::string path,
                         nlohmann::json options) {
  DriverFactory factory;
  {
    std::scoped_lock lock(mutex_);
    if (stopped_)
      throw std::runtime_error("database service is stopped");
    const auto found = drivers_.find(driverName);
    if (found == drivers_.end())
      throw std::invalid_argument("unknown database driver: " + driverName);
    factory = found->second;
  }
  // Factory code may reenter the registry. Recheck shutdown before submitting
  // work or publishing a connection, so a racing factory cannot escape
  // teardown.
  auto driver = factory();
  if (!driver)
    throw std::runtime_error("database driver factory returned no driver");
  std::scoped_lock lock(mutex_);
  if (stopped_)
    throw std::runtime_error("database service is stopped");
  auto connection = std::shared_ptr<DatabaseConnection>(new DatabaseConnection(
      queue_, std::move(driver), std::move(path), std::move(options)));
  connection->record_->handle = connection;
  std::erase_if(connections_, [](const auto &record) {
    std::scoped_lock cleanupLock(record->cleanupMutex);
    return record->handle.expired() && record->cleanupOperation &&
           record->cleanupOperation->completion()->ready();
  });
  connections_.push_back(connection->record_);
  return connection;
}

} // namespace demi::runtime
