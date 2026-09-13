#include "cli/TestCommands.h"
#include "cli/CliArguments.h"
#include "cli/project/ProjectDiscovery.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <poll.h>
#include <regex>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" {
extern char **environ;
}

namespace demi::cli {
namespace {

using Json = nlohmann::json;

struct TestOutcome {
  bool summaryFound = false;
  int passed = 0;
  int failed = 0;
  std::vector<Json> results;
};

[[nodiscard]] TestOutcome parseTestOutput(const std::string &output) {
  TestOutcome outcome;
  std::istringstream lines(output);
  std::string line;
  static const std::regex summaryPattern(R"(passed=(\d+) failed=(\d+))");
  while (std::getline(lines, line)) {
    if (!outcome.summaryFound && line.find("SUMMARY passed=") != std::string::npos) {
      std::smatch digits;
      if (std::regex_search(line, digits, summaryPattern)) {
        outcome.passed = std::atoi(digits[1].str().c_str());
        outcome.failed = std::atoi(digits[2].str().c_str());
        outcome.summaryFound = true;
      }
      continue;
    }
    const auto passPosition = line.find("[test] PASS ");
    const auto failPosition = line.find("[test] FAIL ");
    if (passPosition == std::string::npos && failPosition == std::string::npos)
      continue;
    std::string name;
    std::string status;
    if (passPosition != std::string::npos) {
      name = line.substr(passPosition + std::strlen("[test] PASS "));
      status = "passed";
    } else {
      name = line.substr(failPosition + std::strlen("[test] FAIL "));
      status = "failed";
    }
    while (!name.empty() && (name.back() == '.' || name.back() == '\r'))
      name.pop_back();
    outcome.results.push_back(Json{{"name", name}, {"status", status}});
  }
  return outcome;
}

[[nodiscard]] std::filesystem::path
resolveTestProject(const std::vector<std::string> &args) {
  const std::filesystem::path project =
      projectFileFromArgs(args, std::filesystem::current_path());
  if (!project.empty())
    return project;
  for (std::size_t index = 2; index < args.size(); ++index) {
    const std::string &argument = args[index];
    if (argument == "--timeout" || argument.starts_with("--"))
      continue;
    std::filesystem::path path = argument;
    std::error_code error;
    if (std::filesystem::is_directory(path, error))
      path /= "demi.project.json";
    return path;
  }
  return {};
}

} // namespace

int runTestLinuxCommand(const std::vector<std::string> &args, std::ostream &out,
                        std::ostream &error,
                        const std::string &selfExecutable) {
  const std::filesystem::path project = resolveTestProject(args);
  if (project.empty()) {
    error << "demi test linux requires --project <project>, a project "
             "directory, or a demi.project.json in the current directory.\n";
    return 2;
  }
  const std::filesystem::path testModule =
      project.parent_path() / "scripts" / "tests" / "e2e.lua";
  if (!std::filesystem::is_regular_file(testModule)) {
    error << "demi test linux requires " << testModule
          << " (see docs/e2e-testing.md).\n";
    return 2;
  }
  const std::filesystem::path outputDirectory =
      project.parent_path() / "build" / "linux" / "qualification";
  std::error_code fileSystemError;
  std::filesystem::create_directories(outputDirectory, fileSystemError);

  double timeoutSeconds = 120.0;
  const std::string timeoutValue = valueAfter(args, "--timeout");
  if (!timeoutValue.empty()) {
    try {
      timeoutSeconds = std::stod(timeoutValue);
    } catch (const std::invalid_argument &) {
      error << "--timeout must be a number of seconds.\n";
      return 2;
    }
  }

  int pipeFds[2] = {-1, -1};
  if (pipe(pipeFds) != 0) {
    error << "Could not create the test output pipe.\n";
    return 1;
  }

  std::vector<std::string> childArguments{
      selfExecutable, "run", "--project", project.string(), "--e2e-tests"};
  std::vector<char *> childArgv;
  childArgv.reserve(childArguments.size() + 1);
  for (std::string &argument : childArguments)
    childArgv.push_back(argument.data());
  childArgv.push_back(nullptr);

  posix_spawn_file_actions_t fileActions;
  if (posix_spawn_file_actions_init(&fileActions) != 0) {
    error << "Could not prepare the test process IO.\n";
    return 1;
  }
  (void)posix_spawn_file_actions_addclose(&fileActions, pipeFds[0]);
  (void)posix_spawn_file_actions_adddup2(&fileActions, pipeFds[1],
                                         STDOUT_FILENO);
  (void)posix_spawn_file_actions_adddup2(&fileActions, pipeFds[1],
                                         STDERR_FILENO);
  (void)posix_spawn_file_actions_addclose(&fileActions, pipeFds[1]);
  (void)posix_spawn_file_actions_addopen(&fileActions, STDIN_FILENO,
                                         "/dev/null", O_RDONLY, 0);

  pid_t child = 0;
  const int spawnResult =
      posix_spawnp(&child, selfExecutable.c_str(), &fileActions, nullptr,
                   const_cast<char *const *>(childArgv.data()), environ);
  (void)posix_spawn_file_actions_destroy(&fileActions);
  (void)close(pipeFds[1]);
  if (spawnResult != 0) {
    (void)close(pipeFds[0]);
    error << "Could not start the desktop test process: "
          << std::error_code(spawnResult, std::generic_category()).message()
          << '\n';
    return 1;
  }

  std::string captured;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::duration<double>(timeoutSeconds);
  std::array<char, 4096> buffer{};
  bool timedOut = false;
  while (true) {
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
      timedOut = true;
      break;
    }
    pollfd watcher{.fd = pipeFds[0], .events = POLLIN, .revents = 0};
    const int ready = poll(
        &watcher, 1,
        static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining)
                .count()));
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (ready == 0)
      continue;
    const auto received = ::read(pipeFds[0], buffer.data(), buffer.size());
    if (received <= 0)
      break;
    captured.append(buffer.data(), static_cast<std::size_t>(received));
  }
  (void)close(pipeFds[0]);

  int childStatus = 0;
  if (timedOut) {
    (void)kill(child, SIGTERM);
    const auto stopDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < stopDeadline) {
      if (waitpid(child, &childStatus, WNOHANG) == child)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  } else {
    while (waitpid(child, &childStatus, 0) < 0) {
      if (errno != EINTR)
        break;
    }
  }

  const TestOutcome outcome = parseTestOutput(captured);
  const bool success =
      outcome.summaryFound && !timedOut && outcome.passed > 0 &&
      outcome.failed == 0;
  Json results = Json::array();
  for (const Json &result : outcome.results)
    results.push_back(result);

  const Json report = {
      {"format_version", 1},
      {"platform", "linux"},
      {"project", project.string()},
      {"steps",
       Json::array({Json{
           {"name", "lua_tests"},
           {"status", success ? "passed" : "failed"},
           {"detail",
            timedOut ? std::string("No [test] SUMMARY within the timeout.")
                     : std::string()}}})},
      {"lua_tests",
       {{"passed", outcome.passed},
        {"failed", outcome.failed},
        {"results", results}}},
      {"success", success},
  };
  const std::filesystem::path reportPath =
      outputDirectory / "qualification.json";
  std::ofstream reportOutput(reportPath);
  reportOutput << report.dump(2) << '\n';

  out << "Desktop test summary: passed=" << outcome.passed
      << " failed=" << outcome.failed << (timedOut ? " (timed out)" : "")
      << '\n';
  for (const Json &result : outcome.results)
    out << "  " << result["status"].get<std::string>() << "  "
        << result["name"].get<std::string>() << '\n';
  out << "Wrote desktop test report: " << reportPath << '\n';
  return success ? 0 : 1;
}

} // namespace demi::cli
