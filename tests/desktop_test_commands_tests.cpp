#include "cli/CliArguments.h"
#include "cli/TestCommands.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
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
    const auto project =
        std::filesystem::path(valueAfter(args, "--project")).parent_path();
    if (std::filesystem::exists(project / "fail_before_summary")) {
      std::cerr << "Native HUD RCSS source could not be opened.\n";
      return 7;
    }
    std::cout << "[test] PASS forwarded_options.\n[test] SUMMARY passed=1 "
                 "failed=0.\n";
    return std::filesystem::exists(project / "fail_after_summary") ? 7 : 0;
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
  const auto readText = [](const fs::path &path) {
    std::ifstream source(path);
    return std::string(std::istreambuf_iterator<char>(source),
                       std::istreambuf_iterator<char>());
  };
  const auto runtimeLog = root / "build/linux/qualification/runtime.log";
  assert(readText(runtimeLog).find("SUMMARY passed=1 failed=0") !=
         std::string::npos);
  const std::vector<std::string> validArgs{
      "test",
      "linux",
      "--window-size",
      "1234x567",
      "--max-frames",
      "77",
      root.string(),
      "--profile-frames",
      (root / "trace with spaces.csv").string(),
      "--timeout",
      "5"};
  std::ofstream(root / "fail_before_summary") << "1";
  assert(runTestLinuxCommand(validArgs, out, error, argv[0]) == 1);
  assert(error.str().find("Native HUD RCSS source could not be opened.") !=
         std::string::npos);
  assert(readText(runtimeLog) ==
         "Native HUD RCSS source could not be opened.\n");
  fs::remove(root / "fail_before_summary");
  std::ofstream(root / "fail_after_summary") << "1";
  assert(runTestLinuxCommand(validArgs, out, error, argv[0]) == 1);
  assert(readText(runtimeLog).find("SUMMARY passed=1 failed=0") !=
         std::string::npos);
  const auto failedRuntime = nlohmann::json::parse(
      readText(root / "build/linux/qualification/qualification.json"));
  assert(!failedRuntime.at("success").get<bool>());
  assert(failedRuntime.at("steps").at(0).at("detail").get<std::string>().find(
             "status 7") != std::string::npos);
  fs::remove(root / "fail_after_summary");
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
