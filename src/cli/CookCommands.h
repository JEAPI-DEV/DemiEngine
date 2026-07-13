#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace demi::cli {

[[nodiscard]] int runCookCommand(const std::vector<std::string> &args,
                                 std::ostream &output, std::ostream &error);

} // namespace demi::cli
