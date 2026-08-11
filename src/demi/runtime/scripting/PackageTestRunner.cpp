#include "demi/runtime/scripting/PackageTestRunner.h"

#include "demi/packages/PackageManifest.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <map>
#include <set>

namespace demi::runtime {
namespace {

constexpr std::string_view Harness = R"lua(
Test = { passed = 0, failed = 0, failures = {}, time = 0, _seed = 1 }

local function fail(message) error(message or "assertion failed", 2) end

function Test.case(name, callback)
  local ok, reason = xpcall(callback, debug.traceback)
  if ok then
    Test.passed = Test.passed + 1
  else
    Test.failed = Test.failed + 1
    Test.failures[#Test.failures + 1] = tostring(name) .. ": " .. tostring(reason)
  end
end

function Test.equal(actual, expected, message)
  if actual ~= expected then
    fail(message or ("expected " .. tostring(expected) .. ", got " .. tostring(actual)))
  end
end

function Test.truthy(value, message)
  if not value then fail(message or "expected a truthy value") end
end

function Test.near(actual, expected, epsilon, message)
  epsilon = epsilon or 0.000001
  if math.abs(actual - expected) > epsilon then
    fail(message or ("expected " .. tostring(actual) .. " near " .. tostring(expected)))
  end
end

function Test.advance(seconds)
  if type(seconds) ~= "number" or seconds < 0 then fail("time delta must be non-negative") end
  Test.time = Test.time + seconds
  return Test.time
end

function Test.random()
  Test._seed = (Test._seed * 48271) % 2147483647
  return Test._seed / 2147483647
end

function Test.world()
  local world = { entities = {}, next_id = 1 }
  function world:spawn(values)
    local id = "fixture-" .. self.next_id
    self.next_id = self.next_id + 1
    self.entities[id] = values or {}
    return id
  end
  function world:exists(id) return self.entities[id] ~= nil end
  function world:get(id) return self.entities[id] end
  function world:destroy(id) self.entities[id] = nil end
  return world
end

function Test.events()
  local bus = { listeners = {}, queue = {} }
  function bus:on(name, callback)
    self.listeners[name] = self.listeners[name] or {}
    self.listeners[name][#self.listeners[name] + 1] = callback
    return callback
  end
  function bus:emit(name, payload)
    self.queue[#self.queue + 1] = { name = name, payload = payload }
  end
  function bus:flush()
    while #self.queue > 0 do
      local event = table.remove(self.queue, 1)
      local listeners = self.listeners[event.name] or {}
      for index = 1, #listeners do listeners[index](event.payload) end
    end
  end
  return bus
end
)lua";

void addError(PackageTestResult &result, const std::filesystem::path &path,
              const std::string &message) {
  result.diagnostics.push_back(
      {.severity = Severity::Error,
       .code = "PACKAGE_TEST_FAILED_TO_RUN",
       .message = message,
       .path = path.string(),
       .suggestion = "Fix the package test or its declared modules."});
}

bool execute(lua_State *state, const std::string_view source,
             const std::string &name, std::string &error) {
  if (luaL_loadbuffer(state, source.data(), source.size(), name.c_str()) !=
          LUA_OK ||
      lua_pcall(state, 0, 0, 0) != LUA_OK) {
    error = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1)
                                               : "unknown Lua error";
    lua_pop(state, 1);
    return false;
  }
  return true;
}

bool collectPackageRoots(const std::filesystem::path &packageRoot,
                         std::vector<std::filesystem::path> &roots,
                         PackageTestResult &result) {
  std::map<std::string, std::filesystem::path> available;
  std::error_code directoryError;
  for (const auto &entry : std::filesystem::directory_iterator(
           packageRoot.parent_path(), directoryError)) {
    if (!entry.is_directory())
      continue;
    const auto loaded = packages::loadPackageManifest(
        entry.path() / packages::PackageManifestFilename);
    if (loaded.manifest)
      available[loaded.manifest->name] = entry.path();
  }
  if (directoryError) {
    addError(result, packageRoot.parent_path(),
             "Could not enumerate package test dependencies: " +
                 directoryError.message());
    return false;
  }

  std::set<std::string> visited;
  const auto visit = [&](const auto &self,
                         const std::filesystem::path &root) -> bool {
    const auto loaded =
        packages::loadPackageManifest(root / packages::PackageManifestFilename);
    if (!loaded.manifest) {
      result.diagnostics.insert(result.diagnostics.end(),
                                loaded.diagnostics.begin(),
                                loaded.diagnostics.end());
      return false;
    }
    if (!visited.insert(loaded.manifest->name).second)
      return true;
    roots.push_back(root);
    for (const auto &dependency : loaded.manifest->dependencies) {
      const auto found = available.find(dependency.name);
      if (found == available.end()) {
        addError(result, root,
                 "Package test dependency is unavailable: " + dependency.name);
        return false;
      }
      const auto dependencyManifest = packages::loadPackageManifest(
          found->second / packages::PackageManifestFilename);
      if (!dependencyManifest.manifest ||
          !dependency.constraint.accepts(
              dependencyManifest.manifest->version)) {
        addError(result, found->second,
                 "Package test dependency does not satisfy the declared "
                 "version: " +
                     dependency.name);
        return false;
      }
      if (!self(self, found->second))
        return false;
    }
    return true;
  };
  return visit(visit, packageRoot);
}

} // namespace

PackageTestResult
runPackageTests(const std::filesystem::path &packageRoot,
                const std::vector<std::filesystem::path> &testFiles,
                const std::uint32_t seed) {
  PackageTestResult result;
  lua_State *state = luaL_newstate();
  if (state == nullptr) {
    addError(result, packageRoot, "Could not create the isolated Lua state.");
    return result;
  }
  luaL_openlibs(state);

  std::string error;
  if (!execute(state, Harness, "@demi-package-test-harness", error)) {
    addError(result, packageRoot, error);
    lua_close(state);
    return result;
  }
  lua_getglobal(state, "Test");
  lua_pushinteger(state, static_cast<lua_Integer>(seed == 0 ? 1 : seed));
  lua_setfield(state, -2, "_seed");
  lua_pop(state, 1);

  std::vector<std::filesystem::path> roots;
  if (!collectPackageRoots(packageRoot, roots, result)) {
    lua_close(state);
    return result;
  }
  std::string sourcePaths;
  const auto appendPackagePath = [&](const std::filesystem::path &root) {
    sourcePaths += (root / "scripts" / "?.lua").generic_string() + ";";
    sourcePaths += (root / "scripts" / "?" / "init.lua").generic_string() + ";";
  };
  for (const auto &root : roots)
    appendPackagePath(root);
  lua_getglobal(state, "package");
  lua_getfield(state, -1, "path");
  const char *oldPathValue = lua_tostring(state, -1);
  const std::string oldPath = oldPathValue != nullptr ? oldPathValue : "";
  lua_pop(state, 1);
  const std::string packagePath = sourcePaths + oldPath;
  lua_pushlstring(state, packagePath.data(), packagePath.size());
  lua_setfield(state, -2, "path");
  lua_pop(state, 1);

  for (const auto &relative : testFiles) {
    const auto path = packageRoot / relative;
    if (luaL_loadfile(state, path.string().c_str()) != LUA_OK ||
        lua_pcall(state, 0, 0, 0) != LUA_OK) {
      addError(result, path,
               lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1)
                                                  : "unknown Lua error");
      lua_pop(state, 1);
    }
  }

  lua_getglobal(state, "Test");
  lua_getfield(state, -1, "passed");
  result.passed = static_cast<int>(lua_tointeger(state, -1));
  lua_pop(state, 1);
  lua_getfield(state, -1, "failed");
  result.failed = static_cast<int>(lua_tointeger(state, -1));
  lua_pop(state, 1);
  lua_getfield(state, -1, "failures");
  const std::size_t count = lua_rawlen(state, -1);
  for (std::size_t index = 1; index <= count; ++index) {
    lua_rawgeti(state, -1, static_cast<lua_Integer>(index));
    if (const char *failure = lua_tostring(state, -1); failure != nullptr)
      result.failures.emplace_back(failure);
    lua_pop(state, 1);
  }
  lua_pop(state, 2);
  lua_close(state);
  return result;
}

} // namespace demi::runtime
