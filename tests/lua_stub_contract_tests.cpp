#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/LuaServiceModules.h"
#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <memory>
#include <stdexcept>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace {

using ApiSet = std::set<std::string>;

bool verifyExplicitModulePublication() {
  std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
  if (!state) {
    return false;
  }
  luaL_openlibs(state.get());
  lua_newtable(state.get());
  lua_setglobal(state.get(), "Input");
  lua_newtable(state.get());
  lua_setglobal(state.get(), "GameSettings");
  demi::runtime::publishLuaServiceModules(state.get());
  const char *checkPublication = R"lua(
    assert(Input == nil)
    assert(type(GameSettings) == "table")
    local Controls = require("demi.input")
    assert(Controls == require("demi.input"))
    assert(package.preload["demi.game_settings"] == nil)
  )lua";
  if (luaL_dostring(state.get(), checkPublication) != LUA_OK) {
    std::cerr << lua_tostring(state.get(), -1) << '\n';
    return false;
  }
  demi::runtime::clearLuaServiceModules(state.get());
  if (luaL_dostring(state.get(), R"lua(
    assert(package.loaded["demi.input"] == nil)
    assert(package.preload["demi.input"] == nil)
    assert(type(GameSettings) == "table")
  )lua") != LUA_OK) {
    return false;
  }
  try {
    (void)demi::runtime::luaServiceModuleName("GameSettings");
  } catch (const std::invalid_argument &) {
    return true;
  }
  std::cerr << "Unknown native service was given an inferred import path\n";
  return false;
}

std::string readFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}
std::string withoutLineComments(const std::string &text) {
  std::istringstream input(text);
  std::ostringstream output;
  std::string line;
  while (std::getline(input, line)) {
    const std::size_t comment = line.find("--");
    output << line.substr(0, comment) << '\n';
  }
  return output.str();
}

ApiSet declaredStubApis(const std::filesystem::path &stubPath) {
  if (std::filesystem::is_directory(stubPath)) {
    ApiSet result;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(stubPath)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".lua") continue;
      const auto apis = declaredStubApis(entry.path());
      result.insert(apis.begin(), apis.end());
    }
    return result;
  }
  const std::string text = withoutLineComments(readFile(stubPath));
  const std::regex functionPattern(
      R"(\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*\()");
  ApiSet apis;
  for (std::sregex_iterator it(text.begin(), text.end(), functionPattern), end;
       it != end; ++it) {
    apis.insert((*it)[1].str() + "." + (*it)[2].str());
  }
  return apis;
}

std::set<std::string> localModuleNames(const std::string &text) {
  const std::regex localPattern(R"(\blocal\s+([A-Z][A-Za-z0-9_]*)\s*=)");
  const std::regex functionPattern(R"(\bfunction\s+([A-Z][A-Za-z0-9_]*)[:.])");
  std::set<std::string> names;
  for (std::sregex_iterator it(text.begin(), text.end(), localPattern), end;
       it != end; ++it) {
    names.insert((*it)[1].str());
  }
  for (std::sregex_iterator it(text.begin(), text.end(), functionPattern), end;
       it != end; ++it) {
    names.insert((*it)[1].str());
  }
  return names;
}

bool shouldScanLuaFile(const std::filesystem::path &path) {
  if (path.extension() != ".lua") {
    return false;
  }
  for (const std::filesystem::path &component : path) {
    if (component == "generated" || component == "build" ||
        component == ".demi") {
      return false;
    }
  }
  const std::string generic = path.generic_string();
  // Package-unit Test.case/equal helpers belong to the isolated package runner,
  // not the native runtime E2E Test API. Keep scanning scripts/tests/e2e.lua.
  if (generic.find("/tests/") != std::string::npos &&
      generic.find("/scripts/tests/") == std::string::npos)
    return false;
  return generic.find("/examples/") != std::string::npos ||
         generic.find("/scripts/runtime/") != std::string::npos;
}

