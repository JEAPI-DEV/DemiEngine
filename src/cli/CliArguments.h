#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace demi::cli {

[[nodiscard]] inline bool hasArg(const std::vector<std::string> &args,
                                 const std::string_view key) {
  return std::ranges::find(args, key) != args.end();
}

[[nodiscard]] inline std::string
valueAfter(const std::vector<std::string> &args, const std::string_view key) {
  for (std::size_t index = 0; index + 1 < args.size(); ++index)
    if (args[index] == key)
      return args[index + 1];
  return {};
}

} // namespace demi::cli
