#include "cli/LuaStubExport.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include <fstream>
namespace demi::cli {
bool exportLuaStubs(const std::filesystem::path &source,
                    const std::filesystem::path &destination,
                    std::string &error) {
  try {
    const auto relative =
        std::filesystem::weakly_canonical(destination)
            .lexically_relative(std::filesystem::weakly_canonical(source));
    if (relative.empty() || *relative.begin() != "..") {
      error = "Export Lua stubs outside the checked-in stub source directory.";
      return false;
    }
    if (std::filesystem::exists(destination / "demi.lua")) {
      error = "The destination still contains legacy demi.lua stubs. Move that "
              "file out of the LuaLS library, then regenerate.";
      return false;
    }
    if (!std::filesystem::is_regular_file(source / "demi/input.lua")) {
      error =
          "Native Lua stub module library was not found: " + source.string();
      return false;
    }
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(source)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".lua")
        continue;
      const auto target = destination / entry.path().lexically_relative(source);
      std::filesystem::create_directories(target.parent_path());
      if (std::filesystem::exists(target) &&
          std::filesystem::equivalent(entry.path(), target))
        continue;
      std::filesystem::copy_file(
          entry.path(), target,
          std::filesystem::copy_options::overwrite_existing);
    }
    std::ofstream components(destination / "demi/_components.lua");
    components << "---@meta\n"
               << runtime::scene_loading::generatedLuaComponentTypes();
    if (!components) {
      error = "Failed to write component type annotations";
      return false;
    }
    return true;
  } catch (const std::filesystem::filesystem_error &exception) {
    error = exception.what();
    return false;
  }
}
} // namespace demi::cli
