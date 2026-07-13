#include "cli/CookCommands.h"

#include "cli/BuildCommands.h"
#include "demi/assets/AssetCooker.h"
#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <iostream>

namespace demi::cli {
namespace {

std::string valueAfter(const std::vector<std::string> &args,
                       const std::string &key) {
  for (std::size_t index = 0; index + 1 < args.size(); ++index)
    if (args[index] == key)
      return args[index + 1];
  return {};
}

} // namespace

int runCookCommand(const std::vector<std::string> &args, std::ostream &output,
                   std::ostream &error) {
  const auto project = projectFileFromArgs(args);
  if (project.empty()) {
    error << "cook requires --project <project>.\n";
    return 2;
  }
  const std::string platform = valueAfter(args, "--platform").empty()
                                   ? "linux"
                                   : valueAfter(args, "--platform");
  const std::filesystem::path outputDirectory =
      valueAfter(args, "--output").empty()
          ? project.parent_path() / "generated" / "cooked" / platform
          : std::filesystem::path(valueAfter(args, "--output"));
  const auto diagnostics =
      assets::cookProject({.projectFile = project,
                           .outputDirectory = outputDirectory,
                           .platform = platform});
  if (!diagnostics.empty())
    printDiagnosticsText(error, diagnostics);
  if (hasErrors(diagnostics))
    return 1;
  output << "Cooked project: " << outputDirectory.string() << '\n';
  return 0;
}

} // namespace demi::cli
