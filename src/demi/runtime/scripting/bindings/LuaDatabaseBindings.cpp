#include "demi/runtime/scripting/bindings/LuaDatabaseBindings.h"

#include "demi/runtime/concurrency/AsyncWorkQueue.h"
#include "demi/runtime/database/DatabaseService.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace demi::runtime {
namespace {

using Json = nlohmann::json;
using ConnectResult = std::tuple<sol::object, sol::object>;

struct DatabaseBlob {
  std::string bytes;
};

std::string stringField(const sol::table &table, const char *name,
                        std::string fallback = {}) {
  const sol::object value = table.raw_get<sol::object>(name);
  if (!value.valid() || value == sol::nil)
    return fallback;
  if (value.get_type() != sol::type::string)
    throw std::invalid_argument(std::string(name) + " must be a string");
  return value.as<std::string>();
}

Json jsonField(const sol::table &table, const char *name, Json fallback) {
  const sol::object value = table.raw_get<sol::object>(name);
  if (!value.valid() || value == sol::nil)
    return fallback;
  return luaObjectToJson(value);
}

bool isWithin(const std::filesystem::path &root,
              const std::filesystem::path &candidate) {
  auto part = candidate.begin();
  for (const auto &segment : root) {
    if (part == candidate.end() || *part != segment)
      return false;
    ++part;
  }
  return part != candidate.end();
}

std::string sqlitePath(const std::filesystem::path &userDataPath,
                       const std::string &requested) {
  if (requested == ":memory:")
    return requested;
  const std::filesystem::path relative(requested);
  if (requested.empty() || requested.find('\0') != std::string::npos ||
      relative.is_absolute() || !relative.has_filename())
    throw std::invalid_argument("SQLite path must be a relative file name");
  for (const auto &part : relative) {
    if (part == "." || part == "..")
      throw std::invalid_argument("SQLite path cannot traverse directories");
  }
  if (userDataPath.empty())
    throw std::invalid_argument("Application user data path is unavailable");

  try {
    const auto dataRoot = std::filesystem::weakly_canonical(userDataPath);
    const auto root = std::filesystem::weakly_canonical(dataRoot / "databases");
    if (!isWithin(dataRoot, root))
      throw std::invalid_argument("SQLite path escapes application user data");
    const auto candidate = std::filesystem::weakly_canonical(root / relative);
    if (!isWithin(root, candidate))
      throw std::invalid_argument(
          "SQLite path escapes the databases directory");
    std::filesystem::create_directories(candidate.parent_path());
    // Recheck after creation in case an existing parent was a symlink.
    const auto checked = std::filesystem::weakly_canonical(root / relative);
    if (!isWithin(root, checked))
      throw std::invalid_argument(
          "SQLite path escapes the databases directory");
    return checked.string();
  } catch (const std::filesystem::filesystem_error &) {
    throw std::invalid_argument("SQLite database path is unavailable");
  }
}

template <class Convert>
Json denseArray(const sol::object &value, const char *label, Convert convert) {
  if (value.get_type() != sol::type::table)
    throw std::invalid_argument(std::string(label) + " must be an array");
  const sol::table table = value.as<sol::table>();
  const std::size_t count = table.size();
  std::size_t entries = 0;
  for (const auto &[key, item] : table) {
    (void)item;
    if (key.get_type() != sol::type::number || !key.is<lua_Integer>())
      throw std::invalid_argument(std::string(label) +
                                  " must use dense array indices");
    const lua_Integer index = key.as<lua_Integer>();
    if (index < 1 || static_cast<std::uint64_t>(index) > count)
      throw std::invalid_argument(std::string(label) +
                                  " must use dense array indices");
    ++entries;
  }
  if (entries != count)
    throw std::invalid_argument(std::string(label) +
                                " must use dense array indices");
  Json result = Json::array();
  for (std::size_t index = 1; index <= count; ++index)
    result.push_back(convert(table.raw_get<sol::object>(index)));
  return result;
}

Json parameters(const sol::object &values) {
  return denseArray(values, "SQL parameters", [](const sol::object &value) {
    if (value.is<DatabaseBlob>()) {
      const auto &bytes = value.as<const DatabaseBlob &>().bytes;
      return Json::binary(
          std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    }
    return luaObjectToJson(value);
  });
}

Json parameters(sol::optional<sol::object> values) {
  return values && *values != sol::nil ? parameters(*values) : Json::array();
}

Json statement(const sol::object &entry) {
  if (entry.get_type() != sol::type::table)
    throw std::invalid_argument("transaction entry must be a table");
  Json result = Json::object();
  for (const auto &[key, field] : entry.as<sol::table>()) {
    if (key.get_type() != sol::type::string)
      throw std::invalid_argument("transaction entry keys must be strings");
    const std::string name = key.as<std::string>();
    result[name] =
        name == "parameters" ? parameters(field) : luaObjectToJson(field);
  }
  return result;
}

Json statements(const sol::object &value) {
  return denseArray(value, "transaction statements", statement);
}

Json luaDatabaseResult(Json value) {
  // Keep binary conversion local to database results; the shared JSON bridge
  // has no binary type, while Lua strings preserve every byte including NUL.
  std::vector<Json *> pending{&value};
  while (!pending.empty()) {
    Json *node = pending.back();
    pending.pop_back();
    if (node->is_binary()) {
      const auto &bytes = node->get_binary();
      const std::string text(bytes.begin(), bytes.end());
      *node = text;
    } else if (node->is_structured()) {
      for (auto &child : *node)
        pending.push_back(&child);
    }
  }
  return value;
}

ConnectResult connect(LuaScriptHost &host, const sol::object &settings,
                      sol::this_state lua) {
  lua_State *state = lua;
  try {
    if (settings.get_type() != sol::type::table)
      throw std::invalid_argument("Database settings must be a table");
    const auto table = settings.as<sol::table>();
    const std::string driver = stringField(table, "driver", "sqlite");
    std::string path =
        stringField(table, "path", driver == "sqlite" ? "game.db" : "");
    Json options = jsonField(table, "options", Json::object());
    if (driver == "sqlite") {
      if (!options.is_object())
        throw std::invalid_argument("SQLite options must be an object");
      path = sqlitePath(host.applicationServices().userDataPath(), path);
    }
    auto connection = host.databaseService().connect(driver, std::move(path),
                                                     std::move(options));
    return {sol::make_object(state, std::move(connection)),
            sol::make_object(state, sol::nil)};
  } catch (const std::invalid_argument &error) {
    return {sol::make_object(state, sol::nil),
            sol::make_object(state, error.what())};
  } catch (const Json::exception &) {
    return {sol::make_object(state, sol::nil),
            sol::make_object(state, "Database options could not be encoded")};
  } catch (const std::exception &) {
    return {sol::make_object(state, sol::nil),
            sol::make_object(state, "Database connection submission failed")};
  }
}

} // namespace

void LuaDatabaseBindingModule::install(LuaScriptHost &host,
                                       lua_State *state) const {
  sol::state_view lua(state);
  lua.new_usertype<AsyncWorkOperation>(
      "DatabaseOperation", sol::no_constructor, "done",
      [](const AsyncWorkOperation &operation) {
        return operation.completion()->ready();
      },
      "result",
      [](const AsyncWorkOperation &operation, sol::this_state lua) {
        lua_State *state = lua;
        if (!operation.completion()->ready())
          return sol::make_object(state, sol::nil);
        const auto value = operation.result();
        return value ? jsonToLuaDataObject(state, luaDatabaseResult(*value))
                     : sol::make_object(state, sol::nil);
      },
      "error",
      [](const AsyncWorkOperation &operation) { return operation.error(); },
      "cancel", [](AsyncWorkOperation &operation) { operation.cancel(); });
  lua["DatabaseOperation"] = sol::nil;

  lua.new_usertype<DatabaseConnection>(
      "DatabaseConnection", sol::no_constructor, "connect_operation",
      [](const DatabaseConnection &connection) {
        return connection.connectOperation();
      },
      "query",
      [](DatabaseConnection &connection, std::string sql,
         sol::optional<sol::object> values) {
        return connection.query(std::move(sql), parameters(values));
      },
      "execute",
      [](DatabaseConnection &connection, std::string sql,
         sol::optional<sol::object> values) {
        return connection.execute(std::move(sql), parameters(values));
      },
      "transaction",
      [](DatabaseConnection &connection, sol::object batch) {
        return connection.transaction(statements(batch));
      },
      "close",
      [](DatabaseConnection &connection) { return connection.close(); });
  lua["DatabaseConnection"] = sol::nil;

  sol::table database = lua.create_named_table("Database");
  lua.new_usertype<DatabaseBlob>("DatabaseBlob", sol::no_constructor);
  lua["DatabaseBlob"] = sol::nil;
  database.set_function(
      "blob", [](std::string bytes) { return DatabaseBlob{std::move(bytes)}; });
  database.set_function(
      "connect", [&host](const sol::object settings, sol::this_state lua) {
        return connect(host, settings, lua);
      });
}

} // namespace demi::runtime
