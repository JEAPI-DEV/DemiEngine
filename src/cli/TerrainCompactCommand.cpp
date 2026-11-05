#include "cli/TerrainCompactCommand.h"
#include "cli/CommandLine.h"

#include "demi/filesystem/AtomicTextFile.h"
#include "demi/filesystem/AuthoredJsonPatch.h"
#include "demi/runtime/terrain/TerrainBrushStroke.h"
#include "demi/runtime/terrain/TerrainRecipe.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <system_error>

namespace demi::cli {
namespace {
std::string readSource(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Could not read terrain source: " + path.string());
  std::string text{std::istreambuf_iterator<char>(input), {}};
  if (input.bad())
    throw std::runtime_error("Could not finish reading terrain source");
  return text;
}
} // namespace

int runTerrainCompactCommand(const std::vector<std::string> &args,
                             std::ostream &out, std::ostream &error) {
  CLI::App command{"Compact terrain source without changing native operations",
                   "demi terrain compact"};
  std::filesystem::path path;
  bool write = false;
  command.add_option("path", path)->required();
  command.add_flag("--write", write);
  if (const auto status = parseCommand(command, args, 2, out, error)) {
    return *status;
  }
  try {
    const auto original = readSource(path);
    const auto before = nlohmann::json::parse(original);
    auto after = before;
    auto &recipe = after.contains("recipe") ? after.at("recipe") : after;
    const auto nativeBefore = runtime::TerrainRecipe::parse(recipe);
    recipe = runtime::compactTerrainBrushRecipe(std::move(recipe));
    const auto nativeAfter = runtime::TerrainRecipe::parse(recipe);
    if (nativeBefore.toJson() != nativeAfter.toJson())
      throw std::runtime_error("Compaction changed native terrain operations");
    const auto patched =
        filesystem::patchAuthoredJsonSource(original, before, after);
    if (!patched)
      throw std::runtime_error("Could not preserve terrain source formatting");
    out << "Source bytes: " << original.size() << " -> " << patched->size()
        << "; sculpt stamps retained: " << nativeAfter.edits.size() << '\n';
    if (write && *patched != original) {
      if (readSource(path) != original)
        throw std::runtime_error("Terrain source changed during compaction");
      std::error_code failure;
      if (!atomicWriteText(path, *patched, failure))
        throw std::runtime_error("Could not save compact terrain: " +
                                 failure.message());
      out << "Compacted source saved. Reimport its asset manifest if "
             "registered.\n";
    } else {
      out << (write ? "Already compact.\n"
                    : "Preview only; add --write to save.\n");
    }
    return 0;
  } catch (const std::exception &failure) {
    error << "Terrain compaction failed: " << failure.what() << '\n';
    return 1;
  }
}
} // namespace demi::cli
