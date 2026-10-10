#include "cli/CliArguments.h"
#include "cli/TestCommands.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main(int argc, char **argv) {
  using namespace demi::cli;
  if (argc > 1 && std::string_view(argv[1]) == "run") {
    std::vector<std::string> args(argv + 1, argv + argc);
    const bool valid =
        valueAfter(args, "--window-size") == "1234x567" &&
        valueAfter(args, "--max-frames") == "77" &&
        valueAfter(args, "--profile-frames")
            .ends_with("trace with spaces.csv") &&
        valueAfter(args, "--project").ends_with("demi.project.json");
    if (!valid)
      return 9;
    std::cout << "[test] PASS forwarded_options.\n[test] SUMMARY passed=1 "
                 "failed=0.\n";
    return 0;
  }
  namespace fs = std::filesystem;
  const auto root =
      fs::temp_directory_path() /
      ("demi desktop args " +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root / "scripts/tests");
  std::ofstream(root / "demi.project.json") << "{\"format_version\":1}";
  std::ofstream(root / "scripts/tests/e2e.lua") << "return {}";
  std::ostringstream out, error;
  assert(runTestLinuxCommand(
             {"test", "linux", "--window-size", "1234x567", "--max-frames",
              "77", root.string(), "--profile-frames",
              (root / "trace with spaces.csv").string(), "--timeout", "5"},
             out, error, argv[0]) == 0);
  assert(fs::exists(root / "build/linux/qualification/qualification.json"));
  for (const auto &option :
       std::vector<std::vector<std::string>>{{"--window-size", "bad"},
                                             {"--max-frames", "0"},
                                             {"--timeout", "nan"},
                                             {"--max-frames"},
                                             {"--unknown", "1"}}) {
    std::vector<std::string> args{"test", "linux", "--project", root.string()};
    args.insert(args.end(), option.begin(), option.end());
    assert(runTestLinuxCommand(args, out, error, argv[0]) == 2);
  }
  fs::remove_all(root);
}
