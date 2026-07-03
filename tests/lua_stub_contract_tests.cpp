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

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string withoutLineComments(const std::string& text) {
  std::istringstream input(text);
  std::ostringstream output;
  std::string line;
  while (std::getline(input, line)) {
    const std::size_t comment = line.find("--");
    output << line.substr(0, comment) << '\n';
  }
  return output.str();
}

ApiSet declaredStubApis(const std::filesystem::path& stubPath) {
  const std::string text = withoutLineComments(readFile(stubPath));
  const std::regex functionPattern(R"(\bfunction\s+([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*\()");
  ApiSet apis;
  for (std::sregex_iterator it(text.begin(), text.end(), functionPattern), end; it != end; ++it) {
    apis.insert((*it)[1].str() + "." + (*it)[2].str());
  }
  return apis;
}

std::set<std::string> localModuleNames(const std::string& text) {
  const std::regex localPattern(R"(\blocal\s+([A-Z][A-Za-z0-9_]*)\s*=)");
  const std::regex functionPattern(R"(\bfunction\s+([A-Z][A-Za-z0-9_]*)[:.])");
  std::set<std::string> names;
  for (std::sregex_iterator it(text.begin(), text.end(), localPattern), end; it != end; ++it) {
    names.insert((*it)[1].str());
  }
  for (std::sregex_iterator it(text.begin(), text.end(), functionPattern), end; it != end; ++it) {
    names.insert((*it)[1].str());
  }
  return names;
}

bool shouldScanLuaFile(const std::filesystem::path& path) {
  if (path.extension() != ".lua") {
    return false;
  }
  const std::string generic = path.generic_string();
  return generic.find("/examples/") != std::string::npos || generic.find("/scripts/runtime/") != std::string::npos;
}

bool verifyGameCallsAreStubbed(const std::filesystem::path& root, const ApiSet& stubApis) {
  const std::regex callPattern(R"(\b([A-Z][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*\()");
  bool passed = true;

  for (const std::filesystem::path searchRoot : {root / "examples", root / "scripts" / "runtime"}) {
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(searchRoot)) {
      if (!entry.is_regular_file() || !shouldScanLuaFile(entry.path())) {
        continue;
      }

      const std::string text = withoutLineComments(readFile(entry.path()));
      const std::set<std::string> localNames = localModuleNames(text);
      for (std::sregex_iterator it(text.begin(), text.end(), callPattern), end; it != end; ++it) {
        const std::string service = (*it)[1].str();
        const std::string function = (*it)[2].str();
        if (localNames.contains(service)) {
          continue;
        }

        const std::string api = service + "." + function;
        if (!stubApis.contains(api)) {
          std::cerr << entry.path().string() << ": game Lua call is missing from scripts/stubs/demi.lua: " << api << '\n';
          passed = false;
        }
      }
    }
  }

  return passed;
}

bool requireStub(const ApiSet& stubApis, const std::string_view api) {
  if (stubApis.contains(std::string(api))) {
    return true;
  }
  std::cerr << "scripts/stubs/demi.lua is missing required game-facing API: " << api << '\n';
  return false;
}

} // namespace

int main(int argc, char** argv) {
  const std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::current_path();
  const ApiSet stubApis = declaredStubApis(root / "scripts" / "stubs" / "demi.lua");
  bool passed = true;

  for (const std::string_view api : {
         "Transform3D.get_position",
         "Transform3D.set_position",
         "Transform3D.add_position",
         "Transform3D.get_rotation",
         "Transform3D.set_rotation",
         "Transform3D.get_scale",
         "Transform3D.set_scale",
         "Hud.set_button_label",
         "Runtime.get_max_fps",
         "Runtime.set_max_fps",
       }) {
    passed = requireStub(stubApis, api) && passed;
  }

  passed = verifyGameCallsAreStubbed(root, stubApis) && passed;
  return passed ? 0 : 1;
}
