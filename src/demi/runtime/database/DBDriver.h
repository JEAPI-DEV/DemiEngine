#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>

namespace demi::runtime {

// Requests on each connection are serialized, but successive calls can use
// different pool threads. Drivers must observe cancellation and throw on
// failure. Normal driver operations do not run on the game loop; destruction
// after service shutdown may run on the owner thread after workers have joined.
class DBConnection {
public:
  virtual ~DBConnection() = default;
  virtual nlohmann::json query(const std::string &sql,
                               const nlohmann::json &parameters,
                               const std::atomic<bool> &cancelled) = 0;
  virtual nlohmann::json execute(const std::string &sql,
                                 const nlohmann::json &parameters,
                                 const std::atomic<bool> &cancelled) = 0;
  virtual nlohmann::json transaction(const nlohmann::json &statements,
                                     const std::atomic<bool> &cancelled) = 0;
  virtual void close() = 0;
};

class DBDriver {
public:
  virtual ~DBDriver() = default;
  virtual std::unique_ptr<DBConnection>
  connect(const std::string &path, const nlohmann::json &options,
          const std::atomic<bool> &cancelled) = 0;
};

} // namespace demi::runtime
