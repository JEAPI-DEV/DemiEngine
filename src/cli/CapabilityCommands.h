#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace demi::cli {

struct CapabilityCommandContext {
  std::filesystem::path sourceRoot;
};

int runCapabilityCommand(const std::vector<std::string> &args,
                         const CapabilityCommandContext &context,
                         std::ostream &out, std::ostream &error);

} // namespace demi::cli
