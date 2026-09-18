#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/LuaServiceModules.h"
#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"

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

namespace {

using ApiSet = std::set<std::string>;

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
  const auto liveBody = host.executeConsole(R"lua(
    local Body = require("demi.physics.rigidbody3d")
    local Entity = require("demi.entity")
    local value = Body.state("native_body")
    assert(value and value.mass == 9 and value.body_type == "dynamic")
    assert(value.use_gravity == false and value.velocity[2] == 2)
    assert(Entity.get("native_body", "Rigidbody3D", "mass") == 1)
    assert(Body.state("missing") == nil)
  )lua");
  if (!liveBody.succeeded) { std::cerr << "Live body state contract failed: " << liveBody.error << '\n'; passed = false; }
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
  bool passed = true;

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
           "Physics.set_enabled",
           "Sprite2D.set_color",
           "Input.is_pressed",
           "Input.mouse_delta",
           "Input.ui_pointer_captured",
       }) {
    passed = requireStub(stubApis, api) && passed;
  }

  passed = verifyGameCallsAreStubbed(root, stubApis) && passed;
  passed = verifyInstalledApisMatchStubs(stubApis, root) && passed;
  return passed ? 0 : 1;
}
