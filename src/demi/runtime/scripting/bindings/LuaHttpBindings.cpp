#include "demi/runtime/scripting/bindings/LuaHttpBindings.h"

#include "demi/runtime/network/HttpClient.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>

namespace demi::runtime {
namespace {

struct HttpRequestHandle {
  std::shared_ptr<HttpOperation> operation;

  ~HttpRequestHandle() {
    if (operation)
      operation->cancel();
  }
};

using RequestResult = std::tuple<sol::object, sol::object>;

std::string stringOption(const sol::table &options, const char *name,
                         const std::string &fallback = {}) {
  const sol::object value = options.raw_get<sol::object>(name);
  if (!value.valid() || value == sol::nil)
    return fallback;
  if (value.get_type() != sol::type::string)
    throw std::invalid_argument(std::string(name) + " must be a string");
  return value.as<std::string>();
}

std::size_t integerOption(const sol::table &options, const char *name,
                          std::size_t fallback, std::size_t maximum) {
  const sol::object value = options.raw_get<sol::object>(name);
  if (!value.valid() || value == sol::nil)
    return fallback;
  if (value.get_type() != sol::type::number)
    throw std::invalid_argument(std::string(name) + " must be an integer");
  const double number = value.as<double>();
  if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
      number > static_cast<double>(maximum))
    throw std::invalid_argument(std::string(name) +
                                " is outside its integer range");
  return static_cast<std::size_t>(number);
}

bool sameHeaderName(const std::string &left, const std::string &right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](unsigned char a, unsigned char b) {
                      return std::tolower(a) == std::tolower(b);
                    });
}

HttpRequest readRequest(const sol::table &options,
                        const std::string &urlOverride,
                        const std::string &methodOverride) {
  static const std::set<std::string> names{"url",
                                           "method",
                                           "headers",
                                           "body",
                                           "json",
                                           "timeout_ms",
                                           "connect_timeout_ms",
                                           "max_response_bytes",
                                           "max_header_bytes",
                                           "ca_certificate_pem"};
  for (const auto &[key, value] : options) {
    (void)value;
    if (key.get_type() != sol::type::string ||
        !names.contains(key.as<std::string>()))
      throw std::invalid_argument("Unknown HTTP request option");
  }

  HttpRequest request;
  request.url =
      methodOverride.empty() ? stringOption(options, "url") : urlOverride;
  request.method = methodOverride.empty()
                       ? stringOption(options, "method", "GET")
                       : methodOverride;
  request.body = stringOption(options, "body");
  request.caCertificatePem = stringOption(options, "ca_certificate_pem");
  request.timeoutMs =
      static_cast<int>(integerOption(options, "timeout_ms", request.timeoutMs,
                                     std::numeric_limits<int>::max()));
  request.connectTimeoutMs = static_cast<int>(
      integerOption(options, "connect_timeout_ms", request.connectTimeoutMs,
                    std::numeric_limits<int>::max()));
  // Lua numbers must represent the integer exactly, even on 64-bit hosts.
  constexpr std::size_t maxLuaInteger =
      sizeof(std::size_t) >= 8 ? 9007199254740991ULL : 4294967295ULL;
  request.maxResponseBytes = integerOption(
      options, "max_response_bytes", request.maxResponseBytes, maxLuaInteger);
  request.maxHeaderBytes = integerOption(options, "max_header_bytes",
                                         request.maxHeaderBytes, maxLuaInteger);

  const sol::object headers = options.raw_get<sol::object>("headers");
  if (headers.valid() && headers != sol::nil) {
    if (headers.get_type() != sol::type::table)
      throw std::invalid_argument("headers must be a table of string values");
    for (const auto &[key, value] : headers.as<sol::table>()) {
      if (key.get_type() != sol::type::string ||
          value.get_type() != sol::type::string)
        throw std::invalid_argument(
            "HTTP header names and values must be strings");
      request.headers.emplace_back(key.as<std::string>(),
                                   value.as<std::string>());
    }
    std::sort(request.headers.begin(), request.headers.end());
  }

  const sol::object json = options.raw_get<sol::object>("json");
  if (json.valid() && json != sol::nil) {
    const sol::object body = options.raw_get<sol::object>("body");
    if (body.valid() && body != sol::nil)
      throw std::invalid_argument("Specify either body or json, not both");
    request.body = luaObjectToJson(json).dump();
    const bool hasContentType =
        std::ranges::any_of(request.headers, [](const auto &header) {
          return sameHeaderName(header.first, "Content-Type");
        });
    if (!hasContentType)
      request.headers.emplace_back("Content-Type", "application/json");
  }
  return request;
}

