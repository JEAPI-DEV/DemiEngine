#include "cli/CookCommands.h"

#include "cli/CommandLine.h"
#include "cli/project/ProjectDiscovery.h"
#include "demi/assets/AssetCooker.h"
#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <iostream>

namespace demi::cli {
int runCookCommand(const std::vector<std::string> &args, std::ostream &output,
                   std::ostream &error) {
  CLI::App command{"Cook project assets", "demi cook"};
  std::string requestedProject;
  std::string platform = "linux";
  std::filesystem::path outputDirectory;
  command.add_option("--project", requestedProject);
  command.add_option("--platform", platform)
      ->check(CLI::IsMember({"linux", "linux_server", "android"}));
  command.add_option("--output", outputDirectory);
  if (const auto status = parseCommand(command, args, 1, output, error)) {
    return *status;
  }
  const auto project = resolveProjectFile(requestedProject);
  if (project.empty()) {
    error << "cook requires --project <project>.\n";
    return 2;
  }
  if (outputDirectory.empty()) {
    outputDirectory = project.parent_path() / "generated" / "cooked" / platform;
  }
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
