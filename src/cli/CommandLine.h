#pragma once

#include <CLI/CLI.hpp>
#include <algorithm>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace demi::cli {

// CLI11's vector overload consumes arguments in reverse order. Public command
// entry points retain their argv-shaped inputs for embedding and focused tests.
inline std::optional<int>
parseCommand(CLI::App &command, const std::vector<std::string> &arguments,
             std::size_t prefix, std::ostream &output, std::ostream &error) {
  std::vector<std::string> remaining(arguments.rbegin(),
                                     arguments.rend() -
                                         std::min(prefix, arguments.size()));
  try {
    command.parse(remaining);
    return std::nullopt;
  } catch (const CLI::ParseError &failure) {
    command.exit(failure, output, error);
    return failure.get_exit_code() == 0 ? 0 : 2;
  }
}

} // namespace demi::cli
