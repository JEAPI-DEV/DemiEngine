#include "demi/runtime/platform/ManagedProcess.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using demi::runtime::platform::ManagedProcess;
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void wait(ManagedProcess &process) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (process.running() && std::chrono::steady_clock::now() < deadline) {
    process.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  require(!process.running(),
          "Child failed to finish within the test deadline");
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "--child") {
    std::cout << argv[2];
    return 7;
  }
  if (argc > 1 && std::string(argv[1]) == "--stream") {
    std::cout << std::string(2 * 1024 * 1024, 'x') << "FINAL SUMMARY";
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "--wait") {
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
  }
  try {
    ManagedProcess process;
    std::string error;
    const auto executable = std::filesystem::absolute(argv[0]).string();
    const std::string literal = "spaces and ; shell-like characters";
    require(process.start({executable, "--child", literal}, error),
            error.c_str());
    wait(process);
    require(process.exitCode() == 7 && process.output() == literal,
            "Argument, output or exit status was not preserved");
    require(process.start({executable, "--stream"}, error), error.c_str());
    wait(process);
    require(process.exitCode() == 0 &&
                process.output().ends_with("FINAL SUMMARY") &&
                process.output().size() <= 1024 * 1024,
            "Output draining lost the final result or grew without bound");
    require(process.start({executable, "--wait"}, error), error.c_str());
    require(!process.start({executable, "--wait"}, error),
            "A running child was replaced");
    process.stop();
    require(!process.running() && process.exitCode().has_value(),
            "Cancellation left a child running");
    require(!process.start({}, error), "Missing executable was accepted");
    std::cout << "Managed process tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