sol::object responseObject(lua_State *state, const HttpRequestHandle &handle) {
  const auto response = handle.operation->response();
  if (!response)
    return sol::make_object(state, sol::nil);
  sol::state_view lua(state);
  sol::table result = lua.create_table();
  result["ok"] = response->ok;
  result["status"] = response->status;
  result["body"] = response->body;
  result["error"] = response->error;
  result["error_code"] = response->errorCode;
  sol::table headers = lua.create_table();
  for (const auto &[name, value] : response->headers) {
    std::string normalized = name;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    sol::object previous = headers.raw_get<sol::object>(normalized);
    sol::table values = previous.get_type() == sol::type::table
                            ? previous.as<sol::table>()
                            : lua.create_table();
    values[values.size() + 1] = value;
    headers[normalized] = values;
  }
  result["headers"] = headers;
  if (!response->body.empty()) {
    const auto json = nlohmann::json::parse(response->body, nullptr, false);
    if (json.is_discarded()) {
      result["json_error"] = "Response body is not valid JSON";
    } else {
      try {
        result["json"] = jsonToLuaDataObject(state, json);
        result["json_error"] = "";
      } catch (const std::overflow_error &) {
        result["json_error"] =
            "Response JSON contains an integer outside Lua's range";
      }
    }
  } else {
    result["json_error"] = "";
  }
  return sol::make_object(state, result);
}

RequestResult startRequest(LuaScriptHost &host, lua_State *state,
                           const sol::table &options,
                           const std::string &url = {},
                           const std::string &method = {}) {
  try {
    std::string error;
    auto operation =
        host.httpClient().request(readRequest(options, url, method), error);
    if (!operation)
      return {sol::make_object(state, sol::nil),
              sol::make_object(state, error)};
    auto handle = std::make_shared<HttpRequestHandle>();
    handle->operation = std::move(operation);
    return {sol::make_object(state, std::move(handle)),
            sol::make_object(state, sol::nil)};
  } catch (const std::invalid_argument &error) {
    return {sol::make_object(state, sol::nil),
            sol::make_object(state, error.what())};
  } catch (const nlohmann::json::exception &) {
    return {sol::make_object(state, sol::nil),
            sol::make_object(state, "Request JSON could not be encoded")};
  }
}

} // namespace

void LuaHttpBindingModule::install(LuaScriptHost &host,
                                   lua_State *state) const {
  sol::state_view lua(state);
  lua.new_usertype<HttpRequestHandle>(
      "HttpRequestHandle", sol::no_constructor, "done",
      [](const HttpRequestHandle &handle) { return handle.operation->done(); },
      "cancel", [](HttpRequestHandle &handle) { handle.operation->cancel(); },
      "response",
      [state](const HttpRequestHandle &handle) {
        return responseObject(state, handle);
      });
  lua["HttpRequestHandle"] = sol::nil;
  sol::table http = lua.create_named_table("Http");
  http.set_function("request", [&host, state](const sol::table options) {
    return startRequest(host, state, options);
  });
  http.set_function("get", [&host, state](const std::string &url,
                                          sol::optional<sol::table> options) {
    sol::state_view lua(state);
    return startRequest(host, state, options.value_or(lua.create_table()), url,
                        "GET");
  });
  http.set_function("post", [&host, state](const std::string &url,
                                           sol::optional<sol::table> options) {
    sol::state_view lua(state);
    return startRequest(host, state, options.value_or(lua.create_table()), url,
                        "POST");
  });
}

} // namespace demi::runtime
