#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace demi::cli {
int runTerrainCompactCommand(const std::vector<std::string> &args,
                             std::ostream &out, std::ostream &error);
} // namespace demi::cli
