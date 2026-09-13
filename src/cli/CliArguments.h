#pragma once

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace demi::cli {

[[nodiscard]] inline std::optional<std::pair<int, int>>
parseWindowSize(std::string_view text) {
  const auto separator = text.find('x');
  if (separator == std::string_view::npos)
    return std::nullopt;
  int width = 0, height = 0;
  const auto first =
      std::from_chars(text.data(), text.data() + separator, width);
  const auto second = std::from_chars(text.data() + separator + 1,
                                      text.data() + text.size(), height);
  if (first.ec != std::errc{} || second.ec != std::errc{} ||
      first.ptr != text.data() + separator ||
      second.ptr != text.data() + text.size() || width < 1 || width > 65535 ||
      height < 1 || height > 65535)
    return std::nullopt;
  return std::pair{width, height};
}

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
