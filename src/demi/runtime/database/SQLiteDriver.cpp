#include "demi/runtime/database/SQLiteDriver.h"

#include <sqlite3.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace demi::runtime {
namespace {

using Json = nlohmann::json;

void checkCancelled(const std::atomic<bool> &cancelled) {
  if (cancelled.load())
    throw std::runtime_error("cancelled");
}

class Statement final {
public:
  Statement(sqlite3 *database, const std::string &sql) {
    if (sql.find('\0') != std::string::npos)
      throw std::invalid_argument("SQL contains NUL");
    const char *tail = nullptr;
    const int status =
        sqlite3_prepare_v2(database, sql.c_str(), -1, &value_, &tail);
    if (status != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(database));
    if (!value_)
      throw std::invalid_argument("SQL statement is empty");
    sqlite3_stmt *trailing = nullptr;
    const int trailingStatus =
        sqlite3_prepare_v2(database, tail, -1, &trailing, nullptr);
    sqlite3_finalize(trailing);
    if (trailingStatus != SQLITE_OK || trailing) {
      sqlite3_finalize(value_);
      value_ = nullptr;
      throw std::invalid_argument("SQL contains trailing content");
    }
  }

  ~Statement() { sqlite3_finalize(value_); }
  Statement(const Statement &) = delete;
  Statement &operator=(const Statement &) = delete;
  sqlite3_stmt *get() const { return value_; }

private:
  sqlite3_stmt *value_ = nullptr;
};

void bind(sqlite3_stmt *statement, const Json &parameters) {
  if (!parameters.is_array())
    throw std::invalid_argument("SQL parameters must be an array");
  if (parameters.size() !=
      static_cast<std::size_t>(sqlite3_bind_parameter_count(statement)))
    throw std::invalid_argument(
        "SQL parameter count does not match placeholders");
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    const Json &value = parameters[index];
    const int position = static_cast<int>(index + 1);
    int status = SQLITE_MISUSE;
    if (value.is_null())
      status = sqlite3_bind_null(statement, position);
    else if (value.is_boolean())
      status = sqlite3_bind_int(statement, position, value.get<bool>() ? 1 : 0);
    else if (value.is_number_unsigned()) {
      const auto number = value.get<std::uint64_t>();
      if (number >
          static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::invalid_argument("unsigned SQL integer exceeds int64 range");
      status = sqlite3_bind_int64(statement, position,
                                  static_cast<std::int64_t>(number));
    } else if (value.is_number_integer()) {
      status =
          sqlite3_bind_int64(statement, position, value.get<std::int64_t>());
    } else if (value.is_number_float()) {
      const double number = value.get<double>();
      if (!std::isfinite(number))
        throw std::invalid_argument("SQL floating parameter must be finite");
      status = sqlite3_bind_double(statement, position, number);
    } else if (value.is_string()) {
      const auto &string = value.get_ref<const std::string &>();
      if (string.size() >
          static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("SQL string parameter is too large");
      status =
          sqlite3_bind_text(statement, position, string.data(),
                            static_cast<int>(string.size()), SQLITE_TRANSIENT);
    } else if (value.is_binary()) {
      const auto &bytes = value.get_binary();
      if (bytes.size() >
          static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("SQL binary parameter is too large");
      status = bytes.empty()
                   ? sqlite3_bind_zeroblob(statement, position, 0)
                   : sqlite3_bind_blob(statement, position, bytes.data(),
                                       static_cast<int>(bytes.size()),
                                       SQLITE_TRANSIENT);
    } else {
      throw std::invalid_argument("unsupported SQL parameter type");
    }
    if (status != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(statement)));
  }
}

Json columnValue(sqlite3_stmt *statement, int index) {
  switch (sqlite3_column_type(statement, index)) {
  case SQLITE_NULL:
    return nullptr;
  case SQLITE_INTEGER:
    return sqlite3_column_int64(statement, index);
  case SQLITE_FLOAT:
    return sqlite3_column_double(statement, index);
  case SQLITE_TEXT: {
    const auto *data =
        reinterpret_cast<const char *>(sqlite3_column_text(statement, index));
    const int size = sqlite3_column_bytes(statement, index);
    return std::string(data, static_cast<std::size_t>(size));
  }
  case SQLITE_BLOB: {
    const auto *data = static_cast<const std::uint8_t *>(
        sqlite3_column_blob(statement, index));
    const int size = sqlite3_column_bytes(statement, index);
    if (size == 0)
      return Json::binary(std::vector<std::uint8_t>{});
    return Json::binary(std::vector<std::uint8_t>(data, data + size));
  }
  default:
    throw std::runtime_error("unknown SQLite column type");
  }
}

class SQLiteConnection final : public DBConnection {
public:
  explicit SQLiteConnection(sqlite3 *database) : database_(database) {
    sqlite3_set_authorizer(
        database_,
        [](void *context, int action, const char *, const char *, const char *,
           const char *) -> int {
          const auto *self = static_cast<const SQLiteConnection *>(context);
          // ATTACH would bypass the path selected and scoped by the caller.
          if (action == SQLITE_ATTACH || action == SQLITE_DETACH)
            return SQLITE_DENY;
          if (self->transactionBatch_ &&
              (action == SQLITE_TRANSACTION || action == SQLITE_SAVEPOINT))
            return SQLITE_DENY;
          return SQLITE_OK;
        },
        this);
  }
  ~SQLiteConnection() override { close(); }

  Json query(const std::string &sql, const Json &parameters,
             const std::atomic<bool> &cancelled) override {
    return run(sql, parameters, cancelled, true);
  }

  Json execute(const std::string &sql, const Json &parameters,
               const std::atomic<bool> &cancelled) override {
    return run(sql, parameters, cancelled, false);
  }

  Json transaction(const Json &statements,
                   const std::atomic<bool> &cancelled) override {
    if (!statements.is_array())
      throw std::invalid_argument("transaction statements must be an array");
    checkCancelled(cancelled);
    raw("BEGIN IMMEDIATE");
    transactionBatch_ = true;
    try {
      Json results = Json::array();
      for (const auto &item : statements) {
        checkCancelled(cancelled);
        if (!item.is_object() || !item.contains("sql") ||
            !item["sql"].is_string())
          throw std::invalid_argument("transaction entry requires SQL text");
        const auto parameters = item.value("parameters", Json::array());
        const bool isQuery = item.value("query", false);
        results.push_back(run(item["sql"].get<std::string>(), parameters,
                              cancelled, isQuery));
      }
      checkCancelled(cancelled);
      transactionBatch_ = false;
      raw("COMMIT");
      return results;
    } catch (...) {
      transactionBatch_ = false;
      try {
        raw("ROLLBACK");
      } catch (...) {
      }
      throw;
    }
  }

  void close() override {
    if (database_) {
      sqlite3_close_v2(database_);
      database_ = nullptr;
    }
  }

private:
  struct ProgressScope {
    sqlite3 *database;
    explicit ProgressScope(sqlite3 *db, const std::atomic<bool> &cancelled)
        : database(db) {
      sqlite3_progress_handler(
          database, 1000,
          [](void *context) -> int {
            return static_cast<const std::atomic<bool> *>(context)->load() ? 1
                                                                           : 0;
          },
          const_cast<std::atomic<bool> *>(&cancelled));
    }
    ~ProgressScope() {
      sqlite3_progress_handler(database, 0, nullptr, nullptr);
    }
  };

  Json run(const std::string &sql, const Json &parameters,
           const std::atomic<bool> &cancelled, bool isQuery) {
    if (!database_)
      throw std::runtime_error("database connection is closed");
    checkCancelled(cancelled);
    ProgressScope progress(database_, cancelled);
    Statement statement(database_, sql);
    bind(statement.get(), parameters);
    const int count = sqlite3_column_count(statement.get());
    Json columns = Json::array();
    for (int index = 0; index < count; ++index)
      columns.push_back(sqlite3_column_name(statement.get(), index));
    Json rows = Json::array();
    int status;
    while ((status = sqlite3_step(statement.get())) == SQLITE_ROW) {
      checkCancelled(cancelled);
      if (isQuery) {
        Json row = Json::array();
        for (int index = 0; index < count; ++index)
          row.push_back(columnValue(statement.get(), index));
        rows.push_back(std::move(row));
      }
    }
    checkCancelled(cancelled);
    if (status != SQLITE_DONE)
      throw std::runtime_error(sqlite3_errmsg(database_));
    if (isQuery)
      return Json{{"columns", std::move(columns)}, {"rows", std::move(rows)}};
    return Json{{"changes", sqlite3_changes64(database_)},
                {"last_insert_id", sqlite3_last_insert_rowid(database_)}};
  }

  void raw(const char *sql) {
    char *message = nullptr;
    const int status = sqlite3_exec(database_, sql, nullptr, nullptr, &message);
    if (status != SQLITE_OK) {
      std::string error = message ? message : sqlite3_errmsg(database_);
      sqlite3_free(message);
      throw std::runtime_error(error);
    }
  }

  sqlite3 *database_ = nullptr;
  bool transactionBatch_ = false;
};

} // namespace

std::unique_ptr<DBConnection>
SQLiteDriver::connect(const std::string &path, const Json &options,
                      const std::atomic<bool> &cancelled) {
  checkCancelled(cancelled);
  if (path.empty() || path.find('\0') != std::string::npos)
    throw std::invalid_argument(
        "SQLite path must be nonempty and contain no NUL");
  if (!options.is_object())
    throw std::invalid_argument("SQLite options must be an object");
  for (auto option = options.begin(); option != options.end(); ++option)
    if (option.key() != "busy_timeout_ms")
      throw std::invalid_argument("unknown SQLite option: " + option.key());
  int timeout = 5000;
  if (options.contains("busy_timeout_ms")) {
    const Json &value = options.at("busy_timeout_ms");
    if (value.is_number_unsigned()) {
      const auto number = value.get<std::uint64_t>();
      if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument(
            "busy_timeout_ms must be an integer from 0 to INT_MAX");
      timeout = static_cast<int>(number);
    } else if (value.is_number_integer()) {
      const auto number = value.get<std::int64_t>();
      if (number < 0 || number > std::numeric_limits<int>::max())
        throw std::invalid_argument(
            "busy_timeout_ms must be an integer from 0 to INT_MAX");
      timeout = static_cast<int>(number);
    } else {
      throw std::invalid_argument(
          "busy_timeout_ms must be an integer from 0 to INT_MAX");
    }
  }
  sqlite3 *database = nullptr;
  const int status = sqlite3_open_v2(
      path.c_str(), &database,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
      nullptr);
  if (status != SQLITE_OK) {
    const std::string message =
        database ? sqlite3_errmsg(database) : "SQLite open failed";
    if (database)
      sqlite3_close_v2(database);
    throw std::runtime_error(message);
  }
  auto connection = std::make_unique<SQLiteConnection>(database);
  sqlite3_busy_timeout(database, timeout);
  checkCancelled(cancelled);
  return connection;
}

} // namespace demi::runtime