bool verifyGameCallsAreStubbed(const std::filesystem::path &root,
                               const ApiSet &stubApis) {
  const std::regex callPattern(
      R"(\b([A-Z][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*\()");
  bool passed = true;
  std::map<std::string, std::string> moduleServices;
  for (const auto &api : stubApis) {
    const auto service = api.substr(0, api.find('.'));
    moduleServices.emplace(demi::runtime::luaServiceModuleName(service), service);
  }
  const std::regex importPattern(R"lua(\blocal\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*require\s*\(\s*["']([^"']+)["']\s*\))lua");

  for (const std::filesystem::path searchRoot :
       {root / "examples", root / "scripts" / "runtime"}) {
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator(searchRoot)) {
      if (!entry.is_regular_file() || !shouldScanLuaFile(entry.path())) {
        continue;
      }

      const std::string text = withoutLineComments(readFile(entry.path()));
      const std::set<std::string> localNames = localModuleNames(text);
      std::map<std::string, std::string> aliases;
      for (std::sregex_iterator it(text.begin(), text.end(), importPattern), end; it != end; ++it) {
        const auto module = moduleServices.find((*it)[2].str());
        aliases[(*it)[1].str()] = module == moduleServices.end() ? "" : module->second;
      }
      for (std::sregex_iterator it(text.begin(), text.end(), callPattern), end;
           it != end; ++it) {
        const std::string service = (*it)[1].str();
        const std::string function = (*it)[2].str();
        const auto alias = aliases.find(service);
        if ((alias != aliases.end() && alias->second.empty()) ||
            (alias == aliases.end() && localNames.contains(service))) continue;
        const std::string api = (alias == aliases.end() ? service : alias->second) + "." + function;
        if (!stubApis.contains(api)) {
          std::cerr
              << entry.path().string()
              << ": game Lua call is missing from scripts/stubs/demi/: "
              << api << '\n';
          passed = false;
        }
      }
    }
  }

  return passed;
}

bool requireStub(const ApiSet &stubApis, const std::string_view api) {
  if (stubApis.contains(std::string(api))) {
    return true;
  }
  std::cerr << "scripts/stubs/demi/ is missing required game-facing API: "
            << api << '\n';
  return false;
}

