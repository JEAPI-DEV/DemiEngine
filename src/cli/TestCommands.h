#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace demi::cli {

// Desktop runtime tests with a wall timeout and forwarded frame/viewport options;
// the desktop counterpart of
// `demi test android`. Launches the project with the Lua mobile test harness
// enabled, waits for the `[test] SUMMARY` marker, and writes the same report
// family under `<project>/build/linux/qualification/`.
[[nodiscard]] int runTestLinuxCommand(const std::vector<std::string> &args,
                                      std::ostream &out, std::ostream &error,
                                      const std::string &selfExecutable);

} // namespace demi::cli
