#include "cli/CommandLine.h"
#include "demi/core/Version.h"
#include "demi/runtime/app/RuntimeApp.h"

#include <iostream>

int main(int argc, char **argv) {
  CLI::App command{std::string(demi::EngineName) + " runtime " +
                       std::string(demi::EngineVersion),
                   "demi-runtime"};
  demi::runtime::RuntimeOptions options;
  command.add_option("--project", options.projectPath)->required();
  command.add_option("--max-frames", options.maxFrames)
      ->check(CLI::NonNegativeNumber);
  command.add_flag("--profiler", options.profiler);
  const std::vector<std::string> arguments(argv + 1, argv + argc);
  if (const auto status = demi::cli::parseCommand(command, arguments, 0,
                                                  std::cout, std::cerr)) {
    return *status;
  }
  return demi::runtime::runProject(options);
}
