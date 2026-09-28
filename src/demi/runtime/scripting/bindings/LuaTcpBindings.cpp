#include "demi/runtime/scripting/bindings/LuaTcpBindings.h"

#include "demi/runtime/concurrency/AsyncCompletion.h"
#include "demi/runtime/network/TcpClient.h"
#include "demi/runtime/scripting/LuaWorkerContext.h"

#include <sol/sol.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>

namespace demi::runtime {
namespace {

struct LuaTcpOperation {
  std::shared_ptr<TcpOperation> operation;

  ~LuaTcpOperation() {
    if (operation)
      operation->cancel();
  }
};

struct LuaTcpConnection {
  std::shared_ptr<TcpConnection> connection;
};

using StartResult = std::tuple<sol::object, sol::object>;

double integerValue(const sol::object &value, const char *name, double minimum,
                    double maximum) {
  if (value.get_type() != sol::type::number)
    throw std::invalid_argument(std::string(name) + " must be an integer");
  const double number = value.as<double>();
  if (!std::isfinite(number) || number < minimum || number > maximum ||
      std::floor(number) != number)
    throw std::invalid_argument(std::string(name) +
                                " is outside its integer range");
  return number;
}

int timeoutOption(const sol::optional<sol::object> &options, int fallback) {
  if (!options || !options->valid() || *options == sol::nil)
    return fallback;
  if (options->get_type() != sol::type::table)
    throw std::invalid_argument("TCP options must be a table");
  const sol::table table = options->as<sol::table>();
  for (const auto &[key, value] : table) {
    (void)value;
    if (key.get_type() != sol::type::string ||
        key.as<std::string>() != "timeout_ms")
      throw std::invalid_argument("Unknown TCP option");
  }
  const sol::object timeout = table.raw_get<sol::object>("timeout_ms");
  if (!timeout.valid() || timeout == sol::nil)
    return fallback;
  return static_cast<int>(
      integerValue(timeout, "timeout_ms", 0, std::numeric_limits<int>::max()));
}

std::shared_ptr<LuaTcpOperation>
wrapOperation(std::shared_ptr<TcpOperation> operation) {
  auto handle = std::make_shared<LuaTcpOperation>();
  handle->operation = std::move(operation);
  return handle;
}

sol::object responseObject(const LuaTcpOperation &handle, lua_State *state) {
  const auto response = handle.operation->response();
  if (!response)
    return sol::make_object(state, sol::nil);
  sol::state_view lua(state);
  sol::table table = lua.create_table();
  table["ok"] = response->ok;
  table["data"] = response->data;
  table["error_code"] = response->errorCode;
  table["error"] = response->error;
  if (response->connection) {
    auto connection = std::make_shared<LuaTcpConnection>();
    connection->connection = response->connection;
    table["connection"] = connection;
  } else {
    table["connection"] = sol::nil;
  }
  return sol::make_object(state, table);
}

StartResult waitResponse(const LuaTcpOperation &handle,
                         sol::optional<sol::object> timeoutMs,
                         lua_State *state) {
  try {
    if (!luaWorkerContext(state).isWorker)
      throw std::invalid_argument(
          "TCP operation:wait() is only available in workers");
    std::optional<std::chrono::milliseconds> timeout;
    if (timeoutMs && timeoutMs->valid() && *timeoutMs != sol::nil)
      timeout = std::chrono::milliseconds(static_cast<int>(integerValue(
          *timeoutMs, "timeout_ms", 0, std::numeric_limits<int>::max())));
    const auto result =
        waitLuaWorkerCompletion(state, handle.operation->completion(), timeout);
    if (result != AsyncWaitResult::Ready)
      return {sol::make_object(state, sol::nil),
              sol::make_object(state, result == AsyncWaitResult::Cancelled
                                          ? "cancelled"
                                          : "timeout")};
    return {responseObject(handle, state), sol::make_object(state, sol::nil)};
  } catch (const std::invalid_argument &error) {
    return {sol::make_object(state, sol::nil),
            sol::make_object(state, error.what())};
  }
}

StartResult startConnection(TcpClient &client, const sol::object &hostValue,
                            const sol::object &portValue,
                            const sol::optional<sol::object> &options,
                            lua_State *state) {
  try {
    if (hostValue.get_type() != sol::type::string)
      throw std::invalid_argument("host must be a nonempty string");
    std::string address = hostValue.as<std::string>();
    if (address.empty() || address.find('\0') != std::string::npos)
      throw std::invalid_argument(
          "host must be a nonempty string without NUL bytes");
    const auto port =
        static_cast<std::uint16_t>(integerValue(portValue, "port", 1, 65535));
    const int timeoutMs = timeoutOption(options, 5000);
    auto operation = client.connect(std::move(address), port, timeoutMs);
    return {sol::make_object(state, wrapOperation(std::move(operation))),
            sol::make_object(state, sol::nil)};
  } catch (const std::invalid_argument &error) {
    return {sol::make_object(state, sol::nil),
            sol::make_object(state, error.what())};
  } catch (const std::exception &) {
    return {sol::make_object(state, sol::nil),
            sol::make_object(state, "TCP connection could not start")};
  }
}

} // namespace

void LuaTcpBindingModule::install(LuaScriptHost &host, lua_State *state) const {
  installLuaTcpBindings(state, host.tcpClient());
}

void installLuaTcpBindings(lua_State *state, TcpClient &client) {
  sol::state_view lua(state);
  lua.new_usertype<LuaTcpOperation>(
      "TcpOperation", sol::no_constructor, "done",
      [](const LuaTcpOperation &handle) {
        return handle.operation->completion()->ready();
      },
      "response",
      [](const LuaTcpOperation &handle, sol::this_state current) {
        lua_State *callingState = current;
        return responseObject(handle, callingState);
      },
      "wait",
      [](const LuaTcpOperation &handle, sol::optional<sol::object> timeoutMs,
         sol::this_state current) {
        return waitResponse(handle, timeoutMs, current);
      },
      "cancel", [](LuaTcpOperation &handle) { handle.operation->cancel(); });
  lua["TcpOperation"] = sol::nil;

  lua.new_usertype<LuaTcpConnection>(
      "TcpConnection", sol::no_constructor, "read",
      [](const LuaTcpConnection &handle, const sol::object &maximum,
         sol::optional<sol::object> options) {
        constexpr double exactLuaInteger = 9007199254740991.0;
        const auto largest = std::min(
            exactLuaInteger,
            static_cast<double>(std::numeric_limits<std::size_t>::max()));
        const auto maxBytes = static_cast<std::size_t>(
            integerValue(maximum, "max_bytes", 1, largest));
        return wrapOperation(
            handle.connection->read(maxBytes, timeoutOption(options, 30000)));
      },
      "write",
      [](const LuaTcpConnection &handle, const sol::object &data,
         sol::optional<sol::object> options) {
        if (data.get_type() != sol::type::string)
          throw std::invalid_argument("data must be a string");
        const int timeoutMs = timeoutOption(options, 30000);
        return wrapOperation(
            handle.connection->write(data.as<std::string>(), timeoutMs));
      },
      "close", [](LuaTcpConnection &handle) { handle.connection->close(); },
      "open",
      [](const LuaTcpConnection &handle) { return handle.connection->open(); });
  lua["TcpConnection"] = sol::nil;

  sol::table tcp = lua.create_named_table("Tcp");
  tcp.set_function("connect", [&client](const sol::object &address,
                                        const sol::object &port,
                                        sol::optional<sol::object> options,
                                        sol::this_state current) {
    lua_State *callingState = current;
    return startConnection(client, address, port, options, callingState);
  });
}

} // namespace demi::runtime
