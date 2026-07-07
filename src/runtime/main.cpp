#include "demi/core/Version.h"
#include "demi/runtime/app/RuntimeApp.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

std::string valueAfter(const std::vector<std::string>& args, const std::string& key) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == key) {
      return args[i + 1];
    }
  }
  return {};
}

int numericValueAfter(const std::vector<std::string>& args, const std::string& key) {
  const std::string value = valueAfter(args, key);
  if (value.empty()) {
    return 0;
  }
  try {
    return std::stoi(value);
  } catch (...) {
    return 0;
  }
}

int frameLimitFrom(const std::vector<std::string>& args) {
  const int maxFrames = numericValueAfter(args, "--max-frames");
  return maxFrames > 0 ? maxFrames : numericValueAfter(args, "--frames");
}

} // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  const std::string project = valueAfter(args, "--project");
  if (project.empty()) {
    std::cerr << demi::EngineName << " runtime " << demi::EngineVersion << '\n';
    std::cerr << "Usage: demi-runtime --project <project> [--frames count|--max-frames count]\n";
    return 2;
  }

  return demi::runtime::runProject(demi::runtime::RuntimeOptions{
    .projectPath = project,
    .maxFrames = frameLimitFrom(args),
  });
}
