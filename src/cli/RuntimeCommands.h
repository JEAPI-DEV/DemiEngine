#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace demi::cli {

enum class RuntimeCommandMode { Run, Serve, Develop };

[[nodiscard]] int runRuntimeCommand(const std::vector<std::string> &args,
                                    RuntimeCommandMode mode,
                                    std::ostream &output, std::ostream &error);

} // namespace demi::cli
