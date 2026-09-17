#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scripting/LuaServiceModules.h"

#include <filesystem>
#include <fstream>
#include <iostream>
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

  for (const std::filesystem::path searchRoot :
       {root / "examples", root / "scripts" / "runtime"}) {
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::recursive_directory_iterator(searchRoot)) {
      if (!entry.is_regular_file() || !shouldScanLuaFile(entry.path())) {
        continue;
      }

      const std::string text = withoutLineComments(readFile(entry.path()));
      const std::set<std::string> localNames = localModuleNames(text);
      for (std::sregex_iterator it(text.begin(), text.end(), callPattern), end;
           it != end; ++it) {
        const std::string service = (*it)[1].str();
        const std::string function = (*it)[2].str();
        const auto known = stubApis.lower_bound(service + ".");
        const bool native = known != stubApis.end() && known->starts_with(service + ".");
        if (localNames.contains(service) && !native) {
          continue;
        }

        const std::string api = service + "." + function;
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

bool verifyInstalledApisMatchStubs(const ApiSet &stubApis) {
  demi::runtime::World world;
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
  if (missing.succeeded) { std::cerr << "Implicit engine globals still work\n"; passed = false; }
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
  passed = verifyInstalledApisMatchStubs(stubApis) && passed;
  return passed ? 0 : 1;
}