bool verifySpawnContract(demi::runtime::LuaScriptHost &host) {
  const auto result = host.executeConsole(R"lua(
    local Entity = require("demi.entity")
    local function rejected(id, options, diagnostic)
      local values = table.pack(Entity.spawn(id, options))
      assert(values.n == 2 and values[1] == false)
      assert(type(values[2]) == "string" and values[2]:find(diagnostic, 1, true))
      assert(not Entity.exists(id))
      assert(not Entity.has_component(id, "Transform2D"))
      assert(not Entity.has_component(id, "Transform3D"))
    end
    rejected("spawn_prefab", {prefab="prefab://player"}, "Prefab.instantiate")
    rejected("spawn_ttl", {ttl=0, position={1,2}}, "Timer.after")
    rejected("spawn_false_ttl", {ttl=false}, "Timer.after")
    for index, vector in ipairs({
      {}, {1}, {1,2,3,4}, {x=1,y=2,z=3}, {[1]=1,[3]=3},
      {1,"2"}, {1,true}, {1,math.huge}, {1,0/0}, {1,1e100},
      {[1]=1,[2]=2,[2.5]=3}, {1,2,extra=3}, 42
    }) do
      for _, field in ipairs({"position", "velocity"}) do
        rejected("spawn_invalid_" .. field .. index, {[field]=vector}, field)
      end
    end
    rejected("spawn_dimensions", {position={1,2}, velocity={1,2,3}}, "matching dimensions")
    rejected("spawn_components", {position={1,2}, components=false}, "components")
    rejected("spawn_array_options", {1,2}, "specification table")
    rejected("spawn_block", {position={1,2}, components={Transform2D=5}}, "Transform2D")
    rejected("spawn_schema", {components={UnknownSpawnComponent={}}}, "UnknownSpawnComponent")
    rejected("spawn_name", {name={}}, "Entity.spawn")
    rejected("", {}, "id")
    local function created(id, options)
      local values = table.pack(Entity.spawn(id, options))
      assert(values.n == 2 and values[1] == true and values[2] == nil)
    end
    created("spawn_2d", {position={1,2}, velocity={3,4}})
    assert(Entity.has_component("spawn_2d", "Transform2D"))
    assert(not Entity.has_component("spawn_2d", "Transform3D"))
    local position = Entity.get_config("spawn_2d", "Transform2D", "position")
    local velocity = Entity.get_config("spawn_2d", "Rigidbody2D", "velocity")
    assert(#position == 2 and position[1] == 1 and position[2] == 2)
    assert(#velocity == 2 and velocity[1] == 3 and velocity[2] == 4)
    created("spawn_3d", {position={1,2,3}, velocity={4,5,6}})
    assert(not Entity.has_component("spawn_3d", "Transform2D"))
    velocity = Entity.get_config("spawn_3d", "Rigidbody3D", "velocity")
    assert(#velocity == 3 and velocity[3] == 6)
    created("spawn_override", {position={1,2}, components={Transform2D={position={8,9}}}})
    position = Entity.get_config("spawn_override", "Transform2D", "position")
    assert(position[1] == 8 and position[2] == 9)
    created("spawn_plain", {})
    created("spawn_velocity", {velocity={7,8,9}})
    assert(Entity.has_component("spawn_velocity", "Rigidbody3D"))
    assert(not Entity.has_component("spawn_velocity", "Transform3D"))
    local ok, err = Entity.spawn("spawn_plain", {})
    assert(ok == false and type(err) == "string" and err:find("pending", 1, true))
    ok, err = Entity.spawn("native_body", {})
    assert(ok == false and type(err) == "string" and err:find("exists", 1, true))
  )lua");
  if (!result.succeeded)
    std::cerr << "Entity.spawn contract failed: " << result.error << '\n';
  return result.succeeded;
}

bool verifyLookAtContract(demi::runtime::LuaScriptHost &host) {
  const auto result = host.executeConsole(R"lua(
    local Entity = require("demi.entity")
    local T3 = require("demi.transform3d")
    local function near(a, b)
      assert(math.abs(a - b) < 0.0001,
        string.format("expected %.9g, got %.9g", b, a))
    end
    local function unchanged(expected, ...)
      local actual = table.pack(...)
      assert(actual.n == 3)
      for i = 1, 3 do assert(actual[i] == expected[i]) end
    end
    local function rejected(id)
      local position = {T3.get_position(id)}
      local rotation = {T3.get_rotation(id)}
      local scale = {T3.get_scale(id)}
      assert(T3.look_at(id, -7, 11, 13) == false)
      unchanged(position, T3.get_position(id))
      unchanged(rotation, T3.get_rotation(id))
      unchanged(scale, T3.get_scale(id))
    end
    assert(T3.set_position("spatial", 10, -4, 6))
    assert(T3.set_rotation("spatial", 0.3, 0.6, -0.4))
    assert(T3.set_scale("spatial", 2, 3, 4))
    assert(T3.set_position("spatial_child", 1.25, -2.5, 0.75))
    assert(T3.set_rotation("spatial_child", -0.2, 0.4, 0.1))
    assert(T3.set_scale("spatial_child", 0.5, 1.5, 2.5))
    local position = {T3.get_position("spatial_child")}
    local scale = {T3.get_scale("spatial_child")}
    local origin = Entity.world_position("spatial_child")
    assert(T3.look_at("spatial_child", -7, 11, 13))
    unchanged(position, T3.get_position("spatial_child"))
    unchanged(scale, T3.get_scale("spatial_child"))
    local worldPosition = Entity.world_position("spatial_child")
    local direction = {-7-origin[1], 11-origin[2], 13-origin[3]}
    local length = math.sqrt(direction[1]^2 + direction[2]^2 + direction[3]^2)
    local forward = {T3.forward("spatial_child")}
    for i = 1, 3 do
      near(worldPosition[i], origin[i])
      near(forward[i], direction[i] / length)
    end
    assert(T3.look_at("missing", 1, 2, 3) == false)
    assert(T3.look_at("planar", 1, 2, 3) == false)
    -- A resolved but noninvertible parent must also leave the child unchanged.
    assert(T3.set_scale("spatial", 0, 3, 4))
    rejected("spatial_child")
    assert(T3.set_scale("spatial", 2, 3, 4))

    assert(Entity.create("aim_pending_root", {components={Transform3D={
      position={2,3,4}, scale={2,3,4}
    }}}))
    assert(T3.look_at("aim_pending_root", 7, 3, 4))
    local rx, ry, rz = T3.get_rotation("aim_pending_root")
    near(rx, 0); near(ry, math.pi / 2); near(rz, 0)
    unchanged({2,3,4}, T3.get_position("aim_pending_root"))
    unchanged({2,3,4}, T3.get_scale("aim_pending_root"))
    for _, parent in ipairs({"aim_pending_root", "aim_missing_parent"}) do
      local id = "aim_child_of_" .. parent
      assert(Entity.create(id, {components={Transform3D={
        parent=parent, position={1,2,3}, rotation={0.2,0.3,0.4}, scale={2,3,4}
      }}}))
      rejected(id)
    end
  )lua");
  if (!result.succeeded)
    std::cerr << "Transform3D.look_at contract failed: " << result.error << '\n';
  return result.succeeded;
}

bool verifyLuaValueContracts(demi::runtime::LuaScriptHost &host) {
  bool passed = true;
  const auto spatialValues = host.executeConsole(R"lua(
    local Entity = require("demi.entity")
    local T2 = require("demi.transform2d")
    local T3 = require("demi.transform3d")
    local Camera = require("demi.camera3d")
    local Body = require("demi.physics.rigidbody3d")
    local V3 = require("demi.math.vector3")
    local function near(a, b)
      assert(math.abs(a - b) < 0.0001,
        string.format("expected %.9g, got %.9g", b, a))
    end
    local function vector(value, count)
      assert(type(value) == "table" and #value == count)
      local keys = 0
      for key, item in pairs(value) do
        assert(type(key) == "number" and key >= 1 and key <= count)
        assert(key % 1 == 0 and type(item) == "number")
        keys = keys + 1
      end
      assert(keys == count)
    end
    local function nils(count, ...)
      assert(select("#", ...) == count)
      for i = 1, count do assert(select(i, ...) == nil) end
    end
    -- Missing components and IDs preserve tuple arity, not just the first nil.
    nils(2, T2.get_position("spatial"))
    nils(3, T3.get_position("planar"))
    nils(3, T3.get_rotation("missing"))
    nils(3, Body.get_velocity("spatial"))
    nils(1, Body.state("spatial"))
    nils(1, Entity.world_position("missing"))
    assert(T3.set_position("missing", 1, 2, 3) == false)
    assert(T2.set_position("planar", 4, 5))
    local xy = table.pack(T2.get_position("planar"))
    assert(xy.n == 2 and xy[1] == 4 and xy[2] == 5)
    vector(Entity.local_position("planar"), 2)
    assert(T2.set_rotation("planar", math.pi / 2))
    near(T2.get_rotation("planar"), math.pi / 2)

    -- A radian quarter-turn rotates the unparented +Z direction to +X.
    assert(T3.set_position("spatial", 10, 0, 0))
    assert(T3.set_rotation("spatial", 0, math.pi / 2, 0))
    local angles = table.pack(T3.get_rotation("spatial"))
    assert(angles.n == 3)
    near(angles[2], math.pi / 2)
    local fx, fy, fz = T3.forward("spatial")
    near(fx, 1); near(fy, 0); near(fz, 0)
    fx, fy, fz = T3.forward("spatial_child")
    near(fx, 1); near(fy, 0); near(fz, 0)
    local localPoint = Entity.local_position("spatial_child")
    local worldPoint = Entity.world_position("spatial_child")
    vector(localPoint, 3); vector(worldPoint, 3)
    assert(localPoint[1] == 0 and localPoint[3] == 2)
    near(worldPoint[1], 12)
    near(worldPoint[2], 0)
    near(worldPoint[3], 0)

    -- Compare orientations, not nonunique Euler triples. Build the expected
    -- Rz * Ry * Rx action directly, independently of quaternion conversion.
    local function rotated(axis, roll, pitch, yaw)
      local x, y, z = axis[1], axis[2], axis[3]
      y, z = math.cos(roll)*y - math.sin(roll)*z,
             math.sin(roll)*y + math.cos(roll)*z
      x, z = math.cos(pitch)*x + math.sin(pitch)*z,
             -math.sin(pitch)*x + math.cos(pitch)*z
      x, y = math.cos(yaw)*x - math.sin(yaw)*y,
             math.sin(yaw)*x + math.cos(yaw)*y
      return {x, y, z}
    end
    for _, sign in ipairs({-1, 1}) do
      for _, offset in ipairs({0, -0.000001, 0.000001, -0.0001, 0.0001, -0.01, 0.01}) do
        for _, angles in ipairs({{0.37, -0.61}, {-1.2, 0.83}, {0.9, 0.9}}) do
          local roll, yaw = angles[1], angles[2]
          local pitch = sign * math.pi / 2 + offset
          assert(T3.set_rotation("spatial", 0, pitch, yaw))
          assert(T3.set_rotation("spatial_child", roll, 0, 0))
          for _, basis in ipairs({
            {T3.right, {1,0,0}}, {T3.up, {0,1,0}}, {T3.forward, {0,0,1}}
          }) do
            local actual = table.pack(basis[1]("spatial_child"))
            local expected = rotated(basis[2], roll, pitch, yaw)
            assert(actual.n == 3)
            for i = 1, 3 do near(actual[i], expected[i]) end
          end
        end
      end
    end
    assert(T3.set_rotation("spatial_child", 0, 0, 0))
    local velocity = table.pack(Body.get_velocity("native_body"))
    assert(velocity.n == 3 and velocity[1] == 1 and velocity[3] == 3)
    vector(Body.state("native_body").velocity, 3)
    vector(Body.state("native_body").angular_velocity, 3)
    local sum = V3.add({1, 2, 3}, {4, 5, 6})
    vector(sum, 3)
    assert(sum[1] == 5 and sum[2] == 7 and sum[3] == 9)

    assert(T3.set_position("spatial", 0, 0, 0))
    assert(T3.set_rotation("spatial", 0, 0, 0))
    local ray = Camera.screen_ray("spatial", 75, 25, 100, 100)
    vector(ray.origin, 3); vector(ray.direction, 3)
    near(V3.length(ray.direction), 1)
    local point = table.pack(Camera.screen_to_world("spatial", 75, 25, 100, 100, 5))
    assert(point.n == 3)
    for i = 1, 3 do near(point[i], ray.origin[i] + ray.direction[i] * 5) end
    local screen = Camera.world_to_screen("spatial", point[1], point[2], point[3], 100, 100)
    vector(screen, 2); near(screen[1], 75); near(screen[2], 25)
    nils(1, Camera.world_to_screen("spatial", 0, 0, -1, 100, 100))
    nils(1, Camera.screen_ray("missing", 0, 0, 100, 100))
    nils(3, Camera.screen_to_world("missing", 0, 0, 100, 100, 5))
  )lua");
  if (!spatialValues.succeeded) {
    std::cerr << "Spatial Lua value contract failed: " << spatialValues.error
              << '\n';
    passed = false;
  }
  const auto structuredValues = host.executeConsole(R"lua(
    local Grid = require("demi.grid")
    local Navigation = require("demi.navigation2d")
    local Physics = require("demi.physics.query3d")
    local Animation = require("demi.animation")
    local Data = require("demi.data")
    local Network = require("demi.network")
    local path, diagnostic = Grid.path(0, 0, 1, 1)
    assert(path == nil and type(diagnostic) == "string" and diagnostic ~= "")
    path, diagnostic = Navigation.path(0, 0, 1, 1)
    assert(type(path) == "table" and next(path) == nil)
    assert(diagnostic == "PATH_START_OUT_OF_BOUNDS")
    assert(Navigation.configure(2, 1, 10, 100, 200))
    path, diagnostic = Navigation.path(0, 0, 1, 0)
    assert(diagnostic == "OK" and #path == 2)
    assert(path[1][1] == 0 and path[1][2] == 0)
    assert(path[1].world_x == 105 and path[1].world_y == 205)
    assert(path[2][1] == 1 and path[2].world_x == 115)
    assert(Physics.raycast(0, 0, 0, 0, 0, 1, 10) == nil)
    local hits = Physics.overlap_sphere_all(0, 0, 0, 1)
    assert(type(hits) == "table" and next(hits) == nil)
    local hit = Physics.raycast(20, 0, 0, 0, 0, 1, 10)
    assert(hit and hit.entity_id == "query_box")
    assert(#hit.point == 3 and #hit.normal == 3)
    assert(hit.point.x == nil and hit.normal.z == nil)
    assert(math.abs(hit.point[3] - 4) < 0.0001)
    assert(math.abs(hit.normal[3] + 1) < 0.0001)
    assert(math.abs(hit.distance - 4) < 0.0001)
    assert(Physics.raycast(20, 0, 0, 0, 0, 1, 10, "query_box") == nil)
    assert(Animation.solve_two_bone_3d({upper_length=0, lower_length=1}) == nil)
    local ik = Animation.solve_two_bone_3d({
      root={0,0,0}, target={3,0,0}, pole={0,1,0}, upper_length=1, lower_length=1
    })
    assert(ik.reached == false and #ik.joint == 3 and #ik.end_position == 3)
    assert(ik.end_position[1] == 2 and ik.end_position.x == nil)
    local value, err = Data.load("asset://contract/missing")
    assert(value == nil and err.code == "DATA_ASSET_NOT_FOUND")
    assert(err.path == "asset://contract/missing" and type(err.message) == "string")
    assert(err.id == nil)
    value, err = Data.parse_yaml("point: [1, 2, 3]\nempty: null\n")
    assert(err == nil and Data.kind(value.point) == "array")
    assert(#value.point == 3 and value.point[3] == 3 and Data.is_null(value.empty))
    local message = Network.decode(Network.encode("position", {point={1,2,3}}))
    assert(message.type == "position" and #message.payload.point == 3)
    assert(message.payload.point[2] == 2 and message.payload.point.x == nil)
    assert(Network.decode("not json") == nil)
  )lua");
  if (!structuredValues.succeeded) {
    std::cerr << "Structured Lua value contract failed: " << structuredValues.error
              << '\n';
    passed = false;
  }
  return passed;
}

bool verifyInstalledApisMatchStubs(const ApiSet &stubApis, const std::filesystem::path &root) {
  demi::runtime::World world;
  demi::runtime::Entity generated;
  generated.id = "native_body";
  demi::runtime::Rigidbody3DComponent body;
  body.mass = 9;
  body.useGravity = false;
  body.velocity = {1,2,3};
  generated.setComponent(body);
  generated.serializedComponents["Rigidbody3D"] = R"({"mass":1})";
  world.entities.push_back(std::move(generated));
  demi::runtime::Entity spatial;
  spatial.id = "spatial";
  spatial.setComponent(demi::runtime::Transform3DComponent{});
  spatial.setComponent(demi::runtime::Camera3DComponent{});
  world.entities.push_back(std::move(spatial));
  demi::runtime::Entity child;
  child.id = "spatial_child";
  demi::runtime::Transform3DComponent childTransform;
  childTransform.parent = "spatial";
  childTransform.position = {0, 0, 2};
  child.setComponent(childTransform);
  world.entities.push_back(std::move(child));
  demi::runtime::Entity planar;
  planar.id = "planar";
  planar.setComponent(demi::runtime::Transform2DComponent{});
  world.entities.push_back(std::move(planar));
  demi::runtime::Entity queryBox;
  queryBox.id = "query_box";
  demi::runtime::Transform3DComponent queryTransform;
  queryTransform.position = {20, 0, 5};
  queryBox.setComponent(queryTransform);
  demi::runtime::BoxCollider3DComponent collider;
  collider.size = {2, 2, 2};
  queryBox.setComponent(collider);
  world.entities.push_back(std::move(queryBox));
  demi::runtime::InputState input;
  demi::runtime::LuaScriptHost host;
  std::string error;
  if (!host.initialize(world, input, nullptr, error)) {
    std::cerr << "Could not inspect installed Lua bindings: " << error << '\n';
    return false;
  }

  const std::vector<std::string> installed = host.publicLuaApi();
  const ApiSet installedApis(installed.begin(), installed.end());
  bool passed = true;
  std::set<std::string> services;
  for (const auto &api : installed) services.insert(api.substr(0, api.find('.')));
  for (const auto &service : services) {
    const auto module = demi::runtime::luaServiceModuleName(service);
    auto relative = module;
    std::replace(relative.begin(), relative.end(), '.', '/');
    const auto path = root / "scripts/stubs" / (relative + ".lua");
    if (!std::filesystem::is_regular_file(path)) {
      std::cerr << "Missing module-aligned stub: " << path << '\n';
      passed = false;
    } else {
      const auto moduleApis = declaredStubApis(path);
      for (const auto &api : installedApis)
        if (api.starts_with(service + ".") && !moduleApis.contains(api)) {
          std::cerr << "API is not declared in its own module: " << api << '\n';
          passed = false;
        }
    }
    const auto result = host.executeConsole(
        "assert(_G['" + service + "'] == nil); local api = require('" + module +
        "'); assert(type(api) == 'table'); assert(api == require('" + module +
        "')); assert(_G['" + service + "'] == nil)");
    if (!result.succeeded) {
      std::cerr << "Module import contract failed: " << module << ": " << result.error << '\n';
      passed = false;
    }
  }
  const auto missing = host.executeConsole("return Input.down('left')");
  const auto privateTypes = host.executeConsole(R"lua(
    local Input = require("demi.input")
    local Entity = require("demi.entity")
    assert(Input.is_down == nil and Input.action_down == nil)
    assert(Input.action_vector == nil and Input.action_pressed == nil)
    assert(Entity.get == nil and Entity.set == nil)
    assert(ProceduralMeshBuilder == nil and VoxelWorldHandle == nil)
    assert(HudNodeHandle == nil and HudVirtualLayout == nil)
    local Mesh = require("demi.mesh.procedural")
    local builder = Mesh.create()
    builder:add_vertex(0, 0, 0, 0, 1, 0, 0, 0)
    assert(builder:vertex_count() == 1)
    builder:clear()
    assert(builder:vertex_count() == 0)
  )lua");
  if (!privateTypes.succeeded) {
    std::cerr << "Private type registration failed: " << privateTypes.error << '\n';
    passed = false;
  }
  const auto liveBody = host.executeConsole(R"lua(
    local Body = require("demi.physics.rigidbody3d")
    local Entity = require("demi.entity")
    local value = Body.state("native_body")
    assert(value and value.mass == 9 and value.body_type == "dynamic")
    assert(value.use_gravity == false and value.velocity[2] == 2)
    assert(Entity.get_config("native_body", "Rigidbody3D", "mass") == 1)
    assert(Body.state("missing") == nil)
  )lua");
  if (!liveBody.succeeded) { std::cerr << "Live body state contract failed: " << liveBody.error << '\n'; passed = false; }
  passed = verifyLuaValueContracts(host) && passed;
  passed = verifyLookAtContract(host) && passed;
  passed = verifySpawnContract(host) && passed;
  if (missing.succeeded) { std::cerr << "Implicit engine globals still work\n"; passed = false; }
  for (const char *old : {"demi.network_session", "demi.tls_server", "demi.tls_client",
                         "demi.crypto", "demi.audio_source", "demi.procedural_mesh",
                         "demi.mesh_deformation", "demi.voxel_world", "demi.vector2",
                         "demi.vector3", "demi.mathf", "demi.random", "demi.physics2d",
                         "demi.physics3d", "demi.rigidbody2d", "demi.rigidbody3d",
                         "demi.character_controller3d", "demi.destruction3d"}) {
    const auto rejected = host.executeConsole(std::string("assert(not pcall(require, '") + old + "'))");
    if (!rejected.succeeded) { std::cerr << "Retired module is still importable: " << old << '\n'; passed = false; }
  }
  const auto captureTime = host.executeConsole("captured_time = require('demi.time')");
  host.beginFrame(0.25F);
  const auto updatedTime = host.executeConsole("assert(captured_time.delta_time == 0.25)");
  if (!captureTime.succeeded || !updatedTime.succeeded) {
    std::cerr << "Imported Time table did not receive native frame updates\n";
    passed = false;
  }
  for (const std::string &api : stubApis) {
    if (!installedApis.contains(api)) {
      std::cerr << "Lua stub documents an API that is not installed: " << api
                << '\n';
      passed = false;
    }
  }
  for (const std::string &api : installedApis) {
    if (!stubApis.contains(api)) {
      std::cerr << "Installed Lua API is missing from stubs: " << api << '\n';
      passed = false;
    }
  }
  return passed;
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1])
                                              : std::filesystem::current_path();
  const ApiSet stubApis =
      declaredStubApis(root / "scripts" / "stubs" / "demi");
  bool passed = verifyExplicitModulePublication();

  for (const std::string_view api : {
           "Transform3D.get_position",
           "Transform3D.set_position",
           "Transform3D.add_position",
           "Transform3D.get_rotation",
           "Transform3D.set_rotation",
           "Transform3D.get_scale",
           "Transform3D.set_scale",
           "Physics3D.overlap_sphere",
           "Physics3D.raycast",
           "Hud.set_font_size",
           "Hud.set_background_color",
           "Hud.canvas_size",
           "Hud.set_position",
           "Hud.set_size",
           "Hud.set_opacity",
           "Hud.set_image_animation_frame",
           "Application.max_fps",
           "Application.set_max_fps",
           "Application.mouse_captured",
           "Application.set_mouse_captured",
           "Application.mouse_visible",
           "Application.set_mouse_visible",
           "Physics.set_enabled",
           "Sprite2D.set_color",
           "Input.key_pressed",
           "Input.mouse_delta",
           "Input.ui_pointer_captured",
       }) {
    passed = requireStub(stubApis, api) && passed;
  }

  passed = verifyGameCallsAreStubbed(root, stubApis) && passed;
  passed = verifyInstalledApisMatchStubs(stubApis, root) && passed;
  return passed ? 0 : 1;
}
